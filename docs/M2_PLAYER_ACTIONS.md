# M2 — Real Player Action Primitives

Date: 2026-08-26. The headless client can now perform core Ultima Online player actions through the ordinary network protocol, and higher-level code drives them without knowing a single packet id.

## 1. Overview

M2 adds an **action layer** on top of the M1.5 client. The distinction it exists to enforce is *"the packet was sent"* versus *"the server did it"*: every primitive starts an asynchronous action, watches for the packets that constitute the server's answer, and reports a typed result. Scenarios branch on that result; they never assume success and they never sleep-and-hope.

Nothing here changes the M1.5 foundations: `Start`/`Tick`/`Finished`/`ExitCode`, process-scoped winsock, per-session logging, `SubmitStep()` as the sole `0x02` sender, multi-session isolation and the renderer/stdin restrictions are all untouched.

**Source-X modifications: 0.** Scripts-X modifications: 0. `sphere.ini` modifications: 0 (still exactly the four M0 lines).

## 2. Architecture

```
Sphere / Source-X
        ▲   UO protocol
network + packet parser         Client.cpp handlers, PacketStream, Huffman
        ▲
client world/session state      containers, equipment, mobiles, player stats
        ▲
action primitives          <-- M2: uo/actions.h + Client::Action*()
        ▲
Revolution mechanics       <-- future M3
        ▲
bot behaviour / professions
```

`include/uo/actions.h` holds the protocol-free core — the result model and the state machines — so it is unit-testable against the exact code the client runs:

| Type | Purpose |
|---|---|
| `act::Result` | `Pending / Success / Timeout / Rejected / InvalidState / Unavailable / ServerFailure` |
| `act::Kind` | which primitive is in flight, for logs and confirmation rules |
| `act::Action` | the single in-flight action: context, deadline, target generation |
| `act::TargetState` | per-session target cursor with a **generation counter** |
| `act::DragState` | the two halves of an item move (lift, drop) as one transaction |
| `act::LifeState` | alive/dead, derived only from what the server sends |

Everything is a `Client` member. No process-global mutable state was added; the M1.5 rule holds.

Movement is deliberately **not** an action — it keeps its own controller so a bot can walk while an action is outstanding.

## 3. Implemented primitives

Public API on `Client` (`src/Client.h`), all expressing intent rather than packets:

| Primitive | Sends | Server-confirmed by |
|---|---|---|
| `ActionUseObject(serial)` | `0x06` | `0x24` container open, or a `0x6C` cursor, or a system message |
| `ActionOpenContainer(serial)` | `0x06` | `0x24` for that serial, then `0x3C` contents |
| `ActionMoveItem(item, amount, dest)` | `0x07` + `0x08` | `0x25` item in the destination; `0x27` ⇒ `Rejected` |
| `ActionDropGround(item, amount, x,y,z)` | `0x07` + `0x08` | `0x1A` world item |
| `ActionEquip(item, layer)` | `0x07` + `0x13` | `0x2E` on our mobile at that layer |
| `ActionUnequip(item)` | `0x07` + `0x08` | `0x25` into the backpack |
| `ActionTargetObject / Ground / CancelTarget` | `0x6C` | generation-checked; single-shot |
| `ActionUseSkill(id, target)` | `0x12` ext `0x24` | the skill's own result line |
| `ActionCastSpell(id, target)` | `0x12` ext `0x56` | mana drop, or a refusal message |
| `ActionCastScroll(scroll, target)` | `0x06` + `0x6C` | the scroll being consumed (`0x1D`) |
| `ActionAttack(serial)` | `0x05` | `0xAA` echoing the accepted serial (0 ⇒ refused) |
| `ActionWarMode(on)` | `0x72` | — |
| `ActionUseBandage(item, target)` | `0x06` + `0x6C` | heal result, or a refusal |
| `ActionOpenBank(banker, phrase)` | `0x03` speech | the bank container the server opens |
| `ActionVendorOpen(vendor, phrase)` | `0x03` speech | the assembled `0x3C`+`0x74` offer |
| `ActionVendorBuy(vendor, item, qty)` | `0x3B` | purchased item delivered |
| `ActionGoto / ActionGotoMobile` | `0x02` via `SubmitStep` | arrival |
| `ActionSay(text)` | `0x03` | — |
| `ActionResurrectAccept()` | `0x2C` | body change back to a living body |

