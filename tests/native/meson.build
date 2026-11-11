native_sharing = executable('native_sharing', 'sharing.cpp',
  dependencies: [directx_native_dep],
  include_directories: [mullion_include_path, include_directories('../../src/util')],
)

test('native_sharing', native_sharing,
  args: [mullion_native_lib],
  env: ['MULLION_LOG_PATH=none'],
  suite: 'gpu',
)
