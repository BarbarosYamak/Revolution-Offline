# Deterministic protocol/state tests. Built with the normal build and run by
# ctest; they need no server, no MUL files and no network.
#
# The older *_probe.cpp files in this directory are manual diagnostic tools
# (they want game assets or a live shard) and are deliberately not wired up.

add_executable(sphere_regression sphere_regression.cpp
    ${CMAKE_SOURCE_DIR}/src/net/PacketStream.cpp
    ${CMAKE_SOURCE_DIR}/src/builders/Builders.cpp)
target_include_directories(sphere_regression PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME sphere_regression COMMAND sphere_regression)

# M2 action layer: targeting, action results, drag transactions, life state.
add_executable(m2_actions m2_actions.cpp)
target_include_directories(m2_actions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m2_actions COMMAND m2_actions)

# M2.5 world model, whole-world routing, journey state machine, war watchdog
# and per-character knowledge. Links uo_world and the travel state machines --
# the same objects the client links -- and needs no server, no MULs and no
# generated atlas: the world under test is synthesised inline.
add_executable(m25_world m25_world.cpp
    ${CMAKE_SOURCE_DIR}/src/travel/Journey.cpp
    ${CMAKE_SOURCE_DIR}/src/travel/PersonalKnowledge.cpp
    ${CMAKE_SOURCE_DIR}/src/travel/WarMode.cpp)
target_include_directories(m25_world PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(m25_world PRIVATE uo_world)

add_test(NAME m25_world COMMAND m25_world)

# path_probe: run the bot's own tile A* against the real MULs and visualise a
# window around a failure. A diagnostic, not a test -- it needs client data --
# so it is built but not registered with ctest. MULs come from UO_MUL_DIR.
add_executable(path_probe path_probe.cpp)
target_include_directories(path_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(path_probe PRIVATE uo_world)

# M3 progression, economy and secure-trade state machines. Pure logic: no
# server, no MULs, no world data.
add_executable(m3_progression m3_progression.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Progression.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Trade.cpp)
target_include_directories(m3_progression PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m3_progression COMMAND m3_progression)
