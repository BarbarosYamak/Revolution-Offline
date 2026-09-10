# Living Fleet roadmap

## Purpose

Turn independent autonomous characters into a persistent, believable UO
population. Each character keeps its own identity and judgement; the fleet
layer supplies long-term direction, population balance, and shared economic
context. It never issues movement or combat packets directly.

## Foundation already present

- A stable identity, home city, profession, build, skills, memories, bank
  stock, prices, ledger, spellbook knowledge, orders, and session history are
  saved per character in `PersistentState`.
- The runner already resolves immediate needs into survival, work, training,
  trade, travel, social, and recovery goals.
- Characters can observe and respond to player orders, trade, sparring, mounts,
  local resources, vendor stock, and danger.

The milestones below extend that foundation. A fleet policy may suggest an
objective, but the character's survival and local evidence always win.

## Gate 0 — reliable individual lives

Finish the current archetype validation before adding population pressure.

Each archetype must show, in a focused live run:

- safe login, useful work, banking, and acknowledged logout;
- no repeated goal spin or packet retry loop;
- its core production, combat, or service loop produces measurable progress;
- recoverable supply, navigation, and danger failures are bounded and reported.

The fleet report must separate initial state from in-session outcomes: deaths,
kills, skill gains, production, NPC sales, player trades, committed orders, and
delivered orders.

## Milestone 1 — character aspirations

Give every character one durable aspiration in addition to immediate goals.
Examples: become a capable Minoc smith, build a reagent reserve for a mage,
sell fish from Vesper, train a weapon build, or establish a tailoring stock.

Implementation:

- Add an `Aspiration` record to the character's persistent state: kind, target,
  progress, chosen home/region, and a bounded reassessment date.
- Translate each aspiration into ordinary existing needs rather than a separate
  action system. A smith aspiration may favour mining, smelting, crafting,
  selling, and Blacksmith training; survival still preempts all of them.
- Log progress as observable events: skill milestones, accumulated gold, stock
  produced, orders delivered, or hunting-ground proficiency.

Acceptance gate: after five resumed sessions, each sampled character retains
its aspiration, makes observable progress toward it, and can still respond to
danger, stock shortages, and player interaction.

## Milestone 2 — population composition and regions

Maintain a planned mix of archetypes and spread them across valid cities and
resource areas. This prevents a fleet of duplicate workers crowding one bank or
mine.

Implementation:

- Create a fleet roster with desired ranges per archetype, city, and activity.
- Assign new characters to an open roster slot using their profession's valid
homes. Keep specialist anchors such as Minoc miners and lumberjacks, while
spreading coastal fishers and town-based casters.
- Add a local crowd rule: when an area is already busy, choose another valid
work site or an alternate home-city service location.
- Report actual versus planned population by archetype, city, and active goal
family.

Acceptance gate: a 120-character run stays within each roster range and no
single city or resource entrance holds an avoidable majority of a profession.

## Milestone 3 — shared economy and contracts

Let the fleet create useful supply chains without granting characters hidden
inventory knowledge.

Implementation:

- Maintain a fleet ledger built only from observed sales, bank stock volunteered
by its owner, completed trades, and open orders.
- Publish shortages as opportunities: miners supply ingots, lumberjacks supply
boards, tailors supply cloth and bandages, scribes supply scrolls, and combat
characters buy consumables.
- Reserve stock when an order is accepted; delivery and secure trade settle the
reservation before surplus may be sold elsewhere.
- Keep NPC purchasing as the final fallback after player supply, self-production,
and reasonable collection routes have failed.

Acceptance gate: a mixed-archetype run completes at least one full observed
producer-to-consumer chain, including reservation, delivery, and settlement.

## Milestone 4 — daily rhythms and social lives

Make character activity look like a population instead of a synchronized test
batch.

Implementation:

- Give each character a persisted schedule preference: work, train, resupply,
trade, socialise, or rest windows with randomized but bounded start times.
- Form compatible sparring pairs, hunting parties, and market meetings only
when both participants are ready and consent through existing social protocol.
- Vary safe idle locations around a town, service routes, and work sites rather
than placing every character inside the same bank tile.
- Keep mounts with their owner and move groups through normal commands such as
`all follow me`.

Acceptance gate: session telemetry shows varied concurrent goal families and
completed social activities without a rise in deaths, stuck travel, or unwanted
combat.

## Milestone 5 — fleet director and operations dashboard

Introduce a coordinator that allocates aspirations and contracts, monitors the
population, and intervenes only through legal high-level suggestions.

Implementation:

- The director reads per-character summaries and the shared observed ledger.
- It assigns only a next aspiration, preferred region, or contract; each Runner
still chooses its own immediate goal from live observations.
- Detect unhealthy patterns: idle concentration, failed production chains,
overcrowded work sites, repeated deaths, unfilled contracts, and professions
below the planned range.
- Produce a per-archetype and per-region dashboard covering active goals,
skill progression, production, gold flow, trade success, deaths, and time idle.

Acceptance gate: a multi-session 120-character fleet sustains its target
archetype mix, spreads across regions, completes player supply chains, and
recovers from individual failures without a manual fleet reset.

## Operating rules

- Server observations remain authoritative. The director never assumes stock,
spell availability, safety, or a reachable route.
- Survival, healing, recovery, and session shutdown always override population
policy.
- Every cross-character action needs an observable protocol and a bounded
failure path.
- Fleet metrics measure outcomes, not just process starts.
