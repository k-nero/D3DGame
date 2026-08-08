cmake_minimum_required(VERSION 4.3)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)          # clangd on the Mac side
set(VCPKG_INSTALLED_DIR ${CMAKE_SOURCE_DIR}/vcpkg_installed)
set(VCPKG_MANIFEST_DIR ${CMAKE_SOURCE_DIR})
set(VCPKG_MANIFEST_MODE ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
option(ENG_BUILD_TESTS "Build unit tests" ON)

project(engine VERSION 0.1.0 LANGUAGES CXX C )

# Objective-C++ needed only for the macOS window stub (app_macos.mm)
if(APPLE)
    enable_language(OBJCXX)
endif()


# ------------------------------------------------------------------
# eng_common: warnings + defines shared by every target.
# INTERFACE target so it adds flags without being a real library.
# ------------------------------------------------------------------
add_library(eng_common INTERFACE)

if(MSVC)
    target_compile_options(eng_common INTERFACE /W4 /permissive- /MP /utf-8)
    target_compile_definitions(eng_common INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(eng_common INTERFACE -Wall -Wextra -Wshadow)
endif()

target_compile_definitions(eng_common INTERFACE
    $<$<CONFIG:Debug>:ENG_DEBUG=1>
    $<$<PLATFORM_ID:Windows>:ENG_PLATFORM_WINDOWS=1>
    $<$<PLATFORM_ID:Darwin>:ENG_PLATFORM_MACOS=1>
)

include(cmake/CompileShaders.cmake)

add_subdirectory(engine)
add_subdirectory(game)

if(ENG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
