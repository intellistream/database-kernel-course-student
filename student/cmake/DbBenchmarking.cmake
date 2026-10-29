include_guard(GLOBAL)

include(CMakeParseArguments)

function(db_add_benchmark target)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES LIBRARIES)
    cmake_parse_arguments(
        PARSE_ARGV 1
        DB_BENCHMARK
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
    )

    if(NOT DB_BENCHMARK_SOURCES)
        message(FATAL_ERROR "db_add_benchmark(${target}) requires SOURCES")
    endif()

    add_executable(${target} ${DB_BENCHMARK_SOURCES})
    db_configure_target(${target})
    target_link_libraries(
        ${target}
        PRIVATE
            benchmark::benchmark_main
            db_benchmark_support
            ${DB_BENCHMARK_LIBRARIES}
    )
endfunction()
