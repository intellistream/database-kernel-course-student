add_library(db_engine STATIC)
add_library(db::engine ALIAS db_engine)

target_sources(
    db_engine
    PRIVATE
        src/catalog_snapshot_adapter.cpp
        src/command_scope.cpp
        src/copy_executor.cpp
        src/copy_text_decoder.cpp
        src/maintenance_executor.cpp
        src/database.cpp
        src/database_startup.cpp
        src/dispatch.cpp
        src/materialized_result_receiver.cpp
        src/heap_seq_scan_port.cpp
        src/heap_index_scan_port.cpp
        src/logical_scan_ssi.cpp
        src/physical_modify_port.cpp
        src/storage_plan_factory.cpp
        src/query_error.cpp
        src/storage_query_binding.cpp
        src/query_executor.cpp
        src/result.cpp
        src/session.cpp
        src/ddl_executor.cpp
        src/utility_executor.cpp
)
# Preserve the utility workflow's compilation boundary while separating its
# source responsibilities. Other engine sources remain separate translation units.
set_target_properties(db_engine PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_MODE GROUP
    # CMake intentionally includes these .cpp files in its generated wrapper.
    UNITY_BUILD_CODE_BEFORE_INCLUDE "// NOLINTNEXTLINE(bugprone-suspicious-include)"
)
set_source_files_properties(src/ddl_executor.cpp src/utility_executor.cpp
    PROPERTIES UNITY_GROUP utility)

target_include_directories(
    db_engine
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_engine PUBLIC cxx_std_23)
target_link_libraries(
    db_engine
    PUBLIC
        db::base
        db::types
    PRIVATE
        db::catalog
        db::access
        db::execution
        db::io
        db::planner
        db::recovery
        db::sql
        db::storage
        db::transaction
        db::wal
)
db_configure_target(db_engine)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
