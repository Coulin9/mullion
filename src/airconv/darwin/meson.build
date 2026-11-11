native_llvm_path = get_option('native_llvm_path')

if not native_llvm_path.startswith('/')
  # Meson only allows relative include paths in case the toolchains are located inside
  # of the project source root, so handle that here.
  llvm_include_path_darwin = include_directories(join_paths('../../..', native_llvm_path, 'include'))
  native_llvm_path = join_paths(meson.project_source_root(), native_llvm_path)
else
  llvm_include_path_darwin = include_directories(join_paths(native_llvm_path, 'include'))
endif

llvm_ld_flags_darwin = [
  '-lm', '-lz', '-lcurses', '-lxml2'
]

# Link the LLVM archives explicitly. Adding their directory to the global
# linker search also picks up the toolchain's libc++ and libunwind dylibs.
llvm_archives_darwin = []
foreach dep : llvm_deps
  llvm_archives_darwin += native_llvm_path / 'lib' / ('lib' + dep.substring(2) + '.a')
endforeach

if native_llvm_path.startswith('/usr/local/opt') or native_llvm_path.startswith('/opt/homebrew/opt')
  homebrew_prefix = native_llvm_path.startswith('/opt/homebrew/opt') ? '/opt/homebrew' : '/usr/local'
  llvm_ld_flags_darwin = [
    llvm_ld_flags_darwin,
    join_paths(homebrew_prefix, 'opt/zstd/lib/libzstd.a'), join_paths(native_llvm_path, 'lib/libunwind.a')
  ]
endif

airconv_lib_darwin = static_library('airconv', airconv_src,
  include_directories : [ mullion_include_path, llvm_include_path_darwin ],
  cpp_args            : [ airconv_args ],
  dependencies        : [ dxbc_parser_native_dep ],
  link_args           : llvm_ld_flags_darwin,
  native              : mullion_crossbuild
)

airconv_dep_darwin = declare_dependency(
  link_with           : [ airconv_lib_darwin ],
  include_directories : [ mullion_include_path, include_directories('..') ],
  link_args           : [ llvm_ld_flags_darwin, llvm_archives_darwin ]
)

executable('airconv', airconv_src + airconv_cli_src,
  include_directories : [ mullion_include_path, llvm_include_path_darwin ],
  cpp_args            : [ airconv_args ],
  dependencies        : [ dxbc_parser_native_dep ],
  link_args           : [ llvm_ld_flags_darwin, llvm_archives_darwin ],
  native              : mullion_crossbuild
)
