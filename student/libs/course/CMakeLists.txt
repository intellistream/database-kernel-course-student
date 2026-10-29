function(db_add_course_topic topic)
    set(target "db_course_${topic}")
    add_library(${target} INTERFACE)
    add_library("db::course_${topic}" ALIAS ${target})
    target_include_directories(
        ${target}
        INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_compile_features(${target} INTERFACE cxx_std_23)
    target_link_libraries(${target} INTERFACE ${ARGN})
endfunction()

db_add_course_topic(
    foundations
    db::api
    db::base
    db::catalog
    db::execution
    db::types
)
db_add_course_topic(storage db::storage)
db_add_course_topic(access db::access)
db_add_course_topic(
    query
    db::base
    db::execution
    db::planner
    db::sql
    db::types
)
db_add_course_topic(transaction db::transaction)
db_add_course_topic(wal db::wal)

# Compatibility umbrella for short labs. Larger consumers link one or more
# topic targets so unrelated database subsystems do not become implicit edges.
add_library(db_course INTERFACE)
add_library(db::course ALIAS db_course)
target_link_libraries(
    db_course
    INTERFACE
        db::course_access
        db::course_foundations
        db::course_query
        db::course_storage
        db::course_transaction
        db::course_wal
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
