cmake_minimum_required(VERSION 3.20)
project(tactix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure C++20 is used for all targets
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")

include(FetchContent)

# -------------------------------------------------------
# 1. Fetch Raylib
# -------------------------------------------------------
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG master
)
FetchContent_MakeAvailable(raylib)

# -------------------------------------------------------
# 2. Fetch Dear ImGui (no CMakeLists, we add sources manually)
# -------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# -------------------------------------------------------
# 3. Fetch rlImGui (also no CMakeLists)
# -------------------------------------------------------
FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG main
)
FetchContent_MakeAvailable(rlImGui)

# -------------------------------------------------------
# 4. Fetch spdlog (use header-only mode to avoid compilation issues)
# -------------------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# -------------------------------------------------------
# Sources
# -------------------------------------------------------
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(tactix ${PROJECT_SOURCES})

# -------------------------------------------------------
# ImGui sources (required)
# -------------------------------------------------------
target_sources(tactix PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

# Optional ImGui backends (you can enable when needed)
# target_sources(tactix PRIVATE
#     ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
#     ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
# )

target_include_directories(tactix PRIVATE ${imgui_SOURCE_DIR})

# -------------------------------------------------------
# rlImGui sources
# -------------------------------------------------------
target_sources(tactix PRIVATE
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(tactix PRIVATE ${rlimgui_SOURCE_DIR})

# -------------------------------------------------------
# Linking
# -------------------------------------------------------
target_link_libraries(tactix PRIVATE
    raylib
    spdlog::spdlog_header_only
)

# -------------------------------------------------------
# Includes
# -------------------------------------------------------
target_include_directories(tactix PRIVATE
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

# -------------------------------------------------------
# Copy assets to build folder
# -------------------------------------------------------
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})
