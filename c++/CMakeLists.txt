cmake_minimum_required(VERSION 3.20)
project(tactix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Makefiles, Ninja) otherwise default to an
# unoptimised, un-flagged build, silently producing meaningless benchmark
# numbers when configured with a bare `cmake -B build`. Multi-config
# generators (Visual Studio, Xcode) select config at build time via
# `--config` instead and have no single CMAKE_BUILD_TYPE, and must be left
# alone -- forcing CMAKE_BUILD_TYPE there doesn't affect the actual build,
# but is still the wrong thing to set. GENERATOR_IS_MULTI_CONFIG is only
# reliably populated once project()/enable_language() has run, so this check
# has to come after project(), unlike CMAKE_CXX_STANDARD etc above.
get_property(TACTIX_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE AND NOT TACTIX_IS_MULTI_CONFIG)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Determinism requires a 64-bit target: 32-bit x86 evaluates float
# expressions at 80-bit x87 precision and rounds unpredictably on spill.
if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "Tactix requires a 64-bit target for reproducible floating point.")
endif()

include(FetchContent)

# --- Dependencies (all pinned; floating tags make builds irreproducible) ---
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking)
FetchContent_MakeAvailable(imgui)

# rlImGui has no release tags; pinned to a commit SHA instead of a floating
# branch name. NOT the default-branch HEAD (db823914...): HEAD requires
# ImGui's post-1.92 texture rewrite (ImTextureData / ImGuiPlatformIO::Textures)
# which does not exist in imgui v1.91.5-docking above, and fails to compile
# against it. Pinned instead to the last rlImGui commit before that rewrite
# landed (parent of dc7479b "update to ImGui 1.92.1 and new font system").
FetchContent_Declare(rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        ceceffd5e8148d4781f5f58cffe1641cf50f8f68)
FetchContent_MakeAvailable(rlImGui)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1)
FetchContent_MakeAvailable(spdlog)

# v2.4.11's CMakeLists.txt requires only CMake 3.0, which CMake 4.x refuses
# to configure (minimum supported policy is 3.5). Pinned to v2.5.3 instead,
# the newest tag, whose CMakeLists.txt requires CMake 3.14.
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3)
FetchContent_MakeAvailable(doctest)

# --- Floating-point environment ---
# Carried by every target that compiles simulation code. Disabling fast-math
# is NOT sufficient on its own: FMA contraction is on by default on GCC/Clang
# and silently changes rounding.
add_library(tactix_fp_flags INTERFACE)
if(MSVC)
    target_compile_options(tactix_fp_flags INTERFACE /fp:precise)
else()
    target_compile_options(tactix_fp_flags INTERFACE -ffp-contract=off)
endif()

# --- Warnings for first-party code only ---
# Deliberately no -Werror: pre-existing warnings must not break the build.
# Not applied to vendored imgui/rlImGui sources (see below), to keep the
# signal limited to code this project actually owns.
add_library(tactix_warnings INTERFACE)
if(MSVC)
    target_compile_options(tactix_warnings INTERFACE /W4)
else()
    target_compile_options(tactix_warnings INTERFACE -Wall -Wextra)
endif()

# --- Simulation core: MUST NOT link raylib ---
# Explicit source list, not GLOB: GLOB does not re-run reliably on file
# addition and silently produces stale builds.
add_library(tactix_sim STATIC
    src/Simulation.cpp
    src/SpatialHash.cpp
    src/JobSystem.cpp
    src/Squads.cpp
    src/Soldiers.cpp
)
target_include_directories(tactix_sim PUBLIC src)
target_link_libraries(tactix_sim PUBLIC tactix_fp_flags spdlog::spdlog_header_only)
target_link_libraries(tactix_sim PRIVATE tactix_warnings)

# --- Headless benchmark harness: MUST NOT link raylib ---
add_executable(tactix_bench bench/main.cpp)
target_include_directories(tactix_bench PRIVATE bench)
target_link_libraries(tactix_bench PRIVATE tactix_sim tactix_warnings)

# --- GUI application ---
add_executable(tactix
    src/main.cpp
    src/Renderer.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(tactix PRIVATE ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(tactix PRIVATE tactix_sim raylib)
# Warnings only on first-party sources of this target; the vendored
# imgui/rlImGui sources above are compiled without them.
set_source_files_properties(src/main.cpp src/Renderer.cpp PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>")

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# --- Tests ---
enable_testing()
add_executable(tactix_tests
    tests/main.cpp
    tests/test_spatialhash.cpp
    tests/test_rng.cpp
    tests/test_detmath.cpp
    tests/test_determinism.cpp
    tests/test_counters.cpp
    tests/test_units.cpp
    tests/test_formation.cpp
    tests/test_squads.cpp
    tests/test_deployment.cpp
    tests/test_steering.cpp
    tests/test_advance.cpp
)
target_link_libraries(tactix_tests PRIVATE tactix_sim doctest::doctest tactix_warnings)
target_compile_definitions(tactix_tests PRIVATE
    TACTIX_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/baseline")
add_test(NAME unit COMMAND tactix_tests)
