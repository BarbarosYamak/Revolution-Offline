# Magery practice reads the whole spell table (2026-09-02)

Owner ruling: "for mage to cast there are lots of skills, don't hard code
Create Food." The hand-picked twelve-spell list in `include/uo/spellcast.h`
(`SelfSafeSpells`) is gone.

## Ground truth and export

`runtime/scripts/spells/spells_magery.scp` -> `tools/spellgen.py` ->
`data/revolution_spells.tsv` (64 rows, header
`spell defname name circle minskill mana flags reagents`).

Verified off the script, 2026-09-02:

* `grep -i "^SKILLREQ" spells_magery.scp | sort | uniq -c` = 8 rows each at
  MAGERY 10.0/20.0/.../80.0 -> **circle = SKILLREQ / 10** (derived, not assumed).
* `RESOURCES=` carries no quantity prefix -> one of each reagent per cast.
* 22 distinct `spellflag_*` tokens occur; the loader knows all 22 and marks any
  other token `unknownFlags`, which makes the spell ineligible (UNKNOWN -> skip).
* `[SPELL 7]` reads `...|spellflag_good//|spellflag_playeronly` -- the `//` is a
  comment, so playeronly is NOT set. The generator strips at `//`.
* `python tools/spellgen.py --check` re-verifies the export against the script.

## Rules the chooser applies (`spell::ChoosePracticeSpell`)

1. **Book gate** - spell number must be in the spellbook (`Runner::BookHasSpell`,
   amount field carries the spell number).
2. **Safety** - `SafeToPractiseOnSelf`: no harm/damage/curse/field/summon, no
   targ_xyz/obj/item/dead, and the spell must carry good|bless|heal. That last
   clause is stricter than "not harmful" on purpose: `playeronly` alone also
   covers Incognito, Summon Creature and Dispel Field. Create Food falls out of
   practice here and stays in `DoGetFood`.
   Result on the real table: 13 beneficial self-castable spells - c1 {4,6,7},
   c2 {9,10,11,15,16}, c4 {25,26,29}, c5 {36}, c6 {44}. Circles 3, 7, 8 have
   none, which is a property of the shard's own flags.
3. **Skill gate** - `SKILLREQ` is a hard server gate; below it the spell is not
   a candidate.
4. **Mana gate** - `MANAUSE <= obs.mana`; does not strike the spell off.
5. **Gain window** - highest circle first, walking downward, restricted to
   spells whose requirement is within ONE CIRCLE of the character's Magery.
   The window width is measured from the data (`CircleSpacingTenths()` = the
   gap between circle 1 and circle 2 requirements = 100 tenths), not chosen.
   **UNKNOWN**: the actual Sphere gain curve. `[SKILL 25]
   ADV_RATE=10.0,200.0,800.0` (skill25_magery.scp) is a rate triple, not a
   window; sphere.ini has no gain-window key; the server source is not in this
   repo. The window is therefore a candidate filter, not a claim about gain
   rate, and it falls back to the hardest castable spell when the book holds
   nothing inside it.
6. **Reagent gate + rotation** - within the chosen ring, cast the least-cast
   spell (ties by spell number), so one word is not spammed.
7. **Shopping list** - when no ring member is paid for, `missing` is the UNION
   of the reagents of the spells the chooser WOULD pick at that circle, with
   `shortFor` = the cheapest of them. Quantity logic (`ExpectedPracticeCasts` /
   `PlanReagentBuy`) is unchanged from the reagent fix.

## Gates

* `python tools/rev.py test` -> **43/43 ctest pass**.
* `m4_life` 566 checks / 0 failures, including the rewritten
  `TestPracticeChecksTheReagentPouch`, which drives the chooser over a FAKE
  8-row TSV via `spell::LoadSpellTableFromText` and covers: table load, window
  width, gain window (circle 2 preferred over circle 1 at Magery 20; circle 4
  once Magery 40), book gate, skill gate, mana gate, reagent gate, rotation
  (16 -> 9 -> 16), refusal fallback to a lower circle, unknown-flag skip,
  harmful/ground-target skip, and the union shopping list.

## Live smoke - INCONCLUSIVE for the practice path

`python tools/rev.py gates CHARS=Aurelius,Selene MINUTES=5` +
`wait` (2026-09-02 13:24-13:29). Both reached `logout_complete`.

* `grep -c "You lack"` = 0 for both. No "not in your spellbook".
* **Zero `practice: casting` lines.** PRACTICE_SKILL was never selected:
  `run_gates/g_Aurelius.console.txt` shows FILL_SPELLBOOK holding the whole
  session (16 mentions, `restored objective KEPT: FILL_SPELLBOOK (progress 0)`,
  trips 1 and 2), and `g_Selene.console.txt` shows
  `goal_changed=FILL_SPELLBOOK from=TRAIN_AT_NPC reason="FILL_SPELLBOOK 58.7
  superseded TRAIN_AT_NPC 49.5"`.
* Root cause is PRE-EXISTING and outside this task: the scribe and the mage
  both stock only `blank scrolls` (`spellbook: this scribe has nothing the book
  lacks`), so FILL_SPELLBOOK can never complete, while NeedSpells keeps
  outscoring practice. Same defect already recorded in
  `artifacts/reagent_fix_2026-09-02.md` ("Runs after the follow-up fixes").
* A second identical 5-minute run was not spent: nothing in it would differ.

So the chooser is proven by unit test and by the exported table, and NOT by
runtime. To prove it live, either a character whose book is already full must
be gated, or the FILL_SPELLBOOK-vs-practice scoring defect must be fixed first.
