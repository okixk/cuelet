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
bash = find_program('bash')
python = find_program('python3')

version_configuration = configuration_data()
version_configuration.set_quoted('CUELET_VERSION', meson.project_version())
configure_file(
  output: 'CueletVersion.h',
  configuration: version_configuration,
)

gtk_dep = dependency('gtk4', version: '>=4.10')
adw_dep = dependency('libadwaita-1', version: '>=1.5')
gio_dep = dependency('gio-2.0')
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
  'src/CueletAboutDialog.cpp',
  'src/CueletCli.cpp',
  'src/CueletPopoverLifecycle.cpp',
  'src/CueletWindow.cpp',
  'src/CueletWindowAbout.cpp',
  'src/CueletWindowCli.cpp',
  'src/CueletWindowLibrary.cpp',
  'src/CueletWindowManagement.cpp',
  'src/CueletWindowPreferences.cpp',
  'src/CueletWindowRouting.cpp',
  'src/CueletWindowShortcuts.cpp',
  'src/CueletWindowState.cpp',
  'src/CueletWindowVisualCapture.cpp',
  'src/CueletWindowWidgets.cpp',
  'src/services/LinuxAudioService.cpp',
  'src/services/GioGlobalShortcutsPortalAdapter.cpp',
  'src/services/LinuxGlobalShortcutsController.cpp',
  'src/services/LinuxLibraryImportService.cpp',
  'src/services/LinuxLibraryManagementService.cpp',
  'src/services/LinuxPipeWireRoutingPlan.cpp',
  'src/services/LinuxPipeWireRoutingService.cpp',
  'src/services/LinuxSettingsStore.cpp',
  'src/services/LinuxVirtualMicrophoneService.cpp',
  'src/services/PipeWireVirtualMicrophoneBackend.cpp',
)

executable(
  'cuelet',
  linux_sources,
  resources,
  include_directories: [linux_include, core_include],
  dependencies: [gtk_dep, adw_dep, gio_dep, gst_dep, gst_pbutils_dep, json_glib_dep],
  link_with: cuelet_core,
  install: true,
)

install_data(
  'data/io.cuelet.Cuelet.desktop',
  install_dir: get_option('datadir') / 'applications',
)

padded_application_icon = custom_target(
  'padded-application-icon',
  input: 'data/io.cuelet.Cuelet.svg',
  output: 'io.cuelet.Cuelet.svg',
  command: [
    python,
    files('scripts/generate-padded-icon.py'),
    '@INPUT@',
    '@OUTPUT@',
  ],
  build_by_default: true,
  install: true,
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

install_data(
  'data/io.cuelet.Cuelet.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)

install_data(
  'data/INSTALL.md',
  rename: 'README.md',
  install_dir: get_option('datadir') / 'doc' / 'cuelet',
)

install_data(
  'data/installed-files.txt',
  install_dir: get_option('datadir') / 'doc' / 'cuelet',
)

install_data(
  '../../LICENSE',
  install_dir: get_option('datadir') / 'licenses' / 'cuelet',
)

gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)

test_core = executable(
  'cuelet-core-tests',
  files('tests/core_tests.cpp'),
  include_directories: core_include,
  dependencies: [json_glib_dep],
  link_with: cuelet_core,
)

test_metadata_store = executable(
  'cuelet-metadata-store-tests',
  files('tests/metadata_store_tests.cpp'),
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

test_popover_lifecycle = executable(
  'cuelet-popover-lifecycle-tests',
  files(
    'tests/popover_lifecycle_tests.cpp',
    'src/CueletPopoverLifecycle.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [gtk_dep],
)

test_about_dialog = executable(
  'cuelet-about-dialog-tests',
  files(
    'tests/about_dialog_tests.cpp',
    'src/CueletAboutDialog.cpp',
  ),
  resources,
  include_directories: linux_include,
  dependencies: [gtk_dep, adw_dep],
)

test_global_shortcuts = executable(
  'cuelet-global-shortcuts-tests',
  files(
    'tests/global_shortcuts_tests.cpp',
    'src/services/LinuxGlobalShortcutsController.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [gtk_dep, gio_dep, json_glib_dep],
  link_with: cuelet_core,
)

test_settings_store = executable(
  'cuelet-settings-store-tests',
  files(
    'tests/settings_store_tests.cpp',
    'src/services/LinuxSettingsStore.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [json_glib_dep],
)

test_library_import = executable(
  'cuelet-library-import-tests',
  files(
    'tests/library_import_service_tests.cpp',
    'src/services/LinuxLibraryImportService.cpp',
    'src/services/LinuxLibraryManagementService.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [json_glib_dep],
  link_with: cuelet_core,
)

test_pipewire_routing = executable(
  'cuelet-pipewire-routing-tests',
  files(
    'tests/pipewire_routing_tests.cpp',
    'src/services/LinuxPipeWireRoutingPlan.cpp',
  ),
  include_directories: linux_include,
)

test_pipewire_routing_service = executable(
  'cuelet-pipewire-routing-service-tests',
  files(
    'tests/pipewire_routing_service_tests.cpp',
    'src/services/LinuxPipeWireRoutingPlan.cpp',
    'src/services/LinuxPipeWireRoutingService.cpp',
  ),
  include_directories: linux_include,
  dependencies: [gio_dep],
)

test_virtual_microphone = executable(
  'cuelet-virtual-microphone-tests',
  files(
    'tests/virtual_microphone_tests.cpp',
    'src/services/LinuxVirtualMicrophoneService.cpp',
  ),
  include_directories: [linux_include, core_include],
  dependencies: [gio_dep, gst_dep],
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
test('metadata-store', test_metadata_store)
test('audio-service', test_audio_service)
test('cli', test_cli)
test('category-helpers', test_category_helpers)
test('popover-lifecycle', test_popover_lifecycle)
test('about-dialog', test_about_dialog)
test('global-shortcuts', test_global_shortcuts)
test('settings-store', test_settings_store)
test('library-import', test_library_import)
test('pipewire-routing', test_pipewire_routing)
test('pipewire-routing-service', test_pipewire_routing_service)
test('virtual-microphone', test_virtual_microphone)
test(
  'release-metadata',
  bash,
  args: files('tests/release_metadata_tests.sh'),
)
test(
  'padded-icon',
  python,
  args: [
    files('tests/padded_icon_tests.py'),
    files('scripts/generate-padded-icon.py'),
    files('data/io.cuelet.Cuelet.svg'),
    padded_application_icon,
  ],
  depends: padded_application_icon,
)
