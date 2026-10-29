add_library(db_types STATIC)
add_library(db::types ALIAS db_types)

target_sources(
    db_types
    PRIVATE
        src/codec.cpp
        src/coercion.cpp
        src/datetime.cpp
        src/operator_registry.cpp
        src/type_io.cpp
        src/type_registry.cpp
        src/value.cpp
)
target_include_directories(
    db_types
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_types PUBLIC cxx_std_23)
target_link_libraries(db_types PUBLIC db::base)
db_configure_target(db_types)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
