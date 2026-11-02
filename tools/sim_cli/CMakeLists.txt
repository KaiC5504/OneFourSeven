# The simulation CLI (spec 21, M1): simulate, replay, fuzz, bench. Its owner adds files freely,
# hence the glob.
file(GLOB OFS_SIM_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(ofs_sim_cli ${OFS_SIM_CLI_SOURCES})
target_link_libraries(ofs_sim_cli PRIVATE ofs_core)
if(MSVC)
    target_compile_options(ofs_sim_cli PRIVATE /W4)
else()
    target_compile_options(ofs_sim_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_definitions(ofs_sim_cli PRIVATE
    OFS_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/core/tests/fixtures")

# The commit at configure time, so a logged shot names the code that ran it. A later commit
# without a reconfigure keeps the old sha; the core version in the same field still changes.
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE OFS_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE OFS_GIT_RESULT)
    if(OFS_GIT_RESULT EQUAL 0 AND OFS_GIT_SHA)
        target_compile_definitions(ofs_sim_cli PRIVATE OFS_GIT_SHA="${OFS_GIT_SHA}")
    endif()
endif()
