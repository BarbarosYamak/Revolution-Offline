# M5.10: Party hunting, done properly

Date: 2026-09-30. **STATUS: BUILT AND TESTED OFFLINE, NOT YET RUN AGAINST SPHERE.**

These are the seven gaps the owner listed after the review of party hunting,
and what each one does now. The pure decisions are in
`include/uo/party_hunt.h`; the runner code is in
`src/life/runner/PartyHunt.cpp`, `Social.cpp`, `Train.cpp` and `Survive.cpp`.

| Gap | Now |
|---|---|
| **No focus fire** | The leader calls its target aloud when it engages: **"hedef: \<monster\>"**. Members pick the called monster nearest the leader, or failing a call, whatever stands in the fighting leader's reach. That overrides their own nearest-monster pick. |
| **No healing while hunting** | Every 6 seconds a member checks its friends' health bars (it asks for them every 3 seconds). Melee members bandage the lowest friend within reach who is below 70%. A caster (Magery 40+, mana, Greater Heal in its book) casts Greater Heal on a friend below 60% up to 10 tiles away, before it attacks. |
| **Pairs only** | The group size is 2–5, from the leader's sociability. After the first companion joins, the leader waits up to a minute at the meeting place: it invites each character that said "I'll join your hunt.", one at a time through the real party cursor, and calls the hunt once more for latecomers. |
| **Graveyards only** | The invitation names the place: "Anyone for a **Despise** hunt? Meet here." The choice depends on skill and group size: Despise from 70.0 (a bigger group lowers the bar), Covetous from 90.0 with three or more. A joiner refuses a place far above its level. The leader takes the group to the dungeon's **first level** and patrols there. If the route fails, the group goes back to the graveyards. The graveyard wording is unchanged, so older characters still hear it. |
| **No loot sharing** | Kills are looted **in turn round the party roster**: the Nth kill belongs to member N mod the group size. Everyone counts the same kills because everyone hits the same monster. |
| **No role split** | Melee is the **tank**: it follows at 2 tiles and opens the fight. Archers are **strikers** and casters are **healers**: they keep 4 tiles back and hold their first blow until the leader is engaged, unless something is already attacking them. |
| **Who invites was a coin flip** | A hunter now asks for a hunt **two sessions in three**. The third session it spars if its kit allows, otherwise it trains. Whoever called a hunt leads it and sends the party invites; the serial tie-break stays for two-person activities. |

**UNKNOWN:**
* Revolution's dungeon spawns per level. Only the first level is ever the
  target, and the danger memory and the per-fight retreat still apply.
* Where the group gathers is still the town meeting spot, not the dungeon
  entrance.

## Verified

* `party_hunt`: 28 checks. They cover:
  * group size;
  * ground choice, joiner suitability, the old phrase byte for byte, dungeon
    calls heard as hunts by older code, first level only;
  * focus calls and picks;
  * healing by bandage and by spell;
  * loot turns;
  * roles and who opens;
  * two hunts in three for hunters;
  * no small-talk line reads as a target call.
* `life_world_harness`: 433 checks, 0 failures. With a real 0xBF party
  roster:
  * the leader's spoken "hedef: a skeleton" makes the member choose the
    skeleton over a nearer zombie;
  * a real 0xA1 health bar at 45% gets the leader bandaged with a real
    double-click;
  * the first kill is the leader's to loot and the second is ours.

  All earlier social and sparring checks still pass.
* Full ctest under Wine: 62/63; the known missing TSV is the one failure.
  The Python suites pass.
