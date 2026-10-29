add_library(db_execution STATIC)
add_library(db::execution ALIAS db_execution)

target_sources(
    db_execution
    PRIVATE
        src/exec_context.cpp
        src/aggregate.cpp
        src/append.cpp
        src/expression.cpp
        src/operators.cpp
        src/plan_state.cpp
        src/projection_program.cpp
        src/scan_state.cpp
        src/limit.cpp
        src/hash_join.cpp
        src/merge_join.cpp
        src/modify_table.cpp
        src/nested_loop.cpp
        src/row_count.cpp
        src/sort.cpp
        src/sort_support.cpp
        src/subplan.cpp
        src/subquery_scan.cpp
        src/topn.cpp
        src/tuple.cpp
        src/unique.cpp
)
target_include_directories(
    db_execution
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_execution PUBLIC cxx_std_23)
target_link_libraries(
    db_execution
    PUBLIC
        db::base
        db::planner
        db::types
)
db_configure_target(db_execution)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
