# M0 — Sphere Source-X Local Configuration Changes

Date: 2026-08-25. Authority: the locally checked-out Source-X (`server/Source-X`, commit `dd4183ddc97b494b4c6c9e5d453b73910dfa02a2`) and `runtime/sphere.ini` (byte-identical to `server/Source-X/src/sphere.ini` before edits — verified with `diff`).

Baseline preserved at `runtime/sphere.ini.baseline` (CRLF, 56,662 B). All edits were made byte-wise on the CRLF file; `diff runtime/sphere.ini.baseline runtime/sphere.ini` shows exactly the four lines below.

Nothing gameplay-related was touched: no Magery/combat/precast/spell timing/skill gain/stat/crafting/loot/economy/taming/era key was changed. `FeatureT2A/LBR/AOS/SE/ML/KR/SA/TOL`, `Map0`, `WalkBuffer`, `ClientLinger`, `UseAuthID`, `ClientVersion` all remain at vanilla defaults.

## sphere.ini

| # | Key (line) | Original | New | Why required | Evidence |
|---|---|---|---|---|---|
| 1 | `AGREE` (line 14) | `//AGREE=1` (commented) | `AGREE=1` | Nightly builds refuse to start without it. Boot attempt #1 died with `ERROR: Please write AGREE=1 in Sphere.ini … FATAL: Server terminated: Failed to load server settings (code -3)` (`runtime/logs/boot1_sphere2026-08-25.log`). | `src/game/CServer.cpp:2768-2774` (`#ifdef _NIGHTLYBUILD … if (!g_Cfg.m_fAgree) return false;`); key parse `src/game/CServerConfig.cpp:763`. The exe is a nightly (`CMAKE_CONFIGURATION_TYPES=Debug;Release;Nightly`, built as `Nightly`; banner "SphereServer Version X1-Nightly"). |
| 2 | `MulFiles` (line 75) | `//MulFiles=mul/` (commented) | `MulFiles=mul/` | Without it, `CUOInstall::FindInstall()` looks up a UO install in the Windows registry (none exists here) and `OpenFile` then falls back to the exe directory / CD path. The MULs live in `runtime/mul/`. | `src/common/CUOInstall.cpp:32-60` (registry keys), `:119-135` (`m_sPreferPath` used first when set), `src/game/CServerConfig.cpp:1312-1313` (`RC_MULFILES → g_Install.SetPreferPath`). Path relative to the working directory (`CSFile::GetMergedFileName`). |
| 3 | `AccApp` (line 162) | `AccApp=0` (Closed) | `AccApp=2` (Free) | Local development: any unknown account/password logging in creates a full account automatically — needed so the headless bot (and a human dev client) can log in without console `ACCOUNT ADD`. | Enum `src/game/CServerDef.h:28-38` (`ACCAPP_Free = 2`); auto-create decision `src/game/clients/CClientMsg.cpp:3230` (`fAutoCreate = (m_eAccApp == ACCAPP_Free || GuestAuto || GuestTrial)`) → `CAccounts::Account_FindCreate` `src/game/clients/CAccount.cpp:189-211`. Ini comment at `sphere.ini:154-161` documents the same codes. |
| 4 | `UseNoCrypt` (line 196) | `UseNoCrypt=0` | `UseNoCrypt=1` | The bot client (uo-client) is unencrypted; Source-X rejects unencrypted logins unless this is set. `UseCrypt=1` left as-is so the real Revolution client (encrypted) still works. | Parse table `src/game/CServerConfig.cpp:1025` (`"USENOCRYPT" → m_fUsenocrypt`); enforcement `src/game/clients/CClientLog.cpp:1002-1024` (`xCanEncLogin`). Nocrypt key is always index 0 of the key table (`src/common/crypto/CCrypto.cpp:58-64`). |

Unchanged but relevant (verified vanilla values): `ServIP=127.0.0.1` (line 21), `ServPort=2593` (line 24), `ScpFiles=scripts/` (line 55), `Map0=7168,4096,-1,0,0` (line 95 — matches Revolution `map0.mul` size, see `REVOLUTION_CLIENT_INVENTORY.md`), `UseAuthID=1` (line 245), `ClientVersion` commented (line 190).

## runtime/scripts/spheretables.scp (Scripts-X resource list)

| Change | Original | New | Why required | Evidence |
|---|---|---|---|---|
| 21 lines under `// maps` for `maps/map2/*`, `maps/map3/*`, `maps/map4/*`, `maps/map5/*` (lines 106-129) | active | commented out (`//… // M0: disabled - no mapN.mul …`) | Revolution ships only `map0.mul`. With `map2..5.mul` absent, Source-X disables those maps, and every point in those script files (areas, teleporters, moongates) is **"auto-fixed" onto map 0** — i.e. ~1,600 Ilshenar/Malas/Tokuno/Ter Mur teleporters and regions would be planted on Felucca at their original coordinates (boot #2 produced 1,606 `ERROR:(mapN_*.scp) Unsupported map #N specified. Auto-fixing that to 0.` lines and would have realised them as live `CTeleport`s). This is a world-integrity issue, not a cosmetic warning. Disabling the script files for non-existent maps is operator configuration, not a mechanics change. | Map disabled when MULs missing: `src/common/CUOInstall.cpp:395-412`. Auto-fix: `src/common/CPointBase.cpp:996-1002` (`if (!g_MapList.IsMapSupported(m)) { EventError("Unsupported map …"); ptTest.m_map = 0; }`), `src/common/CRect.cpp:174`. Teleporters realised from these points: `src/game/CServerConfig.cpp:4133-4143` (`RES_TELEPORTERS → new CTeleport(...)->RealizeTeleport()`), `src/game/CTeleport.cpp:23-24`. Boot #2 log: `runtime/logs/boot2_sphere2026-08-25.log`; boot #3 log after the change has zero script errors: `runtime/logs/sphere2026-08-25.log`. |

`git -C runtime/scripts diff` shows this as the only local deviation from Scripts-X commit `27e78bc896da239d3738fe02a6d6bf8e9045c16d`.

Not changed, but noted: `maps/map1/*` (Trammel) entries were left active. Source-X's vanilla behaviour when `map1.mul` is missing is to read map 1 from `map0.mul` (`src/common/CUOInstall.cpp:408-409`: `if (index == 1 && m_Maps[0].IsFileOpen()) iNum = 0`), so a Trammel facet that mirrors Felucca terrain exists in this server. Whether Revolution had a Trammel facet is UNKNOWN; decision deferred to the Revolution-fidelity milestone.

## Not applied (considered and rejected)

- `ServIP` "localhost only": `CSocket::Bind` (`src/network/CSocket.cpp:324-332`) deliberately substitutes `INADDR_ANY` whenever `ServIP` is a local address, so the listener is `0.0.0.0:2593` regardless of ini. Restricting to loopback would require an engine change or a host firewall rule — out of scope for a vanilla M0.
- Disabling `Map1..Map5` lines in `sphere.ini`: unnecessary — maps 2-5 are already disabled by the MUL check, and Map1 is vanilla mirror behaviour.
- `DebugFlags` packet logging: not enabled for M0 (no client traffic yet); to be enabled in the bot-login milestone.
