db_add_test(
    db_engine_tests
    SOURCES database_test.cpp session_test.cpp dispatch_test.cpp ddl_test.cpp copy_test.cpp prepared_test.cpp query_test.cpp static_checkpoint_test.cpp union_test.cpp result_test.cpp
    LIBRARIES db::engine db::catalog db::wal db::test_support
    LABELS engine integration
)

add_library(db_engine_public_header_check OBJECT database_header_compile.cpp)
target_link_libraries(db_engine_public_header_check PRIVATE db::engine)
db_configure_target(db_engine_public_header_check)

add_library(db_engine_session_public_header_check OBJECT session_header_compile.cpp)
target_link_libraries(db_engine_session_public_header_check PRIVATE db::engine)
db_configure_target(db_engine_session_public_header_check)

add_library(db_engine_dispatch_public_header_check OBJECT dispatch_header_compile.cpp)
target_link_libraries(db_engine_dispatch_public_header_check PRIVATE db::engine)
db_configure_target(db_engine_dispatch_public_header_check)

add_library(db_engine_result_public_header_check OBJECT result_header_compile.cpp)
target_link_libraries(db_engine_result_public_header_check PRIVATE db::engine)
db_configure_target(db_engine_result_public_header_check)

add_library(db_engine_private_header_check OBJECT
    checkpoint_admission_header_compile.cpp
    database_startup_header_compile.cpp
    ddl_executor_header_compile.cpp
    copy_text_decoder_header_compile.cpp
    maintenance_executor_header_compile.cpp
    utility_execution_state_header_compile.cpp

    physical_modify_port_header_compile.cpp
    storage_plan_factory_header_compile.cpp
    query_error_header_compile.cpp
    storage_query_binding_header_compile.cpp
    heap_row_decoder_header_compile.cpp
    logical_scan_ssi_header_compile.cpp
    storage_scan_ports_header_compile.cpp
)
target_link_libraries(db_engine_private_header_check PRIVATE db::engine
    $<TARGET_PROPERTY:db_engine,LINK_LIBRARIES>)
db_configure_target(db_engine_private_header_check)
