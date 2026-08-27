# Observer Client (ClassicUO)

> **SUPERSEDED for day-to-day use.** The observer is now **`uo_viewer`**, a
> native client built from this project's own code
> (`bot/uo-client/src/viewer/ViewerMain.cpp`). It logs in through the same
> `uo::Client` the headless bots use, so it is a real player on the wire, and it
> routes every tiledata / art / hue / anim lookup through
> `bot/uo-client/include/uo/safe_graphics.h` — which is why it survives the
> out-of-era graphics that killed ClassicUO on this shard (see
> §10 *"Client connects but nothing renders"* and the `IndexOutOfRangeException`
> on the unicorn mount item `0x3EB4`, a ship "prow" in Revolution's Renaissance
> tiledata). Run it with:
>
> ```
> cd bot\uo-client\build-m1
> uo_viewer.exe                       # account from local\dev\bot-credentials.env
> uo_viewer.exe --user revolutionbot02
> uo_viewer.exe --audit-only          # graphics crash-proofing self-test, no network
> ```
>
> Esc logs out cleanly (`0xD1` + socket close). Everything below still documents
> the ClassicUO setup, which is kept as a cross-check reference — the connection
> settings, client data directory, client version and encryption setting in §3–§4
> are what `uo_viewer` uses too.

Date: 2026-08-26.

Purpose: a **graphical, human-driven UO client** that connects to the same local
SphereServer the headless bots use, so a person can *watch* the bots play in the
world instead of reading packet logs.

The observer is a normal player. It has no special powers, no GM privileges, and
no ability to inspect server state. It sees exactly what any human player would
see from where its character is standing.

> For the **privileged** client — the `Admin` account at `PLEVEL 7` (Owner),
> launched by `tools\launch_admin.bat` with its own
> `tools\ClassicUO\settings.admin.json` — see **`docs/ADMIN_CLIENT.md`**. It
> reuses this document's ClassicUO build, client data directory, client version,
> encryption setting and troubleshooting table unchanged. The two clients are
> separate accounts and can run at the same time.

**Zero server modifications were made for this.** Nothing under
`server/Source-X/` or `runtime/scripts/` was changed. Source-X was read for
diagnosis only. The running `SphereSvrX64_nightly.exe` was never stopped,
restarted, saved, or sent a console command.

---

## 1. Why ClassicUO

| Option | Verdict |
|---|---|
| **ClassicUO** | **Chosen.** Open-source, actively maintained C# reimplementation of the 2D client. Reads plain `.mul` data (which is all Revolution ships — there are no `.uop` files). Supports `verdata.mul` patching, which Revolution relies on. Configurable client version and configurable encryption, including *no* encryption. Runs on Windows x64 with no UO installation registry keys. |
| `Revolution.exe` / `client.dll` (the shipped Revolution client) | Rejected. `Revolution.exe` is not the game client — it is `WCPLuncher.exe`, the "Wild Anti Cheat" launcher/updater (`CompanyName: Wild Anti Cheat`). `client.dll` is the real UO 2D client renamed. Both are third-party anti-cheat-wrapped binaries that phone home to the updater; per `REVOLUTION_CLIENT_INVENTORY.md` they are inventoried but must never be run. |
| OrionUO | Not needed. ClassicUO connected and logged in on the first correctly-configured attempt (evidence in §7). Keep OrionUO as a fallback only if a future rendering problem is traced to ClassicUO specifically. |

---

## 2. Install location

