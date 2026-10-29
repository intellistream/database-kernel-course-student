add_library(db_access_private_test_support INTERFACE)
target_include_directories(
    db_access_private_test_support
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(
    db_access_private_test_support
    INTERFACE db::access
)

db_add_test(
    db_access_tests
    SOURCES
        btree_key_test.cpp
        btree_model_test.cpp
        btree_bulk_build_test.cpp
        btree_insert_test.cpp
        btree_maintenance_test.cpp
        btree_page_test.cpp
        btree_relation_scan_test.cpp
        btree_scan_bounds_test.cpp
        btree_unique_test.cpp
        heap_delete_test.cpp
        heap_fetch_test.cpp
        heap_page_test.cpp
        heap_insert_test.cpp
        heap_maintenance_test.cpp
        heap_scan_test.cpp
        heap_table_test.cpp
        heap_update_test.cpp
        tuple_codec_test.cpp
    LIBRARIES
        db_access_private_test_support
        db::access
        db::test_support
    LABELS access unit
)

add_library(
    db_access_public_header_check
    OBJECT
        btree_key_header_compile.cpp
        btree_maintenance_header_compile.cpp
        btree_page_header_compile.cpp
        btree_relation_scan_header_compile.cpp
        btree_unique_header_compile.cpp
        btree_wal_header_compile.cpp
        dead_tid_store_header_compile.cpp
        heap_context_header_compile.cpp
        heap_delete_header_compile.cpp
        heap_fetch_header_compile.cpp
        heap_maintenance_header_compile.cpp
        heap_page_header_compile.cpp
        heap_scan_header_compile.cpp
        heap_table_header_compile.cpp
        heap_update_header_compile.cpp
        heap_wal_header_compile.cpp
        tuple_codec_header_compile.cpp
)
target_link_libraries(db_access_public_header_check PRIVATE db::access)
db_configure_target(db_access_public_header_check)

add_library(
    db_access_private_header_check
    OBJECT
        btree_concurrency_header_compile.cpp
        btree_insert_internal_header_compile.cpp
        btree_logging_internal_header_compile.cpp
        btree_page_builder_header_compile.cpp
        btree_page_read_view_header_compile.cpp
        heap_page_scan_view_header_compile.cpp
        heap_tuple_state_internal_header_compile.cpp
)
target_link_libraries(
    db_access_private_header_check
    PRIVATE db_access_private_test_support
)
db_configure_target(db_access_private_header_check)
