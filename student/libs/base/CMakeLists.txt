add_library(db_base STATIC)
add_library(db::base ALIAS db_base)

target_sources(db_base PRIVATE src/crc32c.cpp src/error.cpp src/version.cpp)
target_include_directories(
    db_base
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(db_base PUBLIC cxx_std_23)
db_configure_target(db_base)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