| Thing | Path |
|---|---|
| ClassicUO binaries | `C:\Projects\RevolutionOffline\tools\ClassicUO\` |
| ClassicUO executable | `C:\Projects\RevolutionOffline\tools\ClassicUO\ClassicUO.exe` |
| ClassicUO config | `C:\Projects\RevolutionOffline\tools\ClassicUO\settings.json` |
| Source zip | `C:\Projects\RevolutionOffline\local\downloads\ClassicUO-win-x64-release.zip` |
| Launch script | `C:\Projects\RevolutionOffline\tools\launch_observer.bat` |
| Observer credentials | `C:\Projects\RevolutionOffline\local\dev\observer-credentials.env` |

Build: **ClassicUO 1.1.0.337** (`main-release`, win-x64), commit
`ac6163efe1af6218368026bd67ff4cde54071b96`, built 2026-07-31.

The distribution is a .NET Framework 4.7.2 launcher (`ClassicUO.exe`) plus a
NativeAOT-compiled `cuo.dll` and FNA/SDL3/Vulkan native libraries. It ships **no
`Data/` folder**; ClassicUO writes `Data\Client\*.txt` (cave/chair/containers/
lights/tree/vegetation definitions) itself on first run.

`tools/ClassicUO/` is listed in the project root `.gitignore` — the binary
client is redownloadable and is not committed. The committed artifacts are
`tools/launch_observer.bat` and this document.

---

## 3. Client data directory

```
C:\Projects\RevolutionOffline\local\revolution-client
```

This is the Revolution UO client data extracted in M0 (see
`REVOLUTION_CLIENT_INVENTORY.md`). **Do not point ClassicUO at any other UO
installation** — the shard's `verdata.mul` patches art, tiledata and statics,
and the server loads the same patched files. Client and server must agree.

ClassicUO loads it cleanly:

```
Ultima Online installation folder: C:\Projects\RevolutionOffline\local\revolution-client
Client version: 33555200
Protocol: CF_RE
...
>> PATCHING WITH VERDATA.MUL
<< PATCHED.
Files loaded in: 471 ms!
```

`33555200` = `0x02000300` = client version 2.0.3.0. `CF_RE` = the Renaissance
protocol family.

---

## 4. Connection settings

Confirmed from `runtime/sphere.ini`:

| Setting | Value | Line | Meaning |
|---|---|---|---|
| `ServIP` | `127.0.0.1` | 21 | login endpoint |
| `ServPort` | `2593` | 24 | binds `0.0.0.0:2593` |
| `UseCrypt` | `1` | 193 | encrypted clients allowed |
| `UseNoCrypt` | `1` | 196 | **unencrypted clients also allowed** |
| `AccApp` | `2` (Free) | 162 | unknown account auto-created on first login |
| `ClientVersion` | commented out | 190 | **any supported client version may connect** |
| `MaxCharsPerAccount` | `5` | 171 | observer may hold up to 5 characters |
| `LocalIPAdmin` | `1` | 168 | see the warning in §5 |

`[SERVERS]` (lines 1250-1253) advertises a single shard named `MyShard` at
`127.0.0.1:2593`.

### Encryption

**Disabled** (`"encryption": 0`). `UseNoCrypt=1` means Sphere accepts a plaintext
client, and the headless bots already log in unencrypted (they send the raw seed
`0x7F000001` in plaintext). Using no encryption keeps the observer on exactly the
same wire format as the bots, which makes packet-level comparison meaningful.

### Client version

**`2.0.3`**, chosen to match the actual Revolution client binary, not guessed
from the data shape:

- `local\revolution-client\client.dll` is the genuine UO 2D client, renamed.
  Its PE `TimeDateStamp` is **2000-10-18**, and the only version string it
  contains is **`2.0.3`**.
- Its string table knows `Trammel`, `cliloc`, `verdata.mul` and `UseVerData`,
  but has no `Ilshenar` / `Malas` — i.e. Renaissance era, pre-LBR/AOS.
- This matches the value Sphere itself ships commented out as its default
  (`//ClientVersion=2.0.3`, line 190) and is adjacent to the `2.0.7` the
  headless bots report in their `0xBD` packet.

Sphere accepted `2.0.3` with no `BadVersion` error, so no neighbouring versions
had to be tried. Note that `tiledata.mul` is 1,036,288 B — the pre-7.0.9 32-bit
flag format — so the client version **must stay below 7.0.9** regardless.

---

## 5. The Observer account

- Account name: **`Observer`**
- Password: in `local\dev\observer-credentials.env` (16 characters; `local/` is
  gitignored, and the password is never written into any committed file)
- Privilege: **normal player**. No GM, no admin, no elevated `PLEVEL`. Nothing
  was done to raise it.

Because `AccApp=2` (Free), the account did not need to be created by hand — it
was created by the act of logging in with it, and the password supplied on that
first login became the account password. **This has already happened** (see §7),
so the account now exists on the running server.

> **Password length matters.** Sphere truncates stored passwords to 16
> characters (`MAX_ACCOUNT_PASSWORD_ENTER`, `src/common/sphereproto.h`). A
> longer password could never log in again. The generated password is exactly
> 16 characters.

> **Persistence.** Sphere holds accounts in memory and flushes them to
> `runtime/accounts/sphereacct.scp` on a world save. `SavePeriod=20`, so the
> `Observer` account persists automatically at the next autosave. If the server
> were killed before any save after account creation, the account would simply
> be recreated by the next login with the same credentials.

