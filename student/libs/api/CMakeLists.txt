add_library(db_api STATIC)
add_library(db::api ALIAS db_api)

target_sources(
    db_api
    PRIVATE
        src/api.cpp
        src/result.cpp
)
target_include_directories(
    db_api
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_api PUBLIC cxx_std_23)
target_link_libraries(
    db_api
    PUBLIC
        db::base
        db::types
    PRIVATE
        db::engine
)
db_configure_target(db_api)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