Scenario verbs mirror these one-for-one, plus `wait_action`, `expect <result>`, `wait_target`, `wait_dead`, `wait_alive`, `remember`, `require`.

## 4. Protocol findings (Sphere-specific)

Everything below was determined from the Source-X sources and then confirmed on the wire.

1. **Vendors do not open on double-click.** A double-click on a human NPC returns its paperdoll (`CClientEvent.cpp:2378`). The shop opens from **speech** ("buy"), which the SPEECH scripts turn into `NV_BUY` (`CCharNPCAct.cpp:147-160`). `ActionVendorOpen` therefore speaks.
2. **Banking is speech too.** `src.BANKSELF` in `speech/jobs/job_banker.scp:132-138`; the client just recognises the container. Bank gump id is **0x4A**, and the box is sent with graphic forced to `0x0E7C`.
3. **Our own death is `0x2C`, not `0xAF`.** `0xAF` goes to bystanders and explicitly excludes the dying client (`CCharAct.cpp:4446`); the ghost body switch sends no packet at all (`:4493-4494`).
4. **Replying to `0x2C` never resurrects.** Both choices take the same branch (`receive.cpp:616-639`). Real resurrection is a healer NPC acting on its own (`CCharNPCAct.cpp:895-937`) or a shrine double-click.
5. **A 2.0.7 client gets no drop acknowledgement.** `0x28` is never sent by Source-X and `0x29` is KR-only (`send.cpp:1035-1037`). Success is `0x25`/`0x1A`; failure is `0x27` with a reason code.
6. **A container must be open before its contents can be lifted** (`CCharAct.cpp:2856-2896`), and pickups are throttled to 333 ms.
7. **`0x74` carries no serials** — it is positionally joined to the preceding `0x3C`.
8. **Target cursor ids are `CLIMODE` values, echoed verbatim.** Observed live: `0x2CE` = `CLIMODE_TARG_USE_ITEM` (718), `0x2C9` = `CLIMODE_TARG_SKILL` (713), `0x2CA` = `CLIMODE_TARG_SKILL_MAGERY` (714). Only one cursor may be outstanding; Sphere silently discards a reply whose context does not match.
9. **A 2.0.7 client receives no damage packet at all** (`0x0B` needs 4.0.7a, `0xBF.0x22` needs 4.0.0). Health must be tracked from `0xA1`/`0x11`.
10. **Skill ids are Sphere's `SKILL_TYPE` enum, not the client's list order.** This was a live M1 bug: the test character was created with 31/33/17 believing that meant Swordsmanship/Tactics/Healing; it actually produced **Archery 50 / Stealing 30 / Healing 20**. Fixed in `build::CreateCharacterParams` (now 40/27/17) and documented.
11. **Healing cannot be started from the skill list.** `SKILL_HEALING` is absent from `Event_Skill_Use`'s switch and falls through to *"There is no such skill"*. Bandages must be double-clicked.

### Magery in detail

- **Initiation.** Four entry points exist; the client uses `0x12` ext `0x56` (cast by id) for `ActionCastSpell`, and `0x06` on a scroll for `ActionCastScroll`. **Speech casting does not exist on Sphere** — `m_sRunes` is only used server→client to *emit* the words.
- **Targeting sequence.** `MagicFlags` is unset in this shard, so **precast is OFF**: the target cursor (`0x6C`, context `0x2CA`) arrives **first**, and the cast runs after the target is answered. `SpellTimeout=0`, so the cursor never expires.
- **Spellbook.** Mandatory for a normal cast (`CCharSpell.cpp:2517-2529`); no ini flag disables it. A scroll bypasses it because the scroll is the caster.
- **Reagents.** `ReagentsRequired=0` in this shard's `sphere.ini`, so none are consumed.
- **Mana.** Deducted only in `Spell_CastDone`, which is why a mana drop is a reliable success signal.
- **Observed timing.** 1st-circle scroll cast: **550 ms** from target reply to scroll consumption.
- **Packets observed for one cast:** `0x6E` cast animation → `0x1C` words of power (`In Mani` for Heal, `Uus Wis` for Cunning) → `0x1D` scroll consumed → `0x70` effect → `0x54` sound ×2 → `0x11` status update.
- **Failure cases seen and classified:** *"This is beyond your ability."* ⇒ `ServerFailure` (no spellbook / insufficient skill).

## 5. Scenario tests

