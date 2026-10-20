# SPDX-License-Identifier: CC0-1.0
# Runs existing upstream CPU tests without building Dawn or changing submodules.
cmake_minimum_required(VERSION 3.25)
project(dusklight_switch_host_checks LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "These are host checks, not Switch execution")
endif()
get_filename_component(AURORA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/aurora" ABSOLUTE)
foreach(source IN ITEMS SDL_SOURCE GTEST_SOURCE)
    if(NOT EXISTS "${${source}}/CMakeLists.txt")
        message(FATAL_ERROR "Set -D${source}=<verified local checkout>")
    endif()
endforeach()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Headless CPU-only checks" FORCE)
set(SDL_X11 OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_VIDEO OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_SOURCE}" sdl EXCLUDE_FROM_ALL)
add_subdirectory("${GTEST_SOURCE}" googletest EXCLUDE_FROM_ALL)
enable_testing()
include(GoogleTest)

# Keep test sources and production code exactly as pinned by Dusklight.
add_executable(aurora_io_tests
    "${AURORA_ROOT}/tests/io_test.cpp"
    "${AURORA_ROOT}/lib/card/FileIO.cpp"
    "${AURORA_ROOT}/lib/io.cpp")
target_include_directories(aurora_io_tests PRIVATE "${AURORA_ROOT}/lib")
target_link_libraries(aurora_io_tests PRIVATE GTest::gtest_main SDL3::SDL3-static)
gtest_discover_tests(aurora_io_tests)
add_executable(aurora_time_tests
    "${AURORA_ROOT}/tests/time_test.cpp"
    "${AURORA_ROOT}/lib/time.cpp")
target_include_directories(aurora_time_tests PRIVATE "${AURORA_ROOT}/include" "${AURORA_ROOT}/lib")
target_link_libraries(aurora_time_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(aurora_time_tests)

add_executable(dksh_layout_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/switch/dksh_layout_test.cpp")
target_include_directories(dksh_layout_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../probe")
add_test(NAME dksh_structural_tests COMMAND dksh_layout_tests)
