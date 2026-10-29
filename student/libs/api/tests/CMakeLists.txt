db_add_test(
    db_api_tests
    SOURCES api_test.cpp
    LIBRARIES db::api db::test_support
    LABELS api integration
)

db_add_test(
    db_api_result_tests
    SOURCES result_test.cpp allocation_failure.cpp
    LIBRARIES db::api
    LABELS api result unit
)

add_library(
    db_api_public_header_check
    OBJECT
        api_header_compile.cpp
        database_header_compile.cpp
        result_header_compile.cpp
        session_header_compile.cpp
)
target_link_libraries(db_api_public_header_check PRIVATE db::api)
db_configure_target(db_api_public_header_check)
