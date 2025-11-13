cmake_minimum_required(VERSION 3.20)
project(tactix LANGUAGES CXX)

# Compiler standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories (place build artifacts under the build dir)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(ENABLE_PROFILING "Enable Tracy Profiler" OFF)

# Sources
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(tactix ${PROJECT_SOURCES})

# Include path
target_include_directories(tactix PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Platform defines
if(APPLE)
    target_compile_definitions(tactix PRIVATE PLATFORM_MACOS)
elseif(WIN32)
    target_compile_definitions(tactix PRIVATE PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(tactix PRIVATE PLATFORM_LINUX)
endif()

# Compiler warnings (helpful defaults)
if (MSVC)
    target_compile_options(tactix PRIVATE /W4 /permissive-)
else()
    target_compile_options(tactix PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