> **`LocalIPAdmin=1` caveat.** `runtime/sphere.ini:168` says "local ip is assumed
> to be the admin". This is a property of the *server configuration*, not
> something granted to this account, and it was deliberately left alone — the
> constraint was zero server modifications. The `Observer` account itself was
> given no privilege flags. If the shard should not treat loopback connections
> as staff, that is a separate, deliberate `sphere.ini` decision for later; it
> equally affects the bot accounts today.

### ClassicUO stores the password in an obfuscated form

This is the one non-obvious part of the setup. `settings.json`'s `password`
field is **not** plaintext. ClassicUO (`src/ClassicUO.Utility/Crypter.cs`)
stores it as:

```
"1-" + hex( password[i] XOR Environment.MachineName[i % MachineName.Length] )
```

Consequences:

1. Writing the plaintext password into `settings.json` **does not work** — the
   client XOR-decrypts it into binary garbage and the server rejects the login
   (this was observed; see §7).
2. The stored value is **tied to this machine's computer name**. If the machine
   is renamed, or the config is copied to another PC, the saved password becomes
   invalid and must be retyped once in the client.

The value currently in `settings.json` was computed with this algorithm and
verified against a ciphertext ClassicUO produced itself, so no retyping is
needed on this machine.

---

## 6. Configuration file (exact contents)

**File edited: `C:\Projects\RevolutionOffline\tools\ClassicUO\settings.json`**

ClassicUO takes its entire configuration from this one file, read from its own
directory. There are no command-line arguments to pass. Final contents, password
redacted:

```json
{
  "username": "Observer",
  "password": "<REDACTED - derived from local/dev/observer-credentials.env, see §5>",
  "ip": "127.0.0.1",
  "port": 2593,
  "ignore_relay_ip": false,
  "ultimaonlinedirectory": "C:\\Projects\\RevolutionOffline\\local\\revolution-client",
  "profilespath": "",
  "clientversion": "2.0.3",
  "lang": "ENU",
  "lastservernum": 1,
  "last_server_name": "MyShard",
  "fps": 60,
  "screen_scale": 1,
  "window_position": null,
  "window_size": null,
  "is_win_maximized": true,
  "saveaccount": true,
  "autologin": false,
  "reconnect": false,
  "reconnect_time": 1000,
  "login_music": false,
  "login_music_volume": 0,
  "fixed_time_step": true,
  "run_mouse_in_separate_thread": true,
  "force_driver": 0,
  "use_verdata": true,
  "maps_layouts": "",
  "encryption": 0,
  "plugins": [],
  "files_override": null
}
```

Notable choices:

| Key | Value | Why |
|---|---|---|
| `encryption` | `0` | no encryption; allowed by `UseNoCrypt=1`, same as the bots |
| `clientversion` | `"2.0.3"` | matches `client.dll`; see §4 |
| `use_verdata` | `true` | Revolution ships a 7,194,036 B `verdata.mul` that patches art/tiledata/statics; the server loads it too, so the client must as well |
| `saveaccount` | `true` | keeps `username`/`password` in this file so login is one click |
| `autologin` | `false` | **deliberate.** With autologin the client dives straight at the server the moment it starts, which makes a bad password or a downed server loop noisily. Flip it to `true` once a character exists and you want to land in-world with no clicks at all. |
| `login_music` | `false` | the observer is for watching bots; silence by default |
| `reconnect` | `false` | do not fight the server if it goes away |
| `lang` | `"ENU"` | see the cliloc note in §9 |

ClassicUO rewrites and normalises this file on exit and on login (it drops keys
it does not know and adds its own defaults), so the shape above is this build's
authoritative schema. Do not hand-add keys from other ClassicUO versions.

---

## 7. Verification performed

### TCP

`SphereSvrX64_nightly.exe` (PID 30864) confirmed listening, without touching it:

```
netstat -ano | findstr 2593
  TCP    0.0.0.0:2593    0.0.0.0:0    LISTENING    30864
```

### Full login handshake — SUCCEEDED

`runtime/logs/sphere2026-08-26.log`:

```
00:54:8:Client connected [Total:2]. IP='127.0.0.1'. (Connecting/Connected: 1/2).
00:54:8:Login for account 'Observer'. IP='127.0.0.1'. ConnectionType: ServerList.
00:54:9:Client connected [Total:2]. IP='127.0.0.1'. (Connecting/Connected: 1/2).
00:54:8:Client disconnected [Total:1]. Account: 'Observer'. IP='127.0.0.1'.
00:54:9:Login for account 'Observer'. IP='127.0.0.1'. ConnectionType: CharList/Game.
```

Matching ClassicUO log:

```
Start login to: 127.0.0.1,2593
Connecting to tcp://127.0.0.1:2593/
Connected!
Connecting to tcp://127.0.0.1:2593/      <- relay to the game server
```

