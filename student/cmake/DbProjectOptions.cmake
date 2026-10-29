include_guard(GLOBAL)

include(DbAnalysis)
include(DbCoverage)
include(DbSanitizers)
include(DbWarnings)

function(db_setup_project_options)
    add_library(db_warnings INTERFACE)
    add_library(db::warnings ALIAS db_warnings)
    db_set_warnings(db_warnings)

    add_library(db_sanitizers INTERFACE)
    add_library(db::sanitizers ALIAS db_sanitizers)
    db_enable_sanitizers(db_sanitizers)

    add_library(db_coverage INTERFACE)
    add_library(db::coverage ALIAS db_coverage)
    db_enable_coverage(db_coverage)

    add_library(db_project_options INTERFACE)
    add_library(db::project_options ALIAS db_project_options)
    target_compile_features(db_project_options INTERFACE cxx_std_23)
    target_link_libraries(
        db_project_options
        INTERFACE
            db::warnings
            db::sanitizers
            db::coverage
    )

    if(DB_NATIVE_CPU)
        target_compile_options(
            db_project_options
            INTERFACE $<$<CONFIG:Release>:-march=native>
        )
    endif()
endfunction()

function(db_configure_target target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Cannot configure missing target: ${target}")
    endif()

    target_link_libraries(${target} PRIVATE db::project_options)
    db_enable_clang_tidy(${target})
endfunction()
