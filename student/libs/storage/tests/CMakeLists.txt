add_library(db_storage_private_test_support INTERFACE)
target_include_directories(
    db_storage_private_test_support
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(
    db_storage_private_test_support
    INTERFACE db::storage db::io
)

db_add_test(
    db_storage_tests
    SOURCES
        buffer_pool_test.cpp
        disk_manager_test.cpp
        file_layout_test.cpp
        frame_test.cpp
        free_space_map_test.cpp
        page_test.cpp
        replacement_test.cpp
    LIBRARIES
        db_storage_private_test_support
        db::test_support
    LABELS storage unit
)

add_library(
    db_storage_public_header_check
    OBJECT
        buffer_pool_header_compile.cpp
        disk_manager_header_compile.cpp
        durability_header_compile.cpp
        frame_header_compile.cpp
        free_space_map_header_compile.cpp
        page_header_compile.cpp
        replacement_header_compile.cpp
)
target_link_libraries(db_storage_public_header_check PRIVATE db::storage)
db_configure_target(db_storage_public_header_check)

add_library(
    db_storage_private_header_check
    OBJECT
        durable_metadata_header_compile.cpp
        file_layout_header_compile.cpp
        relation_segment_header_compile.cpp
)
target_link_libraries(
    db_storage_private_header_check
    PRIVATE db_storage_private_test_support
)
db_configure_target(db_storage_private_header_check)
