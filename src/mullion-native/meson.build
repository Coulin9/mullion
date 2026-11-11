native_modules = [ dxgi_lib, d3d11_lib, winemetal_lib ]
native_deps = [ dxgi_dep, d3d11_dep, winemetal_dep, mullion_dep, util_dep, airconv_dep_darwin, directx_native_dep ]
native_exports = [ files('mullion_native.exports') ]

if get_option('enable_d3d12')
  native_modules += d3d12_dll
  native_deps += d3d12_dep
  native_exports += files('mullion_native_d3d12.exports')
endif

export_list = custom_target('mullion-native-exports',
  input: native_exports,
  output: 'mullion-native.exports',
  command: [ 'cat', '@INPUT@' ],
  capture: true,
)

mullion_native_lib = shared_library('mullion-native', 'mullion_native.cpp', 'virtual_memory.cpp',
  link_whole: native_modules,
  dependencies: native_deps,
  include_directories: [ mullion_include_path ],
  link_args: [ '-Wl,-exported_symbols_list,' + join_paths(meson.current_build_dir(), 'mullion-native.exports') ],
  link_depends: [ export_list ],
  install: true,
)
