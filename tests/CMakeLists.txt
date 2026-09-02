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
    # S4: Market.cpp asks life::DecideAcquire whether a life would buy and
    # wear an offered piece, rather than keeping a second copy of that sum.
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp
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

# Wave-1 fix 2: the trip-veto arithmetic (uo::life::EstimateTripTimeMs /
# TripFitsSessionBudget, include/uo/life.h). Pure -- header-only, no Client,
# no server, no world data -- so a single-file test links it directly.
add_executable(trip_budget trip_budget.cpp)
target_include_directories(trip_budget PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME trip_budget COMMAND trip_budget)

# wave 2, 2026-09-01, cluster 2: the ring search that salvages an unwalkable
# literal goal (uo::nav::FindWalkableNearGoal, include/uo/nav_rules.h).
# Pure -- header-only template, no Client, no server, no world data.
add_executable(goal_snap goal_snap.cpp)
target_include_directories(goal_snap PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME goal_snap COMMAND goal_snap)

# --- M5.1: the profession catalogue ----------------------------------------
# Checks every entry against BOTH rulesets -- Revolution's creation rule
# (two skills at 50.0, 50 stat points) and the finished-build caps (700/225).
# A bad table entry fails here rather than on a live shard. Pure data.
# Identity.cpp comes along because M5 also owns the profession -> BuildPlan
# translation and the "what should I pay a trainer for" choice; both are pure
# functions over the catalogue, so they belong in the same no-server test.
add_executable(m5_professions m5_professions.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Professions.cpp
    # The catalogue now names a combat strategy per profession, so the
    # strategy's own names come along -- the test checks that how a life
    # fights agrees with what the rest of its record says it is.
    ${CMAKE_SOURCE_DIR}/src/life/strategies/CombatStrategy.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Identity.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp
    # ChooseCraft asks the recipe graph what a thing is made of and the faucet
    # registry whether it may be sold, so both come with Identity now.
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Market.cpp
    # S4: Market.cpp asks life::DecideAcquire whether a life would buy and
    # wear an offered piece, rather than keeping a second copy of that sum.
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp
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
    # S4: Market.cpp asks life::DecideAcquire whether a life would buy and
    # wear an offered piece, rather than keeping a second copy of that sum.
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp
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
    ${CMAKE_SOURCE_DIR}/src/life/interaction/Progress.cpp
    # S3: the craft and train halves of section 18's table, extracted out of
    # Runner.cpp so the very rule the live bot applies is the one under test.
    ${CMAKE_SOURCE_DIR}/src/life/activities/CraftConfirm.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/TrainConfirm.cpp)
target_include_directories(interaction_progress PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME interaction_progress COMMAND interaction_progress)

# --- S6: cliloc message decode (0xC1 / 0xCC) --------------------------------
#
# docs/S3_CHARACTERIZATION.md's "shard messages" section: 0xC1 used to be
# discarded as a no-op at Client.cpp:569, so the crafting system's
# cliloc-numbered messages (crafting_messages.scp) never reached the bot.
# Links src/net/Cliloc.cpp (the client-free packet decode/format) and
# CraftConfirm.cpp (to confirm the decoded fallback text lands in
# CraftFailures()'s table) -- no Client, no server, no MULs.
add_executable(cliloc_decode cliloc_decode.cpp
    ${CMAKE_SOURCE_DIR}/src/net/Cliloc.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/CraftConfirm.cpp
    ${CMAKE_SOURCE_DIR}/src/life/interaction/Progress.cpp)
target_include_directories(cliloc_decode PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME cliloc_decode COMMAND cliloc_decode)

# --- The interaction layer: the handshake -----------------------------------
#
# docs/BOT_ARCHITECTURE.md section 17. One rule, broken five times in five
# files before it became a type: a retry issued inside the action's own
# deadline cannot resolve, only supersede itself. Also that a definitive
# refusal ends the wait AT ONCE -- "You can't reach that." was read and
# discarded 311 times in one session.
add_executable(interaction_handshake interaction_handshake.cpp
    ${CMAKE_SOURCE_DIR}/src/life/interaction/Handshake.cpp)