Small, independently runnable, in `bot/uo-client/scripts/scenarios/`:

| Scenario | Covers |
|---|---|
| `m2_inventory.txt` | container open, item identification, use, equip, unequip, ground drop, move |
| `m2_skill_magery.txt` | targeted skill, untargeted skill, cast refusal path |
| `m2_magery.txt` | full scroll cast with server confirmation |
| `m2_bank.txt` | walk to banker, speech, bank container |
| `m2_vendor.txt` | walk to vendor, offer parsing, purchase |
| `m2_combat_bandage.txt` | bandage target round-trip, attack initiation |
| `m2_death_resurrection.txt` | death by real combat, ghost state, resurrection |
| `m2_resurrection.txt` | ghost walks to a healer and is raised |
| `m2_vendor_sell.txt` | vendor trade identification, `0x9E` sell list, `0x9F` sale |
| `m2_pvp_wound_a.txt` / `_b.txt` | two sessions: real combat damage, then a real bandage heal |
| `m2_newchar_kit.txt` | create-character, then read what the server's newbie template gave |

## 6. Results

Build: **PASS**. CTest: **2/2 suites, ~100 checks, 0 failures** (`sphere_regression` 50 + `m2_actions`).

| Capability | Result | Evidence |
|---|---|---|
| Use object | **PASS** | `use_object success (15ms) server armed a target cursor` |
| Open container | **PASS** | `open_container success (0ms) container opened` + `0x3C` 8 items |
| Inventory move | **PASS** | `move_item success (8ms) item is in the destination` |
| Drop to ground | **PASS** | `drop_ground success (12ms) item is on the ground at the target` |
| Equip | **PASS** | `equip success` confirmed by `0x2E` on the requested layer |
| Unequip | **PASS** | `unequip success (0ms) item is in the destination` |
| Target object | **PASS** | cursor `0x2C9`/`0x2CA`/`0x2CE` answered with generation checks |
| Target cancel | **PASS** | cancel classified as `Rejected` for the waiting action |
| Skill (targeted) | **PASS** | Anatomy → *"RevolutionBot01 looks to be of normal strength and moderately dexterous."* (2837 ms) |
| Skill (untargeted) | **PASS** | Hiding → *"You have hidden yourself well"* / *"You can't seem to hide here."* both classified |
| Magery | **PASS** | `cast_spell success (550ms) scroll consumed by the cast`, words of power `In Mani` |
| Magery failure path | **PASS** | *"This is beyond your ability."* ⇒ `server_failure` |
| Speech | **PASS** | Sphere logs `'RevolutionBot01' Says '…'`; NPCs answer |
| Combat initiation | **PASS** | `0xAA attacking 0x000003DF` ⇒ `attack success (15ms) server accepted the target` |
| Bandage (actual heal) | **PASS** | hp **22 -> 30**, bandages **53 -> 52**; see §6.1 |
| Bank | **PASS** | `bank container=0x40001FFF gump=0x004A`, `open_bank success (17ms)` |
| Vendor buy | **PASS** | 37-item offer parsed; *"Here you are… That will be 1 gold coin."* |
| Vendor sell | **PASS** | gold **1000 -> 1005**, candle **1 -> 0**; see §6.1 |
| Death | **PASS** | killed by a wild grey wolf; see §6.1 |
| Multi-session isolation | **PASS** | two live sessions fought each other; no state crossed; see §6.1 |
| Resurrection | **PASS** | raised by a healer NPC; see §6.1 |
| Logout | **PASS** | `0xD1` → ack → clean close, every run |

Server rejects (`0x27` drag cancels, `0x21` move rejects): **0** across all M2 runs. Cross-session contamination: **0**.

That zero holds **after** movement switched to running. Bots now default to
`Gait::Auto`, which runs (see `bot-client.md`); the late M2 runs -- the two-session
bandage proof and the second resurrection -- were all run-gait and produced no
`0x21` reject, no resync and no drag cancel. Walking is now the exception the
path planner asks for deliberately (final approach, doorways, shoves), not the
baseline. A bot crossing Yew at 400 ms/tile reads as an NPC to anyone watching.

### 6.0 Regression, re-run end to end after the three gaps closed

Every scenario below was re-run live against Source-X on the final build, with
running as the default gait.

