cmake_minimum_required(VERSION 3.20)
project(uo_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
    add_compile_options(/W4 /EHs-c- /GR- /utf-8 /permissive-)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
    )
else()
    add_compile_options(-Wall -Wextra -fno-exceptions -fno-rtti)
endif()

# Sources are added incrementally as M1 phases land.
set(UO_CLIENT_SOURCES
    src/main.cpp
    src/Client.cpp
    src/Logger.cpp
    src/net/Socket.cpp
    src/net/PacketStream.cpp
    src/net/Huffman.cpp
    src/builders/Builders.cpp
    src/bot/Pathfinding.cpp
    src/bot/Blacklist.cpp
    src/render/Renderer.cpp
    src/render/MiniFBWindow.cpp
)

add_executable(uo_client ${UO_CLIENT_SOURCES})
target_include_directories(uo_client PRIVATE include src)
target_link_libraries(uo_client PRIVATE uo_mul)

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
