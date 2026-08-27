# Revolution 10 Client Inventory

Date: 2026-08-25. Milestone M0, Phase 1.

Source: `local/downloads/Revolution10.exe` (229,699,135 B). Detected as a **WinRAR SFX** — PE32 stub with RAR signature at offset 186981 (`7z l`: `Type = Rar`, `Offset = 186981`, 202 files / 9 folders, 590,132,390 B uncompressed).

Extraction: **static only**, `7z x` (7-Zip 26.02 installed via `winget install 7zip.7zip`). The SFX stub was never executed. Nothing in `local/revolution-client/` was executed or modified. `Revolution.exe`, `client.dll`, `LoaderDLL.dll`, `WCP.dll`, `plugins/Revolution.wcp`, `unrar.dll` are inventoried but **must never be run**.

Target: `local/revolution-client/` (excluded from git via `/local/` in `.gitignore`).

Authenticity check: SHA1 of extracted files matches the client's own update manifest `WCP/Download/updt1062.txt` for every file listed there (e.g. `tiledata.mul 6DECD36B…`, `verdata.mul 6A354F80…`, `staidx0.mul 8AC31651…`, `statics0.mul EBCD7CDC…`, `hues.mul BB69FDE6…`).

## Sphere requirement basis

Source-X opens exactly this mask at startup — `server/Source-X/src/game/CServerConfig.cpp:5016-5023`:
`VERFILE_MAP | VERFILE_STAIDX | VERFILE_STATICS | VERFILE_TILEDATA | VERFILE_MULTIIDX | VERFILE_MULTI | VERFILE_VERDATA`.
Missing file ⇒ `LOGL_FATAL "File '%s' not found"` (`:5024-5028`), **except `verdata.mul` which is optional** (`server/Source-X/src/common/CUOInstall.cpp:244-256`). File names: `CUOInstall::GetBaseFileName` `CUOInstall.cpp:136-160`. `mobtypes.txt` is also looked up in the MUL dir but is optional (`src/game/uo_files/CUOMobtypes.cpp:22`). Everything else (art, anim, gumps, sound, hues, fonts, textures) is client-only.

## Required-file verification

| File | Present | Size (B) | SHA1 |
|---|---|---|---|
| map0.mul | YES | 89,915,392 | ffcd163eb0371b941be7499072d2520f7aae67f1 |
| statics0.mul | YES | 21,971,754 | ebcd7cdcd348d71f0e64f01e73c96706fb85cdce |
| staidx0.mul | YES | 4,718,592 | 8ac31651c4b618583f1cc3c4a6edc89e0ec04b5e |
| multi.mul | YES | 653,976 | 250b0c0faa0111f837548633453e87b260497ecd |
| multi.idx | YES | 98,184 | 9feb70f36de9b562c33e8f959529e46461aeeca3 |
| tiledata.mul | YES | 1,036,288 | 6decd36bb0ebd0ec11622755b7f8a87561ee01de |
| hues.mul | YES | 265,500 | bb69fde641c5bb2f0d500a03cc8b150de66d6b8c |

All present. **No STOP condition.**

Map geometry: `map0.mul` = 89,915,392 B = 896 × 512 blocks × 196 B ⇒ **7168 × 4096 (ML-size Felucca)**. This matches the default `Map0=7168,4096,-1,0,0` in `runtime/sphere.ini:95`. Only map 0 ships; no `map1..5`.

`tiledata.mul` = 1,036,288 B ⇒ pre-HS (7.0.9) 32-bit-flag format (512 land groups × 836 B + 512 static groups × 1188 B = 1,036,288 B). Source-X handles both (`CUOTiledata`).

## Full inventory (top level)

Legend — Sphere: **REQ** = fatal if missing, **OPT** = loaded if present, **no** = not opened by Source-X. Client: needed by the 2D client / by uo-client (bot) renderer or walkability.

