add_library(db_planner STATIC)
add_library(db::planner ALIAS db_planner)

target_sources(
    db_planner
    PRIVATE
        src/metadata_port.cpp
        src/equivalence.cpp
        src/explain.cpp
        src/join_order.cpp
        src/join_strategy.cpp
        src/lowering_bound_adapter.cpp
        src/lowering_aggregate.cpp
        src/lowering_dml.cpp
        src/lowering_expression.cpp
        src/lowering_join_strategy.cpp
        src/lowering_query.cpp
        src/lowering_required_columns.cpp
        src/lowering_select_ops.cpp
        src/lowering_statement.cpp
        src/lowering_subplan.cpp
        src/lowering_subplan_rewrite.cpp
        src/lowering_table.cpp
        src/plan.cpp
        src/plan_descriptor_validation.cpp
        src/plan_expression_validation.cpp
        src/plan_validation.cpp
        src/path_restrictions.cpp
        src/path_index.cpp
        src/path_relation.cpp
        src/preprocess_convert.cpp
        src/preprocess_expression.cpp
        src/preprocess_expression_compare.cpp
        src/preprocess_expression_features.cpp
        src/preprocess_query.cpp
        src/preprocess_validation.cpp
)
target_include_directories(
    db_planner
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_planner PUBLIC cxx_std_23)
target_link_libraries(
    db_planner
    PUBLIC
        db::base
        db::types
        db::sql
)
db_configure_target(db_planner)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
