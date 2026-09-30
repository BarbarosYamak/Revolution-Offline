# M5.14: Guilds: awareness built, membership blocked on the server

Date: 2026-09-30.

## Built

* **Guild tags.** The client reads the guild abbreviation the server shows in
  a mobile's name label: "Kemal [RVL]" gives `RVL`
  (`Client::MobileGuildTag`). Each session a bot single-clicks itself once to
  learn its own tag.
* **Guildmates.** Someone carrying our tag, or the server's guild-green
  notoriety (2), is a guildmate (`Runner::GuildMate`). A guildmate is:
  * trusted (trust 4), so they are greeted as a friend and picked first for
    parties;
  * a **PvP ally**: never a PK victim, and counted among our friends in a
    fight.
* **Guild wars.** Orange (notoriety 5) war enemies were already lawful targets
  for anti-PK fighters (M5.7).
* The tag is in `status.json` (`guild`).

## Blocked: founding and joining a guild

This runtime has no guild stone scripts; the Scripts-X core that holds
`t_stone_guild` and the guild deed is not in the repo. `OF_EnableGuildAlignNotoriety`
is off. How a player founds a guild, recruits, accepts candidates and declares
war on this tree is therefore **UNKNOWN**, and not built rather than invented.
Revolution clearly had guilds: guild runebooks came from guild stones
(07.01.2012), and there is guild-war evidence in the Bible §29.

**To unblock:** commit your `runtime/scripts` folder (the guild stone deed and
its menus). A bot can then buy a guild deed, place the stone, and recruit
friends and family through the stone's own menu.

## Verified

`life_world_harness`: 459 checks, 0 failures.
* Tags are read from real 0x1C labels for us, a guildmate and another guild.
* The same tag makes someone a guildmate; another guild's tag does not;
  guild-green always counts.
