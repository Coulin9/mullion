// The embedded ABI exports GPU-written texture data and a pollable fence to Neptune.
#include <windows.h>
#include <d3d12.h>
#include <directx-native/dxn_embedder.h>
#include <directx-native/dxn_shared.h>
#include "com/com_pointer.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                                                      \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)
#define CHECK_HR(call) CHECK(SUCCEEDED(call))

using mullion::Com;

int
main(int argc, char **argv) {
  CHECK(argc == 2);
  void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!module) {
    std::fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  auto create_device = reinterpret_cast<decltype(&D3D12CreateDevice)>(dlsym(module, "D3D12CreateDevice"));
  auto native_abi = reinterpret_cast<PFN_dxn_native_abi_version>(dlsym(module, "mullion_native_abi_version"));
  auto set_executable_path = reinterpret_cast<PFN_dxn_set_executable_path>(dlsym(module, "mullion_set_executable_path"));
  auto event_create = reinterpret_cast<PFN_dxn_event_create>(dlsym(module, "mullion_event_create"));
  auto event_close = reinterpret_cast<PFN_dxn_event_close>(dlsym(module, "mullion_event_close"));
  auto event_dup_fd = reinterpret_cast<PFN_dxn_event_dup_fd>(dlsym(module, "mullion_event_dup_fd"));
  auto close_shared = reinterpret_cast<PFN_dxn_shared_handle_close>(dlsym(module, "mullion_shared_handle_close"));
  CHECK(create_device && native_abi && event_create && event_close && event_dup_fd && close_shared);
  CHECK(native_abi() == DXN_ABI_VERSION);
  CHECK(set_executable_path && set_executable_path(nullptr) == 0);
  CHECK(set_executable_path("C:\\Games\\..") < 0);
  CHECK(set_executable_path("\xff.exe") < 0);
  CHECK(set_executable_path("C:\\Games\\native_sharing.exe") == 0);

  Com<ID3D12Device> device;
  CHECK_HR(create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  Com<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  CHECK_HR(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  Com<ID3D12CommandAllocator> allocator;
  CHECK_HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  Com<ID3D12GraphicsCommandList> list;
  CHECK_HR(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr(), nullptr, IID_PPV_ARGS(&list)));

  D3D12_RESOURCE_DESC texture_desc{};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 61;
  texture_desc.Height = 17;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
  Com<ID3D12Resource> texture;
  auto flags = D3D12_HEAP_FLAGS(D3D12_HEAP_FLAG_SHARED | DXN_MISC_LINEAR_EXPORT);
  CHECK_HR(device->CreateCommittedResource(&heap, flags, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&texture)));
  D3D12_RESOURCE_DESC reported_desc{};
  texture->GetDesc(&reported_desc);
  CHECK(reported_desc.Width == texture_desc.Width && reported_desc.Height == texture_desc.Height);

  HANDLE shared = nullptr;
  constexpr DWORD generic_all = 0x10000000u;
  CHECK_HR(device->CreateSharedHandle(texture.ptr(), nullptr, generic_all, nullptr, &shared));
  auto *descriptor = static_cast<const dxn_shared_texture *>(shared);
  CHECK(dxn_shared_check(descriptor, DXN_SHARED_TEXTURE_MAGIC, sizeof(*descriptor)));
  CHECK(descriptor->meta.dxgi_format == texture_desc.Format && descriptor->meta.width == texture_desc.Width &&
        descriptor->meta.height == texture_desc.Height);
  CHECK(descriptor->plane_count == 1 && descriptor->planes[0].offset == 0 && descriptor->drm_modifier == 0 &&
        descriptor->planes[0].pitch >= texture_desc.Width * 4 &&
        descriptor->size == descriptor->planes[0].pitch * texture_desc.Height);
  dxn_shared_texture exported = *descriptor;
  exported.fd = fcntl(descriptor->fd, F_DUPFD_CLOEXEC, 0);
  CHECK(exported.fd >= 0);
  CHECK(close_shared(shared) == 0);

  Com<ID3D12Resource> imported;
  auto invalid = exported;
  invalid.planes[0].pitch = 4;
  CHECK(FAILED(device->OpenSharedHandle(&invalid, IID_PPV_ARGS(&imported))) && !imported);
  CHECK_HR(device->OpenSharedHandle(&exported, IID_PPV_ARGS(&imported)));

  D3D12_TEXTURE_COPY_LOCATION source{};
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  uint64_t upload_size = 0;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &source.PlacedFootprint, nullptr, nullptr, &upload_size);
  CHECK(upload_size > 0);
  D3D12_RESOURCE_DESC upload_desc{};
  upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  upload_desc.Width = upload_size;
  upload_desc.Height = 1;
  upload_desc.DepthOrArraySize = 1;
  upload_desc.MipLevels = 1;
  upload_desc.SampleDesc.Count = 1;
  upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  Com<ID3D12Resource> upload;
  CHECK_HR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&upload)));
  D3D12_RANGE empty{0, 0};
  void *mapped = nullptr;
  CHECK_HR(upload->Map(0, &empty, &mapped));
  auto pixel_byte = [](uint32_t row, uint32_t column) { return uint8_t(row * 11 + column * 37); };
  for (uint32_t row = 0; row < texture_desc.Height; row++)
    for (uint32_t column = 0; column < texture_desc.Width * 4; column++)
      static_cast<uint8_t *>(mapped)[row * source.PlacedFootprint.Footprint.RowPitch + column] = pixel_byte(row, column);
  upload->Unmap(0, nullptr);
  source.pResource = upload.ptr();
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = texture.ptr();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  CHECK_HR(list->Close());

  Com<ID3D12Fence1> shared_fence;
  CHECK_HR(device->CreateFence(4, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&shared_fence)));
  CHECK(shared_fence->GetCreationFlags() == D3D12_FENCE_FLAG_SHARED);
  CHECK(shared_fence->GetCompletedValue() == 4);
  CHECK(device->CreateSharedHandle(shared_fence.ptr(), nullptr, generic_all, nullptr, &shared) == E_NOTIMPL && !shared);
  Com<ID3D12Fence> cross_adapter;
  CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER, IID_PPV_ARGS(&cross_adapter)) == E_NOTIMPL &&
        !cross_adapter);

  Com<ID3D12CommandQueue> producer;
  CHECK_HR(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&producer)));
  Com<ID3D12Fence> fence;
  CHECK_HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  HANDLE event = event_create(1, 0);
  CHECK(event);
  int event_fd = event_dup_fd(event);
  CHECK(event_fd >= 0);
  CHECK_HR(fence->SetEventOnCompletion(1, event));
  event_close(event);
  CHECK_HR(queue->Wait(shared_fence.ptr(), 5));
  CHECK_HR(queue->Signal(fence.ptr(), 1));
  struct pollfd notification{event_fd, POLLIN, 0};
  CHECK(poll(&notification, 1, 50) == 0);
  ID3D12CommandList *commands[] = {list.ptr()};
  producer->ExecuteCommandLists(1, commands);
  CHECK_HR(producer->Signal(shared_fence.ptr(), 5));
  CHECK(poll(&notification, 1, 10000) == 1 && (notification.revents & POLLIN));
  CHECK(fence->GetCompletedValue() == 1);
  CHECK(shared_fence->GetCompletedValue() == 5);
  close(event_fd);

  mapped = mmap(nullptr, exported.size, PROT_READ, MAP_SHARED, exported.fd, 0);
  CHECK(mapped != MAP_FAILED);
  auto *pixels = static_cast<const uint8_t *>(mapped);
  for (uint32_t row = 0; row < texture_desc.Height; row++)
    for (uint32_t column = 0; column < texture_desc.Width * 4; column++)
      CHECK(pixels[row * exported.planes[0].pitch + column] == pixel_byte(row, column));
  munmap(mapped, exported.size);
  close(exported.fd);
  return 0;
}
