# SPDX-License-Identifier: CC0-1.0
cmake_minimum_required(VERSION 3.25)
project(dusklight_deko3d_probe LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    message(FATAL_ERROR "Use the pinned devkitPro image and -DCMAKE_TOOLCHAIN_FILE=$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

# This standalone fixture is NOT Dusklight and does not implement the Dawn backend.
# Keep deko3d's debug/validation checks even when compiling this executable in Release.
find_path(DEKO3D_INCLUDE_DIR deko3d.h HINTS "$ENV{DEVKITPRO}/libnx/include" REQUIRED)
find_library(DEKO3D_VALIDATED_LIBRARY NAMES deko3dd HINTS "$ENV{DEVKITPRO}/libnx/lib" REQUIRED)
find_program(UAM_EXECUTABLE uam HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
find_program(ELF2NRO_EXECUTABLE elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
find_program(NACPTOOL_EXECUTABLE nacptool HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)

set(shader_outputs)
foreach(stage IN ITEMS vert frag)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/romfs/shaders/triangle.${stage}.dksh")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/romfs/shaders"
        COMMAND "${UAM_EXECUTABLE}" -s ${stage} -o "${output}" "${CMAKE_CURRENT_SOURCE_DIR}/triangle.${stage}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/triangle.${stage}" "${UAM_EXECUTABLE}"
        VERBATIM)
    list(APPEND shader_outputs "${output}")
endforeach()

add_executable(dusklight_deko3d_probe main.cpp)
target_compile_features(dusklight_deko3d_probe PRIVATE cxx_std_20)
target_compile_options(dusklight_deko3d_probe PRIVATE -Wall -Wextra -Werror)
target_compile_options(dusklight_deko3d_probe PRIVATE
    "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=/dusklight-switch-probe/source"
    "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=/dusklight-switch-probe/build")
target_include_directories(dusklight_deko3d_probe PRIVATE "${DEKO3D_INCLUDE_DIR}")
target_link_libraries(dusklight_deko3d_probe PRIVATE "${DEKO3D_VALIDATED_LIBRARY}" nx)

set(nacp "${CMAKE_CURRENT_BINARY_DIR}/dusklight_deko3d_probe.nacp")
add_custom_command(OUTPUT "${nacp}"
    COMMAND "${NACPTOOL_EXECUTABLE}" --create "Dusklight deko3d probe (not game)" "Dusklight port research" "0.1.0" "${nacp}"
    DEPENDS "${NACPTOOL_EXECUTABLE}"
    VERBATIM)
set(nro "${CMAKE_CURRENT_BINARY_DIR}/dusklight_deko3d_probe.nro")
add_custom_command(OUTPUT "${nro}"
    COMMAND "${ELF2NRO_EXECUTABLE}" "$<TARGET_FILE:dusklight_deko3d_probe>" "${nro}"
        "--nacp=${nacp}" "--romfsdir=${CMAKE_CURRENT_BINARY_DIR}/romfs"
    DEPENDS dusklight_deko3d_probe "${nacp}" ${shader_outputs} "${ELF2NRO_EXECUTABLE}"
    VERBATIM)
add_custom_target(probe_nro ALL DEPENDS "${nro}")
