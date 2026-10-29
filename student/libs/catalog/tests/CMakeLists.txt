add_library(db_catalog_private_test_support INTERFACE)
target_include_directories(
    db_catalog_private_test_support
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(
    db_catalog_private_test_support
    INTERFACE db::catalog
)

db_add_test(
    db_catalog_tests
    SOURCES
        consistency_test.cpp
        format_test.cpp
        history_reopen_test.cpp
        id_allocator_test.cpp
        concurrency_test.cpp
        metadata_test.cpp
        schema_validation_test.cpp
        statistics_test.cpp
        store_test.cpp
    LIBRARIES
        db::catalog
        db::io
        db::test_support
        db_catalog_private_test_support
    LABELS catalog unit
)

db_add_test(
    db_catalog_access_contract_tests
    SOURCES schema_access_contract_test.cpp
    LIBRARIES
        db::access
        db::catalog
    LABELS catalog integration
)

add_library(
    db_catalog_public_header_check
    OBJECT
        analyze_header_compile.cpp
        consistency_header_compile.cpp
        format_header_compile.cpp
        id_state_header_compile.cpp
        index_schema_header_compile.cpp
        metadata_header_compile.cpp
        schema_header_compile.cpp
        snapshot_header_compile.cpp
        statistics_header_compile.cpp
        store_header_compile.cpp
)
target_link_libraries(db_catalog_public_header_check PRIVATE db::catalog)
db_configure_target(db_catalog_public_header_check)

add_library(
    db_catalog_private_header_check
    OBJECT
        consistency_internal_header_compile.cpp
        format_internal_header_compile.cpp
        format_validation_header_compile.cpp
        id_allocator_header_compile.cpp
        metadata_validation_header_compile.cpp
        store_internal_header_compile.cpp
)
target_link_libraries(
    db_catalog_private_header_check
    PRIVATE db_catalog_private_test_support
)
db_configure_target(db_catalog_private_header_check)
