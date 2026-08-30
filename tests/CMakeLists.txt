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
    ${CMAKE_SOURCE_DIR}/src/progression/CombatPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(m38_closure PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m38_closure COMMAND m38_closure ${CMAKE_SOURCE_DIR}/data)

# --- M4 Slice 1: identity, persistence, needs, utility goals ---------------
# Pure logic and one temp directory. No server, no MULs, no world data; the
# store test writes under the build tree so a run leaves nothing behind in the
# working copy.
add_executable(m4_life m4_life.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Json.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Professions.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Market.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Identity.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Needs.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Goals.cpp
    ${CMAKE_SOURCE_DIR}/src/life/State.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(m4_life PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m4_life COMMAND m4_life ${CMAKE_CURRENT_BINARY_DIR})

# --- M4 economy invariant: the anti-arbitrage ratchet ----------------------
# Asserts on the committed output of tools/economy_arbitrage.py, which
# recomputes the NPC craft loops from the shard's own itemdefs. The profitable
# count may only go DOWN; a rise fails loudly with the offending loop named.
# Pure data plus arithmetic -- no server, no MULs, no world data.
add_executable(m4_economy_invariant m4_economy_invariant.cpp)
target_include_directories(m4_economy_invariant PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m4_economy_invariant COMMAND m4_economy_invariant
         ${CMAKE_SOURCE_DIR}/../../docs/tns_exports/economy_arbitrage_loops.tsv)

# --- M5.1: the profession catalogue ----------------------------------------
# Checks every entry against BOTH rulesets -- Revolution's creation rule
# (two skills at 50.0, 50 stat points) and the finished-build caps (700/225).
# A bad table entry fails here rather than on a live shard. Pure data.
# Identity.cpp comes along because M5 also owns the profession -> BuildPlan
# translation and the "what should I pay a trainer for" choice; both are pure
# functions over the catalogue, so they belong in the same no-server test.
add_executable(m5_professions m5_professions.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Professions.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Identity.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp
    # ChooseCraft asks the recipe graph what a thing is made of and the faucet
    # registry whether it may be sold, so both come with Identity now.
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Market.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp)
target_include_directories(m5_professions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m5_professions COMMAND m5_professions)

# --- M6.1: legal target classification -------------------------------------
# Pure logic over what a player can see on screen. Every rule it asserts is
# either a line of this shard's sphere.ini or a line of Source-X, and the
# citation is in the check name -- including the one that killed a character
# in M3.9 (GuardsInstantKill=1 + a swing at a blue).
add_executable(m6_targeting m6_targeting.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/Targeting.cpp)
target_include_directories(m6_targeting PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m6_targeting COMMAND m6_targeting)

# --- M7.1: producer/consumer, observed prices, gold ledger ------------------
# The rule this suite exists for is the easy one to break by accident: a bot
# may not know the market. A character with no observation of a price must
# answer "I do not know", not a plausible number.
# Production.cpp comes along because the anti-arbitrage guard asks the real
# production graph what an item's RAW inputs are -- there is no second, softer
# copy of the recipe list for the economy layer to consult.
add_executable(m7_market m7_market.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Market.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Professions.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(m7_market PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m7_market COMMAND m7_market)

# --- M7: the Gold Faucet Registry ------------------------------------------
# The rule: technical vendor acceptance is not Revolution authenticity. Stock
# Scripts-X buys hundreds of player-crafted goods, and this suite checks the
# registry keeps that apart from what the shard actually intended -- including
# that BLOCKED, UNKNOWN and REFUSED stay three distinct answers.
add_executable(m7_faucets m7_faucets.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    # The fish-visibility regression needs the graphic tables themselves: the
    # defect was a gap between a faucet row and the graphic it is reachable by.
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp)
target_include_directories(m7_faucets PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME m7_faucets COMMAND m7_faucets)

# --- The interaction layer: progress verification ---------------------------
#
# docs/BOT_ARCHITECTURE.md section 18. "sent packet = success" is the rule this
# suite exists to make impossible, and every case in it is a shape this shard
# actually produced: the order Sphere refused wholesale while the bot recorded
# eight buys, the trainer fee with no skill gain (`training_unverified`), the
# gear that never reached the paperdoll so the next tick bought another.
#
# Client-free by construction, exactly like the life and combat layers, so
# ctest exercises the very object the live bot runs rather than a copy of it.
add_executable(interaction_progress interaction_progress.cpp
    ${CMAKE_SOURCE_DIR}/src/life/interaction/Progress.cpp)
target_include_directories(interaction_progress PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME interaction_progress COMMAND interaction_progress)
