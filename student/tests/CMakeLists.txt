# Cross-component and end-to-end tests are registered here. Component unit
# tests live beside their owning component and are still discovered by CTest.

add_subdirectory(support)
add_subdirectory(protocol)

db_add_test(
    db_shell_tests
    SOURCES db_shell_test.cpp
    LIBRARIES db::shell db::test_support
    LABELS shell integration
)

db_add_test(
    db_sql_regress_tests
    SOURCES sql_regress_test.cpp
    LIBRARIES db::sql_regress_support
    LABELS sql regression integration
)
target_compile_definitions(
    db_sql_regress_tests
    PRIVATE DB_SQL_REGRESS_ROOT="${PROJECT_SOURCE_DIR}/tests/sql-regress"
)
