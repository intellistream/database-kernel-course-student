cmake_minimum_required(VERSION 3.22)

project(
    DB
    VERSION 0.1.0
    DESCRIPTION "A complete, independent C++ implementation of DB"
    LANGUAGES CXX
)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "DB-cpp must be compiled with GNU g++; Clang is tooling-only")
endif()

# GCC 13.x through 15.x are the supported majors: 13.x matches the frozen
# final_test/grader runtime (Ubuntu 24.04) and 15.x is the tm4 server baseline.
# Both compile the full tree with warnings-as-errors; other GCC majors are
# rejected instead of being silently accepted with a different warning set.
if(
    CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0
    OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0
)
    message(FATAL_ERROR "DB-cpp requires g++ 13.x through 15.x")
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a CMake preset.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# DB deliberately uses ordinary compilation and linking. Keep LTO/IPO
# disabled for every configuration and dependency assembled by this project.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(DB_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" ON)
option(DB_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)
option(
    DB_ENABLE_ANALYSIS_TARGETS
    "Require clang-format/clang-tidy and expose their explicit quality targets"
    OFF
)
option(DB_ENABLE_ADDRESS_SANITIZER "Enable AddressSanitizer" OFF)
option(DB_ENABLE_UNDEFINED_SANITIZER "Enable UndefinedBehaviorSanitizer" OFF)
option(DB_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(DB_ENABLE_COVERAGE "Instrument first-party targets for GCC/lcov coverage" OFF)
option(
    DB_COVERAGE_OPTIMIZED
    "Keep the selected build-type optimization while compiling GCC/lcov coverage"
    OFF
)
option(DB_NATIVE_CPU "Tune Release first-party code for the current CPU" OFF)
option(DB_BUILD_BENCHMARKS "Build the standalone performance benchmarks" OFF)
set(
    DB_TPCC_LIVE_ADDRESS
    ""
    CACHE STRING
          "Optional loaded TPC-C server endpoint used only by the live 57-statement bind test"
)
set(
    DB_REFERENCE_ROOT
    ""
    CACHE PATH
          "Optional read-only Ferrum checkout used only by explicit interoperability/differential tests"
)

include(DbProjectOptions)
db_setup_project_options()

include(CTest)

add_subdirectory(third_party)

if(BUILD_TESTING)
    include(DbTesting)
endif()

add_subdirectory(libs)
add_subdirectory(apps)

if(DB_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(DbArchitecture)
db_validate_architecture()

db_add_analysis_targets()
db_add_coverage_targets()
