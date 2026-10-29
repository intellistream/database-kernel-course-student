add_library(db_test_support STATIC)
add_library(db::test_support ALIAS db_test_support)

target_sources(db_test_support PRIVATE src/test_directory.cpp)
target_include_directories(
    db_test_support
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(db_test_support PUBLIC db::base)
target_compile_features(db_test_support PUBLIC cxx_std_23)
db_configure_target(db_test_support)

add_library(db_sql_regress_support STATIC)
add_library(db::sql_regress_support ALIAS db_sql_regress_support)

target_sources(
    db_sql_regress_support
    PRIVATE
        src/sql_regress.cpp
        src/sql_regress_output.cpp
        src/sql_regress_script.cpp
)
target_include_directories(
    db_sql_regress_support
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    db_sql_regress_support
    PUBLIC
        db::base
    PRIVATE
        db::engine
        db::test_support
)
target_compile_features(db_sql_regress_support PUBLIC cxx_std_23)
db_configure_target(db_sql_regress_support)

db_add_test(
    db_test_support_tests
    SOURCES tests/test_support_test.cpp
    LIBRARIES db::test_support
    LABELS support unit
)

add_library(
    db_test_support_public_header_check
    OBJECT tests/test_support_header_compile.cpp
)
target_link_libraries(db_test_support_public_header_check PRIVATE db::test_support)
db_configure_target(db_test_support_public_header_check)
