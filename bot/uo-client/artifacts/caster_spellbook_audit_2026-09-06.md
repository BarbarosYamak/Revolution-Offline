# Caster spellbook audit — 2026-09-06

Scope: every mage/warlock/scribe in `bot/uo-client/artifacts/fleet_ramp_20260906/results.md`
(39 characters). Source: live world save under `runtime/save/` via
`python tools/world_query.py --char <name>` and `--item <uid>`, run
2026-09-06. Each `i_spellbook` is located via CONT chain (WORN = CONT is
the character's own serial directly, i.e. no backpack/bank intermediary;
PACK = nested under `i_backpack`; BANK = nested under `i_bankbox`). Spell
count = popcount of the `MORE1` bitmask (23 spells = full standard book).

## Table

| Character | Profession | Book location | Book UID | MORE1 | Spells (set bits) | Note |
|---|---|---|---|---|---|---|
| Aurelius | mage | BANK | 0x4000edd5 | 0386affff | 23 |  |
| Aurelius | mage | PACK | 0x40013046 | (none) | 0 | EMPTY |
| Leander | mage | WORN(on char) | 0x4000ed77 | 0386eecff | 21 |  |
| Lyra | scribe | WORN(on char) | 0x4000f898 | 0b82bffff | 24 |  |
| Dravys | warlock | PACK | 0x4001110e | 0383a8cb8 | 14 |  |
| Alder | warlock | PACK | 0x40010ffc | 0382acc78 | 14 |  |
| Baelos | warlock | PACK | 0x40010cb7 | 0382a8c38 | 12 |  |
| Illyria | mage | WORN(on char) | 0x4000ed2b | 0382accff | 18 |  |
| Ardor | warlock | PACK | 0x40010ac7 | 0382aac38 | 13 |  |
| Veyin | mage | WORN(on char) | 0x40010a75 | 0382acc7d | 16 |  |
| Narus | warlock | PACK | 0x400105cb | 0382a9c38 | 13 |  |
| Aurir | mage | PACK | 0x4000f689 | 0382adcff | 19 |  |
| Calar | warlock | PACK | 0x4000debe | 0382aac38 | 13 |  |
| Zaran | mage | WORN(on char) | 0x4000d89a | 0382a9cff | 18 |  |
| Caelazar | warlock | PACK | 0x400093fd | 038aa9c3c | 15 |  |
| Talran | mage | WORN(on char) | 0x400016d6 | 0b82bccff | 20 |  |
| Thalia | scribe | WORN(on char) | 0x4000efba | 0b82bffff | 24 |  |
| Narvar | warlock | PACK | 0x4001741f | 0382a9c78 | 14 |  |
| Ithvar | mage | WORN(on char) | 0x400164c6 | 0382a9d3c | 15 |  |
| Beleth | warlock | PACK | 0x40011321 | 0382acc39 | 14 |  |
| Neriel | mage | WORN(on char) | 0x4001052a | 0382b8e38 | 14 |  |
| Cyron | warlock | PACK | 0x4000c8c1 | 0382a9c38 | 13 |  |
| Coriel | scribe | WORN(on char) | 0x400091d9 | 03ceafc7c | 20 |  |
| Fenael | mage | WORN(on char) | 0x4001a82e | 0382a8dff | 18 |  |
| Zephrin | warlock | PACK | 0x400191b7 | 038aa8c3a | 14 |  |
| Galrin | mage | WORN(on char) | 0x40018efc | 03aba9c3c | 17 |  |
| Kharos | warlock | PACK | 0x40018029 | 0382e8eb8 | 15 |  |
| Caelos | mage | WORN(on char) | 0x40017c0c | 0382eecbc | 17 |  |
| Varos | warlock | PACK | 0x40017a4d | 0383a8cb8 | 14 |  |
| Halos | mage | WORN(on char) | 0x40017946 | 0386acc7c | 16 |  |
| Eldis | warlock | PACK | 0x40017906 | 0383acc3c | 15 |  |
| Ithiel | mage | WORN(on char) | 0x400178c5 | 0386aac7f | 18 |  |
| Asheth | scribe | WORN(on char) | 0x4001755f | 038ea9cbc | 17 |  |
| Ghalys | warlock | PACK | 0x400174fd | 0382e8c38 | 13 |  |
| Nairyn | mage | WORN(on char) | 0x400174c2 | 0386acc7d | 17 |  |
| Elvus | warlock | PACK | 0x400173e2 | 0386a8c38 | 13 |  |
| Galar | mage | WORN(on char) | 0x400172f2 | 0383aacff | 19 |  |
| Zaren | warlock | PACK | 0x400172ae | 0382a8c38 | 12 |  |
| Daelran | mage | WORN(on char) | 0x4001726e | 0382ecc7f | 18 |  |
| Lorys | mage | WORN(on char) | 0x40017231 | 0b82bccff | 20 |  |

## Summary

- 39 casters audited: 18 mage, 17 warlock, 4 scribe.
- 38/39 have exactly one spellbook, filled (MORE1 set, 12-24 spells), and it is
  in an accessible location: 18 in PACK, 20 WORN directly on the character
  (CONT = char serial, not inside a container item — equivalent to holding
  the book, casting-accessible same as PACK).
- 1/39 (Aurelius) is broken: her only filled book (23 spells, uid
  0x4000edd5) is inside her BANK box (inaccessible while away from a bank),
  and she additionally carries a second, completely EMPTY spellbook
  (uid 0x40013046, no MORE1 field at all) in her PACK — the one she can
  actually reach has zero castable spells.
- No caster was found with NO spellbook anywhere (0 "NO_BOOK" cases).
- No other caster was found with a book banked, or with more than one book.

Aurelius is currently a singular outlier, not evidence of a systemic
placement bug across the fleet — but it is a live, reproducible cast-blocking
defect for her specifically.
