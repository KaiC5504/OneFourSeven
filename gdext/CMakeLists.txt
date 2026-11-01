set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API the extension targets")

add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/godot-cpp ${CMAKE_CURRENT_BINARY_DIR}/godot-cpp)

add_library(onefourseven SHARED
    src/register_types.cpp
    src/ofs_log.cpp
    src/ofs_table.cpp
    src/ofs_devlink.cpp
    src/engine_logger.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    target_sources(onefourseven PRIVATE ios/bonjour_browser.mm)
    target_compile_options(onefourseven PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(onefourseven PRIVATE "-framework Foundation" "-framework Network")
else()
    target_sources(onefourseven PRIVATE src/bonjour_browser_stub.cpp)
endif()

target_include_directories(onefourseven PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(onefourseven PRIVATE ofs_core godot-cpp)

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(OFS_GDEXT_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/game/bin")

set_target_properties(onefourseven PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "$<1:${OFS_GDEXT_OUTPUT_DIR}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${OFS_GDEXT_OUTPUT_DIR}>"
    PDB_OUTPUT_DIRECTORY "$<1:${OFS_GDEXT_OUTPUT_DIR}>"
    PREFIX "lib"
    OUTPUT_NAME "onefourseven${GODOTCPP_SUFFIX}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Packaged into an .xcframework by scripts/build_ios_ext.sh.
    set_target_properties(onefourseven PROPERTIES
        FRAMEWORK TRUE
        MACOSX_FRAMEWORK_IDENTIFIER com.kaichuan.onefourseven.ext
        MACOSX_FRAMEWORK_BUNDLE_VERSION 1
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING 0.1.0
    )
endif()
