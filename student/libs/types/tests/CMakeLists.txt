db_add_test(
    db_types_tests
    SOURCES
        codec_test.cpp
        coercion_test.cpp
        data_type_test.cpp
        datetime_test.cpp
        operator_registry_test.cpp
        type_baseline_test.cpp
        type_io_test.cpp
        type_registry_test.cpp
        value_test.cpp
    LIBRARIES db::types
    LABELS types unit
)

add_library(
    db_types_public_header_check
    OBJECT
        codec_header_compile.cpp
        coercion_header_compile.cpp
        data_type_header_compile.cpp
        datetime_header_compile.cpp
        operator_header_compile.cpp
        operator_registry_header_compile.cpp
        type_io_header_compile.cpp
        type_registry_header_compile.cpp
        value_header_compile.cpp
)
target_link_libraries(db_types_public_header_check PRIVATE db::types)
db_configure_target(db_types_public_header_check)
