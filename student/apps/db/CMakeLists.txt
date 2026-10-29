add_library(db_shell STATIC)
add_library(db::shell ALIAS db_shell)

target_sources(
    db_shell
    PRIVATE
        src/shell.cpp
)
target_include_directories(
    db_shell
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_shell PUBLIC cxx_std_23)
target_link_libraries(
    db_shell
    PUBLIC
        db::api
    PRIVATE
        db::sql
)
db_configure_target(db_shell)

add_executable(db src/main.cpp)
target_compile_features(db PUBLIC cxx_std_23)
target_link_libraries(db PRIVATE db::shell)
db_configure_target(db)

add_library(db_shell_public_header_check OBJECT tests/shell_header_compile.cpp)
target_link_libraries(db_shell_public_header_check PRIVATE db::shell)
db_configure_target(db_shell_public_header_check)
