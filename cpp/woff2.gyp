{
  'variables': {
    'ots_include_dirs': [
       # This isn't particularly elegant, but it works
       '../ots-read-only/include',
       '../ots-read-only/src',
     ],
  },
  'target_defaults': {
    'defines': [
      'OTS_DEBUG',
    ],
  },
  'targets': [
    {
      'target_name': 'woff2',
      'type': 'static_library',
      'sources': [
        'woff2.cc',
      ],
      'include_dirs': [
        '<@(ots_include_dirs)',
      ],
      'dependencies': [
        '../ots-read-only/ots-standalone.gyp:ots',
      ],
    },
    {
      'target_name': 'woff2-decompress',
      'type': 'executable',
      'sources': [
        'woff2-decompress.cc',
      ],
      'include_dirs': [
        '<@(ots_include_dirs)',
      ],
      'dependencies': [
        'woff2',
      ],
    },
  ],
}

