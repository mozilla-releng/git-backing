{
  'target_defaults': {
    'configurations': {
      'Release': {},
    },
  },
  'targets': [
    {
      'target_name': 'a',
      'type': 'static_library',
      'dependencies': ['b'],
    },
    {
      'target_name': 'b',
      'type': 'static_library',
    },
    {
      'target_name': 'c',
      'type': 'static_library',
    },
  ],
}
