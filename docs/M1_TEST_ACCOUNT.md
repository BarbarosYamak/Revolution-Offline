# M1 — Test Account and Character

Date: 2026-08-25. How the development account and character are created, and why this is the supported path rather than a shortcut.

## Summary

| Item | Value |
|---|---|
| Account | `revolutionbot01` |
| Character | `RevolutionBot01` (slot 0) |
| Password | 16 random alphanumerics, stored only in `local/dev/bot-credentials.env` (gitignored) |
| Account created by | Sphere's own auto-creation on first login (`AccApp=2`) |
| Character created by | the client's ordinary `0x00` character-creation packet |
| Server data hand-edited | **none** |

## Account: Sphere creates it on first login

`sphere.ini` has `AccApp=2` (Free) from M0 (`M0_SPHERE_CONFIG.md`, change #3). On an unknown account name, `CClient::LogIn` sets `fAutoCreate` and `CAccounts::Account_FindCreate` creates a full account:

- `src/game/clients/CClientMsg.cpp:3230-3231` — `fAutoCreate = (m_eAccApp == ACCAPP_Free || GuestAuto || GuestTrial)`
- `src/game/clients/CAccount.cpp:189-211` — `Account_FindCreate(name, fAutoCreate)`

The password is taken from that first login: `CAccount::CheckPassword` sets the stored password when the account's password is empty (`src/game/clients/CAccount.cpp:941-946`).

Observed (Sphere console, first bot connection):

```
23:12:0:Client connected [Total:1]. IP='127.0.0.1'. (Connecting/Connected: 1/1).
23:12:0:Login for account 'revolutionbot01'. IP='127.0.0.1'. ConnectionType: ServerList.
```

### Password length: a real 16-character limit

The first attempt failed with `LOGIN DENIED (3)` and, server-side:

```
23:09:0: 'revolutionbot01' bad password
23:09:WARNING:0:Bad Login 3. The password entered is not correct.
```

Cause: `CAccount::SetPassword` truncates the stored password to `MAX_ACCOUNT_PASSWORD_ENTER` before storing it —

- `src/common/sphereproto.h:757` — `#define MAX_ACCOUNT_PASSWORD_ENTER 16 // client only allows n chars.`
- `src/game/clients/CAccount.cpp:1018-1020` — `minimum(MAX_ACCOUNT_PASSWORD_ENTER, enteredPasswordLength)`, then `Str_CopyLimitNull`

A 20-character password was therefore stored as its first 16 characters and never matched the 20 characters sent on the next login. **Development passwords for Sphere must be ≤ 16 characters.** The credentials file was regenerated with a 16-character password; the account that had been auto-created with the truncated password existed only in memory (it was absent from `runtime/accounts/sphereaccu.scp`) and was discarded by restarting the server — no save file was edited.

## Character: the client's own creation packet

Sphere has **no console verb that creates a character**. The account command tables are:

- `CAccounts::sm_szVerbKeys` — `ADD, ADDMD5, BLOCKED, HELP, JAILED, UNUSED, UPDATE` (`src/game/clients/CAccount.cpp:309-318`)
- `CAccount::sm_szVerbKeys` — `BLOCK, DELETE, KICK, TAGLIST` (`src/game/clients/CAccount.cpp:1624-1630`)

`ACCOUNT ADD` creates an *account* only. The supported way to add a character is what every real player does: the client sends the `0x00` create-character packet, and the server builds the character under its own rules. So the bot does exactly that (`--create-char`), and only when the account has no matching character.

Server-side handling:

- `PacketCreate::onReceive` — `src/network/receive.cpp:58-152` (104-byte packet; `PacketCreate(uint size = 104)`, `src/network/receive.h:41`)
- `PacketCreate::doCreate` — `src/network/receive.cpp:158-245`: refuses if already online, if a character is idling, or if the account is at its character cap; runs `f_onchar_create_init` / `f_onchar_create`; then `Setup_Start()`
- `CChar::InitPlayer` — `src/game/chars/CChar.cpp:1704-1810`: **the server decides everything.** It randomises all skills first, clamps each stat to 60 and their sum to 80, clamps each of the three skills to 50 and their sum to 100, validates the name, and places the character at a start location from the shard's own list.

The bot's requested values (`build::CreateCharacterParams`, `include/uo/builders.h`) are therefore *requests*, not grants: STR 30 / DEX 30 / INT 20 and Swordsmanship 50 / Tactics 30 / Healing 20, all within what the server allows. **No skill, stat, item or gold is set by the bot** — this satisfies the project's Core Principle.

Observed (Sphere console):

```
23:12:0:Account 'revolutionbot01' created new char 'RevolutionBot01' [01]
23:12:0:Character startup for account 'revolutionbot01', char 'RevolutionBot01'. IP='127.0.0.1'.
```

Bot side, same moment:

```
[0xA9] 5 slot(s) (populated 0):
[0x00] creating character 'RevolutionBot01' in slot 0
[0x1B] serial=0x00000001 body=0x0190 pos=(633,858,0)
[0x55] login complete — entering world
```

The character starts with an 8-item backpack that Sphere's own Scripts-X starting kit provides (`[0x3C] container contents: 8 item(s)`).

On every later run the character already exists and the ordinary selection path runs instead:

```
[0xA9] 5 slot(s) (populated 1):
  [0] RevolutionBot01
[ui] playing slot 0 ('RevolutionBot01')
```

## Credentials handling

`local/dev/bot-credentials.env` (gitignored through `/local/` in the project `.gitignore`):

```
UO_BOT_USER=revolutionbot01
UO_BOT_PASS=<16 random alphanumerics>
UO_BOT_CHAR=RevolutionBot01
```

- The client reads `UO_BOT_USER` / `UO_BOT_PASS` from the environment when `--user`/`--pass` are absent (`src/main.cpp`), so no credential is ever a build-time constant.
- Passwords are never written to the packet log: `Client::LogPacketRedacted` masks the 30-byte password field of `0x80` and `0x91` with `0xEE` before the hex dump (`src/Client.cpp`).
- Nothing about the account is committed. The repository holds no password, and `local/` is excluded.

## Reproducing from scratch

1. Ensure `sphere.ini` has `AccApp=2` and `UseNoCrypt=1` (M0 configuration).
2. Start `runtime/SphereSvrX64_nightly.exe` with `runtime/` as the working directory.
3. Create `local/dev/bot-credentials.env` with a **≤16-character** password.
4. Run the client once with `--create-char`; the account is created on login and the character on `0x00`.

No further preparation is needed: subsequent runs select the existing character by name.

## Note for later milestones

An administrator account (`PLEVEL 7`) still has to be made with the console command Sphere itself suggests at startup — `ACCOUNT ADD <login> <password>` followed by `ACCOUNT <login> PLEVEL 7`. M1 does not need one, so none was created.