| Run | Result |
|---|---|
| `ctest` — `sphere_regression`, `m2_actions` | **2/2 suites, 0 failures** (23 new gait checks) |
| `m1_smoke` | login -> walk -> speech; ends on `hold` by design, so it idles rather than finishing |
| `m15_nav` | finished, 0 rejects |
| `m15_two_bots_a` + `_b` (concurrent) | both clean, 0 rejects, no state crossed |
| `m2_inventory` | finished, 5/5 `expect success` |
| `m2_bank` | finished |
| `m2_vendor` (buy) | finished |
| `m2_skill_magery` | finished |
| `m2_magery` | finished — `In Mani`, `cast_spell success (546ms) scroll consumed by the cast` |
| `m2_vendor_sell` | **PASS** (§6.1) |
| `m2_pvp_wound_a` + `_b` | **PASS** — the bandage heal (§6.1) |
| `m2_resurrection` | **PASS**, twice, on two different characters |

Two of these needed a fixture, not a fix. `m2_magery` binds `i_scroll_heal`
(`0x1F31`) and scrolls are **consumed by the cast**, so the original mage had
none left; a second mage was created the same ordinary way. `m2_inventory`
binds a dagger, which `RevolutionBot01` lost to its corpse. Neither is a client
regression, and both are worth knowing: a scenario that consumes what it binds
is only repeatable while the character is re-supplied.

One infrastructure note: Source-X flood protection (`MaxConnectRequestsPerIP`,
`NetTTL=300`) blocked 127.0.0.1 partway through, and **every retry re-arms the
300 s TTL**, so retrying makes it worse. Restarting the server clears it. Space
out reconnects when running scenarios in a batch.

### 6.1 The three capabilities closed after the first M2 pass

**Death — by a real creature, no assistance.** The character was left in the
wildlife clearing at (689,753) and a wild animal killed it. Sphere's own log:

```
01:36:P'RevolutionBot01' was killed by N'grey wolf'.
```

The client recognised the ghost from the body the server sent, with no local
state change and no guesswork:

```
[0x1B] serial=0x00000001 body=0x0192 pos=(689,753,0)
[STATE] dead (body 0x0192)
event state_dead: server body change
System: You are a ghost
System: Your ghostly hand passes through the object.
```

No GM kill, no HP write, no alive/dead flag set locally. `act::LifeState` is
derived only from the body graphic, which is why this worked without a single
line of special-case code.

**Resurrection — by a healer NPC.** Replying to `0x2C` does not raise anyone on
this server, so the ghost walked to the fixed Yew healer spawn (540,966) in
three A* legs and waited. The healer decided on its own:

```
[01:48:39] Martina: Thou art dead, but 'tis within my power to resurrect thee. Live!
[01:48:39] [STATE] alive (body 0x0190)
[01:48:39] event state_resurrected: server body change
```

The client stayed protocol-coherent across the transition — the very next
action succeeded: `open_container success (1ms) container opened`.

Timing note: the healer took **3m31s** to notice the ghost, which is why
`kResurrectTimeoutMs` was raised from 2 minutes to 15. The state transition was
detected correctly even on the run where the action itself had already timed
out — the life state does not depend on the action layer.

**Bandage — a real heal on a wounded character.** `m2_pvp_wound_a.txt` /
`_b.txt`, two live sessions:

```
[03:52:02] mark_hp = 22
[03:52:02] mark_item 0x0E21 = 53
[03:52:02] RevolutionMedic: *You apply bandages to self*
[03:52:04] System: You put the bloody bandage in your pack.
[03:52:14] hp gain confirmed: 22 -> 30
[03:52:14] item 0x0E21 drop confirmed: 53 -> 52
```

The 8 hit points were lost to real combat: `RevolutionSpar` attacked and the
server took the character from 30 to 22. Both assertions read server state, so
the target cursor completing proves nothing on its own — and did not: an
earlier run asserted 1.5 s after *"You apply bandages"* and correctly **failed**,
because that message is the *start* of the heal. Sphere runs Healing on a skill
timer and only credits the hit points when it fires, which is why the scenario
now waits and why the bandage count drops on a different tick from the heal.

**Why two sessions and not a creature.** Three separate runs chased the
worldgen wildlife at (689,753), naming valid creatures by body id and walking
into weapon range. They produced **zero** `0x2F` swings, **zero** `0xA1` health
updates, and ended with *"Grizzly Bear has retreated from the battle"*. That
spawn is not reliably hostile and this client deliberately has no combat AI to
provoke it. Two characters we control is a legitimate, repeatable substitute:
ordinary players, ordinary `0x05`/`0x02` packets, no server assistance.

