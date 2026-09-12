cmake_minimum_required(VERSION 3.21)
project(game_storage VERSION 0.1.0 LANGUAGES CXX)

option(GAME_STORAGE_BUILD_TESTS "Build core tests" ${PROJECT_IS_TOP_LEVEL})
option(GAME_STORAGE_BUILD_EXAMPLES "Build the usage example" ${PROJECT_IS_TOP_LEVEL})

add_library(game_storage STATIC src/storage.cpp)
add_library(game_storage::game_storage ALIAS game_storage)
target_include_directories(game_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(game_storage PUBLIC cxx_std_17)
set_target_properties(game_storage PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(game_storage PRIVATE /W4 /permissive-)
else()
    target_compile_options(game_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(EMSCRIPTEN)
    # Exceptions from callbacks and checked Value access retain desktop semantics.
    target_compile_options(game_storage PUBLIC -fexceptions)
    target_link_options(game_storage PUBLIC -fexceptions)
endif()

if(GAME_STORAGE_BUILD_EXAMPLES)
    add_executable(storage_example examples/basic.cpp)
    target_link_libraries(storage_example PRIVATE game_storage::game_storage)
    if(EMSCRIPTEN)
        set_target_properties(storage_example PROPERTIES SUFFIX ".html")
    endif()
endif()

if(GAME_STORAGE_BUILD_TESTS)
    enable_testing()
    add_executable(storage_tests tests/storage_tests.cpp)
    target_link_libraries(storage_tests PRIVATE game_storage::game_storage)
    add_test(NAME storage_core COMMAND storage_tests)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS game_storage EXPORT game_storageTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT game_storageTargets NAMESPACE game_storage::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/game_storage)
configure_package_config_file(cmake/game_storageConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/game_storageConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/game_storage)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/game_storageConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/game_storageConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/game_storageConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/game_storage)