That is the complete sequence: login server → account accepted and auto-created
→ shard list → relay → game server → **character list**. No `BadVersion`, no
`EncCrypt`/`EncNoCrypt`, no AuthID failure.

The client is left sitting on the **character-selection screen with zero
characters**. Creating a character needs mouse input (§8).

### The failure that was diagnosed and fixed

Before the password encoding was understood, the login was rejected:

```
00:48:WARNING:3:Bad Login 9. The account details entered are invalid (username or
password is too short, too long or contains invalid characters). This can
sometimes be caused by incorrect/missing encryption keys.
```

Sphere's error table (`src/game/clients/CClientLog.cpp:106-126` against the
`PacketLoginError::Reason` enum in `src/network/send.h:891-919`) makes code
**9 = `BadPassword`**, distinct from code 8 = `BadAccount`. The account name
`Observer` therefore arrived intact and passed every check — which independently
proves the **encryption setting and the client version were already correct**,
and isolated the fault to the password field alone. `src/game/clients/CClientMsg.cpp:3182-3188`
rejects a password whose `Str_GetBare` length differs from its raw length (i.e.
it contained non-bare bytes), and `:3222-3227` rejects an empty password with the
same code. Both were observed, and both were the XOR-obfuscation issue described
in §5. Once the password was stored in ClassicUO's own format, the login
succeeded on the next attempt.

### Not verified

The observer **has not yet seen the bots**, because no observer character exists
yet. That last step is manual (§8).

---

## 8. Manual steps for the user (first run only)

1. Make sure the Sphere server is running and listening on `127.0.0.1:2593`.
2. Run `C:\Projects\RevolutionOffline\tools\launch_observer.bat`.
   (A ClassicUO instance may already be running and parked on the character
   screen from the setup session — reuse it, or close it and relaunch.)
3. The login screen appears with **`Observer`** and the password already filled
   in. Click the **gold arrow** at the bottom right, or press **Enter**.
4. The shard list shows **`MyShard`**. Double-click it, or press **Enter**.
5. The character selection screen appears, empty. Click **`New Character`**.
6. Create the character:
   - Any name and appearance. `Observer` is a reasonable character name too.
   - Skills and stats do not matter — this character is only there to look
     around. Do not use it to interfere with the bots.
   - **Starting city: choose `Yew`** (`The Empath Abbey`). It is the first entry
     in `runtime/scripts/maps/map0/map0_starts.scp` and puts you at Felucca
     **633, 858** — the exact tile the bots spawn at. Any other city drops you
     a continent away from them.
7. Click the arrow to enter the world. You should be standing next to
   `RevolutionBot01` / `RevolutionBot02` whenever they are logged in.

On later runs only steps 1-2 and a click on the gold arrow are needed. If you
also want to skip that click, set `"autologin": true` in `settings.json` once
the character exists.

---

## 9. Start order

1. **Sphere** — `runtime\SphereSvrX64_nightly.exe`, working directory
   `C:\Projects\RevolutionOffline\runtime\`. Wait for
   `Startup complete (items=…, chars=…, accounts=…)` in
   `runtime\logs\sphere<date>.log`. *(A server is already running; do not start
   a second one — port 2593 would be taken.)*
2. **Bots** — start the headless clients from `bot\uo-client` as documented in
   `M1_BOT_ALIVE.md` / `M1_5_CLIENT_HARDENING.md`. Confirm
   `Login for account 'revolutionbot01'` / `…bot02` appears in the Sphere log.
3. **Observer** — `tools\launch_observer.bat`, then §8.

The order matters only in that the server must be up first. The observer can be
started and stopped freely at any time; it is just another client, and closing
it has no effect on the bots.

---

## 10. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Bad Login 9` in the Sphere log | Bad password. Almost always the ClassicUO XOR encoding (§5) — a plaintext password in `settings.json`, or a config copied from a machine with a different computer name. Fix: blank the `password` field, launch, type the password by hand once with `saveaccount: true`, and ClassicUO will store the correct encoded form. |
| `Bad Login 8` | Bad *account name*. Check `"username": "Observer"` and that no stray whitespace crept in. |
| `Bad Login 5` / `Invalid client version` | Only possible if `ClientVersion` is uncommented in `sphere.ini`. It is commented out (line 190), so any supported version is accepted. Do not "fix" this by editing the server. |
| `Bad Login 13` / `Unencrypted client not permitted` | `UseNoCrypt` was turned off. It is `1` (line 196). Alternatively set `"encryption"` back to `0` in `settings.json`. |
| `Bad Login 14` / `Another character on this account is already ingame` | Something else is already logged in on that account. Observed for `revolutionbot01` during testing when a bot run overlapped. Close the other client. |
| Login screen never appears / window is black | Check `stderr`; ClassicUO reports its graphics backend there (`SDL_GPU Driver: Vulkan`, `Vulkan Device: …`). If Vulkan fails, try `"force_driver": 1` (OpenGL) in `settings.json`. |
| `cliloc not found` in the ClassicUO log | Expected. See §11. Harmless. |
| Client connects but nothing renders / wrong art | Confirm `ultimaonlinedirectory` points at `local\revolution-client` and `"use_verdata": true`. Server and client must both apply `verdata.mul`. |
| Config changes seem to vanish | ClassicUO rewrites `settings.json` on exit and on login. Edit it while the client is **closed**, or your edits will be overwritten. |
| Can't find the bots in-world | They spawn at Felucca **633, 858** (Yew). If the observer character was created in another city, walk/recall there or make a new character with `Yew` as the starting city. |