`RevolutionBot01` was tried as the sparring partner first and missed every
swing — its Wrestling is **14.1**. That is a training problem, not a bug, so a
sparring character was created through the ordinary create-character path
asking for `SKILL_WRESTLING(43):50, SKILL_TACTICS(27):30, SKILL_ANATOMY(1):20`.
Source-X clamps every one of those in `CChar::InitPlayer`; the client only asks.

**Vendor sell.** `m2_vendor_sell.txt`:

```
[02:40:20] [VENDOR] sell list from 0x00001867: 5 item(s)
[02:40:21] mark_gold = 1000
[02:40:21] mark_item 0x0A28 = 1
[02:40:21] [VENDOR] sell item=0x40001FF4 qty=1 to vendor=0x00001867 gold=1000
[02:40:21] vendor_sell success (121ms) gold 1000 -> 1005
[02:40:23] gold gain confirmed: 1000 -> 1005
[02:40:23] item 0x0A28 drop confirmed: 1 -> 0
[02:40:24] [VENDOR] sell list from 0x00001867: 4 item(s)
```

The item is a candle (`0x0A28`, `VALUE=6`) named **by graphic**, so the choice
never depends on the order Sphere happens to send. The vendor's own list going
5 -> 4 items is the server agreeing the candle is gone.

`0x9E` is parsed into `vendorSellOffer_` (serial, graphic, amount, price, name
— unlike `0x74` this list carries serials, so no positional join is needed) and
`0x9F` is built with its **6-byte** records — note this differs from `0x3B`'s
7-byte records, which carry a leading layer byte the server discards.
`ActionVendorSellOpen` speaks "sell" and completes on the `0x9E`;
`ActionVendorSell` completes when gold actually arrives, which Source-X only
credits after the items have changed hands.

**Which NPC you talk to is the whole problem.** A vendor buys only what its own
`LAYER_VENDOR_BUYS` box lists, and both the Yew innkeeper and the cobbler answer
*"You have nothing I'm interested in"*. `0x98` returns an NPC's **first name
only** — `"Balayna"`, `"Lillie"` — with no trade in it. The trade is in the
**paperdoll title**, which arrives as `0x88` after a double-click, exactly how a
human player tells one shopkeeper from another. `ActionScanMobiles` now clicks
each nearby mobile once and the titles resolve the ambiguity outright:

```
[0x88] paperdoll 0x00001867: "Balayna, the provisioner"
[0x88] paperdoll 0x0000184B: "Lillie, the cobbler"
```

**Multi-session isolation, under load.** The bandage proof ran two live
sessions attacking each other for a minute. Each kept its own action slot,
target generation, mobile cache and gold; nothing crossed. The one surprise was
not a client bug: a character that logs out wounded keeps a **client-linger
body** in the world (`i_handr_1`, `t_eq_client_linger`), and in one run the
sparring partner killed that lingering body after its session had ended. Real
death by real combat, just not the one that run was for — the scenarios now
stop the attacker before the wounded session logs out.

### 6.2 Target selection

`mobile_nearest` was the weak point of the first attempt: in a town it picks
the innkeeper, and in the wilds it picked horses. Scenarios now name what they
mean, and there are three selectors because there are three different questions:

| Selector | Resolves against | Use it for |
|---|---|---|
| `mobile_body <hex,…>` | `0x78` body graphic | creatures — a CHARDEF id *is* its body, so this is exact |
| `mobile_name <text>` | `0x98` names and `0x88` paperdoll titles | players by name, vendors by trade |
| `vendor_sell_graphic:<hex>` | the parsed `0x9E` offer | naming the exact item to sell |

**Body id is not enough for players.** Body `0x0190` is *every* male human in
view. A run that bound both sessions by body had them both bind the same
passing townsman (`0x0000063C`), and Source-X refused the attack with
`0xAA 0x4FFFFFFF` — an attack acknowledgement whose serial is not the requested
one means *refused*, and the action layer already treats it that way. Binding
by name fixed it; body ids stay for creatures, where they are exact.

## 7. World preparation (server operation, not a cheat)

