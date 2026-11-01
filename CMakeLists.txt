cmake_minimum_required(VERSION 3.28)
project(OneFourSeven LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OFS_BUILD_TESTS "Build the native core tests" ON)
option(OFS_BUILD_GDEXT "Build the Godot extension" ON)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(OFS_BUILD_TESTS OFF)
endif()

# Results must not depend on the compiler fusing multiply-adds differently per platform
# (spec 16.10, determinism across machines).
if(MSVC)
    add_compile_options(/fp:precise)
else()
    add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# godot-cpp always links the static release CRT; everything in the extension must match.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(OFS_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

if(OFS_BUILD_GDEXT)
    add_subdirectory(gdext)
endif()
