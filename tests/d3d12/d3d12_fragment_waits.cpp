// A color pass can wait for its texture inputs at the fragment stage. If a later draw in the same pass reads a
// texture in its vertex shader, the encoder also needs its earlier wait before vertices.
#include "d3d12_test.hpp"

static const char hlsl[] = R"hlsl(
cbuffer Parameters : register(b0) { uint value, work, mask; };
Texture2D<uint> source : register(t0);
struct Vertex { float4 position : SV_Position; nointerpolation uint texel : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
  Vertex v = {float4(id == 1 ? 3 : -1, id == 2 ? -3 : 1, 0, 1), 0};
  return v;
}
Vertex vs_read(uint id : SV_VertexID) {
  Vertex v = vs(id);
  v.texel = source.Load(int3(0, 0, 0));
  return v;
}
uint ps_write(Vertex v) : SV_Target {
  uint x = uint(v.position.x) ^ uint(v.position.y);
  [loop] for (uint i = 0; i < work; i++)
    x = x * 1664525u + 1013904223u;
  return value + (x & mask);
}
uint ps_read(Vertex v) : SV_Target { return source.Load(int3(0, 0, 0)) + value; }
uint ps_vertex(Vertex v) : SV_Target { return v.texel + value; }
)hlsl";

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler))
    return 77;
  auto vs = compiler.compile(hlsl, "vs", "vs"), vs_read = compiler.compile(hlsl, "vs_read", "vs");
  auto ps_write = compiler.compile(hlsl, "ps_write", "ps"), ps_read = compiler.compile(hlsl, "ps_read", "ps"),
       ps_vertex = compiler.compile(hlsl, "ps_vertex", "ps");
  if (!expect(!vs.empty() && !vs_read.empty() && !ps_write.empty() && !ps_read.empty() && !ps_vertex.empty(),
              "HLSL did not compile"))
    return verdict();
  if (argc > 2 && !strcmp(argv[2], "shaders"))
    return verdict();

  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1};
  D3D12_ROOT_PARAMETER parameters[] = {{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS},
                                       {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}};
  parameters[0].Constants.Num32BitValues = 3;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].DescriptorTable = {1, &range};
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  auto pixel = root_signature(device.Get(), {UINT(std::size(parameters)), parameters});
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  auto vertex = root_signature(device.Get(), {UINT(std::size(parameters)), parameters});
  CHECK(pixel && vertex ? S_OK : E_FAIL);
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{pixel.Get(), bytecode(vs), bytecode(ps_write)};
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = ~0u;
  desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc = {1, 0};
  ComPtr<ID3D12PipelineState> writing, reading, reading_vertex;
  CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&writing)));
  desc.PS = bytecode(ps_read);
  CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&reading)));
  desc.pRootSignature = vertex.Get();
  desc.VS = bytecode(vs_read);
  desc.PS = bytecode(ps_vertex);
  CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&reading_vertex)));

  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));

  constexpr UINT side = 128;
  constexpr auto rt = D3D12_RESOURCE_STATE_RENDER_TARGET, ps = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 vs_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  D3D12_RESOURCE_DESC texture_desc{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, side, side, 1, 1, DXGI_FORMAT_R32_UINT,
                                  {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
  D3D12_HEAP_PROPERTIES properties{D3D12_HEAP_TYPE_DEFAULT};
  ComPtr<ID3D12Resource> textures[3];
  ComPtr<ID3D12DescriptorHeap> rtv_heap, srv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3};
  D3D12_DESCRIPTOR_HEAP_DESC srv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2,
                                    D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  CHECK(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)));
  CHECK(device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap)));
  UINT rtv_step = device->GetDescriptorHandleIncrementSize(rtv_desc.Type);
  UINT srv_step = device->GetDescriptorHandleIncrementSize(srv_desc.Type);
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
  for (UINT i = 0; i < std::size(textures); i++) {
    CHECK(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture_desc, i < 2 ? rt : ps,
                                          nullptr, IID_PPV_ARGS(&textures[i])));
    rtvs[i] = {rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * rtv_step};
    device->CreateRenderTargetView(textures[i].Get(), nullptr, rtvs[i]);
    if (i < 2)
      device->CreateShaderResourceView(textures[i].Get(), nullptr,
                                       {srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * srv_step});
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  UINT64 image_bytes;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr, nullptr, &image_bytes);
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, 2 * image_bytes, D3D12_RESOURCE_STATE_COPY_DEST);
  CHECK(readback ? S_OK : E_FAIL);
  CHECK(forget(readback.Get()));

  ID3D12DescriptorHeap *heaps[] = {srv_heap.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT viewport{0, 0, float(side), float(side), 0, 1};
  D3D12_RECT scissor{0, 0, LONG(side), LONG(side)};
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  auto draw = [&](ID3D12PipelineState *pipeline, ID3D12RootSignature *root, UINT srv, UINT value, UINT work = 0) {
    list->SetPipelineState(pipeline);
    list->SetGraphicsRootSignature(root);
    list->SetGraphicsRootDescriptorTable(1, {srv_heap->GetGPUDescriptorHandleForHeapStart().ptr + srv * srv_step});
    const UINT constants[] = {value, work, 0};
    list->SetGraphicsRoot32BitConstants(0, UINT(std::size(constants)), constants, 0);
    list->DrawInstanced(3, 1, 0, 0);
  };
  for (UINT use_vertex = 0; use_vertex < 2; use_vertex++) {
    list->OMSetRenderTargets(1, &rtvs[0], FALSE, nullptr);
    // Use different values so a vertex reading before the slow draw can see the preceding iteration's result.
    draw(writing.Get(), pixel.Get(), 0, 16 + use_vertex, 8192);
    transition(list.Get(), textures[0].Get(), rt, vs_state);
    list->OMSetRenderTargets(1, &rtvs[1], FALSE, nullptr);
    draw(writing.Get(), pixel.Get(), 0, 21);
    transition(list.Get(), textures[1].Get(), rt, ps);
    transition(list.Get(), textures[2].Get(), ps, rt);
    list->OMSetRenderTargets(1, &rtvs[2], FALSE, nullptr);
    draw(reading.Get(), pixel.Get(), 1, 3);
    if (use_vertex)
      draw(reading_vertex.Get(), vertex.Get(), 0, 5);
    transition(list.Get(), textures[2].Get(), rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{textures[2].Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint = footprint;
    dst.PlacedFootprint.Offset = use_vertex * image_bytes;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    transition(list.Get(), textures[2].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, ps);
    transition(list.Get(), textures[1].Get(), ps, rt);
    transition(list.Get(), textures[0].Get(), vs_state, rt);
  }
  CHECK(submit(device.Get(), queue.Get(), list.Get()));
  BYTE *bytes;
  CHECK(readback->Map(0, nullptr, reinterpret_cast<void **>(&bytes)));
  for (UINT use_vertex = 0; use_vertex < 2; use_vertex++) {
    UINT wrong = 0;
    for (UINT y = 0; y < side; y++)
      for (UINT x = 0; x < side; x++) {
        UINT value;
        memcpy(&value, bytes + use_vertex * image_bytes + y * footprint.Footprint.RowPitch + x * sizeof(UINT),
               sizeof(value));
        wrong += value != (use_vertex ? 22 : 24);
      }
    expect(!wrong, "%s: %u pixels differ", use_vertex ? "vertex dependency within render pass" : "pixel dependency",
           wrong);
  }
  readback->Unmap(0, nullptr);
  return verdict();
}
