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

# M3.5 authenticity units: fleet connection admission (the IP-ban guard M3
# tripped live), bounded per-character route variation, and economic
# transformation knowledge. Pure logic; no server, no MULs, no world data.
add_executable(m35_authenticity m35_authenticity.cpp
    ${CMAKE_SOURCE_DIR}/src/fleet/Fleet.cpp
    ${CMAKE_SOURCE_DIR}/src/travel/RouteStyle.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Economy.cpp)
target_include_directories(m35_authenticity PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m35_authenticity COMMAND m35_authenticity)

# M3.6: the navigation recovery lifecycle (the M3.5 orphaned-recovery bug), the
# historical Magery training bands, and the Revolution rules profile. Links
# Journey for the recovery tests; still no server, no MULs, no atlas.
add_executable(m36_progression m36_progression.cpp
    ${CMAKE_SOURCE_DIR}/src/travel/Journey.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Training.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp
    ${CMAKE_SOURCE_DIR}/src/travel/TravelMode.cpp)
target_include_directories(m36_progression PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(m36_progression PRIVATE uo_world)

add_test(NAME m36_progression COMMAND m36_progression)

# Observer client (uo_viewer) crash-proofing. Drives the whole 32-bit input
# domain through include/uo/safe_graphics.h -- the layer every tiledata / art /
# hue / anim lookup in the viewer goes through -- and asserts that the graphics
# that killed ClassicUO on this shard (0x3EB4 and friends) resolve to a
# placeholder instead of an out-of-range index. Runs with NO client data (the
# unbound/null-table case); if UO_MUL_DIR is set it additionally sweeps the real
# MULs. Links uo_mul for the loaders.
add_executable(viewer_safety viewer_safety.cpp)
target_include_directories(viewer_safety PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(viewer_safety PRIVATE uo_mul)

add_test(NAME viewer_safety COMMAND viewer_safety)

# M3.7: the Revolution production graph, the NPC vendor authenticity policy and
# the acquisition primitives. Pure data and logic -- no server, no MULs, no
# world data, no generated atlas.
add_executable(m37_economy m37_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(m37_economy PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m37_economy COMMAND m37_economy)

# --- M3.8 pre-M4 closure: mount rules + export drift -----------------------
# The data dir is passed so the export test can compare the committed TSVs
# against a fresh in-memory regeneration, wherever CTest runs from.
add_executable(m38_closure m38_closure.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Mounts.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/MountPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Supplier.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Pet.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(m38_closure PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m38_closure COMMAND m38_closure ${CMAKE_SOURCE_DIR}/data)
