# This is an intentionally narrow, vendored third-party target. Its raw node
# and exception interfaces never escape db_sql, so it must not inherit the
# first-party warning/tidy policy or appear in an installed public interface.
add_library(
    db_duckdb_pg_query
    STATIC
    pg_functions.cpp
    src_backend_parser_parser.cpp
    src_backend_parser_gram.cpp
    src_backend_nodes_list.cpp
    src_backend_parser_scansup.cpp
    src_backend_nodes_makefuncs.cpp
    src_backend_nodes_value.cpp
    src_backend_parser_scan.cpp
    src_common_keywords.cpp
)
add_library(db::duckdb_pg_query ALIAS db_duckdb_pg_query)

target_include_directories(
    db_duckdb_pg_query
    SYSTEM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_duckdb_pg_query PUBLIC cxx_std_20)
set_target_properties(
    db_duckdb_pg_query
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_CLANG_TIDY ""
)
