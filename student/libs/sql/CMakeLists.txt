add_library(db_sql STATIC)
add_library(db::sql ALIAS db_sql)

target_sources(
    db_sql
    PRIVATE
        src/binder.cpp
        src/binder_scope.cpp
        src/bound_clone.cpp
        src/catalog_port.cpp
        src/parser.cpp
)
target_include_directories(
    db_sql
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_sql PUBLIC cxx_std_23)
target_link_libraries(
    db_sql
    PUBLIC
        db::base
        db::types
    PRIVATE
        db::duckdb_pg_query
)
db_configure_target(db_sql)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
