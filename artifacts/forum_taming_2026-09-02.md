# Taming skill requirements — forum + Scripts-X sweep, 2026-09-02

## Method / access reality

- Forum search endpoint (`index.php?action=search2;search=taming`) requires login —
  confirmed by fetching it directly: response `<title>Giriş Yap</title>` with
  `İletilerde arama yapmak için izniniz yok.` ("You do not have permission to
  search posts. Please log in below..."). No retry, per task instructions.
- Changelog megathread (board 177, topic 15324, "Güncellemeler") IS guest-readable.
  Fetched `index.php?topic=15324.0` (101KB, single page returned). Grepped case-
  insensitively for `taming`, `evcille`, `hayvan eğ`, `tamer`. Three hits, all
  about skill-GAIN-rate changes, none about skill REQUIREMENTS to tame a specific
  creature:
  - undated block (~2009 context by surrounding text) — verbatim: *"Stealing,
    snooping, animal lore, animal taming, stealth, hiding, lockpicking hariç
    bütün yeteneklerin artışı %200-250 kolaylaştırıldı."* (all skills except
    these got their gain rate eased 200-250%; animal taming was explicitly
    excluded from that easing.) FORUM, dated context ~12.03.2009 per adjacent line.
  - dated **28.02.2016** — *"Spawntakip listesi güncellendi, binekler ölene yada
    evcilleştirilene kadar listede gözükmeyecektir."* (mount spawn-tracker
    change, unrelated to skill values.) FORUM.
  - dated **26.02.2012** — *"Takipçi sayısı sınırdayken hayvanların kontrattan
    çıkarılması veya yeni hayvan evcilleştirilmesi mümkün olmaz."* (follower-slot
    cap rule, unrelated to skill values.) FORUM.
  - No numeric taming-difficulty value for any creature found anywhere in this
    thread.
- DuckDuckGo HTML search (`html.duckduckgo.com/html/?q=...`), two queries:
  `revolutionuo taming at 29.1` → only generic non-Revolution UO wikis
  (uoevo.com, uoguide.com, stratics, uo-cah.com) — none reference Revolution.
  `site:revolutionuo.net taming` → returned real revolutionuo.net forum/site
  URLs but all are unrelated WTB/WTS/misc threads (item trades, "Random Chest",
  "Genel" board index) — titles have no taming/horse content; none opened
  further given the board is login-gated anyway for most content.
- Main site pages (per prior sweep, `docs/FORUM_SWEEP_2026_08_30.md`) already
  checked `oyun_rehberi` (game guide) with no taming-difficulty table found.

**Verdict: no forum or site evidence found for horse (or any creature's)
numeric Taming requirement.** The boards that would carry a taming guide
(newbie/rehber boards) are login-gated, consistent with the prior forum sweep's
finding that guest access is limited to board 177 + old 2011-2016 market boards.

## SCRIPTS-X (CURRENT_SCRIPT, authoritative for current runtime)

All four playable horse color variants agree exactly:

- `c_horse_tan` — `runtime\scripts\npcs\c_monster_classic.scp:4256` `TAMING=29.1`
- `c_horse_brown_dk` — `c_monster_classic.scp:4468` `TAMING=29.1`
- `c_horse_gray` — `c_monster_classic.scp:5106` `TAMING=29.1`
- `c_horse_brown_lt` — `c_monster_classic.scp:5147` `TAMING=29.1`
- `c_horse_pack` (pack horse) — `c_monster_classic.scp:5470` `TAMING=29.1`
- `c_horse_sea` (Sea Horse, `c_monster_classic.scp:3880`) — no `TAMING=` line at
  all (not a Taming-skill tameable; treated as a monster-brain creature, no gate).
- `c_ostard_desert` — `c_monster_t2a.scp:2252` `TAMING=29.1` (same tier as horse)

No horse-family chardef in Scripts-X anywhere near 53.1. Owner's remembered
53.1 does not match any current horse/ostard entry; closest 5x.x values in the
whole taming grep sweep were unrelated creatures (`c_wolf_grey`-family region
had none near 53; nearest neighbors were `c_alligator=47.1` and
`c_ostard_frenzied` unread this pass).

## Other Revolution-family taming values found (CURRENT_SCRIPT), same grep sweep

| Creature | Value | Location |
|---|---|---|
| Llama | 35.1 | `c_monster_classic.scp:4943` |
| Pack Llama | (WRESTLING/TACTICS=19.2/29.0 shown, TAMING line cut off past read window — not confirmed) | `c_monster_classic.scp:5474+` |
| Bull (both colors) | 71.1 | `c_monster_classic.scp:5223`, `:5263` |
| Alligator | 47.1 | `c_monster_classic.scp:4391` |
| Pig | 11.1 | `c_monster_classic.scp:4428` |
| Green Dragon | 93.9 | `c_monster_classic.scp:669` |
| Nightmare | 95.1 | `c_monster_t2a.scp:1629` |
| Fire Steed | 106.0 | `c_monster_classic.scp:4308` |
| White Wyrm | not read this pass (`c_monster_t2a.scp:153`, TAMING line beyond the fetched window) | `c_monster_t2a.scp:153` |
| Great Hart | not read this pass | `c_monster_classic.scp:5267` |
| Drake/Bear/other lbr-file entries | mixed 25.0-105.0 range, various | `c_monster_lbr.scp` (multiple, see grep dump) |

## Horse as a tamer-sold market item

Not investigated this pass beyond what's already in agent memory
(`mounts-npc-sold-but-players-cheaper.md`, PLAYER_MEMORY/HYPOTHESIS grade):
runtime currently has **no animal/mount vendor NPC at all** — a known missing-
content gap, not evidence about the historical Turkish shard's horse price.
No forum evidence for horse pricing was found in this pass (see access-reality
section above; the relevant market-board era readable to guests tops out
~2016 and no horse-specific listing was searched for by name this pass —
out of budget).
