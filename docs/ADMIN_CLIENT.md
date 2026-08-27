# Admin Client (ClassicUO, PLEVEL 7 / Owner)

> **SUPERSEDED for day-to-day use.** `tools\launch_admin.bat` now starts
> **`uo_viewer`**, this project's own native client
> (`bot/uo-client/src/viewer/ViewerMain.cpp`), against the same `Admin` account.
> It reads `local\dev\admin-credentials.env` directly — no XOR-obfuscated
> password field, no machine-name dependency — and it survives the out-of-era
> graphics that crash ClassicUO on this shard. **The privilege is unchanged and
> still lives entirely on the server**: `PLEVEL 7` is a property of the account,
> and `uo_viewer` sends exactly the packets a player's client sends.
>
> ```
> tools\launch_admin.bat                      # normal use
> tools\launch_admin.bat --width 1600 --height 900
> tools\launch_admin.bat --classicuo          # legacy ClassicUO path, still here
> ```
>
> Esc logs out cleanly (`0xD1` + socket close). Everything below still documents
> the ClassicUO setup and remains the reference for the account itself.

Date: 2026-08-26.

Purpose: a **graphical, human-driven UO client logged in as the shard owner**, so
a person can administer, inspect and debug the world with GM commands while the
headless bots play in it.

This is the privileged counterpart to `docs/OBSERVER_CLIENT.md`. Read that
document first — everything about the ClassicUO build, the Revolution client data
directory, the client version, the encryption setting, the missing cliloc and the
troubleshooting table applies here unchanged. This document covers only what is
**different** for the admin.

> ### Warning
>
> The `Admin` account has **PLEVEL 7 (`PLEVEL_Owner`)** — the highest privilege
> level Sphere has (`server/Source-X/src/common/CTextConsole.h:14-25`). It can
> create items, set skills, teleport, kill, resurrect, edit any character and
> shut the server down. **None of that may be used on the bots.** `CLAUDE.md`
> requires bots to obtain everything through normal gameplay; using this account
> to hand a bot gold, skills or equipment invalidates the whole simulation. Use
> it for observation, diagnosis and world administration only.

**Zero server modifications were made for this.** Nothing under
`server/Source-X/` or `runtime/scripts/` was changed, and `runtime/sphere.ini`
was not touched. Source-X and the scripts were read for reference only. The
running `SphereSvrX64_nightly.exe` (PID 30864, started 00:33:57) was never
stopped, restarted or asked to save the world — it was only sent `ACCOUNT`
console commands.

---

## 1. The Admin account

| Thing | Value |
|---|---|
| Account name | `Admin` |
| Privilege | `PLEVEL 7` = `PLEVEL_Owner` |
| Password | `local\dev\admin-credentials.env` (16 characters; `local/` is gitignored) |
| Host / port | `127.0.0.1:2593` |

### How it was created

`Observer` was auto-created by `AccApp=2` on first login — but that only ever
produces an ordinary **player** account. An owner account has to be granted, so
`Admin` was created by sending Sphere's own documented admin procedure (the one
printed in the server's startup banner) to the **already-running** server's
console, using `local\dev\sphere_console.ps1`. That helper posts text into the
server window's console input control exactly as an operator typing at the
console would:

```
ACCOUNT ADD Admin <password>
ACCOUNT Admin PLEVEL 7
ACCOUNT Admin              <- read-back; prints the account's current state
```

`ACCOUNT ADD name password` and `ACCOUNT name PLEVEL x` are Sphere's own commands
(`server/Source-X/src/game/clients/CAccount.cpp:445-455`). The console itself
runs at `PLEVEL_Owner` (`src/game/CServer.cpp:1375`), which is what allows it to
grant level 7; `CAccount.cpp:578-579` clamps anything above 7 back down to 7.

> **The server window is hidden.** `SphereSvrX64_nightly.exe` currently runs with
> its window not visible, so `Process.MainWindowHandle` is `0` and the stock
> `sphere_console.ps1` aborts with *"Sphere has no main window handle"*. The
> window still exists — find it by enumerating top-level windows for the process
> and matching the window **class name `SphereServer`**, then find its `Edit`
> child and post `WM_SETTEXT` + `WM_COMMAND(IDOK)` exactly as the helper does.
> If you need this again, either patch that class-based lookup into
> `sphere_console.ps1` as a fallback, or use the same enumeration inline.

