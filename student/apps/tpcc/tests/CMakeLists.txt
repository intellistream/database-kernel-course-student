db_add_test(
    db_tpcc_app_tests
    SOURCES
        config_test.cpp
        load_manifest_test.cpp
        metrics_test.cpp
        prepared_test.cpp
        workload_test.cpp
    LIBRARIES
        db::tpcc_app
        db::base
        db::io
        db::protocol
        db::test_support
        db::types
    LABELS tpcc application
)
target_include_directories(
    db_tpcc_app_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# A canonical 50-warehouse fixture is mutable and intentionally external. Keep
# its live protocol check opt-in so routine CTest has no environmental skips.
if(DB_TPCC_LIVE_ADDRESS)
    db_add_test(
        db_tpcc_live_tests
        SOURCES live_prepare_test.cpp
        LIBRARIES
            db::tpcc_app
            db::io
            db::protocol
        LABELS tpcc application integration
    )
    target_include_directories(
        db_tpcc_live_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    target_compile_definitions(
        db_tpcc_live_tests
        PRIVATE DB_TPCC_LIVE_ADDRESS="${DB_TPCC_LIVE_ADDRESS}"
    )
endif()