The save was empty — worldgen had never been run, so there were no vendors, bankers, healers or creatures to interact with. It was run **through the shard's own console**, which executes script verbs (`CServer::OnConsoleCmd` → `r_Verb`):

```
spawn_vendors_felucca
spawn_towns_life_felucca
spawn_wild_life_felucca
```

driven by `local/dev/sphere_console.ps1`, which posts the text into the server window's console EDIT control exactly as an operator typing would (`src/sphere/ntwindow.cpp:628-644`). 1,914 spawners were created and saved. No server code, script or config was modified.

A **mage character** was created to obtain scrolls: the 0x00 create-character packet with Magery as a starting skill, after which the server's own newbie template supplies the kit. That is an ordinary player action — the client asks, the server decides and clamps (`CChar::InitPlayer`).

The same route supplied the rest of M2. `RevolutionBot01` died to a wolf, and on this shard death means **full loot loss to the corpse** — a later run found its backpack and bank both empty, which is why the vendor-sell and bandage proofs use fresh characters. Each was created with the ordinary `0x00` packet; everything they carry comes from the shard's own newbie templates (`runtime/scripts/templates_special/sp_tm_newbie.scp`): `MALE_DEFAULT` gives 1000 gold, a book, a **candle** and a dagger; `[NEWBIE HEALING]` (line 403) gives **50 bandages** and scissors. Nothing was spawned, granted or edited.

| Character | Skills asked for | Why it exists |
|---|---|---|
| `RevolutionSpar` | Wrestling 50, Tactics 30, Anatomy 20 | deals real combat damage — `RevolutionBot01` at Wrestling 14.1 missed every swing |
| `RevolutionMedic` | Healing 50, Anatomy 30, Wrestling 20 | takes the damage and heals it, with bandages from its own newbie kit |
| `RevolutionBot02` | Swordsmanship 50, Tactics 30, Healing 20 | carried the candle sold in the vendor-sell proof |

Credentials live in `local/dev/bot-credentials.env`, which is covered by `/local/` in `.gitignore` and is not committed.

## 8. Known limitations and deferred work

1. **Rendezvous between two sessions is scheduled, not negotiated.** The two-session scenarios idle for fixed intervals so both characters are in place before either binds the other; four runs were lost to that race before the windows were widened. A real brain will need the sessions to agree out-of-band rather than by `sleep`.
2. **The wildlife spawn is not a usable combat fixture.** See §6.1. Anything needing a hostile should use a controlled partner until the bot can provoke a creature.
3. **JS bindings remain single-owner** (M1.5 debt item 1). M2 did not touch them; the scenario runner needs no JS.
4. **One action in flight per session.** Adequate for a player, but a future brain wanting to walk-and-cast concurrently will need more than one slot.
5. **`mobile_nearest` is a blunt instrument.** In a guarded town it will happily pick a blue NPC; the combat scenario now warns about this. Attacking town NPCs in Yew is a crime (`REGION_FLAG_GUARDED`).
6. **Message-based classification is English-string matching.** It works because Sphere reports outcomes as text, but a Revolution script pack with different message text would need the phrase list revisited.
8. **Death and resurrection are proven, and the note below is kept for the record.** The original text follows.

   ~~Two runs walked the character
   ~140 tiles to the worldgen wildlife spawn at (689,753) and attacked what the
   server showed there. `ActionAttack` was accepted both times
   (`0xAA attacking 0x000003DF`, then `0x000003CA`), but no fight followed:
   zero `0x2F` swings, zero `0xA1` health updates, and the character sat at
   30/30 hit points for minutes. The creature `mobile_nearest` picked was
   passive, and the client deliberately has no combat AI to chase or provoke.

   What *is* proven: attack initiation, and the death/resurrection plumbing --
   `0x2C` is recognised as our own death signal, `LifeState` is derived only
   from the body the server sends, `wait_dead`/`wait_alive` exist, and the unit
   tests cover the alive -> dead -> alive transitions.

   What remains: choosing an actually aggressive creature. The blocker is
   target selection, not protocol; a scenario binding a specific bear or wolf
   serial (or a `mobile_body` selector) would close this with no new client
   capability. Nothing was faked, and no admin command was used to kill the
   character.~~

9. **`LocalIPAdmin=1`** is set in `sphere.ini`. It only affects the **telnet** admin path (`CClientLog.cpp:561-573`), not game clients, so the bots are ordinary players — worth knowing before anyone assumes loopback implies staff.