> **Password length matters.** Sphere truncates stored passwords to 16 characters
> (`MAX_ACCOUNT_PASSWORD_ENTER`). The generated password is exactly 16
> characters, alphanumeric.

> **Persistence.** Accounts live in memory and are flushed to
> `runtime/accounts/sphereacct.scp` on a world save (`SavePeriod=20`). `Admin`
> persists at the next autosave. No world save was forced for this task. If the
> server were killed before any save happened, just re-run the two `ACCOUNT`
> commands above with the password from `admin-credentials.env`.

### Evidence the account exists with PLEVEL 7

Read straight out of the running server's console output buffer (the
`RichEdit20A` child of the `SphereServer` window) after the three commands:

```
Account 'Admin': PLEVEL:7, BLOCK:0, IP:255.255.255.255, CONNECTED:2026/08/26 01:44:22, ONLINE:no
```

Corroborated by a differential test in `runtime/logs/sphere2026-08-26.log`.
`CAccounts::Account_OnCmd` returns `false` for an unknown account, and
`CServer::OnConsoleCmd` logs `unknown command` whenever a verb returns `false`:

```
01:44:unknown command 'ACCOUNT Admin'          <- BEFORE the account was created
01:45:unknown command 'ACCOUNT ZzNoSuchAcct'   <- control: a name that does not exist
```

and there is **no** such line for `ACCOUNT Admin` after creation, nor for
`ACCOUNT Admin PLEVEL 7`. Both succeeded.

Finally, the account survived a world save **and a subsequent server restart**
(the server was restarted at ~02:05 by another workstream; PID changed from
30864 to 26820). `runtime/accounts/sphereaccu.scp` now holds it on disk:

```
[Admin]
PLEVEL=Owner
PRIV=00
RESDISP=1
PASSWORD=<the 16 chars in local\dev\admin-credentials.env - verified identical>
FIRSTCONNECTDATE=2026/08/26 01:44:22
```

`PLEVEL=Owner` is level 7 written by name. Note that Sphere stores account
passwords **in plaintext** in this file; `runtime/accounts/` is gitignored.

### Login confirmed

`runtime/logs/sphere2026-08-26.log`, the full handshake plus character creation:

```
02:07:0:Client connected [Total:1]. IP='127.0.0.1'. (Connecting/Connected: 1/1).
02:07:0:Login for account 'Admin'. IP='127.0.0.1'. ConnectionType: ServerList.
02:07:1:Login for account 'Admin'. IP='127.0.0.1'. ConnectionType: CharList/Game.
02:08:1:Account 'Admin' created new char 'Observer' [063c]
02:08:1:Character startup for account 'Admin', char 'Observer'. IP='127.0.0.1'.
```

No `Bad Login`, no `BadVersion`, no encryption error — the settings file, the
client version, the no-encryption choice and the obfuscated password are all
correct as shipped.

---

## 2. Configuration: a second settings file, selected on the command line

