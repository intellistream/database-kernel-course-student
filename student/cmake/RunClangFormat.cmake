if(NOT DEFINED DB_FORMAT_MODE OR
   NOT DEFINED DB_SOURCE_DIR OR
   NOT DEFINED DB_CLANG_FORMAT)
    message(FATAL_ERROR "RunClangFormat.cmake requires mode, source dir, and executable")
endif()

file(
    GLOB_RECURSE first_party_sources
    LIST_DIRECTORIES false
    "${DB_SOURCE_DIR}/apps/*.cc"
    "${DB_SOURCE_DIR}/apps/*.cpp"
    "${DB_SOURCE_DIR}/apps/*.h"
    "${DB_SOURCE_DIR}/apps/*.hpp"
    "${DB_SOURCE_DIR}/libs/*.cc"
    "${DB_SOURCE_DIR}/libs/*.cpp"
    "${DB_SOURCE_DIR}/libs/*.h"
    "${DB_SOURCE_DIR}/libs/*.hpp"
    "${DB_SOURCE_DIR}/tests/*.cc"
    "${DB_SOURCE_DIR}/tests/*.cpp"
    "${DB_SOURCE_DIR}/tests/*.h"
    "${DB_SOURCE_DIR}/tests/*.hpp"
    "${DB_SOURCE_DIR}/benchmarks/*.cc"
    "${DB_SOURCE_DIR}/benchmarks/*.cpp"
    "${DB_SOURCE_DIR}/benchmarks/*.h"
    "${DB_SOURCE_DIR}/benchmarks/*.hpp"
)

if(NOT first_party_sources)
    message(STATUS "No first-party C++ files require formatting")
    return()
endif()

if(DB_FORMAT_MODE STREQUAL "FIX")
    set(format_arguments -i)
elseif(DB_FORMAT_MODE STREQUAL "CHECK")
    set(format_arguments --dry-run --Werror)
else()
    message(FATAL_ERROR "Unknown format mode: ${DB_FORMAT_MODE}")
endif()

execute_process(
    COMMAND "${DB_CLANG_FORMAT}" ${format_arguments} ${first_party_sources}
    RESULT_VARIABLE format_result
)

if(NOT format_result EQUAL 0)
    message(FATAL_ERROR "clang-format failed with exit code ${format_result}")
endif()
