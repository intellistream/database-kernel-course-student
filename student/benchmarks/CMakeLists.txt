include(DbBenchmarking)

add_library(db_benchmark_support STATIC support/src/benchmark_support.cpp)
target_include_directories(
    db_benchmark_support
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support/include>
)
target_link_libraries(db_benchmark_support PUBLIC db::base db::storage)
db_configure_target(db_benchmark_support)

db_add_benchmark(
    db_buffer_pool_bench
    SOURCES storage/buffer_pool_bench.cpp
    LIBRARIES db::storage
)

db_add_benchmark(
    db_btree_bench
    SOURCES access/btree_bench.cpp
    LIBRARIES
        db::access
        db::recovery
        db::storage
        db::types
        db::wal
)

db_add_benchmark(
    db_heap_scan_bench
    SOURCES access/heap_scan_bench.cpp
    LIBRARIES
        db::access
        db::storage
        db::transaction
)

db_add_benchmark(
    db_expression_bench
    SOURCES execution/expression_bench.cpp
    LIBRARIES
        db::execution
        db::planner
        db::types
)

db_add_benchmark(
    db_modify_bench
    SOURCES execution/modify_bench.cpp
    LIBRARIES
        db::execution
        db::planner
        db::types
)

db_add_benchmark(
    db_join_bench
    SOURCES execution/join_bench.cpp
    LIBRARIES
        db::execution
        db::planner
        db::types
)

db_add_benchmark(
    db_execution_pipeline_bench
    SOURCES execution/pipeline_bench.cpp
    LIBRARIES
        db::execution
        db::planner
        db::types
)

db_add_benchmark(
    db_execution_materialize_bench
    SOURCES execution/materialize_bench.cpp
    LIBRARIES
        db::execution
        db::planner
        db::types
)

db_add_benchmark(
    db_execution_subplan_bench
    SOURCES execution/subplan_bench.cpp
    LIBRARIES
        db::execution
        db::planner
        db::types
)

db_add_benchmark(
    db_execution_e2e_bench
    SOURCES execution/e2e_bench.cpp
    LIBRARIES
        db::engine
        db::types
)

db_add_benchmark(
    db_tpcc_pressure_bench
    SOURCES execution/tpcc_pressure_bench.cpp
    LIBRARIES db::api db::types
)

db_add_benchmark(
    db_protocol_result_bench
    SOURCES protocol/result_bench.cpp
    LIBRARIES db::protocol db::types
)

# This measures the application's private API adapter at its owning source tree.
db_add_benchmark(
    db_server_result_adapter_bench
    SOURCES
        ../apps/db-server/benchmarks/result_adapter_bench.cpp
        ../apps/db-server/benchmarks/allocation_probe.cpp
    LIBRARIES db::server_app db::api db::protocol db::types
)

db_add_benchmark(
    db_wal_reader_bench
    SOURCES wal/reader_bench.cpp
    LIBRARIES db::recovery db::wal
)

db_add_benchmark(
    db_wal_writer_bench
    SOURCES wal/writer_bench.cpp
    LIBRARIES db::wal
)

db_add_benchmark(
    db_transaction_bench
    SOURCES transaction/transaction_bench.cpp
    LIBRARIES db::transaction
)

db_add_benchmark(
    db_sql_bench
    SOURCES sql/sql_bench.cpp
    LIBRARIES db::sql db::types
)

db_add_benchmark(
    db_planner_bench
    SOURCES planner/planner_bench.cpp
    LIBRARIES db::planner db::types
)

db_add_benchmark(
    db_catalog_publication_bench
    SOURCES catalog/publication_bench.cpp
    LIBRARIES db::catalog
)

if(BUILD_TESTING)
    db_add_test(
        db_benchmark_support_tests
        SOURCES support/tests/benchmark_support_test.cpp
        LIBRARIES db_benchmark_support
        LABELS benchmark unit
    )
endif()