| File | Size (B) | UO data type | Sphere | Client only |
|---|---|---|---|---|
| anim.idx | 1,785,720 | index → anim.mul | no | yes (client; uo-client renderer optional) |
| anim.mul | 195,195,766 | mobile/equipment animation frames | no | yes |
| animdata.mul | 1,122,304 | animated static frame tables | no | yes |
| animinfo.mul | 2,000 | walk-cycle cadence tables (Revolution/era-specific) | no | yes |
| art.mul | 66,564,792 | land + static art | no | yes (uo-client renderer mandatory) |
| artidx.mul | 786,432 | index → art.mul | no | yes |
| chardata.mul | 20 | client char cache | no | yes |
| client.dll | 1,409,024 | **Revolution launcher/anti-cheat DLL — DO NOT RUN** | no | yes |
| default.mac | 749 | client macros | no | yes |
| desktop.nwb | 116 | client desktop layout | no | yes |
| fonts.mul | 888,380 | ASCII fonts | no | yes |
| gumpart.mul | 101,633,192 | gump art | no | yes |
| gumpidx.mul | 786,432 | index → gumpart.mul | no | yes |
| hues.mul | 265,500 | colour ramps | no | yes (uo-client renderer optional) |
| ignore.lst | 0 | ignore list | no | yes |
| keynames.txt | 410 | key names | no | yes |
| langcode.iff | 4,612 | language codes | no | yes |
| light.mul | 2,910,700 | light source art | no | yes |
| lightidx.mul | 1,200 | index → light.mul | no | yes |
| LoaderDLL.dll | 76,288 | **Revolution loader — DO NOT RUN** | no | yes |
| macros.txt | 750 | client macros | no | yes |
| map0.mul | 89,915,392 | Felucca terrain 7168×4096 | **REQ** | also client + uo-client walkability |
| multi.idx | 98,184 | index → multi.mul | **REQ** | also client |
| multi.mul | 653,976 | multi (house/boat) definitions | **REQ** | also client |
| multimap.rle | 320,056 | world map overview | no | yes |
| obscene.lst | 61 | profanity list | no | yes |
| options.enu | 5,298 | options strings | no | yes |
| palette.mul | 768 | palette | no | yes |
| plugins/Revolution.wcp | 109,056 | **Revolution plugin — DO NOT RUN** | no | yes |
| prof.txt | 3,852 | char-creation professions | no | yes |
| radarcol.mul | 131,072 | radar colours | no | yes (uo-client minimap optional) |
| Revolution.exe | 1,802,240 | **patched UO client 2016-01-06 — DO NOT RUN** | no | yes |
| Revolution.ini | 3,598 | launcher HTML page (not a config; contains site markup) | no | yes |
| sjis2uni.mul | 131,072 | SJIS→Unicode table | no | yes |
| skillgrp.mul | 255 | skill group names | no | yes |
| skills.idx | 3,072 | index → skills.mul | no | yes |
| skills.mul | 5,001 | skill names/flags | no | yes |
| sound.mul | 56,328,458 | sound samples | no | yes |
| soundidx.mul | 49,152 | index → sound.mul | no | yes |
| staidx0.mul | 4,718,592 | index → statics0.mul | **REQ** | also client + uo-client walkability |
| statics0.mul | 21,971,754 | static objects on map 0 | **REQ** | also client + uo-client walkability |
| texidx.mul | 49,152 | index → texmaps.mul | no | yes (uo-client renderer mandatory) |
| texmaps.mul | 20,783,108 | sloped terrain textures | no | yes |
| tiledata.mul | 1,036,288 | tile flags/heights/names | **REQ** | also client + uo-client walkability |
| unifont.mul | 1,435,288 | Unicode font 0 | no | yes |
| unifont1.mul | 1,433,134 | Unicode font 1 | no | yes |
| unifont2.mul | 1,432,845 | Unicode font 2 | no | yes |
| unrar.dll | 165,376 | SFX helper — DO NOT RUN | no | — |
| uo.cfg | 1,279 | client config (`UnicodeSpeech=on`, `AllowPathfind=on`, `DefaultChar=0`) | no | yes |
| uobscene.lst | 1,048 | profanity list | no | yes |
| verdata.mul | 7,194,036 | patch overlay for art/tiledata/statics etc. | **OPT** (loaded: `CServerConfig.cpp:5032`) | also client |
| WCP.dll | 399,872 | **Revolution updater/anti-cheat — DO NOT RUN** | no | yes |
| WCP/Download/updt1062.txt | 2,211 | update manifest (SHA1 list) | no | reference |
| music/*.mid, music/4MB/*, music/512K/* (146 files) | ~5.3 MB | MIDI + soundfonts | no | yes |

Not present (and not needed by Sphere): `map1..5.mul`, `mapdif*/stadif*`, `*.uop`, `mobtypes.txt`, `cliloc.*`, `speech.mul`.

## Copied to `runtime/mul/` (Phase 2)

Server-required set only, copied (not moved) with timestamps preserved:

```
map0.mul  staidx0.mul  statics0.mul  tiledata.mul  multi.idx  multi.mul  verdata.mul
```

`verdata.mul` included because Source-X loads it when present and the Revolution client applies the same patches; server and client must agree on patched tiledata/statics.

## Observations for later milestones (not acted on)

- `Revolution.exe` timestamp 2016-01-06; `client.dll` 2010-10-24. Actual protocol version of this client is UNKNOWN until a `0xBD` / seed capture — relevant to §2 of `ARCHITECTURE_AUDIT.md`.
- `uo.cfg` has `UnicodeSpeech=on` ⇒ the human client will send `0xAD`; Sphere handles it (`receive.cpp:2090`).
- `animinfo.mul` is present — same file uo-client's renderer reads for walk cadence.
