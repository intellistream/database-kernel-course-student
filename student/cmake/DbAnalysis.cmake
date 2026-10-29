include_guard(GLOBAL)

function(db_clang_tidy_include_args option_name output_variable)
    set(include_arguments)
    foreach(include_directory IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        list(APPEND include_arguments "${option_name}=-isystem${include_directory}")
    endforeach()
    set(${output_variable} "${include_arguments}" PARENT_SCOPE)
endfunction()

function(db_enable_clang_tidy target)
    if(NOT DB_ENABLE_CLANG_TIDY)
        return()
    endif()

    find_program(DB_CLANG_TIDY_22_EXECUTABLE NAMES clang-tidy-22 REQUIRED)
    db_clang_tidy_include_args(--extra-arg-before standard_library_arguments)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_CLANG_TIDY
            "${DB_CLANG_TIDY_22_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy;--extra-arg-before=-Wno-unknown-warning-option;${standard_library_arguments}"
    )
endfunction()

function(db_add_analysis_targets)
    if(NOT DB_ENABLE_ANALYSIS_TARGETS)
        return()
    endif()

    find_program(DB_CLANG_FORMAT_22_EXECUTABLE NAMES clang-format-22 REQUIRED)
    find_program(DB_CLANG_TIDY_22_EXECUTABLE NAMES clang-tidy-22 REQUIRED)
    find_program(DB_RUN_CLANG_TIDY_22_EXECUTABLE NAMES run-clang-tidy-22 REQUIRED)
    db_clang_tidy_include_args(-extra-arg-before standard_library_arguments)

    add_custom_target(
        format
        COMMAND
            "${CMAKE_COMMAND}"
            -DDB_FORMAT_MODE=FIX
            -DDB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DDB_CLANG_FORMAT=${DB_CLANG_FORMAT_22_EXECUTABLE}
            -P "${PROJECT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
        COMMENT "Formatting first-party C++ sources"
        VERBATIM
    )

    add_custom_target(
        format-check
        COMMAND
            "${CMAKE_COMMAND}"
            -DDB_FORMAT_MODE=CHECK
            -DDB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DDB_CLANG_FORMAT=${DB_CLANG_FORMAT_22_EXECUTABLE}
            -P "${PROJECT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
        COMMENT "Checking first-party C++ formatting"
        VERBATIM
    )

    add_custom_target(
        tidy-config-check
        COMMAND
            "${CMAKE_COMMAND}"
            -DDB_CLANG_TIDY=${DB_CLANG_TIDY_22_EXECUTABLE}
            -DDB_CLANG_TIDY_CONFIG=${PROJECT_SOURCE_DIR}/.clang-tidy
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckClangTidyConfig.cmake"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Validating .clang-tidy"
        VERBATIM
    )

    # Explicit first-party unity groups are translation units in the build tree.
    # Include them so grouped sources retain the same clang-tidy coverage.
    set(first_party_source_regex
        "^(${PROJECT_SOURCE_DIR}/(libs|apps|tests|benchmarks)/.*|${PROJECT_BINARY_DIR}/(libs|apps|tests|benchmarks)/.*/Unity/.*)\\.(cc|cpp|cxx)$"
    )
    add_custom_target(
        tidy
        COMMAND
            "${DB_RUN_CLANG_TIDY_22_EXECUTABLE}"
            -j 16
            -p "${PROJECT_BINARY_DIR}"
            -clang-tidy-binary "${DB_CLANG_TIDY_22_EXECUTABLE}"
            "-extra-arg-before=-Wno-unknown-warning-option"
            ${standard_library_arguments}
            -quiet
            "${first_party_source_regex}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running clang-tidy over first-party translation units"
        USES_TERMINAL
        VERBATIM
    )
endfunction()
