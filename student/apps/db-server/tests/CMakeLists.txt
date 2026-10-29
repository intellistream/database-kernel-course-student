db_add_test(
    db_server_app_tests
    SOURCES
        api_result_adapter_test.cpp
        api_session_adapter_test.cpp
        rmdb_live_interop_test.cpp
        server_application_test.cpp
    LIBRARIES
        db::server_app
        db::api
        db::io
        db::protocol
        db::test_support
    LABELS server integration protocol
)
target_include_directories(
    db_server_app_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Reference interop is deliberately opt-in. Normal C++ builds and tests do not
# inspect, build, or run ../Ferrum; a caller must explicitly provide a
# read-only checkout through DB_REFERENCE_ROOT.
if(DB_REFERENCE_ROOT)
    get_filename_component(db_reference_root "${DB_REFERENCE_ROOT}" ABSOLUTE)
    set(db_reference_client_dir "${db_reference_root}/third-party/ferrum-client")
    set(db_reference_server_binary "${db_reference_root}/target/release/ferrum-server")
    if(NOT EXISTS "${db_reference_client_dir}/Cargo.toml")
        message(FATAL_ERROR
            "DB_REFERENCE_ROOT does not contain third-party/ferrum-client/Cargo.toml"
        )
    endif()
    if(NOT EXISTS "${db_reference_server_binary}")
        message(FATAL_ERROR
            "DB_REFERENCE_ROOT has no target/release/ferrum-server; build the read-only reference "
            "separately before enabling interop tests"
        )
    endif()
    find_program(DB_REFERENCE_CARGO_EXECUTABLE NAMES cargo REQUIRED)
    set(db_reference_harness_dir "${PROJECT_BINARY_DIR}/reference-client-harness")
    file(MAKE_DIRECTORY "${db_reference_harness_dir}/src")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/reference-client-harness/Cargo.toml.in"
        "${db_reference_harness_dir}/Cargo.toml"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/reference-client-harness/src/main.rs"
        "${db_reference_harness_dir}/src/main.rs"
        COPYONLY
    )

    db_add_test(
        db_reference_interop_tests
        SOURCES reference_interop_test.cpp
        LIBRARIES
            db::server_app
            db::io
            db::protocol
            db::test_support
        LABELS server protocol interoperability differential
    )
    target_include_directories(
        db_reference_interop_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    target_compile_definitions(
        db_reference_interop_tests
        PRIVATE
            DB_REFERENCE_CARGO_EXECUTABLE="${DB_REFERENCE_CARGO_EXECUTABLE}"
            DB_REFERENCE_CLIENT_MANIFEST="${db_reference_harness_dir}/Cargo.toml"
            DB_REFERENCE_SERVER_BINARY="${db_reference_server_binary}"
    )
endif()
