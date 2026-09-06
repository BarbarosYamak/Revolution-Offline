---
name: a-tool-list-is-a-keep-list
description: DoBank boxes whatever the profession does not declare, so an undeclared tool is dead weight - and a second `p.tools =` in the same row silently deletes the first
metadata:
  type: project
---

A profession's `tools` list is not just a shopping list for GET_TOOL; it is
the keep list DoBank's dead-weight pass reads. Anything not declared is
"this life has no use for it" and goes in the box.

The pure-mage row set `p.tools = {spellbook}` and then, eleven comment
lines later, `p.tools = {dagger}` — an assignment, not an append. Aurelius'
23-spell book was banked as dead weight on 2026-09-06
(artifacts/mage_bank_followup_20260906/Aurelius.console.txt:81) and she
then bought the empty replacement she could not cast from. The same test
that guards this found two more Mage-strategy rows — alchemist and warlock —
that had never declared a book at all.

**Why:** the catalogue rows are long, heavily commented, and edited by
different briefs; a later `p.tools =` reads as an addition and is not one.

**How to apply:** when a bot loses a possession it should obviously keep,
check the profession row's keep list before suspecting the bank handler.
Prefer a class-level rule in the handler too (a caster's spellbook is never
dead weight) so one wrong table row cannot cost the trade. Guard the family
invariant with a catalogue test over `prof::All()`, not with a fix to one row.