target_include_directories(interaction_handshake PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME interaction_handshake COMMAND interaction_handshake)

# --- Activities: the arithmetic of buying -----------------------------------
#
# docs/BOT_ARCHITECTURE.md section 15. Every purchase bug this project has had
# lived in the sums, not the packets: a request phrased "buy one" that could
# not see six shields already in the pack, and a death reserve charged against
# working capital until a scribe refused three 3-gold scrolls all session.
#
# BuyPlan.cpp is deliberately a separate translation unit from Buy.cpp so the
# decision half links without Client and ctest can reach it.
add_executable(activity_buy activity_buy.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/BuyPlan.cpp)
target_include_directories(activity_buy PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_buy COMMAND activity_buy)

# --- Activities: acquiring and equipping ------------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 15 and 18. The six heater shields, as a
# test: each was bought because the hand slot was empty, and nothing asked
# whether STR 56 could wear a shield needing 90. AcquirePlan.cpp is split from
# Acquire.cpp so the decision links without Client.
add_executable(activity_acquire activity_acquire.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp)
target_include_directories(activity_acquire PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_acquire COMMAND activity_acquire)

# --- The interaction layer: the silent-NPC rotation -------------------------
#
# docs/BOT_ARCHITECTURE.md sections 16 and 47. "One silent NPC is not the
# whole trade" -- broken in BOTH directions here: Ysolde lost Meditation
# entirely because one trainer refused her, and the bank ask went to the same
# banker every tick forever because nothing recorded that it had ignored us.
add_executable(interaction_rotation interaction_rotation.cpp
    ${CMAKE_SOURCE_DIR}/src/life/interaction/NpcRotation.cpp)
target_include_directories(interaction_rotation PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME interaction_rotation COMMAND interaction_rotation)

# --- Activities: the arithmetic of crafting ---------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 18 and 19. The WAIT is covered by
# interaction_progress and interaction_handshake; this is the decision that
# precedes it -- how many are still wanted, and whether the only inputs left
# are the working reserve a smith needs to smith tomorrow.
add_executable(activity_craft activity_craft.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/CraftPlan.cpp)
target_include_directories(activity_craft PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_craft COMMAND activity_craft)

# --- Activities: choosing how to train --------------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 18 and 20. A guildmaster sells a shortcut
# to 30.0 and everything above that is earned by doing the thing. The ceiling
# is PER-NPC, which is the fact Ysolde's session turned into "the skill is
# unbuyable" when it only meant "not from him".
add_executable(activity_train activity_train.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/TrainPlan.cpp)
target_include_directories(activity_train PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_train COMMAND activity_train)

# --- Activities: gathering --------------------------------------------------
#
# docs/BOT_ARCHITECTURE.md section 22. One framework for chopping, mining and
# fishing. The first case is the worst bug this project has had: "every tree
# within 24 tiles is worked out" returned goal-COMPLETE, so the planner
# re-picked the same goal for the same character in the same clearing --
# forty completions with progress=0 in under two minutes.
add_executable(activity_gather activity_gather.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/GatherPlan.cpp)
target_include_directories(activity_gather PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_gather COMMAND activity_gather)

# --- Taming: names before judgement, pastures from the world save ----------
add_executable(activity_tame activity_tame.cpp)
target_include_directories(activity_tame PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_tame COMMAND activity_tame)

# --- Strategies: combat -----------------------------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 21 and 54. The ONE deliberate exception to
# "professions are data": a mage and a warrior in a fight are different
# algorithms, not the same behaviour with different numbers. Five strategies
# serve seventeen professions, and none of them knows what a miner is.
#
# Legality is not tested here -- combat/Targeting.cpp owns "may I hit this"
# and has 47 checks of its own. This is "the fight is on and lawful; what now".
add_executable(strategy_combat strategy_combat.cpp
    ${CMAKE_SOURCE_DIR}/src/life/strategies/CombatStrategy.cpp)
target_include_directories(strategy_combat PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME strategy_combat COMMAND strategy_combat)

