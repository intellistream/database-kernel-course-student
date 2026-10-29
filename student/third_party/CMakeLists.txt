# DuckDB's raw parser core is a product dependency. Its CMake target is fully
# private to db_sql and must exist even in a BUILD_TESTING=OFF build.
add_subdirectory(duckdb_libpg_query EXCLUDE_FROM_ALL)

if(BUILD_TESTING)
    # GoogleTest is vendored so a clean build never downloads dependencies.
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    set(gtest_build_tests OFF CACHE BOOL "" FORCE)
    set(gmock_build_tests OFF CACHE BOOL "" FORCE)

    add_subdirectory(googletest EXCLUDE_FROM_ALL)
endif()

if(DB_BUILD_BENCHMARKS)
    # Google Benchmark is benchmark-only. Its own tests and installation rules
    # are not part of DB's offline build graph.
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_LTO OFF CACHE BOOL "" FORCE)
    add_subdirectory(google_benchmark EXCLUDE_FROM_ALL)
endif()
