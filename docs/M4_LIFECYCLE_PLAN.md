# M4 — Persistent Autonomous Character Lifecycle

Date: 2026-08-28. **STATUS: PLAN. First vertical slice only.**

M4 is not thirty archetypes. It is **one character that lives**, logs out, and
picks its life back up next session.

---

## 0. Starting state — the world M4 begins from

Cleared 2026-08-28, before the first slice. **59 test characters deleted**, with
the 804 items they owned (backpacks, worn gear, containers, resolved through the
full `CONT` chain so nothing was left orphaned). Verified: 0 items remained with
an unresolvable parent, and the shard reloaded to
`Startup complete (items=79405, chars=14180, accounts=61)` with no
dangling-character errors.

**The 61 accounts were kept, only their character links removed.** M4 characters
are made through ordinary character creation on accounts that already exist — no
account is granted anything a player could not get.

Kept: `Admin` (Owner) and `Observer`. Backup of the pre-clear world and accounts:
`runtime/save_backup_pre_m4_charclear/`.

### Three load errors that survive this reset, stated rather than hidden

* `VENDOR_S_RANCHER` / `VENDOR_B_RANCHER` are **referenced but never defined** —
  in `c_vendor_human.scp:3667-3668`. The elf and gargoyle vendor files comment
  them out and fall back to BOWYER, which is how the gap is visible at all. A
  rancher vendor therefore stocks nothing. Stock Scripts-X, not ours. Confirmed
  by matching the bare token across all of `scripts/`: use-sites only, no
  defining section header — the search angle that four earlier false "missing"
  calls in this project skipped.
* `tm_armor.scp:111` — gargish stone armor at ID `0x4200`, past this client's
  tiledata ceiling. Post-SA content sitting in a loot template; it errors at
  load and would draw nothing if ever rolled.
* Two placed objects still carry unrenderable graphics:
  `spherestatics.scp:7987` (`0x5738`) and `sphereworld.scp:9268` (`0x4200`).
  Survivors of the 157-item cleanup, which reduced this error class but did not
  reach zero.

This build accepts neither `exit` nor `shutdown 0` as a console keyword, so the
shard was force-terminated after its final save. `sphere.pid` is therefore stale
on the next launch — harmless, and it explains the `sphere.pid already exists`
line in the startup log.

---

## 1. The first character

A **frontier Lumberjack / Swordsman**. Chosen because it exercises the widest set
of already-proven systems without needing mature Mage or Tamer infrastructure:

* gathers a real resource (lumberjacking) → income without a supplier chain
* fights wild, legal targets → PvM, survival, death, corpse recovery
* sells and banks → vendor, gold, weight management
* trains a weapon skill through ordinary work → skill gain, the 700 cap
* needs food → hunger is live (`HitsHungerLoss=1`)

It deliberately does **not** need: Recall (walks and uses moongates), reagents,
taming, or player vendors.

### Its build, and one correction M3.9.1 forced

Target: Swordsmanship / Tactics / Lumberjacking / Healing / Anatomy toward a
700-point build, under the shard owner's stat rule: **225 total, 100 per stat**
(DERIVED in M3.8 from ten player builds across two unrelated threads and two
classes, every one summing to exactly 225). The runtime allows 300, so this is
enforced BOT-SIDE — `rules.h` carries `totalStatCap = 225`, `perStatCap = 100`.

Note `REVOLUTION_UO_BUILD_COMPENDIUM_v2.md` said "UNSPECIFIED — do not encode a
stat cap" until M3.9.1; that entry is now marked superseded, because a stale
"unknown" is worse than no entry at all.

**Two different numbers, easily confused:**

* the **cap** above is what a FINISHED build is planned against;
* **character creation** is clamped by Source-X to 60 per stat and 80 total
  (`CChar::InitPlayer`), so a creation split is only a request.

**Creation split: do NOT use STR 55 / DEX 15 / INT 5.** Six characters died solo
on it against a single awake animal, one from full health in about 34 seconds.
**STR 40 / DEX 35 / INT 5** survived the whole kill-carve-loot chain. That points
at DEX — swing speed and evasion — rather than raw strength. It is a measured
observation from six deaths, not a Revolution-sourced rule, and should be checked
against the shard's actual combat formula before being treated as settled.

---

## 2. The loop

```text
login
  ↓
load persistent identity  (who am I, what am I for)
  ↓
assess state
  ├─ dead?            → healer, resurrect, travel_corpse, recover gear
  ├─ under threat?    → combat_policy: potion / disengage / bandage / flee
  ├─ hungry?          → eat carried food, or buy it
  ├─ no weapon/tool?  → replace from bank or vendor
  ├─ overloaded?      → bank or sell
  ├─ low gold?        → work the profession
  ├─ skill < target?  → train through legitimate work, not macro grinding
  └─ else             → pursue the current goal
  ↓
bank / rest
  ↓
LOG OUT SOMEWHERE SAFE
  ↓
next session continues the same life
```

**Log out somewhere safe is a rule, not a nicety.** A character that logs out in
a graveyard is dead by the next login — that cost this project three characters,
and one died in the gap *after* `logout_ack` because Source-X does not drop a
combat-flagged connection immediately.

---

## 3. What must persist between sessions

| Kind | Why it cannot be recomputed |
|---|---|
| identity, aspiration, target build | defines every decision below it |
| target STR/DEX/INT | build correction above depends on it |
| equipment goals | "what am I missing" needs a target |
| gold and inventory goals | when to bank, when to sell |
| **known suppliers** | only ever created from *observed* shop stock |
| known resource locations | where the trees/ore actually were |
| known dangerous areas | where it died before |
| recent deaths | corpse recovery, and avoiding a repeat |
| learned route knowledge | which destinations are reachable |
| current objective | so a session resumes rather than restarts |

`Supplier::Registry` and `PersonalKnowledge` already model most of this in
memory. M4's work is making it **survive a logout**, not inventing it.

---

## 4. Order of work

1. **Persistence layer** — save/load the above per character. Nothing else can
   be tested repeatedly until a life continues across sessions.
2. **State assessment** — the `assess state` branch, driven by what the client
   can actually observe (health, hunger, pack, gold, skills).
3. **One economic loop** — chop → haul → sell → bank, with weight and hunger
   handled honestly.
4. **Survival integration** — `survival on` becomes the default for autonomous
   characters, with death and corpse recovery as an ordinary branch rather than
   a failure.
5. **Skill progression toward the target build** through that work.

Only after a single character survives several consecutive sessions does a
second archetype get added.

---

## 5. Constraints carried in from M3.9.1

* **Anti-macro is not yet implemented.** M4 skill training may proceed, but it
  **must not be called Revolution-authentic** until anti-macro adaptation is
  built or explicitly accounted for. Carry this as a live constraint.
* Hunting must prefer **wild, hostile, legal** targets. Farm animals near towns
  are owned and innocent; attacking one flags the character criminal, the flag
  persists across sessions, and a guard will execute it.
* No LLM runtime layer. Order stays: deterministic Sphere mechanics →
  deterministic tactics → utility/goal planning → persistent memory → social
  layer much later.
* Interior macro routing is still unbuilt; destinations inside buildings will
  fail in a bounded way. That is acceptable, and the character must react to it
  rather than assume arrival.