# --- Activities: corpse recovery --------------------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 25 and 47. Death here is FULL LOOT, so the
# corpse run is a life's whole equipment -- which is exactly why it has to be
# able to say no. The handler these rules replace walked straight back with no
# health check, no danger memory and no attempt limit, which is the precise
# shape of a death loop. Written BEFORE the first death, because in 21
# recorded runs no bot has ever died and the path has never fired.
add_executable(activity_recovery activity_recovery.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/RecoveryPlan.cpp)
target_include_directories(activity_recovery PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_recovery COMMAND activity_recovery)

# --- Activities: healing ----------------------------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 3 and 47. Four ways to patch up and one
# decision. The case that matters most is Kaelen's deadlock -- hungry, so no
# regeneration; wounded, so under the bar; no bandages, no money -- which the
# old code could only describe as "idle". He spent 73% of a session in it
# while every other need reported BLOCKED and nothing could say why.
add_executable(activity_heal activity_heal.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/HealPlan.cpp)
target_include_directories(activity_heal PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_heal COMMAND activity_heal)

# --- Activities: rest, roam, and knowing when idling is a fault -------------
#
# docs/BOT_ARCHITECTURE.md sections 12, 32 and 47. Idling used to WIN -- 73%
# of one session's picks and up to 85% elsewhere -- because every real errand
# was blocked and the no-op was the only thing that scored. Three states
# looked identical in the logs and are not: explore (blocked for want of
# knowing), rest (a real reason to stand still), stagnant (a fault to report).
add_executable(activity_rest activity_rest.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/RestPlan.cpp)
target_include_directories(activity_rest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_rest COMMAND activity_rest)

# --- Activities: eating -----------------------------------------------------
#
# docs/BOT_ARCHITECTURE.md sections 3, 11 and 54. Sphere reports EIGHT hunger
# levels and the life layer collapsed them to two booleans, so "content at
# 12/15" and "about to take damage" looked alike. v1_Corwyn was CONTENT and
# still made three cross-town trips after a baker: below `hungry`, food is an
# errand of convenience, not a journey.
add_executable(activity_eat activity_eat.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/EatPlan.cpp)
target_include_directories(activity_eat PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_eat COMMAND activity_eat)

# How much of a thing a life keeps, which is a COUNT and never a flag.
# v4_Corwyn read a sell list holding two daggers and six heater shields, sold
# the daggers for 72 gold and left 366 gold of shields in his pack -- because
# a smith produces shields, so "do I need this graphic?" answered yes for the
# sixth as loudly as for the first.
add_executable(activity_disposal activity_disposal.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/DisposalPlan.cpp)
target_include_directories(activity_disposal PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME activity_disposal COMMAND activity_disposal)

