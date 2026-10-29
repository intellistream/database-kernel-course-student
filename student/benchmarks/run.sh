#!/usr/bin/env bash

set -euo pipefail

profile=${1:-smoke}
filter=${2:-.}
source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$source_root/build/gcc-benchmark"
result_root="$source_root/benchmark-results"
run_id=$(date -u +%Y%m%dT%H%M%SZ)
result_dir="$result_root/$run_id-$profile"

case "$profile" in
    smoke)
        benchmark_args=(
            --benchmark_min_time=1x
            --benchmark_repetitions=1
        )
        ;;
    standard)
        benchmark_args=(
            --benchmark_min_warmup_time=0.5
            --benchmark_min_time=1s
            --benchmark_repetitions=7
            --benchmark_enable_random_interleaving=true
        )
        ;;
    deep)
        benchmark_args=(
            --benchmark_min_warmup_time=2
            --benchmark_min_time=3s
            --benchmark_repetitions=10
            --benchmark_enable_random_interleaving=true
        )
        ;;
    *)
        echo "usage: benchmarks/run.sh [smoke|standard|deep] [benchmark-filter]" >&2
        exit 2
        ;;
esac

cmake --preset gcc-benchmark -S "$source_root"
cmake --build "$build_dir" -j16 \
    --target db_buffer_pool_bench db_btree_bench db_heap_scan_bench \
             db_expression_bench db_join_bench db_modify_bench \
             db_execution_pipeline_bench db_execution_materialize_bench \
             db_execution_subplan_bench db_execution_e2e_bench \
             db_tpcc_pressure_bench db_transaction_bench db_sql_bench \
             db_planner_bench db_server_result_adapter_bench

mkdir -p "$result_dir"
commit=$(git -C "$source_root" rev-parse HEAD)
context="git_commit=$commit,profile=$profile,workload_seed=0xF377B00F20260817"

{
    echo "profile=$profile"
    echo "filter=$filter"
    echo "commit=$commit"
    echo "compiler=$(g++-15 --version | head -1)"
    echo "kernel=$(uname -srmo)"
    echo "affinity=0-15"
} >"$result_dir/context.txt"

run_one() {
    local executable=$1
    local output=$2
    taskset -c 0-15 "$executable" \
        "${benchmark_args[@]}" \
        --benchmark_filter="$filter" \
        --benchmark_context="$context" \
        --benchmark_out="$output" \
        --benchmark_out_format=json \
        --benchmark_counters_tabular=true
}

run_one "$build_dir/benchmarks/db_buffer_pool_bench" "$result_dir/buffer-pool.json"
run_one "$build_dir/benchmarks/db_btree_bench" "$result_dir/btree.json"
run_one "$build_dir/benchmarks/db_heap_scan_bench" "$result_dir/heap-scan.json"
run_one "$build_dir/benchmarks/db_expression_bench" "$result_dir/expression.json"
run_one "$build_dir/benchmarks/db_join_bench" "$result_dir/join.json"
run_one "$build_dir/benchmarks/db_modify_bench" "$result_dir/modify.json"
run_one "$build_dir/benchmarks/db_execution_pipeline_bench" "$result_dir/execution-pipeline.json"
run_one "$build_dir/benchmarks/db_execution_materialize_bench" \
    "$result_dir/execution-materialize.json"
run_one "$build_dir/benchmarks/db_execution_subplan_bench" \
    "$result_dir/execution-subplan.json"
run_one "$build_dir/benchmarks/db_execution_e2e_bench" \
    "$result_dir/execution-e2e.json"
run_one "$build_dir/benchmarks/db_tpcc_pressure_bench" "$result_dir/tpcc-pressure.json"
run_one "$build_dir/benchmarks/db_transaction_bench" "$result_dir/transaction.json"
run_one "$build_dir/benchmarks/db_sql_bench" "$result_dir/sql.json"
run_one "$build_dir/benchmarks/db_planner_bench" "$result_dir/planner.json"
run_one "$build_dir/benchmarks/db_server_result_adapter_bench" "$result_dir/server-result-adapter.json"

sha256sum \
    "$build_dir/benchmarks/db_buffer_pool_bench" \
    "$build_dir/benchmarks/db_btree_bench" \
    "$build_dir/benchmarks/db_heap_scan_bench" \
    "$build_dir/benchmarks/db_expression_bench" \
    "$build_dir/benchmarks/db_join_bench" \
    "$build_dir/benchmarks/db_modify_bench" \
    "$build_dir/benchmarks/db_execution_pipeline_bench" \
    "$build_dir/benchmarks/db_execution_materialize_bench" \
    "$build_dir/benchmarks/db_execution_subplan_bench" \
    "$build_dir/benchmarks/db_execution_e2e_bench" \
    "$build_dir/benchmarks/db_tpcc_pressure_bench" \
    "$build_dir/benchmarks/db_transaction_bench" \
    "$build_dir/benchmarks/db_sql_bench" \
    "$build_dir/benchmarks/db_planner_bench" \
    "$build_dir/benchmarks/db_server_result_adapter_bench" >"$result_dir/executables.sha256"

echo "$result_dir"
