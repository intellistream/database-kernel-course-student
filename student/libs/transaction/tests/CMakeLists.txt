add_library(db_transaction_private_test_support INTERFACE)
target_include_directories(
    db_transaction_private_test_support
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(
    db_transaction_private_test_support
    INTERFACE db::transaction
)

db_add_test(
    db_transaction_tests
    SOURCES
        backend_test.cpp
        commit_log_segment_test.cpp
        commit_log_test.cpp
        completion_journal_adapter_test.cpp
        lock_queue_test.cpp
        lock_wait_graph_test.cpp
        lock_manager_test.cpp
        manager_test.cpp
        proc_array_test.cpp
        proc_slot_test.cpp
        ssi_commit_order_test.cpp
        ssi_conflict_graph_test.cpp
        ssi_lifecycle_test.cpp
        ssi_predicate_locks_test.cpp
        ssi_read_locks_test.cpp
        ssi_test.cpp
        state_test.cpp
        visibility_test.cpp
        xid_test.cpp
        xid_waiter_test.cpp
    LIBRARIES
        db::transaction
        db::io
        db::test_support
        db_transaction_private_test_support
    LABELS transaction unit
)

add_library(
    db_transaction_public_header_check
    OBJECT
        backend_header_compile.cpp
        commit_log_header_compile.cpp
        completion_journal_header_compile.cpp
        lock_manager_header_compile.cpp
        manager_header_compile.cpp
        proc_array_header_compile.cpp
        proc_slot_header_compile.cpp
        ssi_header_compile.cpp
        state_header_compile.cpp
        visibility_header_compile.cpp
        xid_header_compile.cpp
        xid_waiter_header_compile.cpp
)
target_link_libraries(db_transaction_public_header_check PRIVATE db::transaction)
db_configure_target(db_transaction_public_header_check)

add_library(
    db_transaction_private_header_check
    OBJECT
        backend_transaction_services_header_compile.cpp
        commit_log_files_header_compile.cpp
        commit_log_format_header_compile.cpp
        commit_log_segment_header_compile.cpp
        completion_journal_adapter_header_compile.cpp
        lock_mode_policy_header_compile.cpp
        lock_manager_state_header_compile.cpp
        lock_queue_header_compile.cpp
        lock_wait_graph_header_compile.cpp
        proc_array_state_header_compile.cpp
        ssi_commit_order_header_compile.cpp
        ssi_conflict_graph_header_compile.cpp
        ssi_lifecycle_header_compile.cpp
        ssi_predicate_locks_header_compile.cpp
        ssi_read_locks_header_compile.cpp
        ssi_state_header_compile.cpp
        transaction_completion_header_compile.cpp
)
target_link_libraries(
    db_transaction_private_header_check
    PRIVATE db_transaction_private_test_support
)
db_configure_target(db_transaction_private_header_check)
