# Build identity is separate from the NRO SHA-256 (known only AFTER packaging).
include("${CMAKE_SOURCE_DIR}/platforms/switch/cmake/CheckPerformanceBuildUnlocked.cmake")
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE PERF_GAME_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/extern/aurora" OUTPUT_VARIABLE PERF_AURORA_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain=v1 --untracked-files=all --ignore-submodules=none
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _perf_status
    COMMAND_ERROR_IS_FATAL ANY)
set(PERF_SOURCE_DIRTY false)
if(NOT "${_perf_status}" STREQUAL "")
    set(PERF_SOURCE_DIRTY true)
endif()
# Rotate on EVERY configuration. Distributed configurations are then sealed;
# building another distributed artifact requires a fresh, unlocked directory.
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef _perf_id)
set(DUSK_PERFORMANCE_BUILD_ID "${_perf_id}" CACHE INTERNAL "Configuration identity, NOT an NRO hash" FORCE)
set(DUSK_PERFORMANCE_TOOLCHAIN_ID "Unavailable" CACHE STRING "Verified build-container image digest")
set(PERF_VULKAN_BACKEND "Vulkan (driver identity Unavailable)")
if(DUSK_PLATFORM_SWITCH AND NXVK_USE_UPSTREAM_SDK)
    set(PERF_VULKAN_BACKEND "Vulkan/NVK; original danfromtico/mesa-switch SDK")
endif()
set(PERF_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/performance-generated")
configure_file("${CMAKE_SOURCE_DIR}/cmake/performance_build_identity.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/performance-generated/performance_build_identity.hpp" @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/performance-build-identity.json"
    "{\n  \"build_id\": \"${DUSK_PERFORMANCE_BUILD_ID}\",\n  \"game_source_revision\": \"${PERF_GAME_REV}\",\n  \"aurora_source_revision\": \"${PERF_AURORA_REV}\",\n  \"source_dirty_at_configure\": ${PERF_SOURCE_DIRTY},\n  \"compiler\": \"${PERF_COMPILER}\",\n  \"toolchain_image\": \"${DUSK_PERFORMANCE_TOOLCHAIN_ID}\"\n}\n")
target_include_directories(dusk_internal PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/performance-generated")
