db_add_test(
    db_planner_unique_lab_tests
    SOURCES unique_lab_test.cpp
    LIBRARIES db::planner
    LABELS planner lab unit
)
target_include_directories(
    db_planner_unique_lab_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

db_add_test(
    db_planner_metadata_port_tests
    SOURCES metadata_port_test.cpp
    LIBRARIES
        db::planner
    LABELS planner catalog unit
)

db_add_test(
    db_planner_equivalence_tests
    SOURCES equivalence_test.cpp
    LIBRARIES
        db::planner
    LABELS planner equivalence unit
)

db_add_test(
    db_planner_join_order_tests
    SOURCES join_order_test.cpp
    LIBRARIES
        db::planner
    LABELS planner join_order unit
)

db_add_test(
    db_planner_join_strategy_tests
    SOURCES join_strategy_test.cpp
    LIBRARIES
        db::planner
    LABELS planner join_strategy unit
)

db_add_test(
    db_planner_lowering_tests
    SOURCES lowering_test.cpp
    LIBRARIES
        db::planner
        db::sql
    LABELS planner lowering unit
)

db_add_test(
    db_planner_differential_tests
    SOURCES planner_differential_test.cpp
    LIBRARIES
        db::planner
        db::sql
    LABELS planner differential integration
)

db_add_test(
    db_planner_plan_tests
    SOURCES plan_test.cpp
    LIBRARIES
        db::planner
    LABELS planner plan explain unit
)

db_add_test(
    db_planner_path_tests
    SOURCES path_test.cpp
    LIBRARIES
        db::planner
    LABELS planner path unit
)

db_add_test(
    db_planner_preprocess_tests
    SOURCES preprocess_test.cpp
    LIBRARIES
        db::planner
        db::sql
    LABELS planner preprocess unit
)

add_library(
    db_planner_public_header_check
    OBJECT
        metadata_port_header_compile.cpp
        equivalence_header_compile.cpp
        join_order_header_compile.cpp
        join_strategy_header_compile.cpp
        lowering_header_compile.cpp
        path_header_compile.cpp
        plan_header_compile.cpp
        preprocess_header_compile.cpp
)
target_link_libraries(db_planner_public_header_check PRIVATE db::planner)
db_configure_target(db_planner_public_header_check)

add_library(
    db_planner_private_header_check
    OBJECT
        plan_descriptor_validation_header_compile.cpp
        plan_expression_validation_header_compile.cpp
)
target_include_directories(
    db_planner_private_header_check
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(db_planner_private_header_check PRIVATE db::planner)
db_configure_target(db_planner_private_header_check)
