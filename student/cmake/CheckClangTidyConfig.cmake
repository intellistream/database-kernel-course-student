if(NOT DEFINED DB_CLANG_TIDY OR NOT DEFINED DB_CLANG_TIDY_CONFIG)
    message(FATAL_ERROR "CheckClangTidyConfig.cmake requires executable and config")
endif()

execute_process(
    COMMAND
        "${DB_CLANG_TIDY}"
        "--config-file=${DB_CLANG_TIDY_CONFIG}"
        --dump-config
    OUTPUT_QUIET
    ERROR_VARIABLE config_error
    RESULT_VARIABLE config_result
)

if(NOT config_result EQUAL 0)
    message(FATAL_ERROR "Invalid clang-tidy configuration:\n${config_error}")
endif()
