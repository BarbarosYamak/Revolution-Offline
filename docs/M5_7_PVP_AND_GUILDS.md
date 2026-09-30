# M5.7: PvP (PK ambush, anti-PK, the alarm) and where guilds stand

Date: 2026-09-29. **STATUS: BUILT AND TESTED OFFLINE, NOT YET RUN AGAINST SPHERE.**

Owner rulings followed:
* "PK activity = ambush AI only";
* "Head Hunters: not needed yet";
* "no fixed disengage": retreat is a per-fight judgement.

## Roles (`bot/uo-client/include/uo/pvp.h`, `src/life/runner/Pvp.cpp`)

| Role | Who | What it does |
|---|---|---|
| **PK** | the `pk` profession | Roams mining and lumber grounds outside the guards, and ambushes a lone worker when the opening beats its nerve. |
| **Anti-PK** | a hunting fighter whose nerve (profession plus persona) is at least 0.55 | Engages **lawful** targets it sees (reds, criminals, guild-war enemies) and answers a "PK var! yardim!" alarm heard within 18 tiles. |
| everyone | victims | Shout **"PK var! yardim!"** when a player attacks them (once a minute; never at a sparring partner). |

**How the PK picks a victim:**
* only innocents or neutrals;
* never inside the guards, and never from inside them
  (`GuardsInstantKill=1`);
* never a guild ally or a friend;
* never when hurt or short of bandages;
* a worker with a pickaxe or hatchet in hand scores higher;
* an armed fighter, a mage or a mounted player scores lower;
* each witness beside the victim scores −0.3;
* anyone who beat the PK before is left alone;
* the bar it must clear is 0.85 − 0.4 × nerve.

**How the anti-PK picks a target:**
* lawful targets only (noto 4/5/6);
* a rescue (the target is attacking a blue) scores higher;
* friends nearby score higher;
* other reds nearby score lower;
* characters with nerve under 0.45 never hunt reds.

**The fight:** the bot opens exactly like a monster hunt (attack request,
then close in), and SURVIVE owns the exchange once it is answered. The
break-off rule (`pvp::ShouldBreakOff`) weighs:
* nerve;
* who is winning the race;
* how outnumbered the bot is;
* bandages;
* a floor at 15% health that no one fights past.

**The server decides every consequence:**
* criminal and murder flags;
* guards;
* looting as a crime;
* murder decay (8 h).

The bot never touches notoriety itself.

`--no-pvp` (client) and `fleet_ramp.py --no-pvp` turn all player fighting
off for a run.

## Guilds: blocked on the server

The runtime in this repo has **no guild stones**: the Scripts-X core that
holds `t_stone_guild` is not checked in. `OF_EnableGuildAlignNotoriety` is
also off. Revolution clearly had guilds (guild runebooks from guild stones,
07.01.2012). The bot side is ready for them:
* guild-green (noto 2) players count as allies and are never victims;
* guild-war enemies (noto 5) are lawful targets.

Creating, joining and declaring war need the stone's menus on this tree,
which are UNKNOWN. That is not built rather than faked.

## Verified

* `pvp`: 23 checks. They cover:
  * victim choice (worker vs armed, witnesses, guards both ways, reds,
    allies, hurt, timid vs bold, grudges);
  * anti-PK (rescue, innocents never, gangs, cowards, guild war);
  * break-off as a judgement;
  * the alarm, with no false alarm from any small-talk line.
* `life_world_harness`: 415 checks, 0 failures. With real packets:
  * a PK sees a lone miner and sends a 0x05 attack;
  * two witnesses remove the opening;
  * a victim that gets a real 0x2F swing from a red shouts "PK var! yardim!".
* Full ctest under Wine: 59/60; the known missing TSV is the one failure.

## Known gaps

* A red PK must still bank somewhere. Its home cities are Buccaneer's Den
  (unguarded) and Britain, and an errand into a guarded town can get it
  killed by guards. This needs a "murderers avoid guarded towns" routing rule.
* Victims do not yet recall away. The mage flee spell is a later step.
