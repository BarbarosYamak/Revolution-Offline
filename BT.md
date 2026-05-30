# Writing bots — the behaviour runner

This is a practical guide to writing automation scripts (bots) for the client in
JavaScript: lumberjack, miner, fisher, cook, tamer — anything. You do **not** need
to touch C++. If you can write a few `async` functions, you can write a bot.

Read this top to bottom once; after that the **Skeleton** near the end is all you
copy.

---

## 1. The big idea

A bot is a **list of behaviours in priority order**. Every ~300 ms the runner asks,
top to bottom: *"is this behaviour allowed to run right now?"* The first one that
says yes gets to run. If a **higher-priority** behaviour becomes allowed while a
lower one is running, the running one is **interrupted** and the higher one takes
over.

The interruption is **higher-priority-only**: a running step is *never* cut short
by something below it. If its own guard goes false and only a lower-priority
behaviour is now ready, the step is left to **finish on its own** (it returns, then
the runner picks the next). This is what lets a step safely change the state its own
guard reads — e.g. `bank` deposits until it's no longer overweight, `flee` clears
its flag once it has recovered — without preempting itself mid-task.

That's the whole model. It is intentionally *not* a full HonorBuddy-style behaviour
tree (no status-returning `Sequence`/`Selector` nodes, no `Running` bookkeeping) —
just a flat priority list of plain `async` functions. The priority list already *is*
a Selector; a Sequence is just `await a(); await b();` inside one step, because
async/await remembers your position across ticks for free. (There is a thin
`this.sequence('a', 'b')` sugar — see the Implementation note — but it only chains
awaited methods; it is not a node type.) It reads top-down like a list of rules:

```
resurrect  (if I'm dead)      — most important
fight      (if something attacks me)
cooldown   (if I just fled, regenerate)
bank       (if I'm full)
mine       (otherwise)        — least important, the default job
```

The miner runs at the bottom. The instant a monster attacks, `fight` outranks
`mine`, so the runner **cancels** mining mid-swing and switches to combat. When the
fight ends, mining resumes. You write each behaviour as if it were the only thing
happening; the runner handles the switching.

**Implementation note:** Bots are typically structured as a single class
(e.g. `Lumberjacker`). The class holds all shared state (the "blackboard") and
helper methods. Each behaviour step is a fat-arrow wrapper that sets `this.token`
before calling the actual method, so all helpers access the current token via
`this.token` without needing it threaded as a parameter through every function call.
For example:
```js
step: (t) => { this.token = t; return this.mine(t); }
```

`this.step('mine')` builds that wrapper for you from a method name. When a
behaviour is just several methods run in order, `this.sequence('goToBank',
'deposit')` chains them — handy for **reuse**: `flee` and `bank` both walk to the
bank, so `goToBank` is its own method, `bank` is `sequence('goToBank', 'deposit')`,
and `flee` calls `goToBank` then arms its cooldown. The sequence is sugar over
`await goToBank(); await deposit();` — a preempt partway throws `CANCELLED` and
skips the rest, same as any step.

---

## 2. The three pieces

### Behaviour
An object with three fields:

```js
{ name: 'mine', when: () => !this.full(), step: (t) => { this.token = t; return this.mineStep(); } }
```

- **`name`** — a unique label. The runner uses it to notice "the winning behaviour
  changed" (a switch).
- **`when()`** — the guard. Return `true` if this behaviour may run *right now*.
  Called every tick, so keep it **cheap and side-effect-free** — just read state.
- **`step(token)`** — the actual work, an `async` function. It runs across many
  ticks until it `return`s. `token` is how it gets cancelled (see §3).

### The runner
`createBehaviorRunner(...)` takes the priority list and pulses it:

```js
const runner = createBehaviorRunner({
    tickMs: 300,                       // how often to re-check priorities
    behaviors: [ /* high → low */ ],
    onError: reportError,              // a step threw something unexpected
    onPreempt: () => Player.stop(),    // called when a step is interrupted
});
runner.start();
```

