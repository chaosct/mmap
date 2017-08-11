{
  "targets": [
    {
      "target_name": "mmap",
      "sources": [ "mmap.cpp" ],
      "conditions": [
        [ 'OS=="mac"', {
          'xcode_settings': {
            'CLANG_CXX_LIBRARY': 'libc++',
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
      }]]
    }
  ]
}
