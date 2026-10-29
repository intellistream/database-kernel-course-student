db_add_test(
    db_execution_append_lab_tests
    SOURCES append_lab_test.cpp
    LIBRARIES db::execution db::planner
    LABELS execution lab unit
)

db_add_test(
    db_execution_unique_lab_tests
    SOURCES unique_lab_test.cpp
    LIBRARIES db::execution db::planner
    LABELS execution lab unit
)

db_add_test(
    db_execution_aggregate_tests
    SOURCES aggregate_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution aggregate unit
)

db_add_test(
    db_execution_expression_tests
    SOURCES expression_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution expression unit
)

db_add_test(
    db_execution_operator_tests
    SOURCES operators_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution operators unit
)

db_add_test(
    db_execution_nested_loop_tests
    SOURCES nested_loop_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution nested_loop unit
)

db_add_test(
    db_execution_hash_join_tests
    SOURCES hash_join_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution hash_join unit
)

db_add_test(
    db_execution_merge_join_tests
    SOURCES merge_join_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution merge_join unit
)

db_add_test(
    db_execution_subplan_tests
    SOURCES subplan_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution subplan unit
)

db_add_test(
    db_execution_modify_table_tests
    SOURCES modify_table_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution modify_table unit
)

db_add_test(
    db_execution_differential_tests
    SOURCES execution_differential_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution differential integration
)

db_add_test(
    db_execution_sort_tests
    SOURCES sort_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution sort unit
)

db_add_test(
    db_execution_window_tests
    SOURCES window_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution window unit
)

db_add_test(
    db_execution_tuple_tests
    SOURCES tuple_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution tuple unit
)

db_add_test(
    db_execution_context_tests
    SOURCES exec_context_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution context unit
)

db_add_test(
    db_execution_plan_state_tests
    SOURCES plan_state_test.cpp
    LIBRARIES
        db::execution
        db::planner
    LABELS execution plan_state unit
)

add_library(
    db_execution_public_header_check
    OBJECT
        exec_context_header_compile.cpp
        aggregate_header_compile.cpp
        append_header_compile.cpp
        expression_header_compile.cpp
        operators_header_compile.cpp
        nested_loop_header_compile.cpp
        hash_join_header_compile.cpp
        merge_join_header_compile.cpp
        modify_port_header_compile.cpp
        modify_table_header_compile.cpp
        scan_port_header_compile.cpp
        plan_state_factory_header_compile.cpp
        limit_header_compile.cpp
        row_count_header_compile.cpp
        sort_header_compile.cpp
        sort_support_header_compile.cpp
        topn_header_compile.cpp
        plan_state_header_compile.cpp
        tuple_header_compile.cpp
        subplan_header_compile.cpp
        subquery_scan_header_compile.cpp
        unique_header_compile.cpp
)
target_link_libraries(db_execution_public_header_check PRIVATE db::execution)
db_configure_target(db_execution_public_header_check)
