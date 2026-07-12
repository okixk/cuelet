project(
  'cuelet-linux',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
  ],
)

gnome = import('gnome')

gtk_dep = dependency('gtk4', version: '>=4.10')
adw_dep = dependency('libadwaita-1', version: '>=1.4')
gst_dep = dependency('gstreamer-1.0', version: '>=1.20')
gst_pbutils_dep = dependency('gstreamer-pbutils-1.0', version: '>=1.20')
json_glib_dep = dependency('json-glib-1.0')

core_include = include_directories('../../core/cuelet-core/include')
linux_include = include_directories('src')

resources = gnome.compile_resources(
  'cuelet-linux-resources',
  'resources/cuelet.gresource.xml',
  source_dir: 'resources',
  c_name: 'cuelet_linux',
)

core_sources = files(
  '../../core/cuelet-core/src/SoundTypes.cpp',
  '../../core/cuelet-core/src/LibraryScanner.cpp',
  '../../core/cuelet-core/src/SoundSearch.cpp',
  '../../core/cuelet-core/src/MetadataStore.cpp',
)

cuelet_core = static_library(
  'cuelet_core',
  core_sources,
  include_directories: core_include,
  dependencies: [json_glib_dep],
)

linux_sources = files(
  'src/main.cpp',
  'src/CueletCli.cpp',
  'src/CueletWindow.cpp',
  'src/CueletWindowCli.cpp',
  'src/CueletWindowPreferences.cpp',
  'src/CueletWindowState.cpp',
  'src/CueletWindowWidgets.cpp',
  'src/services/LinuxAudioService.cpp',
  'src/services/LinuxSettingsStore.cpp',
)

executable(
  'cuelet',
  linux_sources,
  resources,
  include_directories: [linux_include, core_include],
  dependencies: [gtk_dep, adw_dep, gst_dep, gst_pbutils_dep, json_glib_dep],
  link_with: cuelet_core,
  install: true,
)

test_core = executable(
  'cuelet-core-tests',
  files('tests/core_tests.cpp'),
  include_directories: core_include,
  dependencies: [json_glib_dep],
  link_with: cuelet_core,
)

test_audio_service = executable(
  'cuelet-audio-service-tests',
  files(
    'tests/audio_service_tests.cpp',
    'src/services/LinuxAudioService.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [gst_dep, gst_pbutils_dep],
)

test_category_helpers = executable(
  'cuelet-category-helper-tests',
  files('tests/category_helper_tests.cpp'),
  include_directories: [linux_include, core_include],
  dependencies: [gtk_dep, adw_dep],
)

test_cli = executable(
  'cuelet-cli-tests',
  files(
    'tests/cli_tests.cpp',
    'src/CueletCli.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [json_glib_dep],
  link_with: cuelet_core,
)

test('core', test_core)
test('audio-service', test_audio_service)
test('cli', test_cli)
test('category-helpers', test_category_helpers)
