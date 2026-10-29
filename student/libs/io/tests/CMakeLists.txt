db_add_test(
    db_io_tests
    SOURCES
        durable_test.cpp
        file_lock_test.cpp
        posix_test.cpp
        socket_test.cpp
    LIBRARIES
        db::io
        db::test_support
    LABELS io unit
)

# Component-internal test seam for the acquisition guard. The source directory
# remains absent from db::io's public include interface.
add_library(db_io_private_test_support INTERFACE)
target_include_directories(
    db_io_private_test_support
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(db_io_private_test_support INTERFACE db::io)

db_add_test(
    db_io_unique_file_descriptor_tests
    SOURCES unique_file_descriptor_test.cpp
    LIBRARIES db_io_private_test_support
    LABELS io unit
)

add_library(
    db_io_public_header_check
    OBJECT
        durable_header_compile.cpp
        file_lock_header_compile.cpp
        posix_header_compile.cpp
        socket_header_compile.cpp
)
target_link_libraries(db_io_public_header_check PRIVATE db::io)
db_configure_target(db_io_public_header_check)
