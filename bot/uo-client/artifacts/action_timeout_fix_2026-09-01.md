# Action timeout fix — wave2 cluster 6 (2026-09-01)

Source: artifacts/wave2_behaviour_triage_2026-09-01.md cluster 6.
Logs: bot/uo-client/run_gates/g_<Char>.console.txt/.err.txt (18:08-18:14),
runtime/logs/sphere2026-09-01.log. Sliced with tools/log_slice.py only.

## Root cause 1: unrecognised spell-cast refusal text (cast_spell)

Illyria cast id=6 (Night Sight, self) at 18:09:11.302, mana constant 48
across all 26 attempts. g_Illyria.console.txt:86, 1ms after the request:
  System: You lack Sulfurous Ash for this spell
Selene cast id=2 (Create Food) at 18:09:07.805: g_Selene.console.txt:91
  System: You lack Mandrake Root for this spell
Sphere source: core/messages.scp:889 spell_try_nomana "You lack sufficient
mana for this spell", :891 spell_try_noregs "You lack %s for this spell".

Client::ActionOnSysMessage's refusal keyword list only matched "not enough
mana"/"lack the mana"/"more reagents"/"fizzle" -- none of which appear in
Sphere's actual text -- so the message was never classified and the action
sat pending for the full 12s cast deadline every attempt.

## Root cause 2: same shape, vendor rate limit (vendor_sell)

Dorvar vendor_sell at 18:10:42.930: g_Dorvar.console.txt:455, immediately
  System: You are selling too fast.
Sphere source: core/messages.scp:759-760 npc_vendor_buyfast/sellfast.
Not matched by any existing keyword -> 8037ms wait to the 8s vendor deadline
before the (successful) retry.

## Root cause 3: craft menu (0x7C) never confirmed use_object

Thalia g_Thalia.console.txt:598-606: use_object start 18:11:55.823, dialog
0x7C arrives 18:11:55.826 (3ms), use_object timeout logged 18:11:59.859
(4036ms, = kUseTimeoutMs), dialog answered 18:11:59.923 -- the runner's own
ActionBusy() gate (src/life/Runner.cpp:9293) blocks reading CraftMenuOpen()
(:9326) until the action clears, so a dialog that arrived in 3ms could not
be acted on for ~4s. Repeats identically for every craft step (x15 for
Thalia, x6 for Elara). Client::OnOpenDialog (0x7C handler) never called
FinishAction, unlike the analogous 0x24 container-open and 0x6C
target-cursor paths.

## Root cause 4: scroll-into-spellbook consumption never confirmed
(move_item)

Elara g_Elara.console.txt:329-339: drop scroll 0x4001694E into spellbook
0x40010EB3 at 18:09:49.658 ("adding scroll 0x1F2F to the book, 18 spells so
far"), move_item timeout at 18:09:53.670, book reopened at 18:09:53.685
shows 19 items (up from 18). Repeats at :429/:443 (19->20) and identically
in g_Thalia.console.txt:252-262 (16->17). The server absorbed the scroll
correctly every time; Client::ActionOnItemInContainer waits for a 0x25
naming the same serial into the destination container, which a
spell-learned-and-scroll-consumed interaction does not produce.

## Fixes (bot/uo-client)

- include/uo/actions.h: added `act::ContainsCI`, `act::IsSpellCastRefusal`,
  `act::IsVendorRateLimited` -- pure, unit-testable text classifiers for
  Sphere's actual refusal wording.
- src/Client.cpp `ActionOnSysMessage`: calls the two new classifiers instead
  of the old narrower inline keyword list.
- src/Client.cpp `OnOpenDialog` (0x7C) + new `ActionOnMenuOpened()`
  (declared in src/Client.h): a menu dialog arriving while a UseObject
  action is outstanding now finishes it Success, mirroring the existing
  0x24/0x6C confirmation paths.
- src/Client.cpp `ActionOnObjectDeleted` (0x1D): now also finishes
  MoveItem/UseItemOn when the deleted serial is the item being moved
  (consumed by its destination), not just CastSpell-from-scroll.

## Not fixed / open

- Hector's use_object (health potion, not bandage per the raw log)
  "superseded by attack" at 18:10:41.273 is legitimate: HP had already
  recovered to 32/32 by 18:10:41.227, 46ms before the supersede, so the
  SURVIVE->fight transition issuing an attack is correct priority, not the
  attack loop stomping a heal in flight. The underlying potion-drink action
  itself has no consumption-based confirmation (no 0x1D handling for plain
  UseObject) and would otherwise have also run to its own timeout --
  flagged, not fixed, no direct packet evidence in these logs to back a
  change with the same confidence as fixes 1-4.
