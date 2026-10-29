db_add_test(
    db_wal_tests
    SOURCES
        compression_test.cpp
        control_store_test.cpp
        delta_test.cpp
        format_test.cpp
        manager_background_writer_test.cpp
        manager_test.cpp
        move_lifecycle_test.cpp
        reader_test.cpp
        record_test.cpp
        ring_buffer_test.cpp
        segment_storage_test.cpp
        writer_test.cpp
    LIBRARIES db::io db::wal db::test_support
    LABELS wal unit
)
target_include_directories(
    db_wal_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_library(
    db_wal_public_header_check
    OBJECT
        compression_header_compile.cpp
        control_header_compile.cpp
        control_store_header_compile.cpp
        delta_header_compile.cpp
        format_header_compile.cpp
        geometry_header_compile.cpp
        manager_header_compile.cpp
        reader_header_compile.cpp
        record_header_compile.cpp
        resource_manager_header_compile.cpp
        ring_buffer_header_compile.cpp
        segment_storage_header_compile.cpp
        startup_validation_header_compile.cpp
        writer_header_compile.cpp
)
target_link_libraries(db_wal_public_header_check PRIVATE db::wal)
db_configure_target(db_wal_public_header_check)

add_library(
    db_wal_private_header_check
    OBJECT
        format_internal_header_compile.cpp
        manager_background_writer_header_compile.cpp
        manager_checkpoint_header_compile.cpp
        manager_page_images_header_compile.cpp
        manager_startup_header_compile.cpp
        reader_dispatch_header_compile.cpp
        reader_framing_header_compile.cpp
        reader_read_ahead_header_compile.cpp
        record_image_header_compile.cpp
        ring_layout_header_compile.cpp
        segment_directory_header_compile.cpp
        segment_file_header_compile.cpp
        writer_io_header_compile.cpp
)
target_include_directories(
    db_wal_private_header_check
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(db_wal_private_header_check PRIVATE db::io db::wal)
db_configure_target(db_wal_private_header_check)
