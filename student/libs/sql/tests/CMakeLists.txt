db_add_test(
    db_sql_binder_tests
    SOURCES binder_test.cpp
    LIBRARIES
        db::sql
    LABELS sql binder unit
)

db_add_test(
    db_sql_catalog_port_tests
    SOURCES catalog_port_test.cpp
    LIBRARIES
        db::sql
    LABELS sql catalog unit
)

db_add_test(
    db_sql_parser_corpus_tests
    SOURCES parser_corpus_test.cpp
    LIBRARIES
        db::sql
    LABELS sql parser corpus unit
)
target_compile_definitions(
    db_sql_parser_corpus_tests
    PRIVATE DB_SQL_PARSER_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/db_ast_projection.tsv"
)

db_add_test(
    db_sql_parser_tests
    SOURCES parser_test.cpp
    LIBRARIES
        db::sql
    LABELS sql parser unit
)

add_library(
    db_sql_public_header_check
    OBJECT
    binder_header_compile.cpp
    catalog_port_header_compile.cpp
    parser_header_compile.cpp
)
target_link_libraries(db_sql_public_header_check PRIVATE db::sql)
db_configure_target(db_sql_public_header_check)
