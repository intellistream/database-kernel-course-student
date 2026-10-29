include_guard(GLOBAL)

include(CMakeParseArguments)
include(GoogleTest)

function(db_add_test target)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES LIBRARIES LABELS)
    cmake_parse_arguments(
        PARSE_ARGV 1
        DB_TEST
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
    )

    if(NOT DB_TEST_SOURCES)
        message(FATAL_ERROR "db_add_test(${target}) requires SOURCES")
    endif()

    add_executable(${target} ${DB_TEST_SOURCES})
    db_configure_target(${target})
    target_link_libraries(
        ${target}
        PRIVATE
            GTest::gtest_main
            ${DB_TEST_LIBRARIES}
    )

    if(DB_ENABLE_THREAD_SANITIZER)
        find_program(DB_SETARCH_EXECUTABLE NAMES setarch REQUIRED)
        # CMake 3.22 has no TEST_LAUNCHER. GoogleTest uses this legacy
        # execution prefix for discovery and each registered test.
        set_property(
            TARGET ${target}
            PROPERTY CROSSCOMPILING_EMULATOR
                     "${DB_SETARCH_EXECUTABLE};${CMAKE_SYSTEM_PROCESSOR};-R"
        )
    endif()

    gtest_discover_tests(
        ${target}
        DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        PROPERTIES LABELS "${DB_TEST_LABELS}"
    )
endfunction()
