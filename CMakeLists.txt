cmake_minimum_required(VERSION 3.20)
project(uo_client CXX C)   # C enabled for the quickjs-ng dependency

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# --- quickjs-ng (JS scripting runtime) ---------------------------------------
# Pulled via FetchContent at a pinned tag; bump QUICKJS_NG_TAG to update.
# Declared BEFORE our global strict flags (below) so quickjs builds with its
# own default flags — it's C, and linking C objects into our /EHs-c- /GR- C++
# exe is ABI-safe. Examples/CLI/tests are disabled to keep the build lean.
include(FetchContent)
# Plain (non-cache) var so every build dir uses the same version — a CACHE var
# goes stale per-build-dir when bumped. Edit this line to update; tags:
# git ls-remote --tags https://github.com/quickjs-ng/quickjs.git
set(QUICKJS_NG_TAG "v0.15.0")
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI       OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(BUILD_QJS_LIBC      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
    GIT_TAG        ${QUICKJS_NG_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(quickjs)

if(MSVC)
    add_compile_options(/W4 /EHs-c- /GR- /utf-8 /permissive-)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
    )
else()
    add_compile_options(-Wall -Wextra -fno-exceptions -fno-rtti -Ofast)
endif()

# Sources are added incrementally as M1 phases land.
set(UO_CLIENT_SOURCES
    src/main.cpp
    src/Client.cpp
    src/Logger.cpp
    src/js/JsEngine.cpp
    src/js/ClientBindings.cpp
    src/client/ClientRender.cpp
    src/net/Socket.cpp
    src/net/PacketStream.cpp
    src/net/Huffman.cpp
    src/builders/Builders.cpp
    src/bot/Pathfinding.cpp
    src/bot/Scenario.cpp
    src/bot/Blacklist.cpp
    src/navigation/PathPlanner.cpp
    src/navigation/Navigation.cpp
    src/render/Renderer.cpp
    src/render/Text.cpp
    src/render/Minimap.cpp
    src/render/RadarColors.cpp
    src/render/MiniFBWindow.cpp
)

add_executable(uo_client ${UO_CLIENT_SOURCES})
target_include_directories(uo_client PRIVATE include src ${quickjs_SOURCE_DIR})
target_link_libraries(uo_client PRIVATE uo_mul qjs)

if(WIN32)
    target_link_libraries(uo_client PRIVATE ws2_32 user32 gdi32)
endif()

# --- MUL static library (M2) -------------------------------------------
set(UO_MUL_SOURCES
    src/mul/File.cpp
    src/mul/TileDataLoader.cpp
    src/mul/Map.cpp
    src/mul/Verdata.cpp
    src/mul/World.cpp
    src/mul/ArtLoader.cpp
    src/mul/TexmapLoader.cpp
    src/mul/AnimLoader.cpp
    src/mul/AnimDataLoader.cpp
    src/mul/AnimInfoLoader.cpp
    src/mul/HuesLoader.cpp
)
add_library(uo_mul STATIC ${UO_MUL_SOURCES})
target_include_directories(uo_mul PUBLIC include src)

# --- uo_mul_dump CLI ---------------------------------------------------
add_executable(uo_mul_dump src/mul/dump_main.cpp)
target_link_libraries(uo_mul_dump PRIVATE uo_mul)

enable_testing()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
