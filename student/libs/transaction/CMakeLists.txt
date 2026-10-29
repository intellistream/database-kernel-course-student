add_library(db_transaction STATIC)
add_library(db::transaction ALIAS db_transaction)

target_sources(
    db_transaction
    PRIVATE
        src/backend.cpp
        src/backend_transaction_services.cpp
        src/commit_log.cpp
        src/commit_log_files.cpp
        src/commit_log_format.cpp
        src/commit_log_segment.cpp
        src/completion_journal_adapter.cpp
        src/lock_mode_policy.cpp
        src/lock_manager.cpp
        src/lock_queue.cpp
        src/lock_wait_graph.cpp
        src/manager.cpp
        src/proc_array.cpp
        src/proc_array_completion.cpp
        src/proc_array_snapshot.cpp
        src/proc_array_state.cpp
        src/proc_slot.cpp
        src/proc_slot_lease.cpp
        src/ssi.cpp
        src/ssi_commit_order.cpp
        src/ssi_conflict_graph.cpp
        src/ssi_lifecycle.cpp
        src/ssi_predicate_locks.cpp
        src/ssi_read_locks.cpp
        src/snapshot_coordinator.cpp
        src/state.cpp
        src/transaction_completion.cpp
        src/visibility.cpp
        src/xid.cpp
        src/xid_waiter.cpp
)
target_include_directories(
    db_transaction
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_transaction PUBLIC cxx_std_23)
target_link_libraries(
    db_transaction
    PUBLIC db::base db::types
    PRIVATE db::io
)
db_configure_target(db_transaction)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
