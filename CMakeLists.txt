cmake_minimum_required(VERSION 3.21)
project(game_storage VERSION 0.3.0 LANGUAGES CXX)
include(GNUInstallDirs)

option(GAME_STORAGE_BUILD_TESTS "Build core tests" ${PROJECT_IS_TOP_LEVEL})
option(GAME_STORAGE_BUILD_EXAMPLES "Build the usage example" ${PROJECT_IS_TOP_LEVEL})
option(GAME_STORAGE_BUILD_UI "Build renderer-independent table behavior" ON)
option(GAME_STORAGE_BUILD_RAYLIB "Build the optional raylib renderer" OFF)
set(GAME_STORAGE_RAYLIB_SOURCE_DIR "" CACHE PATH "Optional existing raylib source directory")

add_library(game_storage STATIC src/storage.cpp src/json.cpp)
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

set(game_storage_install_targets game_storage)
if(GAME_STORAGE_BUILD_UI)
    add_library(game_storage_ui STATIC src/ui.cpp)
    add_library(game_storage::ui ALIAS game_storage_ui)
    set_target_properties(game_storage_ui PROPERTIES EXPORT_NAME ui CXX_EXTENSIONS OFF)
    target_link_libraries(game_storage_ui PUBLIC game_storage::game_storage)
    list(APPEND game_storage_install_targets game_storage_ui)
endif()
if(GAME_STORAGE_BUILD_RAYLIB)
    if(NOT GAME_STORAGE_BUILD_UI)
        message(FATAL_ERROR "The raylib renderer requires GAME_STORAGE_BUILD_UI=ON")
    endif()
    if(NOT TARGET raylib)
        if(GAME_STORAGE_RAYLIB_SOURCE_DIR)
            enable_language(C)
            set(BUILD_EXAMPLES OFF)
            add_subdirectory("${GAME_STORAGE_RAYLIB_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)
        else()
            find_package(raylib CONFIG REQUIRED)
        endif()
    endif()
    add_library(game_storage_raylib STATIC src/raylib_renderer.cpp)
    add_library(game_storage::raylib ALIAS game_storage_raylib)
    set_target_properties(game_storage_raylib PROPERTIES EXPORT_NAME raylib CXX_EXTENSIONS OFF)
    target_link_libraries(game_storage_raylib PUBLIC game_storage::ui
        $<BUILD_INTERFACE:raylib> $<INSTALL_INTERFACE:raylib>)
    if(WIN32)
        # Some raylib packages omit the timer dependency from their exported target.
        target_link_libraries(game_storage_raylib PUBLIC winmm)
    endif()
    install(TARGETS game_storage_raylib EXPORT game_storageRaylibTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT game_storageRaylibTargets NAMESPACE game_storage::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/game_storage)
    if(GAME_STORAGE_BUILD_EXAMPLES)
        add_executable(storage_ui_example examples/table.cpp)
        target_link_libraries(storage_ui_example PRIVATE game_storage::raylib)
        file(COPY examples/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
        if(EMSCRIPTEN)
            set_target_properties(storage_ui_example PROPERTIES SUFFIX ".html")
            target_link_options(storage_ui_example PRIVATE
                "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/examples/assets@/assets"
                "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/examples/web_shell.html"
                "-sALLOW_MEMORY_GROWTH=1")
            set_property(TARGET storage_ui_example APPEND PROPERTY LINK_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/web_shell.html")
        endif()
    endif()
endif()

foreach(ui_target game_storage_ui game_storage_raylib)
    if(TARGET ${ui_target})
        if(MSVC)
            target_compile_options(${ui_target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${ui_target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()

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
    if(GAME_STORAGE_BUILD_UI)
        add_executable(storage_ui_tests tests/ui_tests.cpp)
        target_link_libraries(storage_ui_tests PRIVATE game_storage::ui)
        add_test(NAME storage_ui COMMAND storage_ui_tests)
    endif()
endif()

include(CMakePackageConfigHelpers)
install(TARGETS ${game_storage_install_targets} EXPORT game_storageTargets
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
