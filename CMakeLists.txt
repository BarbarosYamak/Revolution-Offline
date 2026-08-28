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
    # Pathfinding.cpp / Blacklist.cpp are compiled into uo_world (the
    # navgrid's edge pass runs the same A*), and uo_client links it --
    # listing them here as well compiles them twice into one binary.
    src/bot/Scenario.cpp
    src/navigation/PathPlanner.cpp
    src/navigation/Navigation.cpp
    src/travel/ClientTravel.cpp
    src/fleet/Fleet.cpp
    src/travel/RouteStyle.cpp
    src/travel/TravelMode.cpp
    src/progression/Economy.cpp
    src/progression/Production.cpp
    src/progression/Mounts.cpp
    src/progression/MountPolicy.cpp
    src/progression/Supplier.cpp
    src/progression/Pet.cpp
    src/progression/CombatPolicy.cpp
    src/progression/VendorPolicy.cpp
    src/progression/Training.cpp
    src/progression/Rules.cpp
    src/progression/ClientProgression.cpp
    src/progression/ClientTrade.cpp
    src/progression/Progression.cpp
    src/progression/Trade.cpp
    src/travel/Journey.cpp
    src/travel/PersonalKnowledge.cpp
    src/travel/WarMode.cpp
    # M4 -- the persistent-life layer. Protocol-free by construction (see
    # include/uo/life.h), so the same objects the live bot runs are the ones
    # m4_life exercises under ctest.
    src/life/Json.cpp
    src/life/Identity.cpp
    src/life/Memory.cpp
    src/life/Needs.cpp
    src/life/Goals.cpp
    src/life/State.cpp
    src/life/Professions.cpp
    src/life/Runner.cpp
    src/render/Renderer.cpp
    src/render/Text.cpp
    src/render/Minimap.cpp
    src/render/RadarColors.cpp
    src/render/MiniFBWindow.cpp
)

add_executable(uo_client ${UO_CLIENT_SOURCES})
target_include_directories(uo_client PRIVATE include src ${quickjs_SOURCE_DIR})
target_link_libraries(uo_client PRIVATE uo_mul uo_world qjs)

if(WIN32)
    target_link_libraries(uo_client PRIVATE ws2_32 user32 gdi32)
endif()

# --- uo_viewer: the observer client (replaces ClassicUO) -----------------
# Same session engine as uo_client -- it logs in through the very same
# uo::Client, so it is a real player on the wire and cannot be anything else --
# with a different front end: window always on, camera on the character, Esc
# logs out, and every client-data lookup routed through include/uo/safe_graphics.h
# so an out-of-era graphic (0x3EB4, the unicorn-that-is-a-ship-prow that killed
# ClassicUO) draws a placeholder instead of taking the process down.
# See docs/OBSERVER_CLIENT.md.
set(UO_VIEWER_SOURCES ${UO_CLIENT_SOURCES})
list(REMOVE_ITEM UO_VIEWER_SOURCES src/main.cpp)
list(APPEND UO_VIEWER_SOURCES src/viewer/ViewerMain.cpp)

add_executable(uo_viewer ${UO_VIEWER_SOURCES})
target_include_directories(uo_viewer PRIVATE include src ${quickjs_SOURCE_DIR})
target_link_libraries(uo_viewer PRIVATE uo_mul uo_world qjs)

if(WIN32)
    target_link_libraries(uo_viewer PRIVATE ws2_32 user32 gdi32)
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

# --- uo_world: semantic world model + whole-world routing (M2.5) --------
# Deliberately a separate library from the client: it has no sockets, no
# packets and no session state, so the unit tests link it directly and the
# atlas generator reuses it without dragging the client in.
set(UO_WORLD_SOURCES
    src/world/WorldModel.cpp
    src/world/Atlas.cpp
    src/world/NavGrid.cpp
    src/world/RoutePlanner.cpp
    src/world/SharedWorld.cpp
)
# NavGrid's edge pass runs the bot's own tile A*, so the world library carries
# the pathfinder: what the router believes is crossable is measured with the
# same code the walker uses.
list(APPEND UO_WORLD_SOURCES src/bot/Pathfinding.cpp src/bot/Blacklist.cpp)
add_library(uo_world STATIC ${UO_WORLD_SOURCES})
target_include_directories(uo_world PUBLIC include src)
target_link_libraries(uo_world PUBLIC uo_mul)

# --- uo_atlasgen: derives the atlas + navgrid from the shard's own data --
# Offline tool. See docs/M2_5_WORLD_NAVIGATION.md for the command line.
add_executable(uo_atlasgen src/world/AtlasGenMain.cpp)
target_link_libraries(uo_atlasgen PRIVATE uo_world)

# --- uo_econdump: exports the compiled economy graph as TSV (M3.8 Phase 9) ---
# The graph stays canonical; these files are a VIEW of it, and m38_exports
# fails if the committed view has drifted from the code.
add_executable(uo_econdump
    src/tools/EconDumpMain.cpp
    src/progression/Production.cpp
    src/progression/VendorPolicy.cpp
    src/progression/Mounts.cpp)
target_include_directories(uo_econdump PRIVATE include src)

# --- uo_mul_dump CLI ---------------------------------------------------
add_executable(uo_mul_dump src/mul/dump_main.cpp)
target_link_libraries(uo_mul_dump PRIVATE uo_mul)

enable_testing()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
