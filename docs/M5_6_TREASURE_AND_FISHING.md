# M5.6: Treasure hunting (and where fishing stands)

Date: 2026-09-29. **STATUS: BUILT AND TESTED OFFLINE, NOT YET RUN AGAINST SPHERE.**

## Treasure hunting

Before this change there was no treasure code at all, and the treasure_hunter
archetype was marked "blocked until map, dig and chest handling are
implemented". Now:

1. **Need.** A treasure hunter, or any build planning Cartography of 40 or
   more, gets `NeedTreasure` when it carries a map (0.60) or has a dug chest
   waiting (1.0). The goal is `HUNT_TREASURE`, weight 135, in the Work family,
   with a 20-minute limit.
2. **Decode.** It double-clicks the map. The server runs the Cartography
   check and, on success, shows the map with **0x90** (map details) and
   **0x56** (pin), both newly handled by the client. The pin's pixel is
   converted to the world tile (`treasure::PinToWorld`).
3. **Travel** to the spot, using Recall when a runebook page lands near it
   (M5.5).
4. **Dig.** It double-clicks a shovel or pickaxe and answers the cursor with
   the pinned tile. Revolution: "a pickaxe digs, no Mining needed".
5. **Chest.** The dig is proven by a chest appearing in the world within 3
   tiles of the spot, never by a message.
6. **Open or pick.** It tries the lid; if the lid won't open it uses the
   lockpick on the chest, and it alternates, with caps so neither step loops
   alone.
7. **Guardians.** While anything hostile is near, the treasure step waits and
   the normal fight and survival logic runs.
8. **Loot.** It moves the chest's contents to the pack until the pack is 90%
   full.

It gives up instead of looping when:
* skill is below the level's requirement (Revolution: 40/60/80/100 for levels
  2–5, both Cartography and Lockpicking);
* the map fails to decode 5 times;
* 6 digs find nothing;
* it has no tool or no lockpick;
* it is below 70% health before digging.

A give-up blocks the need for 30 minutes.

Monsters' maps are already picked up, because the hunt looter takes
everything from its own kill.

**UNKNOWN (checked live, never assumed):**
* the map and chest graphics on this tree (broad lists are used);
* the server's decode and dig messages (not relied on);
* the guardians per level;
* map level, which is not read from the item. Level 0 means the server
  decides.

## Fishing

The fisher loop already existed and has run live (Dorvar FISH×36, Ithion
FISH×50 in the 2026-09-04 wave):
* it catches, cuts, cooks at a campfire, eats and sells to NPCs;
* it finds spots, travels to water, and backs off from failed arms.

It is left as it is. **S.O.S bottles do not exist in this runtime**
(PRODUCTION_CHAINS.md:536). Revolution had them (from fishing, and from
mining and lumberjacking after 18.05.2009, `era::SosFromGathering`). They
become possible once the server has them: a bottle opens into a map, and a
map is now huntable.

## Verified

* `treasure`: 21 checks. They cover:
  * pin → world, the far corner, bad input;
  * level requirements;
  * every step and every give-up;
  * guardians wait;
  * pack full.
* `life_world_harness`: 408 checks, 0 failures. The new end-to-end offline
  case:
  * a map in the pack produces the need;
  * the map is double-clicked;
  * real 0x90/0x56 packets give the spot;
  * the shovel is double-clicked;
  * the 0x6C cursor is answered with the pinned tile;
  * a real 0x1A chest is opened;
  * a real 0x25 gold stack is lifted;
  * with guardians near, nothing is sent.
* Full ctest under Wine: 58/59; the known missing TSV is the one failure.
