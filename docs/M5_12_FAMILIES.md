# M5.12: Families (last names)

Date: 2026-09-30. **STATUS: BUILT AND TESTED OFFLINE. The server script has
not yet been loaded on Sphere.**

**Evidence:** owner testimony (the owner played RevolutionUO).
* A family deed (~50,000 gold) made its user the **family head**, who chose
  the **last name**.
* Each invitation cost a deed of ~10,000–20,000 gold, and the invited player
  carried **the same last name**.
* A family was social, shared a home or house, and stood together in PvP.

## Server (`runtime/scripts/revolution/revolution_family.scp`)

| Piece | What it does |
|---|---|
| `i_deed_family` (VALUE 50,000) | Double-click it and the server asks for a last name (0x9A prompt): 2–16 letters, unique across the shard. The user becomes head, and its NAME becomes "Name Lastname". The deed is used up. |
| `i_deed_family_invite` (VALUE 15,000) | Only the head can use it, by targeting a player within 8 tiles. That player gets a gump reading "Aile daveti" (inviter, last name, Accept / Decline). Accepting renames them and uses up the head's deed. |

**Reconstructed (UNKNOWN):**
* which NPC sold the deeds. Add `SELL=i_deed_family` and
  `SELL=i_deed_family_invite` to that vendor's template; GMs can `.add` them
  meanwhile;
* the name being appended to NAME, rather than shown as a title;
* the accept gump;
* uniqueness;
* leaving or disbanding a family, which is not built.

**UNVERIFIED syntax:** `PROMPTCONSOLE` and `STRREGEX` have not been loaded on
this tree. Resync and test each deed once as a GM.

## Bots (`include/uo/family.h`, `src/life/runner/Family.cpp`)

* **Founding.** A character with 75k+ gold, sociability 60+ and at least one
  trusted friend (trust ≥ 3):
  1. takes coin from the bank;
  2. buys a family deed (it asks a banker, then a provisioner, until the real
     seller is known);
  3. uses it, and answers the prompt with a Turkish last name from its own
     list (Yilmaz, Kaya, Demir, …), moving down the list if a name is refused;
  4. counts as head only once **its own name** carries the last name.
* **Inviting.** The head buys an invitation deed when a trusted friend stands
  within 8 tiles and uses it on them.
* **Accepting.** A character answers the server's "Aile daveti" gump: accept
  for a friend with trust ≥ 2, decline for strangers and foes. It counts as a
  member once its own name changes.
* **Being family.**
  * Anyone whose name ends with our last name is trusted (trust 5) and
    remembered as a member.
  * Family members are **allies in PvP**: never victims, and counted as
    friends.
  * They are first picks for parties, through that trust.
  * They get family greetings ("selam kuzen", "naber kardesim").
* **Shared home.** When a new member appears, the head says "aile evi:
  \<town\>" and members move their home town there. The head's house, if
  any, is in that town.
* The deed errands match by **name**, since every deed shares graphic 0x14F0.
  The house errand now asks only for "house" deeds.
* The family name is shown in `status.json` (`last_name`, `family_head`) and
  on the dashboard.

## Verified

* `family`: 14 checks. They cover:
  * the surname list;
  * a deterministic choice, and a different name after a refusal;
  * who founds, who invites, who accepts;
  * last-name recognition;
  * the gump;
  * the home call.
* `life_world_harness`: 450 checks, 0 failures. With real packets:
  * a friend's "Aile daveti" gump is accepted with button 1;
  * the character counts as a member only after the server renames it to
    "Ayse Yilmaz";
  * a stranger's invitation is declined with button 0;
  * a founder answers a real 0x9A prompt with a surname from the list.
