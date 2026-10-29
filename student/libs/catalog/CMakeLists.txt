add_library(db_catalog STATIC)
add_library(db::catalog ALIAS db_catalog)

target_sources(
    db_catalog
    PRIVATE
        src/analyze.cpp
        src/consistency.cpp
        src/format.cpp
        src/format_encode.cpp
        src/format_validation.cpp
        src/id_allocator.cpp
        src/index_schema.cpp
        src/metadata.cpp
        src/metadata_validation.cpp
        src/schema.cpp
        src/snapshot.cpp
        src/statistics.cpp
        src/store.cpp
        src/store_internal.cpp
)
target_include_directories(
    db_catalog
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_catalog PUBLIC cxx_std_23)
target_link_libraries(
    db_catalog
    PUBLIC
        db::base
        db::types
    PRIVATE db::io
)
db_configure_target(db_catalog)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
