include_guard(GLOBAL)

function(db_enable_coverage target)
    if(NOT DB_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "lcov coverage is supported only by the GCC preset")
    endif()

    # Concurrent tests execute instrumented code from several threads. Atomic
    # counter updates keep gcov data valid instead of losing or underflowing
    # counts while those workers finish. The formal preset adds -O0 so GCC 15
    # neither distorts source mappings nor emits optimizer false positives from
    # instrumented libstdc++ hash-table paths. Optimized coverage remains an
    # explicit opt-in for separate trend runs.
    set(coverage_compile_options -g --coverage -fprofile-update=atomic)
    if(NOT DB_COVERAGE_OPTIMIZED)
        list(PREPEND coverage_compile_options -O0)
    endif()
    target_compile_options(${target} INTERFACE ${coverage_compile_options})
    target_link_options(${target} INTERFACE --coverage)
endfunction()

function(db_add_coverage_targets)
    if(NOT DB_ENABLE_COVERAGE)
        return()
    endif()

    find_program(DB_LCOV_EXECUTABLE NAMES lcov REQUIRED)
    find_program(DB_GENHTML_EXECUTABLE NAMES genhtml REQUIRED)
    # CMake exposes the full compiler version but no *_VERSION_MAJOR variable.
    # Select the matching versioned gcov first: a different major cannot decode
    # this build's .gcno/.gcda format and makes lcov fail after the full test run.
    string(REGEX MATCH "^[0-9]+" db_gcc_major "${CMAKE_CXX_COMPILER_VERSION}")
    find_program(
        DB_GCOV_EXECUTABLE
        NAMES
            "gcov-${db_gcc_major}"
            "x86_64-linux-gnu-gcov-${db_gcc_major}"
            gcov
        REQUIRED
    )
    execute_process(
        COMMAND "${DB_GCOV_EXECUTABLE}" --version
        RESULT_VARIABLE db_gcov_version_status
        OUTPUT_VARIABLE db_gcov_version_output
        ERROR_VARIABLE db_gcov_version_error
    )
    if(
        NOT db_gcov_version_status EQUAL 0
        OR NOT db_gcov_version_output MATCHES "[ )]${db_gcc_major}\\."
    )
        message(
            FATAL_ERROR
            "gcov must match GCC ${db_gcc_major}: ${DB_GCOV_EXECUTABLE}: "
            "${db_gcov_version_output}${db_gcov_version_error}"
        )
    endif()

    set(coverage_directory "${PROJECT_BINARY_DIR}/coverage")
    set(raw_tracefile "${coverage_directory}/db-raw.info")
    set(filtered_tracefile "${coverage_directory}/db.info")

    add_custom_target(
        coverage-reset
        COMMAND
            "${DB_LCOV_EXECUTABLE}"
            --zerocounters
            --directory "${PROJECT_BINARY_DIR}"
            --gcov-tool "${DB_GCOV_EXECUTABLE}"
        COMMENT "Resetting DB coverage counters"
        VERBATIM
    )

    add_custom_target(
        coverage-report
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${coverage_directory}"
        COMMAND
            "${DB_LCOV_EXECUTABLE}"
            --capture
            --directory "${PROJECT_BINARY_DIR}"
            --base-directory "${PROJECT_SOURCE_DIR}"
            --gcov-tool "${DB_GCOV_EXECUTABLE}"
            --rc lcov_branch_coverage=1
            --no-external
            --output-file "${raw_tracefile}"
        COMMAND
            "${DB_LCOV_EXECUTABLE}"
            --remove "${raw_tracefile}"
            "${PROJECT_SOURCE_DIR}/third_party/*"
            "${PROJECT_SOURCE_DIR}/tests/*"
            "*/tests/*"
            "${PROJECT_BINARY_DIR}/*"
            --rc lcov_branch_coverage=1
            --output-file "${filtered_tracefile}"
        COMMAND
            "${DB_GENHTML_EXECUTABLE}"
            "${filtered_tracefile}"
            --branch-coverage
            --demangle-cpp
            --output-directory "${coverage_directory}/html"
        COMMAND
            "${DB_LCOV_EXECUTABLE}"
            --summary "${filtered_tracefile}"
            --rc lcov_branch_coverage=1
        COMMENT "Generating DB lcov and HTML coverage reports"
        VERBATIM
    )
endfunction()
