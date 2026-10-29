find_package(Threads REQUIRED)

add_library(db_tpcc_app STATIC)
add_library(db::tpcc_app ALIAS db_tpcc_app)

target_sources(
    db_tpcc_app
    PRIVATE
        src/config.cpp
        src/delivery.cpp
        src/load.cpp
        src/load_manifest.cpp
        src/metrics.cpp
        src/new_order.cpp
        src/order_status.cpp
        src/payment.cpp
        src/phase.cpp
        src/prepared.cpp
        src/report.cpp
        src/runner.cpp
        src/session.cpp
        src/stock_level.cpp
        src/transaction.cpp
        src/transaction_common.cpp
        src/workload.cpp
)
target_compile_features(db_tpcc_app PUBLIC cxx_std_23)
target_include_directories(db_tpcc_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
    db_tpcc_app
    PUBLIC
        db::base
        db::io
        db::protocol
        db::types
    PRIVATE
        Threads::Threads
)
db_configure_target(db_tpcc_app)

add_executable(tpcc src/main.cpp)
target_compile_features(tpcc PRIVATE cxx_std_23)
target_include_directories(tpcc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tpcc PRIVATE db::tpcc_app)
set_target_properties(
    tpcc
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
db_configure_target(tpcc)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
