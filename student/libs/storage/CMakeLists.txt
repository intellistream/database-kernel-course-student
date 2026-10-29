add_library(db_storage STATIC)
add_library(db::storage ALIAS db_storage)

target_sources(
    db_storage
    PRIVATE
        src/buffer_pool.cpp
        src/disk_manager.cpp
        src/durable_metadata.cpp
        src/file_layout.cpp
        src/frame.cpp
        src/free_space_map.cpp
        src/page.cpp
        src/replacement.cpp
)
target_include_directories(
    db_storage
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_storage PUBLIC cxx_std_23)
target_link_libraries(db_storage PUBLIC db::base PRIVATE db::io)
db_configure_target(db_storage)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