Each tick:
1. find the first behaviour whose `when()` is `true` (the highest-priority winner);
2. if it's the **same** one already running, or it's **lower** priority than what's
   running (or nothing is eligible) → do nothing, let the current step keep going;
3. only if the winner is **higher** priority than the running step → cancel that step,
   call `onPreempt()`, start the winner.

When a step finishes on its own (returns), the next tick simply starts whatever is
eligible. So a step is only ever interrupted from *above*, never undercut from below.

`onPreempt` is your chance to physically stop whatever the old step was doing.
For a movement bot it's almost always `() => Player.stop()` (halt walking/following).

### The token (cancellation)
This is the only new concept and the heart of the system. See §3.

---

## 3. Tokens — how a step gets interrupted

A `step` is a long-running `async` function. When the runner wants to switch away
from it, it must be able to **interrupt** that function — even if it's parked in
the middle of `await something()`. That's what the **token** does.

Every step is handed a `token`. The rule is simple:

> **Wrap every long `await` in `token.wait(...)` or `token.sleep(...)`.**

When the step is preempted, those wrapped awaits immediately throw a special value
called `CANCELLED`, which unwinds the function. You normally never catch `CANCELLED`
— you let it bubble up and the function just stops.

```js
async function mineStep(token) {
    await token.sleep(500);                      // cancellable sleep
    const line = await token.wait(               // cancellable await
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
| `token.retry(fn, tries?, intervalMs?)` | Run `fn`, retrying on throw up to `tries` times (default 3, so 4 attempts). `check()` runs before each attempt and the gap between them is a **cancellable** `sleep(intervalMs)` (default 250ms), so a preempt unwinds at once — no waiting out the interval, no manual `token.check()` in `fn`. Never retries `CANCELLED`. This is the only retry helper — always available on the step's token. |
| `token.onCancel(fn)` | Register cleanup to run when preempted (e.g. `() => Player.setWarMode(false)`). |
| `token.cancelled` | `true` once preempted. |

### The one piece of discipline: `goto`

`Player.goto()` is special because it's a long C++ trip. When you preempt a step,
the runner's `onPreempt` calls `Player.stop()`, which makes the parked `goto`
**reject**. But a `goto` can also reject for an ordinary reason (the path was
blocked). So after a `goto` you must tell the two apart:

```js
try {
    await this.token.wait(Player.goto(x, y));
} catch (e) {
    if (this.token.cancelled) throw CANCELLED;   // we were preempted → unwind
    // otherwise: a normal failure (blocked) → handle it (try another cell, etc.)
}
```

The helper `gotoNear()` in `lumberjack.js` already does exactly this; copy it.

### Why this matters (concrete)

A miner is parked in `await this.token.wait(waitForJournal({ms:8000}))` waiting for "you
dig some ore". A monster attacks. The threat meter sets `this.threat`. On the next tick
`fight.when()` returns `true`, outranks `mine`, so the runner cancels the miner's
token. The `this.token.wait` throws `CANCELLED`, the mining function unwinds, the fight
begins — within ~300 ms, not after the 8-second wait finishes. That responsiveness
is the entire reason tokens exist.

---

## 4. The blackboard pattern

Behaviours don't talk to each other directly. Instead, **sensing** code writes a
few shared variables ("the blackboard"), and **guards** read them. In a class-based
bot these live as instance fields:

```js
class Miner {
    constructor() {
        this.threat = null;        // current foe (a live Mobiles handle) or null
        this.cooldownUntil = 0;    // after fleeing, idle until this timestamp
        // ...
    }
}
```

Sensing is done with **event handlers** and the **threat meter**, which run
independently of the runner and just update the blackboard:

```js
Player.on('attacked', (serial) => this._engage(serial));   // a mob swung at us
this.threatMeter = createThreatMeter({
  onDanger: (top) => {
    if (top) this._engage(top);
  },     // scary mob nearby
});
this.threatMeter.start();
```

Then a guard simply reads it: `when: () => !!(this.threat && this.threat.exists)`.

**Why events stay outside the runner:** some things must happen *instantly*,
regardless of what the runner is doing — e.g. the healer's resurrection dialog
freezes you until you answer. Answer it in an event handler, not a behaviour:

```js
Player.on('dialog', (d) => {
    if (!/resurrect|come back to life/i.test(d.question)) return;
    const yes = d.options.find((o) => /^\s*yes/i.test(o.text)) || d.options[0];
    Player.dialogRespond(yes.index);
});
```

Rule of thumb: **a behaviour** = something that *occupies your body* over time
(walking, mining, fighting). **An event handler** = an instant reflex (answer a
prompt, note a foe).

---

## 5. What you can call from a step

These globals are always available (no import needed).

### Player — your character (live, read-only state)
`Player.x/y/z`, `.facing`, `.running`, `.warMode`, `.alive`, `.dead`,
`.hp/.hpMax`, `.mana/.manaMax`, `.stam/.stamMax`, `.weight/.maxWeight`,
`.name`, `.serial`, `.equipment.backpack` (`{serial, items[]}`), `.dialog`.

Actions:
| Call | Use |
|---|---|
| `await Player.goto(x, y[, z][, {terrain}])` | Walk there. Resolves on arrival, rejects on abort. `terrain:false` drops the road bias (direct route). |
| `Player.use(target)` | Double-click an item by serial / graphic id / name. |
| `Player.equip(target)` | Wear an item (layer from tiledata). |
| `Player.target(serial)` / `Player.target(x,y[,z][,graphic])` | Answer a target cursor (object / ground / static). |
| `Player.say(text)` | Speak a line ("bank", "ress", …). |
| `Player.drop(target, container)` | Move a bag item into a container serial. |
| `Player.attack(serial)` / `Player.setWarMode(on)` | Combat. |
| `Player.requestStatus(serial)` | Ask for a mob's HP (`0x34`); server then pushes `0xA1` updates. Passive (no aggro). |
| `Player.follow(serial[, dist])` / `Player.follow(false)` | Path-follow a mobile / stop. |
| `Player.stop()` | Abort goto + follow. (The runner calls this for you via `onPreempt`.) |
| `Player.on/off/once(event, cb)` | Events (see below). |

### World
`World.statics(x, y[, radius])` → `[{x,y,z,graphic,name}]` — the static tiles around
a point (find trees, ore, water, ovens by `name`). `World.markStump(...)` — local
visual tweak. `World.on/once(event, cb)`.

### Mobiles — other creatures/players (live handles)
`Mobiles.get(serial)` → a handle; `Mobiles.all()` → every cached one. A handle is
**live** — each field re-reads the cache, so it never goes stale; check `.exists`.
Fields: `.x/.y/.z`, `.dir`, `.body`, `.hue`, `.notoriety`, `.running`, `.warMode`,
`.name`, `.exists`, `.lastAnim`, `.animMsAgo`, `.hp/.hpMax/.hpPct`.

**Mob HP** is `-1` until you ask for it: call `Player.requestStatus(serial)` once
(the `0x34` "open health bar" query). The server replies with the mob's HP and then
**auto-pushes** updates whenever it changes — no polling. `requestStatus` is passive
(it does **not** aggro the mob, unlike `attack`). If you use `createThreatMeter`, it
already calls `requestStatus` for every dangerous mob in range, so their `.hpPct` is
populated for you — handy to decide *before* engaging whether a fight is winnable.

### Events (`Player.on(name, cb)` / `Player.once(name, ms?)`)
`journal` `{text,…}`, `target` `{id,type}`, `arrival` `{x,y,z}`,
`container_open` `{serial,gump}`, `mobile` (serial), `attacked` (attacker serial),
`combat` (defender serial), `dialog` `{question,options[],…}`, `resurrect_menu`.

### Helpers (from `bootstrap.js`)
| Call | Use |
|---|---|
| `await delay(ms)` | Plain sleep. **Inside a step, prefer `this.token.sleep(ms)`** so it's cancellable. |
| `tileDistance(a, b)` | King-move (Chebyshev) distance between two `{x,y}` points — UO's own range metric (diagonal = 1 tile). For "how many tiles away" tests and nearest-sort. |
| `await waitForJournal({contains?, ms?})` | Resolve with the next matching journal line. |
| `await waitForContainer({serial?, ms?})` | Resolve when a container opens. |
| `createThreatMeter(opts)` | Shared danger scoring → fires `onDanger(topSerial)` / `onLevel(...)`. See `bootstrap.js` for weights. |
| `console.log/info/warn/error(...)` | Output (mirrored to the file log as `[js]`). |

---

## 6. How files load

When you `run scripts/js/yourbot.js`, the engine evaluates, in order:

1. `scripts/js/bootstrap.js` — `console`, `delay`, `waitFor*`, `createThreatMeter`.
2. every `scripts/js/lib/*.js` (sorted) — including **`lib/bt.js`**, which defines
   `createBehaviorRunner`, `makeToken`, `CANCELLED`.
3. your bot script.

So everything above is just there. To share your own helpers across bots, drop a
file in `scripts/js/lib/` — it loads automatically before every script.

Everything is reloaded fresh on each `run`, so editing a file and typing `run …`
again gives a clean restart (no leftover state). `js stop` halts the current bot.

---

## 7. A complete example — a miner

This mirrors the lumberjack. Adjust the spot, the tool, the target tiles, and the
server's journal phrases for your shard (those are the only shard-specific bits).

```js
'use strict';

class Miner {
  constructor() {
    this.PICK = 'pickaxe';
    this.SPOT = { x: 1820, y: 2900 };
    this.BANK = { x: 1819, y: 2824 };
    this.RADIUS = 12;

    this.DIG_WAIT_MS = 8000;
    this.FLEE_HP_FRAC = 0.4;

    this.threat = null;
    this._token = null;

    this.threatMeter = createThreatMeter({
      onDanger: (top) => {
        if (top) this._engage(top);
      },
    });

    Player.on('attacked', (serial) => this._engage(serial));
    Player.on('combat', (serial) => this._engage(serial));
  }

  get token() {
    return this._token;
  }

  full() {
    const m = Player.maxWeight;
    return m > 0 ? Player.weight >= m - 20 : false;
  }

  isRock(s) {
    return s.name.includes('cave') || s.name.includes('mountain');
  }

  classify(t) {
    const s = t.toLowerCase();
    if (s.includes('you dig some')) return 'ok';
    if (s.includes('no metal')) return 'depleted';
    if (s.includes('too far')) return 'far';
    return 'other';
  }

  _engage(serial) {
    if (!serial || serial === Player.serial || (this.threat && this.threat.exists)) return;
    this.threat = Mobiles.get(serial);
    console.warn('[mine] engaging', this.threat.name || serial.toString(16));
  }

  async gotoNear(p, adjacent = false, terrain = true) {
    const me = { x: Player.x, y: Player.y };
    const cells = [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1]]
            .map(([dx, dy]) => ({ x: p.x + dx, y: p.y + dy }))
            .sort((a, b) => tileDistance(me, a) - tileDistance(me, b));
    for (const c of cells) {
      if (c.x === me.x && c.y === me.y) return true;
      try {
        await this.token.wait(Player.goto(c.x, c.y, { terrain }));
        return true;
      } catch (e) {
        if (this.token.cancelled) throw CANCELLED;
      }
    }
    return false;
  }

  async mine() {
    await this.gotoNear(this.SPOT);
    const inPack = Player.equipment.backpack.items.find((it) => it.name.includes(this.PICK));
    if (inPack) {
      Player.equip(inPack.serial);
      await this.token.sleep(800);
    }

    while (!this.full() && !Player.dead) {
      this.token.check();
      const me = { x: Player.x, y: Player.y };
      const rock = World.statics(me.x, me.y, this.RADIUS).filter(s => this.isRock(s))
              .reduce((b, r) => (!b || tileDistance(me, r) < tileDistance(me, b) ? r : b), null);
      if (!rock) {
        console.log('[mine] no rock near');
        await this.token.sleep(3000);
        return;
      }

      Player.use(this.PICK);
      await this.token.wait(Promise.all([
        waitForJournal({ contains: 'what' }),
        Player.once('target', 5000),
      ]));
      Player.target(rock.x, rock.y, rock.z, rock.graphic);
      const kind = this.classify(await this.token.wait(waitForJournal({ ms: this.DIG_WAIT_MS })));
      if (kind === 'depleted' || kind === 'far' || kind === 'other') {
        await this.token.sleep(1500);
      }
    }
  }

  async openContainer(serial) {
    return this.token.retry(async () => {
      const [opened] = await this.token.wait(Promise.all([
        waitForContainer({ serial, ms: 2500 }),
        Player.use(serial),
      ]));
      return opened;
    }, 3, 500);
  }

  async bankRun() {
    if (!await this.gotoNear(this.BANK)) return;
    const box = await this.token.retry(async () => {
      const [b] = await this.token.wait(Promise.all([waitForContainer({ ms: 3000 }), Player.say('bank')]));
      return b;
    }, 4, 1000);
    const ore = Player.equipment.backpack.items.filter((it) => it.name.includes('ore'));
    for (const o of ore) {
      Player.drop('0x' + o.serial.toString(16), box.serial);
      await this.token.sleep(1200);
    }
  }

  async fight() {
    this.token.onCancel(() => {
      Player.follow(false);
      Player.setWarMode(false);
    });
    let following = 0;
    while (this.threat && this.threat.exists && !Player.dead) {
      if (Player.hp / (Player.hpMax || 1) < this.FLEE_HP_FRAC) {
        Player.follow(false);
        Player.setWarMode(false);
        this.threat = null;
        await this.gotoNear(this.BANK);
        await this.token.sleep(60_000);
        return;
      }
      Player.setWarMode(true);
      Player.attack(this.threat.serial);
      if (following !== this.threat.serial) {
        Player.follow(this.threat.serial, 1);
        following = this.threat.serial;
      }
      await this.token.sleep(1100);
    }
    Player.follow(false);
    Player.setWarMode(false);
    this.threat = null;
  }

  run() {
    const runner = createBehaviorRunner({
      behaviors: [
        {
          name: 'fight', when: () => !!(this.threat && this.threat.exists), step: (t) => {
            this.token = t;
            return this.fight();
          }
        },
        {
          name: 'bank', when: () => this.full(), step: (t) => {
            this.token = t;
            return this.bankRun();
          }
        },
        {
          name: 'mine', when: () => true, step: (t) => {
            this.token = t;
            return this.mine();
          }
        },
      ],
      onError: reportError,
      onPreempt: () => Player.stop(),
    });
    this.threatMeter.start();
    runner.start();
  }
}

new Miner().run();
```

Fishing, cooking, etc. are the same shape: replace the *job* behaviour (`mine`) and
its tool/target/phrases. A cook might have no combat at all — just
`[{ bank }, { cook }]`. A fisher adds a "move to next fishing spot when this one is
fished out" inside its job step.

---

## 8. Skeleton to copy

```js
'use strict';

class MyBot {
  constructor() {
    this.threat = null;                   // blackboard
    this._token = null;

    this.threatMeter = createThreatMeter({
      onDanger: (t) => {
        if (t) this._engage(t);
      }
    });
    Player.on('attacked', (s) => this._engage(s));
    Player.on('combat', (s) => this._engage(s));
  }

  get token() {
    return this._token;
  }

  _engage(serial) {
    if (!serial || serial === Player.serial || (this.threat && this.threat.exists)) return;
    this.threat = Mobiles.get(serial);
    console.warn('[bot] engaging', this.threat.name || serial.toString(16));
  }

  async gotoNear(p, adjacent = false, terrain = true) {
    const me = { x: Player.x, y: Player.y };
    const cells = [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1]]
            .map(([dx, dy]) => ({ x: p.x + dx, y: p.y + dy }))
            .sort((a, b) => tileDistance(me, a) - tileDistance(me, b));
    for (const c of cells) {
      if (c.x === me.x && c.y === me.y) return true;
      try {
        await this.token.wait(Player.goto(c.x, c.y, { terrain }));
        return true;
      } catch (e) {
        if (this.token.cancelled) throw CANCELLED;
      }
    }
    return false;
  }

  async job() {
    while (!Player.dead) {
      this.token.check();
      // ... do one unit of work ...
      await this.token.sleep(1000);
    }
  }

  async fight() {
    this.token.onCancel(() => {
      Player.follow(false);
      Player.setWarMode(false);
    });
    while (this.threat && this.threat.exists && !Player.dead) {
      Player.setWarMode(true);
      Player.attack(this.threat.serial);
      Player.follow(this.threat.serial, 1);
      await this.token.sleep(1100);
    }
    Player.follow(false);
    Player.setWarMode(false);
    this.threat = null;
  }

  run() {
    createBehaviorRunner({
      behaviors: [
        {
          name: 'fight', when: () => !!(this.threat && this.threat.exists), step: (t) => {
            this.token = t;
            return this.fight();
          }
        },
        {
          name: 'job', when: () => true, step: (t) => {
            this.token = t;
            return this.job();
          }
        },
      ],
      onError: reportError,
      onPreempt: () => Player.stop(),
    }).start();
  }
}

new MyBot().run();
```

---

## 9. Rules & gotchas (read once, save yourself hours)

- **Always `this.token.wait`/`this.token.sleep`** inside a step. A bare `await delay(...)` or
  `await waitForJournal(...)` can't be interrupted, so your bot will ignore a threat
  until that await finishes.
- **After `Player.goto`, re-throw on cancel:** `catch (e) { if (this.token.cancelled) throw CANCELLED; … }`. Otherwise a preempt looks like a normal "goto failed".
- **Never swallow `CANCELLED`.** Don't write `.catch(() => …)` around a `this.token.wait`
  without re-throwing `CANCELLED`. (Pattern: `catch (e) { if (e === CANCELLED) throw e; /* handle real error */ }`.)
- **Guards (`when`) must be cheap and pure** — they run every tick. Read state only;
  do no work, start no actions.
- **Steps must be restart-safe.** When a step is re-selected it starts *fresh* (there
  is no "resume"). So re-scan the world at the top of the loop rather than caching a
  plan — e.g. the miner re-finds the nearest rock each pass. This is usually what you
  want anyway.
- **Idle when there's nothing to do.** If a job step has no work (no ore in range),
  `await this.token.sleep(3000); return;` instead of spinning — otherwise it busy-loops
  every tick.
- **Higher priority = listed first.** Put `resurrect`/`fight` above the job. A guard
  that's always `true` (`() => true`) belongs last, as the default.
- **Avoid guard flapping.** If `when()` rapidly toggles, the runner thrashes
  (cancel/restart). For threats, the threat meter already smooths this; for your own
  flags, add a small timer (like `cooldownUntil`) rather than flipping instantly.
- **Mob HP needs a request.** `mob.hpPct` is `-1` until you call
  `Player.requestStatus(serial)` (the threat meter does this for dangerous mobs
  automatically). After that the server pushes updates, so it stays live.

---

## 10. Where things live

| File | What |
|---|---|
| `scripts/js/lib/bt.js` | the runner + tokens (`createBehaviorRunner`, `makeToken`, `CANCELLED`) — auto-loaded |
| `scripts/js/bootstrap.js` | `console`, `delay`, `waitFor*`, `createThreatMeter`, the aggressive-mob list |
| `scripts/js/globals.d.ts` | TypeScript types for editor autocomplete (no build step) |
| `scripts/js/lumberjack.js` | the reference bot — read it alongside this guide |
| `src/js/ClientBindings.cpp` | the C++ side of `Player`/`World`/`Mobiles` (only if you need a new primitive) |

For how combat danger is scored and how the server marks creatures aggressive, see
`bot-client.md` ("Server data model (creatures / templates)").
