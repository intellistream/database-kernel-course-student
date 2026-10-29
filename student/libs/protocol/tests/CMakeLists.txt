db_add_test(
    db_protocol_codec_tests
    SOURCES
        frame_codec_test.cpp
        request_dispatcher_test.cpp
        response_encoder_test.cpp
        tcp_server_test.cpp
    LIBRARIES db::protocol
    LABELS protocol unit
)
target_include_directories(
    db_protocol_codec_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/tests/protocol
)

add_library(
    db_protocol_public_header_check
    OBJECT
    frame_codec_header_compile.cpp
    rmdb_client_header_compile.cpp
    request_dispatcher_header_compile.cpp
    response_encoder_header_compile.cpp
    tcp_server_header_compile.cpp
)
target_link_libraries(db_protocol_public_header_check PRIVATE db::protocol)
db_configure_target(db_protocol_public_header_check)
