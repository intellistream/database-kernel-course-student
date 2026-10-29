db_add_test(
    db_course_tests
    SOURCES course_test.cpp
    LIBRARIES db::course
    LABELS course integration
)

set(course_header_checks)

function(db_add_course_header_check topic source library)
    set(target "db_course_${topic}_header_check")
    add_library(${target} OBJECT ${source})
    target_link_libraries(${target} PRIVATE ${library})
    db_configure_target(${target})
    set(course_header_checks ${course_header_checks} ${target} PARENT_SCOPE)
endfunction()

db_add_course_header_check(access access_header_compile.cpp db::course_access)
db_add_course_header_check(course course_header_compile.cpp db::course)
db_add_course_header_check(
    foundations
    foundations_header_compile.cpp
    db::course_foundations
)
db_add_course_header_check(query query_header_compile.cpp db::course_query)
db_add_course_header_check(storage storage_header_compile.cpp db::course_storage)
db_add_course_header_check(
    transaction
    transaction_header_compile.cpp
    db::course_transaction
)
db_add_course_header_check(wal wal_header_compile.cpp db::course_wal)

add_custom_target(db_course_public_header_check DEPENDS ${course_header_checks})
