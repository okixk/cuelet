set(CUELET_TESTS
    test_library_scanner
    test_metadata_store
    test_legacy_settings_importer
    test_sound_filter
    test_path_handling
    test_smoke
)

foreach(test_name IN LISTS CUELET_TESTS)
    qt_add_executable(${test_name}
        ${test_name}.cpp
    )
    target_link_libraries(${test_name} PRIVATE cuelet_core Qt6::Core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
