add_library(db_protocol STATIC)
add_library(db::protocol ALIAS db_protocol)

find_package(Threads REQUIRED)

target_sources(
    db_protocol
    PRIVATE
        src/frame_codec.cpp
        src/network_byte_appender.hpp
        src/rmdb_client.cpp
        src/rmdb_client_request_codec.cpp
        src/request_dispatcher.cpp
        src/response_encoder.cpp
        src/session_port.cpp
        src/timed_tcp_stream.cpp
        src/tcp_server.cpp
)
target_include_directories(
    db_protocol
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_protocol PUBLIC cxx_std_23)
target_link_libraries(
    db_protocol
    PUBLIC db::base db::io db::types
    PRIVATE Threads::Threads
)
db_configure_target(db_protocol)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
