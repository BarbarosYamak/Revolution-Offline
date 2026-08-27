# Revolution Offline

Recreating the gameplay of the Turkish Ultima Online shard **RevolutionUO**
(target window 2008–2010) as a single-player world, and populating it with
autonomous simulated players that obey exactly the same rules a human does.

## The one rule everything else follows

**A bot must play the game.**

It connects over the real UO network protocol, as a real client, to an
authoritative SphereServer. It cannot grant itself skills, generate gold, create
items, teleport without a game mechanic, read global market data, or touch server
state directly. It trains skills the slow way, obeys the 700-point skill cap and
its own STR/DEX/INT, buys or gathers what it needs, and it dies and loses
everything it carried — full loot loss, like anyone else.

That constraint is the point of the project. Anything a bot achieves here, a
player could have achieved the same way.

## Architecture

| Piece | What it is |
|---|---|
| **Server** | [SphereServer Source-X](https://github.com/Sphereserver/Source-X) — the authoritative simulation. Never replaced or bypassed. |
| **Runtime** | A Sphere world built on the [Scripts-X](https://github.com/Sphereserver/Scripts-X) distro, plus this project's own `revolution/*.scp` |
| **Bot** | A headless C++ UO protocol client, forked from [xrip/uo-client](https://github.com/xrip/uo-client). Bots connect exactly like players. |
| **Client data** | Revolution's own client files. **Not distributed here** — copyrighted. |

## What is in this repository

```
docs/                 the milestone record — see below
bot/uo-client/        the headless client: src, include, tests, scenarios
runtime/sphere.ini    server configuration, with the reasoning for each change
runtime/scripts/
  revolution/         this project's own Sphere scripts
local/dev/*.ps1       operator tooling (run a scenario, read the console, soak)
```

Deliberately **not** here: the UO/Revolution client data (`mul/`), account files
(plaintext passwords — the shard runs `Md5Passwords=0`), world saves, logs, build
output, and the upstream Source-X / Scripts-X trees. Clone those from upstream.

## The documentation is the interesting part

`docs/` is a running record of what was proven, how, and — more often — what
turned out to be wrong. A few examples of the latter, because they are the useful
ones:

* **Corpse recovery had never worked.** `NoteDeath` was only called from the
  body-change handler, but Source-X emits no packet when the ghost body is
  swapped in, so the death location was never recorded. Invisible until the world
  became lethal enough to kill a bot.
* **Bots could not kill anything for ten runs.** Every attack was accepted and
  then discarded inside the same call, because Source-X objects are *born
  sleeping* and `Fight_CanHit` rejects a sleeping target before it checks range.
  The fix was `SectorSleep=0`.
* **A runebook displayed filled pages as "(empty)".** `QVAL` evaluates its
  condition numerically, so a page named "Britain" scored 0. The destination
  column beside it worked only because "1490,1555,30" parses as 1490. Travel had
  worked the whole time.
* **A 38-bot soak logged 99,290 travel failures** — a terminal-state hole where a
  journey ran off the end of its route without ever marking itself failed, and
  retried at 16 Hz forever. Now 14.
* **Five separate constants** were wrong because they were reasoned about instead
  of read from the shard's own data. A grey wolf here is `CHARDEF 0x0019`, not
  the `0x001C` generic UO documentation gives.

Where evidence is missing, the docs say **UNKNOWN** rather than inventing
behaviour. Spawn density is UNKNOWN. Reagent vendor sourcing rests on owner
testimony with no dated archive, and says so.

## Status

Milestones M0–M3.9 are recorded in `docs/`. M4 — autonomous character lifecycles
— has not started.

## Credits

Built on [Source-X](https://github.com/Sphereserver/Source-X) and
[Scripts-X](https://github.com/Sphereserver/Scripts-X) by the Sphere community,
and on [xrip/uo-client](https://github.com/xrip/uo-client) for the headless
protocol client. Ultima Online is a trademark of Electronic Arts; no client data
is distributed here.
