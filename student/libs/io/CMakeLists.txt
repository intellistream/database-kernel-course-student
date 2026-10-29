add_library(db_io STATIC)
add_library(db::io ALIAS db_io)

target_sources(
    db_io
    PRIVATE
        src/durable.cpp
        src/file_lock.cpp
        src/posix.cpp
        src/socket.cpp
        src/unique_file_descriptor.hpp
)
target_include_directories(
    db_io
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_io PUBLIC cxx_std_23)
target_compile_definitions(db_io PRIVATE _GNU_SOURCE)
target_link_libraries(db_io PUBLIC db::base)
db_configure_target(db_io)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