The observer and the admin share one ClassicUO installation (`tools\ClassicUO\`)
but must not share a profile, or logging in as one would overwrite the other's
saved account.

**Chosen: a second settings file.** This build of ClassicUO (1.1.0.337) supports
exactly three command-line switches — `-settings`, `-profiler` and `-language`
(verified by extracting the argument-name string table out of `cuo.dll`).
`-settings <file>` makes ClassicUO load *and save* that file instead of
`settings.json`.

| Client | Config file | Selected by |
|---|---|---|
| Observer (plain player) | `tools\ClassicUO\settings.json` | default |
| **Admin (PLEVEL 7)** | `tools\ClassicUO\settings.admin.json` | `ClassicUO.exe -settings settings.admin.json` |

Verified empirically: launching `launch_admin.bat` started the client at
**01:47:28**, and `settings.admin.json` was rewritten at **01:47:45** — ClassicUO
normalises and re-saves whichever settings file it loaded — while
`settings.json` kept its earlier timestamp of **01:36:37**, untouched. A second
copy of the whole client directory was therefore not needed.

`settings.admin.json` is identical to the observer's file except:

| Key | Observer | Admin | Why |
|---|---|---|---|
| `username` | `Observer` | `Admin` | the privileged account |
| `password` | observer's | admin's, same encoding | see below |
| `autologin` | `false` | `true` | these credentials are known-good, so skip the click; the client still stops at the character list until a character exists |

Everything else — `ip`, `port`, `ultimaonlinedirectory`, `clientversion` `2.0.3`,
`encryption` `0`, `use_verdata` `true`, `last_server_name` `MyShard` — matches
the observer, and for the same reasons (`OBSERVER_CLIENT.md` §4).

Character profiles separate themselves automatically: ClassicUO stores them under
`tools\ClassicUO\Data\Profiles\<account>\<server>\`, so the admin gets its own
`Data\Profiles\Admin\MyShard\` the first time a character logs in.

### The password is stored obfuscated, not in plaintext

Same quirk as the observer (`OBSERVER_CLIENT.md` §5). ClassicUO stores:

```
"1-" + hex( password[i] XOR Environment.MachineName[i % MachineName.Length] )
```

The value in `settings.admin.json` was produced with that algorithm and
**verified two ways**: the algorithm reproduces the observer's already-working
stored ciphertext byte-for-byte from the observer's known plaintext, and decoding
the admin ciphertext returns the generated admin password exactly.

It is tied to this machine's computer name (`BARBAROSPC`). If the PC is renamed
or the file is copied elsewhere, blank the `password` field, launch, and type the
password once — ClassicUO will re-store it correctly.

---

## 3. Launching

```
C:\Projects\RevolutionOffline\tools\launch_admin.bat
```

which does, from inside `tools\ClassicUO\`:

```
ClassicUO.exe -settings settings.admin.json
```

It mirrors `launch_observer.bat`: validate that `ClassicUO.exe` and the settings
file exist, then start the client from its own directory. It never starts, stops
or restarts the server.

The observer and the admin can run **at the same time** — two separate accounts,
two separate TCP connections.

---

## 4. Manual first-login steps

`autologin` is on, so the client connects on its own. What is left is the part
that needs mouse input:

1. Make sure the Sphere server is listening on `127.0.0.1:2593`, and that the
   bots have been quiet for a few minutes (see §6 — this is the one thing that
   actually blocks a first login on this box).
2. Run `tools\launch_admin.bat`.
3. The client logs in as `Admin` and lands on the **character-selection
   screen**. Click **`New Character`** (as of 02:08 one character already
   exists on this account — see the naming note below).
4. Create the character:
   - **Name it something unmistakably staff, and do not reuse `Observer`.**
     Suggested: `Adminus`, `Overseer`, `Warden`, or simply `Admin`. The name is
     purely cosmetic to Sphere — no name grants privilege — but it is what every
     bot and every future human player will see over your head, and what appears
     in the server log lines (`Character startup for account 'Admin', char
     '<name>'`). Naming it `Observer` is actively confusing here: `Observer` is
     the separate **unprivileged** account and its character
     (`docs/OBSERVER_CLIENT.md`), so log lines and journal entries stop telling
     you which of the two you were looking at.
     *(A character called `Observer` was created on the `Admin` account at
     02:08 during setup. Delete it and make a differently-named one, or accept
     the ambiguity knowingly.)*
   - Stats and skills do not matter. PLEVEL is a property of the *account*, not
     of the character, so the character is an owner whatever it rolls.
   - **Starting city: choose `Yew`** (`The Empath Abbey`). That is Felucca
     **633, 858**, the tile the bots spawn at
     (`runtime/scripts/maps/map0/map0_starts.scp`). Any other city puts you a
     continent away from them.
5. Click the arrow to enter the world.

On later runs, steps 1-2 only: `autologin` plus a saved character take you
straight in.

If the login screen appears instead of the character list, the password field is
already filled — just click the gold arrow, then double-click `MyShard`.

---

## 5. Confirming the account really is privileged

The command prefix is `.` (`runtime/sphere.ini:230`, `CommandPrefix=.`).

**A — in game, one command that proves it outright.** With the character in the
world, type into the chat line:

```
.account Admin
```

It answers `Account 'Admin': PLEVEL:7, BLOCK:0, ...` in the system message area.
This is the strongest single check because it proves both halves at once:
`ACCOUNT` is a **PLEVEL 6** command (`runtime/scripts/spheretables.scp:326-336`)
*and* `CAccounts::Account_OnCmd` refuses to run below `PLEVEL_Admin`
(`CAccount.cpp:427-428`), so a player or a GM could not have run it at all — and
the line it prints states the level. It changes nothing.

**B — cheaper sanity checks.** `.info` and `.gm` are PLEVEL 2 commands
(`spheretables.scp:284-306`) and `.where` is PLEVEL 1 (line 272). `.info` opening
a dialog tells you staff commands work at all; it does *not* prove owner level.

**C — from the server console** (read-only, no world save, no client needed):

```
ACCOUNT Admin
```

prints the same `PLEVEL:7` line into the server console window. This is the
authoritative answer. See §1 for how to reach the hidden console window.

**D — after the next world save**, `runtime/accounts/sphereacct.scp` contains an
`Admin` account block with `PLEVEL=07`. Purely a persistence check.

> Do **not** confirm privilege by creating items or granting skills, especially
> not anywhere near a bot. `.account Admin` and `.where` change nothing.
>
> Avoid bare `.plevel` — `PLEVEL` is a *writable* character property and a
> PLEVEL 7 command (`spheretables.scp:338-348`); typing it with no argument
> risks setting a level rather than reporting one.

---

## 6. Known obstacle: the shard's own IP flood protection

The first `launch_admin.bat` run reached the server and was refused:

```
01:47:ERROR:Blocked connection from '127.0.0.1' [IP history: blocked=0, ttl=300, pings=4, connecting=0, connected=1].
01:47:ERROR:Reject reason: MaxConnectRequestsPerIP reached 50/50.
01:47:ERROR:Outcome (default): requested kick + IP block allowed by script 'f_onserver_connectreq_ex'.
```

This is **not** an admin-client fault. It is `runtime/sphere.ini:1141`
`MaxConnectRequestsPerIP=50` together with `NetTTL=60*5` (line 1147). Every
client on this box shares one source IP (`127.0.0.1`), and a long bot-testing
session had already spent the 50 allowed connection requests. That counter does
**not** decay — the ini comment says it "resets only after `<NetTTL>` seconds
elapsed since last connection attempt" — so while the bots keep reconnecting, the
window never reopens.

What to do, in order of preference:

1. **Wait.** Stop the bots, leave the server alone for five quiet minutes, then
   launch the admin client. This is the zero-change fix and it is exactly what
   the setting is for.
2. If this shard is going to be developed with many local clients permanently,
   raising `MaxConnectRequestsPerIP` in `sphere.ini` is the real fix — but that
   is a deliberate server-configuration decision for the operator, and it was out
   of scope here. `sphere.ini` was left untouched.

Symptoms: the client opens a TCP session to `127.0.0.1:2593` and is dropped
without logging in; the server log shows the three lines above and **no**
`Login for account 'Admin'`.

The mechanism, from `server/Source-X/src/network/`: `m_iConnectionRequests`
"doesn't decay, it's forgotten when the IP is forgotten"
(`CIPHistoryManager.h:26`). The IP entry is only forgotten when its TTL runs
below zero (`CIPHistoryManager.cpp:105-108`), the TTL only decays while the IP
is **not** blocked and has no live or pending connection
(`CIPHistoryManager.cpp:80-93`), and every fresh attempt resets it back to
`NetTTL` (`:19-20`). So the shortest path really is: block expires (300 s, the
script's default `BAN_TIMEOUT`, `runtime/scripts/core/serv_triggers.scp:203`),
then a further 300 s in which nothing at all connects from `127.0.0.1`. A
retry inside that window restarts the clock.

**In practice it resolved on its own**: the server was restarted at ~02:05 by
another workstream, which discards the in-memory IP history entirely, and the
admin client logged in on the next attempt. A restart is the brute-force fix if
you own the server process — but the `Admin` account only survives a restart
because it had already been flushed to `sphereaccu.scp` by the 01:58 autosave.

---

## 7. Files created / changed

| File | Change |
|---|---|
| `tools\ClassicUO\settings.admin.json` | **created** — the admin ClassicUO profile (§2) |
| `tools\launch_admin.bat` | **created** — launcher; passes `-settings settings.admin.json` |
| `docs\ADMIN_CLIENT.md` | **created** — this document |
| `local\dev\admin-credentials.env` | **created** — account name + password (gitignored, never committed) |
| `docs\OBSERVER_CLIENT.md` | **edited** — one cross-reference block pointing here; no other change |

Server side: the `Admin` account was created **in the running server's memory**
by two `ACCOUNT` console commands. No file under `server/Source-X/`,
`runtime/scripts/` or `runtime/sphere.ini` was modified. `tools/ClassicUO/` is
already covered by `.gitignore`, so `settings.admin.json` is not committed and no
password can leak through git — no `.gitignore` change was needed. Nothing under
`bot/uo-client/` was touched, and no git command was run anywhere.
