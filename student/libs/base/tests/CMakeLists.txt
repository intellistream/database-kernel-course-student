db_add_test(
    db_base_tests
    SOURCES
        bytes_test.cpp
        checked_test.cpp
        crc32c_test.cpp
        error_test.cpp
        exception_boundary_test.cpp
        hash_test.cpp
        identifier_test.cpp
        text_test.cpp
        version_test.cpp
    LIBRARIES db::base
    LABELS base unit
)

add_library(
    db_base_public_header_check
    OBJECT
        bytes_header_compile.cpp
        checked_header_compile.cpp
        crc32c_header_compile.cpp
        error_header_compile.cpp
        exception_boundary_header_compile.cpp
        hash_header_compile.cpp
        identifier_header_compile.cpp
        result_header_compile.cpp
        text_header_compile.cpp
        version_header_compile.cpp
)
target_link_libraries(db_base_public_header_check PRIVATE db::base)
db_configure_target(db_base_public_header_check)
