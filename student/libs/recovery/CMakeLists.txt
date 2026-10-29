add_library(db_recovery STATIC)
add_library(db::recovery ALIAS db_recovery)

target_sources(
    db_recovery
    PRIVATE
        src/btree_wal.cpp
        src/btree_wal_codec.cpp
        src/btree_startup_redo.cpp
        src/catalog_recovery.cpp
        src/catalog_index.cpp
        src/checkpoint.cpp
        src/durable_state_redo.cpp
        src/dispatcher.cpp
        src/full_page_image.cpp
        src/heap_startup_redo.cpp
        src/heap_wal.cpp
        src/heap_wal_codec.cpp
        src/page_image.cpp
        src/recovery_page_cache.cpp
        src/startup_recovery.cpp
        src/startup_runner.cpp
        src/transaction_wal.cpp
        src/wal_durability.cpp
        src/wal_stream.cpp
)
target_include_directories(
    db_recovery
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_recovery PUBLIC cxx_std_23)
target_link_libraries(
    db_recovery
    PUBLIC
        db::access
        db::base
        db::catalog
        db::storage
        db::transaction
        db::wal
)
db_configure_target(db_recovery)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
