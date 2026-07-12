cmake_minimum_required(VERSION 3.21)

project(Cuelet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Test)

qt_standard_project_setup()

add_library(cuelet_core STATIC
    src/app/SoundLibrary.cpp
    src/app/SoundLibrary.h
    src/core/LibraryScanner.cpp
    src/core/LibraryScanner.h
    src/core/SoundClip.h
    src/core/SoundFilter.cpp
    src/core/SoundFilter.h
    src/storage/AppSettings.cpp
    src/storage/AppSettings.h
    src/storage/LegacySettingsImporter.cpp
    src/storage/LegacySettingsImporter.h
    src/storage/MetadataStore.cpp
    src/storage/MetadataStore.h
)
target_include_directories(cuelet_core PUBLIC src)
target_link_libraries(cuelet_core PUBLIC Qt6::Core)

add_library(cuelet_audio STATIC
    src/audio/AudioService.h
    src/audio/QtAudioService.cpp
    src/audio/QtAudioService.h
)
target_include_directories(cuelet_audio PUBLIC src)
target_link_libraries(cuelet_audio PUBLIC Qt6::Core Qt6::Multimedia)

qt_add_executable(Cuelet
    src/main.cpp
    src/platform/PlatformInfo.cpp
    src/platform/PlatformInfo.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MetadataDialog.cpp
    src/ui/MetadataDialog.h
)

target_sources(Cuelet PRIVATE
    resources/app_icon.icns
    resources/app_icon.svg
)
set_source_files_properties(resources/app_icon.icns resources/app_icon.svg PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

qt_add_resources(Cuelet "cuelet_resources"
    PREFIX "/"
    FILES
        resources/app_icon.svg
        resources/icons/edit.svg
        resources/icons/folder-plus.svg
        resources/icons/import.svg
        resources/icons/info.svg
        resources/icons/library.svg
        resources/icons/play.svg
        resources/icons/refresh.svg
        resources/icons/search.svg
        resources/icons/settings.svg
        resources/icons/star.svg
        resources/icons/stop.svg
        resources/icons/tags.svg
        resources/icons/waveform.svg
)

target_link_libraries(Cuelet PRIVATE
    cuelet_core
    cuelet_audio
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
)

set_target_properties(Cuelet PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME Cuelet
    MACOSX_BUNDLE_GUI_IDENTIFIER io.cuelet.Cuelet
    MACOSX_BUNDLE_ICON_FILE app_icon.icns
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
)

install(TARGETS Cuelet
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

enable_testing()

add_subdirectory(tests)