# --- S1: hue-aware metal (docs/CRAFTER_RUN_2026_08_30.md #20) --------------
# (graphic, hue) -> defname against the shard's own COLOR_ORE table. Links
# the same trio as m37_economy: VendorPolicy.cpp calls into the production
# graph (prod::MissingInputs/FindRecipe) even though this suite only reaches
# the two ItemNameForGraphic* lookups. Pure data and logic -- no server, no
# MULs, no world data.
add_executable(hue_metal hue_metal.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(hue_metal PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME hue_metal COMMAND hue_metal)

# --- S5: traders go to the market (docs/S5_MARKET_TRIP_PLAN.md) ------------
# The market is an ATLAS PLACE ID, never a coordinate pair, so this suite has
# to load the real generated atlas -- the data dir arrives as argv[1], the same
# way m38_closure gets it. It also links the planner and the need model,
# because the other half of the slice is a goal that must rest after achieving
# nothing and a clock long enough for a 560 s round trip.
add_executable(m8_market_trip m8_market_trip.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Market.cpp
    # S4: Market.cpp asks life::DecideAcquire whether a life would buy and
    # wear an offered piece, rather than keeping a second copy of that sum.
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    # S4: Market.cpp's gear predicate calls DecideAcquire, so anything that
    # links Market.cpp now links the acquire decision with it.
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Professions.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Identity.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Needs.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Goals.cpp)
target_include_directories(m8_market_trip PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(m8_market_trip PRIVATE uo_world)

add_test(NAME m8_market_trip COMMAND m8_market_trip ${CMAKE_SOURCE_DIR}/data)

# --- Service-place SELECTION policy (docs/CRAFTER_RUN_2026_08_30.md defect 4)
# world_atlas::PickServicePlace ranks candidate places by real trip cost --
# the route planner's tiles and gates, not raw map distance -- so it needs
# uo_world (Atlas + NavGrid + RoutePlanner) and, for the regression half, the
# real generated atlas and navgrid; the data dir arrives as argv[1].
add_executable(m9_service_selection m9_service_selection.cpp)
target_include_directories(m9_service_selection PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(m9_service_selection PRIVATE uo_world)

add_test(NAME m9_service_selection COMMAND m9_service_selection ${CMAKE_SOURCE_DIR}/data)

# --- mine_advance: first-visit deeper-into-the-mine target (M-fix9) --------
# world_atlas::DeeperMiningPoint (src/world/MiningAdvance.cpp) is the pure
# geometry Client::DeeperMiningTarget / Runner::DoMine use so a newborn miner
# with no remembered vein walks past a picked-clean mine mouth instead of
# refusing at it forever (owner, 2026-08-31: "it is not going to mine deep").
# Section B needs the real generated atlas for Minoc Mine 1's actual RECTs;
# the data dir arrives as argv[1], same convention as m9_service_selection.
add_executable(mine_advance mine_advance.cpp)
target_include_directories(mine_advance PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mine_advance PRIVATE uo_world)

add_test(NAME mine_advance COMMAND mine_advance ${CMAKE_SOURCE_DIR}/data)

# --- hunt_ground: early-hunting-grounds resolver ---------------------------
#
# world_atlas::Atlas::NearestHuntingGround (memory: early-hunting-grounds.md,
# owner 2026-08-31) -- the pure, Client-free half of "hunting ground can be
# graveyards for early hunting". m25_world.cpp style, but against the real
# generated atlas so a regenerated atlas that drops Britain's or Yew's
# graveyard fails this suite; the data dir arrives as argv[1].
add_executable(hunt_ground hunt_ground.cpp)
target_include_directories(hunt_ground PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hunt_ground PRIVATE uo_world)

add_test(NAME hunt_ground COMMAND hunt_ground ${CMAKE_SOURCE_DIR}/data)

# --- S4: the gear half of the trade predicate ------------------------------
#
# docs/CRAFTER_RUN_2026_08_30.md section 1, slice S4. `ConsiderOffer` asked
# `Shortfall`, `Shortfall` reads `consumes` and `consumables`, and a SWORD is
# in neither -- so `Profession::wears` had never been consulted by the economy
# layer at all and a swordsman could not want a sword.
#
# The weapon itself is not assumed: every field asserted here is cited to
# runtime/scripts/crafting/interface/def_blacksmithing.scp (the shard's own
# smithing menu) and runtime/scripts/items/weapons/i_weapons.scp (the ITEMDEF).
# AcquirePlan.cpp comes along because the held/worn/reserve arithmetic is
# DecideAcquire's -- the same object the live bot runs, not a copy.
add_executable(trade_gear trade_gear.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Market.cpp
    ${CMAKE_SOURCE_DIR}/src/economy/Faucets.cpp
    # SchoolWeaponFor/WantsToHunt -- the school-weapon-mapping test below.
    # Pure and Client-free, the same reason AcquirePlan.cpp is split out.
    ${CMAKE_SOURCE_DIR}/src/life/Identity.cpp
    ${CMAKE_SOURCE_DIR}/src/life/activities/AcquirePlan.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Production.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/VendorPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Professions.cpp
    ${CMAKE_SOURCE_DIR}/src/progression/Rules.cpp)
target_include_directories(trade_gear PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME trade_gear COMMAND trade_gear)

# --- trade_verify: move-item verification + unicode speech resolution -----
#
# Two false-negative bugs found live in run_r4/pair_Tarath.console.txt and
# pair_Durnholde.console.txt (20:34:17-20:53:38): ActionOnItemInContainer
# (Client.cpp) reporting "server_failure ... item landed in a different
# container" for moves that actually succeeded -- a trade-window drop whose
# confirming 0x25 named the OTHER side's container, and a partial-stack move
# whose remainder echo used the SOURCE container -- and OnUnicodeMessage
# filing an empty speaker name it could have resolved through the world
# cache. Unlike every other suite in this file, this one needs the real
# Client -- its packet handlers are not seamed out from sockets -- so it
# links the WHOLE client (minus main.cpp) exactly the way uo_viewer does in
# the root CMakeLists.txt, and opens a loopback listener of its own so
# Client::Send() has somewhere real to write outbound packets (never
# inspected; only local state is asserted on).
set(TRADE_VERIFY_SOURCES ${UO_CLIENT_SOURCES})
list(REMOVE_ITEM TRADE_VERIFY_SOURCES src/main.cpp)
list(TRANSFORM TRADE_VERIFY_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)

add_executable(trade_verify trade_verify.cpp ${TRADE_VERIFY_SOURCES})
target_include_directories(trade_verify PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${quickjs_SOURCE_DIR})
target_link_libraries(trade_verify PRIVATE uo_mul uo_world qjs)
if(WIN32)
    target_link_libraries(trade_verify PRIVATE ws2_32 user32 gdi32)
endif()

add_test(NAME trade_verify COMMAND trade_verify)

# --- Creation-time appearance roll (docs/APPEARANCE_DESIGN.md layer 1) -----
# Pure function over an identity string: determinism (same id -> identical
# appearance), diversity (20 different ids differ in at least one field),
# and that every hue/style rolled stays inside the exact range Source-X's
# CChar::InitPlayer accepts for the 0x00 create-character packet, cited in
# src/life/Appearance.cpp. No server, no MULs, no world data.
add_executable(appearance appearance.cpp
    ${CMAKE_SOURCE_DIR}/src/life/Appearance.cpp)
target_include_directories(appearance PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME appearance COMMAND appearance)

# --- 0x22 move-ack policy (watchdog + late-ack classification) --------------
# Regression cover for the wave10 fleet run: a Source-X world-save stall froze
# every client for ~5.3s and a flat 5s ack deadline aborted 18 of 33 healthy
# paths, then logged the correct-but-late ack as "unsolicited". Header-only
# pure functions; no server, no MULs.
add_executable(move_ack move_ack.cpp)
target_include_directories(move_ack PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME move_ack COMMAND move_ack)

# --- Moongate dialog retention (0xB0 vs. the packets behind it) -------------
# Regression cover for wave 2026-09-02: Xerxes and Vorar stuck at the Magincia
# gate. The "log + ignore" packet cases in Client.cpp's dispatch had no break
# and fell through into `case 0xB0: OnGenericGump(...)`, whose first statement
# cleared the held gump -- so the gate's own sound/effect destroyed the
# destination dialog it arrives behind, and Sphere never sent another. Like
# trade_verify, this needs the real Client (its packet handlers are not seamed
# out from sockets), so it links the whole client minus main.cpp and opens a
# loopback listener purely so Client::Send() has somewhere to write.
set(MOONGATE_GUMP_SOURCES ${UO_CLIENT_SOURCES})
list(REMOVE_ITEM MOONGATE_GUMP_SOURCES src/main.cpp)
list(TRANSFORM MOONGATE_GUMP_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)

add_executable(moongate_gump moongate_gump.cpp ${MOONGATE_GUMP_SOURCES})
target_include_directories(moongate_gump PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${quickjs_SOURCE_DIR})
target_link_libraries(moongate_gump PRIVATE uo_mul uo_world qjs)
if(WIN32)
    target_link_libraries(moongate_gump PRIVATE ws2_32 user32 gdi32)
endif()

add_test(NAME moongate_gump COMMAND moongate_gump)
