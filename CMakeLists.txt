cmake_minimum_required(VERSION 3.20)

project(D-Engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DNG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(DNG_SOURCE_DIR "${DNG_ROOT}/Source")
set(DNG_TEST_DIR "${DNG_ROOT}/tests")

include("${DNG_ROOT}/cmake/DngLanguagePolicy.cmake")

add_library(dng_core OBJECT
    Source/Core/Memory/Allocator.cpp
    Source/Core/Memory/TrackingAllocator.cpp
    Source/Core/Memory/MemorySystem.Logging.cpp
    Source/Core/Memory/GlobalNewDelete.cpp
    Source/Core/Math/Math.cpp
)

target_include_directories(dng_core PUBLIC "${DNG_SOURCE_DIR}")
target_compile_features(dng_core PUBLIC cxx_std_23)
dng_enforce_language_policy(dng_core)

function(dng_add_compile_only target_name source_file)
    add_library(${target_name} OBJECT "${source_file}")
    target_include_directories(${target_name} PRIVATE "${DNG_SOURCE_DIR}" "${DNG_TEST_DIR}")
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    dng_enforce_language_policy(${target_name})
endfunction()

file(GLOB_RECURSE DNG_TEST_SOURCES CONFIGURE_DEPENDS "${DNG_TEST_DIR}/*.cpp")
list(FILTER DNG_TEST_SOURCES EXCLUDE REGEX "/tests/Policy/")

foreach(test_src IN LISTS DNG_TEST_SOURCES)
    get_filename_component(test_name "${test_src}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${test_name}" test_name_safe)
    set(target_name "dng_test_${test_name_safe}")
    dng_add_compile_only(${target_name} "${test_src}")
endforeach()

set(DNG_POLICY_INCLUDE_DIRS "${DNG_SOURCE_DIR}" "${DNG_TEST_DIR}")
set(DNG_POLICY_COMPILE_OPTIONS "${DNG_LANGUAGE_POLICY_COMPILE_OPTIONS}")
set(DNG_POLICY_VIOLATION_SOURCES
    "${DNG_TEST_DIR}/Policy/policy_violation_throw.cpp"
    "${DNG_TEST_DIR}/Policy/policy_violation_force_include.cpp"
)
set(DNG_POLICY_VIOLATION_EXPECT_FAILS
    TRUE  # throw should fail with exceptions disabled
    FALSE # force-include macro should allow compile to succeed
)
set(DNG_POLICY_VIOLATION_BINARY_ROOT "${CMAKE_BINARY_DIR}/policy_violation_build")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_policy_violation_test.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake/run_policy_violation_test.cmake"
    @ONLY
)

add_custom_target(dng_policy_violation_test
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake/run_policy_violation_test.cmake"
    COMMENT "Expect policy violation compile to fail"
)
