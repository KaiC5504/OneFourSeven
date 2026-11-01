add_library(ofs_core STATIC
    src/version.cpp
    src/log.cpp
    src/log_schema.cpp
    src/params.cpp
    src/table_geometry.cpp
)

target_include_directories(ofs_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/thirdparty
)

set_target_properties(ofs_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(ofs_core PRIVATE /W4)
else()
    target_compile_options(ofs_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Absolute path to core/data so tests read the same files the game embeds.
target_compile_definitions(ofs_core PUBLIC OFS_CORE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

if(OFS_BUILD_TESTS)
    add_executable(ofs_core_tests
        tests/main.cpp
        tests/test_log.cpp
        tests/test_log_schema.cpp
        tests/test_params.cpp
        tests/test_table_geometry.cpp
    )
    target_link_libraries(ofs_core_tests PRIVATE ofs_core)
    target_compile_definitions(ofs_core_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_core_tests COMMAND ofs_core_tests)

    # Prints content_hash for each JSONL line on stdin; tools/test_devlink.py compares it
    # with devlink.py's hash over thousands of values.
    add_executable(ofs_params_hash tools/params_hash.cpp)
    target_link_libraries(ofs_params_hash PRIVATE ofs_core)
endif()