---

## 11. Incompatibilities found

**One, and it is cosmetic.**

### Missing cliloc — the Revolution data set has no localisation file

```
searching for: 'Cliloc.ENU'
'Cliloc.ENU' not found. Rolled back to Cliloc.enu
cliloc not found: 'C:\Projects\RevolutionOffline\local\revolution-client\Cliloc.enu'
```

`local\revolution-client\` contains no `cliloc.*` file at all — consistent with a
2.0.3-era client, which predates the cliloc system being load-bearing.

- **Impact:** non-fatal. ClassicUO logs it as an error, continues loading, and
  runs normally. Any string the server sends as a *cliloc ID* rather than as
  text will render blank or as a `~1_val~` placeholder.
- **Why it does not matter here:** this shard is configured for the old era —
  `FeatureAOS=0` (no tooltips, line 905), `TooltipMode=1`, and a 2.0.3 client —
  so Sphere sends speech, names and system messages as plain ASCII. Nothing
  observed so far depends on cliloc.
- **Do not "fix" this** by dropping a cliloc file from some other UO client into
  the Revolution data directory. That would desynchronise client data from the
  server's `verdata.mul`-patched files and violates the "no generic UO
  assumptions" rule in `CLAUDE.md`.

### Non-issues, checked and cleared

- **No `.uop` files.** ClassicUO reads plain `.mul` and loaded all 55 files.
- **`tiledata.mul` in the pre-7.0.9 format.** Handled correctly at client
  version 2.0.3, which is far below the 7.0.9 cutover.
- **`map0.mul` at ML size (7168×4096) with a Renaissance-era client.** Harmless.
  The map block index is `(x/8) * 512 + (y/8)` regardless of map width, so the
  extra columns only extend the world's maximum X; they do not shift any
  existing tile. Sphere is configured `Map0=7168,4096` (line 95) to match.
- **`verdata.mul` patching.** Applied successfully (`>> PATCHING WITH
  VERDATA.MUL` … `<< PATCHED.`) with no `error while reading verdata.mul`.
- **Pre-existing server script errors** such as
  `Can't resolve <reportedcliver>` and `ITEMDEF has invalid ID=…` appear in the
  Sphere log for the bot sessions too. They are unrelated to the observer and
  were not introduced here.

### Automation limitation (not a product defect)

Driving ClassicUO's GUI from a script is only partly possible. `WM_KEYDOWN`
posted to the window works (it triggers the login and shard-list buttons), but
`WM_CHAR` text input does not register, and Windows refused to bring the window
to the foreground while another application owned the session — so `SendKeys`
could not be used to type into fields. This is why character creation is left as
a manual step rather than being scripted.

---

## 12. Files created / changed

| File | Change |
|---|---|
| `tools\ClassicUO\settings.json` | **created** — the entire observer configuration (§6) |
| `tools\launch_observer.bat` | **created** — launcher; validates the install then starts ClassicUO from its own directory |
| `docs\OBSERVER_CLIENT.md` | **created** — this document |
| `.gitignore` | **edited** — added `/tools/ClassicUO/` so the binary client is not committed |
| `local\dev\observer-credentials.env` | **created** — observer account name + password (gitignored; not committed) |
| `tools\ClassicUO\Data\Client\*.txt` | created *by ClassicUO itself* on first run |

Not touched: `server/Source-X/**` (read-only diagnosis only), `runtime/scripts/**`,
`runtime/sphere.ini`, `bot/uo-client/**`. No git commands were run anywhere.
