add_library(db_wal STATIC)
add_library(db::wal ALIAS db_wal)

target_sources(
    db_wal
    PRIVATE
        src/compression.cpp
        src/control_format.cpp
        src/control_store.cpp
        src/delta.cpp
        src/manager.cpp
        src/manager_background_writer.cpp
        src/manager_checkpoint.cpp
        src/manager_page_images.cpp
        src/manager_startup.cpp
        src/page_format.cpp
        src/reader.cpp
        src/reader_dispatch.cpp
        src/reader_framing.cpp
        src/reader_read_ahead.cpp
        src/record.cpp
        src/record_decode.cpp
        src/record_format.cpp
        src/record_image.cpp
        src/ring_buffer.cpp
        src/ring_layout.cpp
        src/segment_directory.cpp
        src/segment_file.cpp
        src/segment_format.cpp
        src/segment_storage.cpp
        src/writer.cpp
        src/writer_io.cpp
)
target_include_directories(
    db_wal
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_wal PUBLIC cxx_std_23)
target_link_libraries(db_wal PUBLIC db::base PRIVATE db::io)
db_configure_target(db_wal)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
