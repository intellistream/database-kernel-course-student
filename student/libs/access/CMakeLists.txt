add_library(db_access STATIC)
add_library(db::access ALIAS db_access)

target_sources(
    db_access
    PRIVATE
        src/btree_bulk_build.cpp
        src/btree_key.cpp
        src/btree_insert.cpp
        src/btree_logging.cpp
        src/btree_page.cpp
        src/btree_maintenance.cpp
        src/btree_relation.cpp
        src/btree_scan.cpp
        src/heap_delete.cpp
        src/heap_fetch.cpp
        src/heap_index_tid.cpp
        src/heap_maintenance.cpp
        src/heap_page.cpp
        src/heap_prune.cpp
        src/heap_scan.cpp
        src/heap_table.cpp
        src/heap_update.cpp
        src/tuple_codec.cpp
)
target_include_directories(
    db_access
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_access PUBLIC cxx_std_23)
target_link_libraries(
    db_access
    PUBLIC
        db::base
        db::storage
        db::transaction
        db::types
)
db_configure_target(db_access)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
