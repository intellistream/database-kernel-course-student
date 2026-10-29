find_package(Threads REQUIRED)

add_library(db_server_app STATIC)
add_library(db::server_app ALIAS db_server_app)

target_sources(
    db_server_app
    PRIVATE
        src/api_result_adapter.cpp
        src/api_session_adapter.cpp
        src/server_application.cpp
        src/stream_sql_compatibility.cpp
)
target_compile_features(db_server_app PUBLIC cxx_std_23)
target_link_libraries(
    db_server_app
    PUBLIC
        db::base
        db::io
        db::protocol
    PRIVATE
        db::api
        db::types
        Threads::Threads
)
db_configure_target(db_server_app)

function(db_add_server_executable target)
    add_executable(${target} src/main.cpp)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE db::server_app)
    set_target_properties(
        ${target}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    )
    db_configure_target(${target})
endfunction()

# Keep both reference-facing names as genuine C++ executable targets. The
# external launcher builds `rmdb` and executes build/bin/rmdb, while humans
# can use the descriptive db-server binary.
db_add_server_executable(db-server)
db_add_server_executable(rmdb)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
