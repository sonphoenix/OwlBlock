cmake_minimum_required(VERSION 3.20)
project(OwlBlock CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# raylib — using the prebuilt binaries already sitting in lib/, not fetched.
# raylib.h lives in include/vendor/, so no extra include path needed beyond
# the include/ directory added further down.
# ---------------------------------------------------------------------------
add_library(raylib STATIC IMPORTED)
set_target_properties(raylib PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/raylib.lib"
)
target_include_directories(raylib INTERFACE "${CMAKE_SOURCE_DIR}/include/vendor")
target_link_libraries(raylib INTERFACE winmm)

# ---------------------------------------------------------------------------
# Dear ImGui (docking branch) — no CMakeLists of its own, so we build it manually
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# ---------------------------------------------------------------------------
# rlImGui — bridges ImGui rendering to raylib
# ---------------------------------------------------------------------------
FetchContent_Declare(
    rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(rlimgui)

add_library(rlImGui STATIC
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(rlImGui PUBLIC ${rlimgui_SOURCE_DIR})
target_link_libraries(rlImGui PUBLIC raylib imgui)

# ---------------------------------------------------------------------------
# Owl Block sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE OWLBLOCK_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

add_executable(OwlBlock ${OWLBLOCK_SOURCES})

target_include_directories(OwlBlock PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/vendor"
)

target_link_libraries(OwlBlock PRIVATE
    raylib
    imgui
    rlImGui
)

# raylib.dll needs to sit next to the .exe at runtime since we're linking the
# static .lib (which itself wraps the DLL) — copy it after every build.
add_custom_command(TARGET OwlBlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/lib/raylib.dll" "$<TARGET_FILE_DIR:OwlBlock>"
)

# Copy bios/ and roms/ next to the built exe so relative paths in main.cpp still work
add_custom_command(TARGET OwlBlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/bios" "$<TARGET_FILE_DIR:OwlBlock>/bios"
)
add_custom_command(TARGET OwlBlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/roms" "$<TARGET_FILE_DIR:OwlBlock>/roms"
)

if (MSVC)
    target_compile_options(OwlBlock PRIVATE /W3)
endif()