cmake_minimum_required(VERSION 3.20)
project(Tactix LANGUAGES CXX)

# ==============================
#  Compiler & Build Settings
# ==============================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(ENABLE_PROFILING "Enable Tracy Profiler" OFF)

# ==============================
#  Source Files
# ==============================
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

add_executable(tactix ${PROJECT_SOURCES})

target_include_directories(tactix PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# ==============================
#  Third-Party Libraries
# ==============================

# --- SDL2 (system install) ---
find_package(SDL2 REQUIRED)
target_link_libraries(tactix PRIVATE SDL2::SDL2)

# --- OpenGL ---
find_package(OpenGL REQUIRED)

# Link OpenGL to your executable
target_link_libraries(tactix PRIVATE OpenGL::GL)

# --- ImGui ---
add_library(ImGui STATIC
    third_party/ImGui/imgui.cpp
    third_party/ImGui/imgui_draw.cpp
    third_party/ImGui/imgui_tables.cpp
    third_party/ImGui/imgui_widgets.cpp
    third_party/ImGui/backends/imgui_impl_sdl2.cpp
    third_party/ImGui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(ImGui PUBLIC
    third_party/ImGui
    third_party/ImGui/backends
)

# Link ImGui into main binary
target_link_libraries(ImGui PUBLIC SDL2::SDL2)
target_link_libraries(tactix PRIVATE ImGui)

# --- spdlog ---
add_subdirectory(third_party/spdlog)
target_link_libraries(tactix PRIVATE spdlog::spdlog)

# ==============================
#  Platform Definitions
# ==============================
if(APPLE)
    target_compile_definitions(tactix PRIVATE PLATFORM_MACOS)
elseif(WIN32)
    target_compile_definitions(tactix PRIVATE PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(tactix PRIVATE PLATFORM_LINUX)
endif()

# ==============================
#  Compiler Warnings
# ==============================
if(MSVC)
    target_compile_options(tactix PRIVATE /W4 /permissive-)
else()
    target_compile_options(tactix PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ==============================
# Optional: Tracy Profiler
# ==============================
option(ENABLE_PROFILING "Enable Tracy Profiler" OFF)

if(ENABLE_PROFILING)
    message(STATUS "Profiling enabled: Adding Tracy")
    add_subdirectory(third_party/tracy)
    target_link_libraries(tactix PRIVATE TracyClient)
    target_compile_definitions(tactix PRIVATE ENABLE_PROFILING)
endif()

