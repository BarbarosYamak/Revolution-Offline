# Writing bots — the behaviour runner

This is a practical guide to writing automation scripts (bots) for the client in
JavaScript: lumberjack, miner, fisher, cook, tamer — anything. You do **not** need
to touch C++. If you can write a few `async` functions, you can write a bot.

Read this top to bottom once; after that the **Skeleton** near the end is all you
copy. The reference bot is `scripts/js/lumberjack.js` — read it alongside this guide.

---

## 1. The big idea

A bot is a **list of behaviours in priority order**. Every tick the runner asks,
top to bottom: *"is this behaviour allowed to run right now?"* The first one that
says yes gets to run. If a **higher-priority** behaviour becomes allowed while a
lower one is running, the running one is **interrupted** and the higher one takes
over.

The interruption is **higher-priority-only**: a running step is *never* cut short
by something below it. If its own guard goes false and only a lower-priority
behaviour is now ready, the step is left to **finish on its own** (it returns, then
the runner picks the next). This is what lets a step safely change the state its own
guard reads — e.g. `bank` deposits until it's no longer overweight, `fight` clears
`this.threat` once the foe is dead — without preempting itself mid-task.

That's the whole model. It is intentionally *not* a full HonorBuddy-style behaviour
tree (no status-returning `Sequence`/`Selector` nodes, no `Running` bookkeeping) —
just a flat priority list of plain `async` functions. The priority list already *is*
a Selector; a Sequence is just `await a(); await b();` inside one step, because
async/await remembers your position across ticks for free. It reads top-down like a
list of rules (the lumberjack's actual order):

```
resurrect  (if I'm dead)        — most important
fight      (if something attacks me)
bank       (if I fled or I'm full)
eat        (if it's been a while and I have food)
chop       (otherwise)          — least important, the default job
```

The chopper runs at the bottom. The instant a monster attacks, `fight` outranks
`chop`, so the runner **cancels** chopping mid-swing and switches to combat. When the
fight ends, chopping resumes. You write each behaviour as if it were the only thing
happening; the runner handles the switching.

---

## 2. Two layers: the base class and the raw runner

There are two ways to wire a bot. **Almost always use the base class.**

- **`BehaviorScript`** (`lib/bot.js`) — the recommended base class. You `extend` it,
  implement `behaviors()`, and get the whole lifecycle, the `this.token` plumbing,
  movement (`walkTo`), inventory helpers, and opt-in skill mixins for free. This is
  what `lumberjack.js` uses and what the rest of this guide teaches.
- **`createBehaviorRunner(...)`** (`lib/bt.js`) — the raw engine the base class is
  built on. You only touch it directly for a throwaway bot with no shared state. See
  §9 for its shape; everything else here uses the base class.

---

## 3. The `BehaviorScript` base class

```js
class Lumberjack extends BehaviorScript {
    // tunables as class fields (override the base defaults)
    FOOD = ['bread', 'lamb'];
    BANK = { x: 1819, y: 2824 };

    // shared blackboard (sensing writes it, guards read it)
    threat = null;
    fleeing = false;

    constructor() {
        super();
        // wire sensing: threat meter + event handlers (see §6)
        this.threatMeter = createThreatMeter({ onDanger: (s) => this.engage(s) });
        Player.on('attacked', (s) => this.engage(s));
    }

    // THE priority list — highest first, an always-true guard last.
    behaviors() {
        return [
            { name: 'resurrect', when: () => Player.dead,           step: this.step('resurrect') },
            { name: 'fight',     when: () => Boolean(this.threat?.exists), step: this.sequence('fight', 'rest') },
            { name: 'bank',      when: () => this.fleeing || this.full(),  step: this.sequence('goToBank', 'depositLogs', 'withdrawGold', 'restock', 'rest') },
            { name: 'chop',      when: () => true,                  step: this.step('chop') },
        ];
    }

    // lifecycle hooks (all optional — override what you need)
    onStart()        { this.threatMeter.start(); }        // sync setup
    async onStartup() { await this.rest(); await this.restock(); } // async one-time prelude
    onStop()         { this.threatMeter.stop(); }
    onPreempt()      { Player.stop(); }                    // halt movement on interrupt

    // ... your job methods: chop(), goToBank(), depositLogs(), etc.
}

Object.assign(Lumberjack.prototype, BankSkill, SurvivalSkill);  // mix in skills
new Lumberjack().start();
```

### `behaviors()` — you must implement this
Return the priority list: an array of `{ name, when, step }`, highest priority
**first**. The base class throws if you don't override it.

- **`name`** — a unique label, used in lifecycle logs and to notice switches.
- **`when()`** — the guard. `true` if this behaviour may run *right now*. Called every
  tick, so keep it **cheap and side-effect-free** — just read state.
- **`step`** — the work, built with `this.step(...)` or `this.sequence(...)` (below).

### `this.step('method')` and `this.sequence('a', 'b', …)`
A step is really `(token) => Promise`. These two helpers build that wrapper for you
*and set `this.token`* so every method underneath can reach the current cancellation
token via `this.token` without threading it through every call.

- `this.step('chop')` → runs `this.chop()` as the step.
- `this.sequence('goToBank', 'depositLogs', 'rest')` → `await`s each method in order.
  It is plain sugar over `await goToBank(); await depositLogs(); await rest();` — a
  preempt partway throws `CANCELLED` and skips the rest, and each sub-step is logged
  via `onTransition`. **Reuse** is the point: `fight` and `bank` both end in `rest`,
  so `rest` is one method used by several sequences.

> A sequence is **not** a node type and has **no resume**. If a behaviour is
> preempted partway through its sequence and later re-selected, it starts again from
> the first method. Keep steps restart-safe (re-scan the world at the top; don't cache
> a half-finished plan).

### Lifecycle: `start()` → `stop()`
`new Bot().start()` does, in order:

1. build the runner from `behaviors()` (tick interval = `this.tickMs`, default **20 ms**);
2. `onStart()` — **synchronous** setup (start the threat meter, etc.);
3. `_bootstrap()` — run the async **`onStartup()`** prelude to completion, *then* start
   the tick loop;
4. return `this`.

`stop()` cancels the current token, stops the runner, and calls `onStop()`.

| Hook | When | Notes |
|---|---|---|
| `onStart()` | once, synchronously, before the loop | Sync only. Start sensing here. |
| `async onStartup()` | once, **awaited before the first tick** | Has a real `this.token`, so `walkTo`/`rest`/`restock` work. Runs **before** the runner ticks, so it **cannot be preempted** by a behaviour — keep it to short, safe prep (rest, restock), not open-ended work. Default no-op. |
| `onStop()` | on `stop()` | Stop sensing here (e.g. `threatMeter.stop()`). |
| `onPreempt()` | right after a step is cancelled from above | Physically halt the old step's action — almost always `Player.stop()` (the base default). |
| `onTransition(name, event)` | on every behaviour `start`/`step`/`preempt`/`done` | Logging hook. Override to silence/customise. Prints `[bt] ==> name` etc. by default. |

> **Why `onStartup` exists.** A one-time "rest + restock before the first job" is a
> *lifecycle* concern, not a behaviour — modelling it as a self-disabling entry in the
> priority list (a boolean guard that flips itself off) is a hack. `onStartup` runs it
> exactly once, with a token, before the loop, with no behaviour racing it. The
> trade-off is that combat can't interrupt it; that's why it's for short prep only.

### Mixins — opt-in skills
Domain skills are plain objects mixed onto the prototype after the class:

```js
Object.assign(Lumberjack.prototype, BankSkill, SurvivalSkill);
```

Every base helper and mixin method reads `this.token`, so they only work when called
from inside a step (or `onStartup`). See §5 for the full method reference.

---

## 4. Tokens — how a step gets interrupted

A `step` is a long-running `async` function. When the runner wants to switch away
from it, it must be able to **interrupt** that function — even if it's parked in the
middle of `await something()`. That's what the **token** does. The base class stores
it in `this.token` for you (set by `step`/`sequence`/`onStartup`).

The rule is simple:

> **Wrap every long `await` in `this.token.wait(...)` or `this.token.sleep(...)`.**

When the step is preempted, those wrapped awaits immediately throw a special value
called `CANCELLED`, which unwinds the function. You normally never catch `CANCELLED`
— you let it bubble up and the function just stops.

```js
async chop() {
    await this.token.sleep(500);                          // cancellable sleep
    const line = await this.token.wait(                   // cancellable await
        waitForJournal({ ms: 8000 })
    );
    // ... if we get here, we were NOT cancelled ...
}
```

### Token API

| Call | What it does |
|---|---|
| `token.wait(promise)` | Await `promise`, but throw `CANCELLED` the instant the step is preempted. A *normal* rejection of `promise` (e.g. a `goto` that failed) passes through unchanged. Use this for **every** await. |
| `token.sleep(ms)` | Cancellable sleep. Shorthand for `token.wait(delay(ms))`. |
| `token.check()` | Throw `CANCELLED` right now if already preempted. Call it at the top of a loop body for tight loops that don't otherwise await. |
| `token.retry(fn, tries?, intervalMs?)` | Run `fn`, retrying on throw up to `tries` times (default 3, so 4 attempts). `check()` runs before each attempt and the gap between them is a **cancellable** `sleep(intervalMs)` (default 250 ms). Never retries `CANCELLED`. |
| `token.onCancel(fn)` | Register cleanup to run when preempted (e.g. `() => Player.setWarMode(false)`). |
| `token.cancelled` | `true` once preempted. |

### The one piece of discipline: `goto`

`walkTo()` (the base movement helper) already handles this for you — prefer it. But
if you call `Player.goto()` directly, you must tell a preempt apart from a normal
failure. `Player.goto()` is a long C++ trip; on preempt the runner's `onPreempt`
calls `Player.stop()`, which makes the parked `goto` **reject** — same as a blocked
path rejects. So:

```js
try {
    await this.token.wait(Player.goto(x, y));
} catch (e) {
    if (this.token.cancelled) throw CANCELLED;   // we were preempted → unwind
    // otherwise: a normal failure (blocked) → handle it (try another cell, etc.)
}
```

### Why this matters (concrete)

A chopper is parked in `await this.token.wait(waitForJournal({ms:15000}))` waiting for
"logs in your backpack". A monster attacks. The threat meter sets `this.threat`. On
the next tick `fight.when()` returns `true`, outranks `chop`, so the runner cancels
the chopper's token. The `wait` throws `CANCELLED`, the chop function unwinds, the
fight begins — within one tick, not after the 15-second wait finishes. That
responsiveness is the entire reason tokens exist.

---

## 5. Built-in helpers and skills

Everything here is a method on the bot (`this.…`), available inside any step. Base
helpers come from `BehaviorScript`; the rest are mixins you opt into with
`Object.assign(MyBot.prototype, …)`.

### Tunable fields (override as class fields)
| Field | Default | Used by |
|---|---|---|
| `BANK_AT_WEIGHT` | 400 | `full()` when `Player.maxWeight` is unknown |
| `WALLET` | 200 | `withdrawGold()` default gold target |
| `EAT_INTERVAL_MS` | 60000 | your `eat` guard's re-hunger interval |
| `FOOD` | `[]` | `eatFood()` name substrings |
| `BANDAGE` | `'bandage'` | `bandageSelf()` name substring |
| `tickMs` | 20 | runner re-check interval |
| `BANK` | — (you set it) | `withdrawGold()` (bank coords) |

### Base helpers (`BehaviorScript`)
| Method | Use |
|---|---|
| `full()` | `true` when the backpack is heavy enough to bank (server `maxWeight − 20`, else `BANK_AT_WEIGHT`). |
| `hpFrac()` | Current HP as 0..1 (1 if max unknown). |
| `findInPack(name)` | First backpack item whose name contains `name`, or `undefined`. |
| `backpackCount(names)` | Total `amount` of backpack items matching any of `names` (string or array). |
| `await walkTo(target, opts)` | The one movement helper — see below. |

`await walkTo({x,y}, { adjacent?, range?, terrain? })`:
- `range > 0` — walk until within `range` tiles (retries, gives up after 8 attempts).
- `range === 0` (default) — step onto the target tile, or with `adjacent:true` onto
  the nearest of the 8 surrounding cells (stand *next to* a tree/vendor).
- `terrain:false` — drop the road/grass bias (direct route, e.g. between trees).
- Returns `true` on arrival, `false` if every candidate cell was unreachable.

### `BankSkill` (needs `this.BANK`, `this.WALLET`)
| Method | Use |
|---|---|
| `await openContainer(serial)` | Double-click a container, resolve with its `container_open`. |
| `await openBank()` | Say "bank", resolve with the bank box container. |
| `await depositStack(item, containerSerial)` | Drop one stack, retrying past the "must wait" cooldown. Returns `true` once it lands. |
| `await deposit(nameFilter)` | Deposit every backpack stack whose name contains `nameFilter` into the bank box (assumes you're in range). |
| `await withdrawGold(amount = this.WALLET)` | Walk to `BANK` and withdraw gold until the backpack holds `amount`. |

### `SurvivalSkill` (needs `this.BANDAGE`, `this.FOOD`, reads `this.threat`)
| Method | Use |
|---|---|
| `await bandageSelf()` | Apply one bandage to self; `true` if it started. |
| `await rest()` | Bandage until full HP, **bailing the instant `this.threat?.exists`**. |
| `await eatFood()` | Eat `FOOD` from the backpack until full; sets `this.lastAteMs`. |
| `await findVendor(title, coords)` | Double-click nearby mobiles to learn paperdoll titles; return the live handle whose title contains `title` (never a guild master), or `null`. |
| `await buyFrom(vendor, nameVariants, displayName, target)` | Approach, say "vendor buy", buy up to `target` of the matching items. |
| `await restockConsumables(map)` | For every consumable the backpack is **out of** (count 0), route to its vendor and buy up to `target`. No-op (no walking) when everything's stocked. |

`restockConsumables(map)` shape — keyed by item name:
```js
{
  bandage: { target: 20, coords: { x: 1914, y: 2806 }, title: 'healer' },
  bread:   { name: ['lamb', 'bread'], target: 10, coords: { x: 1854, y: 2793 }, title: 'provisioner' },
}
```
`name` is optional (defaults to the key); items sharing `title`+`coords` are grouped
into one stop, and stops are visited in shortest-route order from your position.

---

## 6. The blackboard pattern (sensing)

Behaviours don't talk to each other directly. Instead, **sensing** code writes a few
shared instance fields ("the blackboard"), and **guards** read them. Sensing runs
*independently of the runner* — set it up in the constructor / `onStart`.

```js
class Bot extends BehaviorScript {
    threat = null;       // current foe (a live Mobiles handle) or null
    fleeing = false;     // set true while escaping; bank reads it

    constructor() {
        super();
        this.threatMeter = createThreatMeter({ onDanger: (s) => this.engage(s) });
        Player.on('attacked', (s) => this.engage(s));   // a mob swung at us
        Player.on('combat',   (s) => this.engage(s));
    }
    engage(serial) {
        if (!serial || serial === Player.serial || this.threat?.exists) return;
        this.threat = Mobiles.get(serial);
        Player.requestStatus(serial);   // learn its HP
    }
    onStart() { this.threatMeter.start(); }
    onStop()  { this.threatMeter.stop(); }
}
```

A guard then just reads it: `when: () => Boolean(this.threat?.exists)`.

**Why events stay outside the runner:** some things must happen *instantly*,
regardless of what the runner is doing — e.g. a resurrection dialog freezes you until
you answer. Answer it in an event handler, not a behaviour:

```js
Player.on('dialog', (d) => {
    if (!/resurrect|come back to life/i.test(d.question)) return;
    const yes = d.options.find((o) => /^\s*yes/i.test(o.text)) || d.options[0];
    Player.dialogRespond(yes.index);
});
Player.on('resurrect_menu', () => Player.resurrect());
```

Rule of thumb: **a behaviour** = something that *occupies your body* over time
(walking, chopping, fighting). **An event handler** = an instant reflex (answer a
prompt, note a foe).

---

## 7. What you can call from a step

These globals are always available (no import needed).

### Player — your character (live, read-only state)
`Player.x/y/z`, `.facing`, `.running`, `.warMode`, `.alive`, `.dead`,
`.hp/.hpMax`, `.mana/.manaMax`, `.stam/.stamMax`, `.weight/.maxWeight`,
`.name`, `.serial`, `.equipment.backpack` (`{serial, items[]}`), `.dialog`.

Actions:
| Call | Use |
|---|---|
| `await Player.goto(x, y[, z][, {terrain}])` | Walk there. Resolves on arrival, rejects on abort. Prefer `this.walkTo(...)`. |
| `Player.use(target)` | Double-click an item by serial / graphic id / name. |
| `Player.doubleClick(serial)` | Raw `0x06` double-click; on an NPC opens its paperdoll → `paperdoll` event. |
| `Player.equip(target)` | Wear an item (layer from tiledata). |
| `Player.target(serial)` / `Player.target(x,y[,z][,graphic])` | Answer a target cursor (object / ground / static). |
| `Player.say(text)` | Speak a line ("bank", "vendor buy", …). |
| `Player.drop(target, containerSerial)` | Move a bag item into a container. |
| `Player.take(serial, amount)` | Take `amount` from an open container into the backpack. |
| `Player.containerItems(serial)` | Items in an open container → `[{serial,name,amount,…}]`. |
| `Player.attack(serial)` / `Player.setWarMode(on)` | Combat. |
| `Player.requestStatus(serial)` | Ask for a mob's HP (`0x34`); server then pushes `0xA1` updates. Passive (no aggro). |
| `Player.follow(serial[, dist])` / `Player.follow(false)` | Path-follow a mobile / stop. |
| `Player.stop()` | Abort goto + follow. (The runner calls this for you via `onPreempt`.) |
| `Player.dialogRespond(index)` / `Player.resurrect()` | Answer a gump dialog / confirm a resurrect menu. |
| `Player.on/off/once(event, cb)` | Events (see below). |

### World
`World.statics(x, y[, radius])` → `[{x,y,z,graphic,name}]` — the static tiles around
a point (find trees, ore, water, ovens by `name`). `World.markStump(...)` — local
visual tweak. `World.on/once(event, cb)`.

### Mobiles — other creatures/players (live handles)
`Mobiles.get(serial)` → a handle; `Mobiles.all()` → every cached one. A handle is
**live** — each field re-reads the cache, so it never goes stale; check `.exists`.
Fields: `.x/.y/.z`, `.dir`, `.body`, `.hue`, `.notoriety`, `.running`, `.warMode`,
`.name`, `.title` (paperdoll title, after a `doubleClick`), `.exists`, `.lastAnim`,
`.animMsAgo`, `.hp/.hpMax/.hpPct`.

**Mob HP** is `-1` until you ask: call `Player.requestStatus(serial)` once (the `0x34`
"open health bar" query). The server replies and then **auto-pushes** updates — no
polling. `createThreatMeter` does this for every dangerous mob in range, so their
`.hpPct` is populated for you — handy to decide *before* engaging whether a fight is
winnable.

### Vendor (buying)
`Vendor.buy(vendorSerial, [{serial, qty}])` — send a buy order. `Vendor.on/once/off`
for the events below. (`SurvivalSkill.buyFrom` wraps all of this.)

### Events (`Player.on(name, cb)` / `Player.once(name, ms?)`, plus `World`/`Vendor`)
`journal` `{text,…}`, `target` `{id,type}`, `arrival` `{x,y,z}`,
`container_open` `{serial,gump}`, `container_close` `{serial}`, `mobile` (serial),
`mobile_leave` (serial), `attacked` (attacker serial), `combat` (defender serial),
`dialog` `{question,options[],…}`, `resurrect_menu` `{action}`,
`paperdoll` `{serial,title}`, and on `Vendor`: `vendor_buy` `{vendor,items[]}` /
`vendor_done` `{vendor,flag}`.

### Helpers (from `bootstrap.js`)
| Call | Use |
|---|---|
| `await delay(ms)` | Plain sleep. **Inside a step, prefer `this.token.sleep(ms)`** so it's cancellable. |
| `tileDistance(a, b)` | King-move (Chebyshev) distance between two `{x,y}` points — UO's range metric (diagonal = 1 tile). |
| `await waitForJournal({contains?, ms?})` | Resolve with the next matching journal line. |
| `await waitForContainer({serial?, ms?})` | Resolve when a container opens. |
| `createThreatMeter(opts)` | Shared danger scoring → fires `onDanger(topSerial)` / `onLevel(...)`. Defaults (weights + aggressive-body list) live in `bootstrap.js` and `lib/threat.js`. |
| `console.log/info/warn/error(...)` | Output (mirrored to the file log as `[js]`). |

---

## 8. How files load

When you `run scripts/js/yourbot.js`, the engine evaluates, in order:

1. `scripts/js/bootstrap.js` — `console`, `delay`, `waitFor*`, `createThreatMeter`,
   the aggressive-mob list.
2. every `scripts/js/lib/*.js` (sorted) — `bank.js`, `bot.js` (`BehaviorScript`),
   `bt.js` (`createBehaviorRunner`, `makeToken`, `CANCELLED`), `survival.js`,
   `threat.js`. Cross-references resolve at **runtime** (when `start()` runs), not at
   load time, so load order between lib files doesn't matter.
3. your bot script.

So everything above is just there as a global. To share your own helpers across bots,
drop a file in `scripts/js/lib/` — it loads automatically before every script.

Everything is reloaded fresh on each `run`, so editing a file and typing `run …`
again gives a clean restart (no leftover state). `js stop` halts the current bot.

---

## 9. A complete example — a miner

A full bot as a `BehaviorScript` subclass, mirroring the lumberjack. Adjust the spot,
the tool, the target tiles, and the server's journal phrases for your shard (those are
the only shard-specific bits).

```js
'use strict';

class Miner extends BehaviorScript {
    PICK = 'pickaxe';
    SPOT = { x: 1820, y: 2900 };
    BANK = { x: 1819, y: 2824 };
    RADIUS = 12;
    DIG_WAIT_MS = 8000;
    FLEE_HP_FRAC = 0.4;
    FOOD = ['bread'];

    threat = null;

    constructor() {
        super();
        this.threatMeter = createThreatMeter({ onDanger: (s) => this.engage(s) });
        Player.on('attacked', (s) => this.engage(s));
        Player.on('combat',   (s) => this.engage(s));
    }

    engage(serial) {
        if (!serial || serial === Player.serial || this.threat?.exists) return;
        this.threat = Mobiles.get(serial);
        Player.requestStatus(serial);
        console.warn('[mine] engaging', this.threat.name || serial.toString(16));
    }

    isRock(s) { return s.name.includes('cave') || s.name.includes('mountain'); }

    classify(t) {
        const s = t.toLowerCase();
        if (s.includes('you dig some')) return 'ok';
        if (s.includes('no metal'))     return 'depleted';
        return 'other';
    }

    async mine() {
        await this.walkTo(this.SPOT, { range: 1 });
        const pick = this.findInPack(this.PICK);
        if (pick) { Player.equip(pick.serial); await this.token.sleep(800); }

        while (!this.full() && !Player.dead) {
            this.token.check();
            const me = { x: Player.x, y: Player.y };
            const rock = World.statics(me.x, me.y, this.RADIUS).filter((s) => this.isRock(s))
                .reduce((b, r) => (!b || tileDistance(me, r) < tileDistance(me, b) ? r : b), null);
            if (!rock) { await this.token.sleep(3000); return; }  // idle, don't spin

            Player.use(this.PICK);
            await this.token.wait(Promise.all([
                waitForJournal({ contains: 'what' }),
                Player.once('target', 5000),
            ]));
            Player.target(rock.x, rock.y, rock.z, rock.graphic);
            const kind = this.classify(await this.token.wait(waitForJournal({ ms: this.DIG_WAIT_MS })));
            if (kind !== 'ok') await this.token.sleep(1500);
        }
    }

    async bankRun() {
        await this.walkTo(this.BANK, { range: 1 });
        await this.deposit('ore');     // from BankSkill
    }

    async fight() {
        this.token.onCancel(() => { Player.follow(false); Player.setWarMode(false); });
        let following = 0;
        while (this.threat?.exists && !Player.dead) {
            if (this.hpFrac() < this.FLEE_HP_FRAC) {        // losing → bank + rest
                this.threat = null;
                await this.walkTo(this.BANK, { range: 1 });
                await this.rest();                          // from SurvivalSkill
                return;
            }
            Player.setWarMode(true);
            Player.attack(this.threat.serial);
            if (following !== this.threat.serial) { Player.follow(this.threat.serial, 1); following = this.threat.serial; }
            await this.token.sleep(1100);
        }
        Player.follow(false); Player.setWarMode(false); this.threat = null;
    }

    behaviors() {
        return [
            { name: 'fight', when: () => Boolean(this.threat?.exists), step: this.step('fight') },
            { name: 'bank',  when: () => this.full(),                  step: this.step('bankRun') },
            { name: 'mine',  when: () => true,                         step: this.step('mine') },
        ];
    }

    onStart() { this.threatMeter.start(); }
    onStop()  { this.threatMeter.stop(); }
}

Object.assign(Miner.prototype, BankSkill, SurvivalSkill);
new Miner().start();
```

Fishing, cooking, etc. are the same shape: replace the *job* method (`mine`) and its
tool/target/phrases. A cook might have no combat at all — just `[{ bank }, { cook }]`.

---

## 10. Skeleton to copy

```js
'use strict';

class MyBot extends BehaviorScript {
    BANK = { x: 0, y: 0 };
    FOOD = ['bread'];
    threat = null;

    constructor() {
        super();
        this.threatMeter = createThreatMeter({ onDanger: (s) => this.engage(s) });
        Player.on('attacked', (s) => this.engage(s));
        Player.on('combat',   (s) => this.engage(s));
    }

    engage(serial) {
        if (!serial || serial === Player.serial || this.threat?.exists) return;
        this.threat = Mobiles.get(serial);
        console.warn('[bot] engaging', this.threat.name || serial.toString(16));
    }

    async job() {
        while (!Player.dead) {
            this.token.check();
            // ... do one unit of work; re-scan the world each pass (restart-safe) ...
            await this.token.sleep(1000);
        }
    }

    async fight() {
        this.token.onCancel(() => { Player.follow(false); Player.setWarMode(false); });
        while (this.threat?.exists && !Player.dead) {
            Player.setWarMode(true);
            Player.attack(this.threat.serial);
            Player.follow(this.threat.serial, 1);
            await this.token.sleep(1100);
        }
        Player.follow(false); Player.setWarMode(false); this.threat = null;
    }

    behaviors() {
        return [
            { name: 'fight', when: () => Boolean(this.threat?.exists), step: this.step('fight') },
            { name: 'job',   when: () => true,                         step: this.step('job') },
        ];
    }

    onStart() { this.threatMeter.start(); }
    onStop()  { this.threatMeter.stop(); }
    // optional: async onStartup() { await this.rest(); }   // one-time prep before the loop
}

Object.assign(MyBot.prototype, BankSkill, SurvivalSkill);
new MyBot().start();
```

---

## 11. Rules & gotchas (read once, save yourself hours)

- **Always `this.token.wait`/`this.token.sleep`** inside a step. A bare `await delay(…)`
  or `await waitForJournal(…)` can't be interrupted, so your bot will ignore a threat
  until that await finishes.
- **Prefer `walkTo` over raw `Player.goto`.** It handles range, retries, adjacency, and
  the preempt-vs-failure distinction for you. If you do call `goto`, re-throw on cancel:
  `catch (e) { if (this.token.cancelled) throw CANCELLED; … }`.
- **Never swallow `CANCELLED`.** Don't write `.catch(() => …)` around a `this.token.wait`
  without re-throwing it. (Pattern: `catch (e) { if (e === CANCELLED) throw e; /* real error */ }`.)
- **Guards (`when`) must be cheap and pure** — they run every tick. Read state only; do
  no work, start no actions.
- **Steps and sequences must be restart-safe.** A re-selected step starts *fresh* (no
  resume), and a preempted `sequence` restarts from its first method. Re-scan the world
  at the top of the loop rather than caching a plan.
- **Idle when there's nothing to do.** If a job step has no work (no ore in range),
  `await this.token.sleep(3000); return;` instead of spinning.
- **Higher priority = listed first.** Put `resurrect`/`fight` above the job. A guard
  that's always `true` (`() => true`) belongs last, as the default.
- **`onStartup` runs before the loop and can't be preempted.** Keep it to short prep
  (rest, restock). For anything that must yield to combat, use a behaviour instead.
- **Avoid guard flapping.** If `when()` rapidly toggles, the runner thrashes
  (cancel/restart). The threat meter already smooths threats; for your own flags add a
  small timer/flag (like `fleeing`) rather than flipping instantly.
- **Mob HP needs a request.** `mob.hpPct` is `-1` until `Player.requestStatus(serial)`
  (the threat meter does this for dangerous mobs). After that the server pushes updates.

---

## 12. Where things live

| File | What |
|---|---|
| `scripts/js/lib/bt.js` | the raw runner + tokens (`createBehaviorRunner`, `makeToken`, `CANCELLED`) |
| `scripts/js/lib/bot.js` | **`BehaviorScript`** base class — lifecycle, `step`/`sequence`, `walkTo`, inventory |
| `scripts/js/lib/bank.js` | `BankSkill` mixin — deposit / withdraw / open container |
| `scripts/js/lib/survival.js` | `SurvivalSkill` mixin — bandage / rest / eat / find vendor / restock |
| `scripts/js/lib/threat.js` | `createThreatMeter` internals + the aggressive-body list |
| `scripts/js/bootstrap.js` | `console`, `delay`, `waitFor*`, `createThreatMeter` |
| `scripts/js/globals.d.ts` | TypeScript types for editor autocomplete (no build step) |
| `scripts/js/lumberjack.js` | the reference bot — read it alongside this guide |
| `src/js/ClientBindings.cpp` | the C++ side of `Player`/`World`/`Mobiles`/`Vendor` (only if you need a new primitive) |

For how combat danger is scored and how the server marks creatures aggressive, see
`bot-client.md` ("Server data model (creatures / templates)").

### Appendix: the raw runner (no base class)

For a throwaway bot you can skip `BehaviorScript` and drive `createBehaviorRunner`
directly. You then own the `this.token` wiring (each `step` is `(t) => { this.token = t;
return this.fn(); }`) and there are no lifecycle hooks or mixins.

```js
const runner = createBehaviorRunner({
    tickMs: 300,
    behaviors: [ /* { name, when, step } …, high → low */ ],
    onError: reportError,
    onPreempt: () => Player.stop(),
});
runner.start();
```

This is the engine `BehaviorScript.start()` builds for you; prefer the base class
unless you have a reason not to.
