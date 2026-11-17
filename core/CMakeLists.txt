add_library(ofs_core STATIC
    src/version.cpp
    src/log.cpp
    src/log_schema.cpp
    src/params.cpp
    src/table_geometry.cpp
    src/rng.cpp
    src/math/roots.cpp
    src/physics/physics_params.cpp
    src/physics/serialize.cpp
    src/physics/motion.cpp
    src/physics/nap.cpp
    src/physics/detect.cpp
    src/physics/collide_ball.cpp
    src/physics/collide_cushion.cpp
    src/physics/table_shape.cpp
    src/physics/cue.cpp
    src/physics/shot_record.cpp
    src/physics/simulator.cpp
    src/physics/repro.cpp
    src/physics/rack.cpp
    src/physics/record_query.cpp
    src/rules/names.cpp
    src/rules/referee_params.cpp
    src/rules/frame_state.cpp
    src/rules/engine.cpp
    src/rules/geometry.cpp
    src/rules/spotting.cpp
    src/rules/observe.cpp
    src/rules/match.cpp
    src/rules/serialize.cpp
    src/rules/rules_log.cpp
    src/opponent/standin.cpp
    src/opponent/level.cpp
    src/opponent/pots.cpp
    src/opponent/solve.cpp
    src/opponent/error_model.cpp
    src/opponent/evaluate.cpp
    src/opponent/search.cpp
    src/opponent/decide.cpp
    src/opponent/maximum.cpp
    src/opponent/tactics.cpp
    src/opponent/exhibition.cpp
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
        tests/test_rng.cpp
        tests/physics/test_physics_params.cpp
        tests/physics/test_serialize.cpp
        tests/physics/test_roots.cpp
        tests/physics/test_detect.cpp
        tests/physics/test_motion.cpp
        tests/physics/test_nap.cpp
        tests/physics/test_collide_ball.cpp
        tests/physics/test_collide_cushion.cpp
        tests/physics/test_table_shape.cpp
        tests/physics/test_cue.cpp
        tests/physics/test_shot_record.cpp
        tests/physics/test_simulator.cpp
        tests/physics/test_simulator_regress.cpp
        tests/physics/test_repro.cpp
        tests/physics/test_rack.cpp
        tests/physics/test_record_query.cpp
        tests/physics/test_record_query_impl.cpp
    )
    target_link_libraries(ofs_core_tests PRIVATE ofs_core)
    target_compile_definitions(ofs_core_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_core_tests COMMAND ofs_core_tests)

    # The independent calibration suite (spec 7.11, C1-C14). Its owner adds files freely, hence the
    # glob. The gate runs it in the Release physics lane (label calibration).
    file(GLOB OFS_CALIBRATION_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/calibration/*.cpp)
    add_executable(ofs_calibration_tests ${OFS_CALIBRATION_SOURCES})
    target_link_libraries(ofs_calibration_tests PRIVATE ofs_core)
    target_compile_definitions(ofs_calibration_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_calibration_tests COMMAND ofs_calibration_tests)
    set_tests_properties(ofs_calibration_tests PROPERTIES LABELS calibration)

    # The rules scenarios (spec 16.2): one test per rulebook clause, written from the rulebook by
    # agents who own only tests/rules. tests/rules_unit holds the builders' own tests of the same
    # code. Both sets of owners add files freely, hence the globs.
    file(GLOB OFS_RULES_SCENARIO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/*.cpp)
    add_executable(ofs_rules_tests tests/main.cpp ${OFS_RULES_SCENARIO_SOURCES})
    target_link_libraries(ofs_rules_tests PRIVATE ofs_core)
    target_include_directories(ofs_rules_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/rules)
    target_compile_definitions(ofs_rules_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_rules_tests COMMAND ofs_rules_tests)

    file(GLOB OFS_RULES_UNIT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/rules_unit/*.cpp)
    add_executable(ofs_rules_unit_tests tests/main.cpp ${OFS_RULES_UNIT_SOURCES})
    target_link_libraries(ofs_rules_unit_tests PRIVATE ofs_core)
    target_include_directories(ofs_rules_unit_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/rules ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(ofs_rules_unit_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_rules_unit_tests COMMAND ofs_rules_unit_tests)

    # The opponent (spec 10, 16.4). tests/opponent is written from the spec and the table's geometry by
    # agents who own only that folder; tests/opponent_unit holds the builders' own tests. Both sets of
    # owners add files freely, hence the globs. tests/opponent/rig.hpp is shared by both.
    file(GLOB OFS_OPPONENT_SPEC_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/opponent/*.cpp)
    add_executable(ofs_opponent_tests tests/main.cpp ${OFS_OPPONENT_SPEC_SOURCES})
    target_link_libraries(ofs_opponent_tests PRIVATE ofs_core)
    target_include_directories(ofs_opponent_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/rules ${CMAKE_CURRENT_SOURCE_DIR}/tests/opponent)
    target_compile_definitions(ofs_opponent_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_opponent_tests COMMAND ofs_opponent_tests)

    file(GLOB OFS_OPPONENT_UNIT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/opponent_unit/*.cpp)
    add_executable(ofs_opponent_unit_tests tests/main.cpp ${OFS_OPPONENT_UNIT_SOURCES})
    target_link_libraries(ofs_opponent_unit_tests PRIVATE ofs_core)
    target_include_directories(ofs_opponent_unit_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/rules ${CMAKE_CURRENT_SOURCE_DIR}/tests/opponent
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(ofs_opponent_unit_tests PRIVATE
        OFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ofs_opponent_unit_tests COMMAND ofs_opponent_unit_tests)

    # Prints content_hash for each JSONL line on stdin; tools/test_devlink.py compares it
    # with devlink.py's hash over thousands of values.
    add_executable(ofs_params_hash tools/params_hash.cpp)
    target_link_libraries(ofs_params_hash PRIVATE ofs_core)
endif()
