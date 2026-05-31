// Ambient types for the uo-client JS scripting surface. No build step: the IDE
// reads this for signatures / autocomplete in the scripts. Globals come from
// three places — the C++ binding layer (Player, World, Mobiles, Vendor;
// src/js/ClientBindings.cpp), bootstrap.js (console, delay, tileDistance,
// reportError, waitForJournal, waitForContainer), and the auto-loaded
// scripts/js/lib/*.js: bt.js (createBehaviorRunner, makeToken), threat.js
// (createThreatMeter), bot.js (BehaviorScript), and the skill mixins
// bank.js (BankSkill) + survival.js (SurvivalSkill).

// ===== shared data shapes =====

/** One item inside a container (Player.equipment.backpack.items). */
interface UoItem {
    serial: number;
    graphic: number;
    amount: number;
    hue: number;
    /** Lowercased tiledata name of the graphic. */
    name: string;
}

/** A worn container (e.g. the backpack). `items` is empty until it is opened. */
interface UoContainer {
    serial: number;
    items: UoItem[];
}

/** One static tile from World.statics(). */
interface UoStatic {
    x: number;
    y: number;
    z: number;
    /** Raw tile id. */
    graphic: number;
    /** Lowercased tiledata name (use to spot trees etc.). */
    name: string;
}

// ===== event payloads =====

/** `target`: a target cursor was armed by the server (0x6C). */
interface TargetEvent { id: number; type: number; }

/** `journal`: a new journal line arrived. */
interface JournalEvent {
    text: string;
    type: number;
    serial: number;
    hue: number;
    /** True when it is a system line (no owner mobile). */
    system: boolean;
}

/** `arrival`: a Player.goto() reached its destination. */
interface ArrivalEvent { x: number; y: number; z: number; }

/** `container_open`: a container gump opened (0x24). */
interface ContainerEvent { serial: number; gump: number; }

/** One row of a vendor's buy window (joined 0x3C stock + 0x74 prices). */
interface VendorItem {
    /** Stock item serial — pass to Vendor.buy(). */
    serial: number;
    graphic: number;
    /** Amount the vendor has in stock. */
    amount: number;
    /** Unit price in gold. */
    price: number;
    /** Vendor shop-container layer (0x1A stock / 0x1B offered); echoed by Vendor.buy. */
    layer: number;
    /** Lowercased item name as the vendor lists it. */
    name: string;
}

/** `vendor_buy`: a vendor's buy window finished arriving (one per vendor that
 *  heard "buy"). `vendor` is the vendor mobile serial; pass it to Vendor.buy(). */
interface VendorBuyEvent { vendor: number; items: VendorItem[]; }

/** `vendor_done`: a vendor transaction closed (0x3B). */
interface VendorDoneEvent { vendor: number; flag: number; }

/** `paperdoll`: an 0x88 paperdoll arrived after a double-click. title is
 *  "<name> the <job>" — the only client-visible carrier of an NPC's job. */
interface PaperdollEvent { serial: number; title: string; }

/** One selectable option of a 0x7C dialog/menu. */
interface DialogOption {
    /** 1-based index to pass to Player.dialogRespond(). */
    index: number;
    /** Display model id (0 for a text-only list). */
    model: number;
    hue: number;
    /** Option label as sent, e.g. "YES - You choose to try to come back to life now." */
    text: string;
}

/** `dialog`: the server opened a 0x7C list/menu (e.g. the healer resurrect prompt). */
interface DialogEvent {
    /** Dialog serial (echoed back when answering). */
    id: number;
    menuId: number;
    question: string;
    options: DialogOption[];
}

/** A live mobile handle: holds only the serial; every field re-resolves the
 *  cache, so it never dangles. Read `exists` for liveness. From Mobiles.get/all. */
interface UoMobile {
    readonly serial: number;
    readonly x: number;
    readonly y: number;
    readonly z: number;
    readonly dir: number;
    readonly body: number;
    readonly hue: number;
    /** Notoriety: 1 blue, 2 green, 3/4 gray, 5 orange, 6 red, 7 yellow. */
    readonly notoriety: number;
    readonly running: boolean;
    readonly warMode: boolean;
    /** Name if already known, else "". */
    readonly name: string;
    /** Paperdoll title "<name> the <job>" (e.g. "Aldo the healer"), known only
     *  after Player.doubleClick(serial); "" until the 0x88 arrives. The only way
     *  to tell a vendor's job (healer vs tavernkeeper). */
    readonly title: string;
    /** False once the mobile has left view / been pruned. */
    readonly exists: boolean;
    /** Action code of its last 0x6E animation (swing/cast/get-hit…), or -1 if none. */
    readonly lastAnim: number;
    /** Milliseconds since that animation played, or -1 if none. A small value
     *  means it just acted (often a swing) — useful as a threat signal. */
    readonly animMsAgo: number;
    /** Current health, or -1 until the server sends this mob's bar (usually only
     *  while we fight/target it). Often a 0..hpMax ratio, not absolute hit points. */
    readonly hp: number;
    /** Max health (the ratio's denominator), or -1 if unknown. */
    readonly hpMax: number;
    /** Health fraction 0..1 (hp/hpMax), or -1 if unknown. Use this to judge a fight. */
    readonly hpPct: number;
}

/** Maps an event name to the payload its handler / awaiter receives. */
interface UoEventMap {
    target: TargetEvent;
    journal: JournalEvent;
    arrival: ArrivalEvent;
    container_open: ContainerEvent;
    /** A container closed or was culled for leaving view range. { serial }. */
    container_close: { serial: number };
    /** A mobile newly appeared (0x78). Payload is its serial; use Mobiles.get. */
    mobile: number;
    /** A mobile's record was removed (0x1D delete, or it left the ~18-tile view
     *  range and was culled). Payload is its serial; Mobiles.get now reads
     *  exists=false. Use it to drop the mobile from any local tracking. */
    mobile_leave: number;
    /** Something swung at us (0x2F). Payload is the attacker serial; use Mobiles.get. */
    attacked: number;
    /** We are swinging at a foe (0x2F, us as attacker). Payload is the defender serial. */
    combat: number;
    /** The server opened the resurrection menu (0x2C). action: 0 prompt, 1 resurrect, 2 ghost. */
    resurrect_menu: { action: number };
    /** The server opened a 0x7C list/menu dialog (e.g. healer resurrect). */
    dialog: DialogEvent;
    /** A vendor's buy window finished arriving after "buy" was spoken in range. */
    vendor_buy: VendorBuyEvent;
    /** A vendor buy/sell transaction closed (0x3B). */
    vendor_done: VendorDoneEvent;
    /** A paperdoll (0x88) arrived after a double-click; carries the NPC title. */
    paperdoll: PaperdollEvent;
}

/** on / off / once share one global registry across Player and World. */
interface UoEvents {
    on<K extends keyof UoEventMap>(name: K, cb: (e: UoEventMap[K]) => void): void;
    on(name: string, cb: (e: any) => void): void;
    /** Remove handlers for `name`; pass the same `cb` to remove just that one. */
    off<K extends keyof UoEventMap>(name: K, cb?: (e: UoEventMap[K]) => void): void;
    off(name: string, cb?: (e: any) => void): void;
    /** Resolve with the next `name` payload; rejects on timeout if `ms` > 0. */
    once<K extends keyof UoEventMap>(name: K, ms?: number): Promise<UoEventMap[K]>;
    once(name: string, ms?: number): Promise<any>;
}

// ===== Player =====

interface UoPlayer extends UoEvents {
    // --- live read-only state ---
    readonly serial: number;
    readonly name: string;
    readonly x: number;
    readonly y: number;
    readonly z: number;
    /** Facing direction, 0..7. */
    readonly facing: number;
    readonly running: boolean;
    readonly warMode: boolean;
    /** True while alive (not in ghost form). */
    readonly alive: boolean;
    /** True while dead — body is a ghost (402/403). Authoritative over hp. */
    readonly dead: boolean;
    readonly hp: number;
    readonly hpMax: number;
    readonly mana: number;
    readonly manaMax: number;
    readonly stam: number;
    readonly stamMax: number;
    readonly weight: number;
    readonly maxWeight: number;
    readonly equipment: { backpack: UoContainer };

    // --- actions ---
    /** Walk to (x, y[, z]). Resolves on arrival; rejects Error{reason} on abort.
     *  opts.terrain=false drops the road/grass bias (direct route, e.g. between trees). */
    goto(x: number, y: number, z?: number, opts?: { terrain?: boolean }): Promise<void>;
    goto(x: number, y: number, opts?: { terrain?: boolean }): Promise<void>;
    /** Double-click an item by serial, graphic id, or name substring. */
    use(target: number | string): void;
    /** Raw double-click by serial (0x06), no item resolution — use for MOBILES.
     *  Double-clicking an NPC opens its paperdoll, which fires the `paperdoll`
     *  event (and populates that mobile's `title`). */
    doubleClick(serial: number): void;
    /** Lift `qty` units (default 0 = whole stack) of `serial` from whatever open
     *  container holds it and drop into the backpack (0x07 + 0x08). Use to
     *  withdraw items from the bank box. */
    take(serial: number, qty?: number): void;
    /** Items inside any currently-open container by its serial (e.g. the bank box
     *  after saying "bank"). Returns [] if the container is not open or the items
     *  haven't arrived yet (0x3C). */
    containerItems(serial: number): UoItem[];
    /** Wear an item from the backpack (or world) by serial or name. The layer
     *  comes from tiledata — use this for the axe, which must be in hand to chop. */
    equip(target: number | string): void;
    /** Answer the armed target cursor. */
    target(serial: number): void;                                  // object
    target(x: number, y: number, z?: number): void;               // ground tile
    target(x: number, y: number, z: number, graphic: number): void; // static (tree)
    /** Speak a line (0x03), e.g. "bank". */
    say(text: string): void;
    /** Move a bag item (by serial / graphic / name) into a container serial. */
    drop(target: number | string, container: number): void;
    /** Send an attack request (0x05) at a mobile serial. */
    attack(serial: number): void;
    /** Query a mobile's status (0x34): the server replies with its HP and then
     *  auto-pushes 0xA1 updates, so afterwards Mobiles.get(serial).hp/.hpPct stay
     *  live. Passive — does NOT aggro the target (unlike attack). */
    requestStatus(serial: number): void;
    /** Follow a mobile via the bot pathfinder, keeping within `distance` tiles
     *  (default 1 = melee). Use during a fight to chase + hold facing on the foe.
     *  Call follow(false) / follow(0) to stop. */
    follow(serial: number, distance?: number): void;
    follow(off: false): void;
    /** Abort any in-flight goto path and stop following. Makes a parked
     *  Player.goto() reject — the cancel primitive behaviour steps rely on. */
    stop(): void;
    /** Toggle war mode (0x72); read the current state via `warMode`. */
    setWarMode(on: boolean): void;
    /** Answer the resurrection menu (0x2C). Default 1 = resurrect, 2 = stay ghost. */
    resurrect(choice?: number): void;
    /** The active 0x7C dialog/menu, or null if none is open. */
    readonly dialog: DialogEvent | null;
    /** Answer the active 0x7C dialog by 1-based option index (0 = cancel). */
    dialogRespond(index: number): void;
}

// ===== World =====

interface UoWorld extends UoEvents {
    /** Static tiles in the square around (x, y); radius defaults to 8. */
    statics(x: number, y: number, radius?: number): UoStatic[];
    /**
     * Locally show the tree static `graphic` at (x, y, z) as a stump for
     * `ttlMs` (default 10 min). In-memory only; reverts when the TTL lapses.
     * Use after a tree reports depleted to reflect the chop in the world view.
     */
    markStump(x: number, y: number, z: number, graphic: number, ttlMs?: number): void;
}

/** Live mobile collection. Handles are live (see UoMobile). */
interface UoMobiles {
    /** A live handle for `serial` (always returns one; check .exists). */
    get(serial: number): UoMobile;
    /** Live handles for every cached mobile (excluding the player). */
    all(): UoMobile[];
}

// ===== Vendor =====

/** Vendor buy interface. Open a vendor's window by saying "buy" within 3 tiles
 *  (no name needed); the `vendor_buy` event then carries the offer. Shares the
 *  one event registry, so Vendor.once('vendor_buy') works. */
interface UoVendor extends UoEvents {
    /** Buy from a vendor: pass the `vendor` serial and items from a `vendor_buy`
     *  offer's rows ({serial, qty}; layer defaults to 0x1A stock). Sends 0x3B and
     *  returns the number of rows sent. The server closes with `vendor_done`. */
    buy(vendorSerial: number, items: { serial: number; qty: number; layer?: number }[]): number;
}

declare const Player: UoPlayer;
declare const World: UoWorld;
declare const Mobiles: UoMobiles;
declare const Vendor: UoVendor;

// ===== bootstrap.js globals =====
// (console is provided by the IDE's standard lib — log/info/warn/error/debug.)

/** Resolve after `ms` milliseconds. */
declare function delay(ms: number): Promise<void>;

/** King-move (Chebyshev) distance between two points: max(|dx|, |dy|). This is
 *  UO's own notion of distance (a diagonal step counts as one tile), matching the
 *  server's range checks. Use for "how many tiles away" tests and nearest-sort. */
declare function tileDistance(a: { x: number; y: number }, b: { x: number; y: number }): number;

/** Print an error (and its stack) to stderr. */
declare function reportError(e: unknown): void;

/** Wait for a journal line; resolves with the line text. Rejects on timeout. */
declare function waitForJournal(opts: {
    contains?: string | RegExp;
    ms?: number;
}): Promise<string>;

/** Wait for a container to open. With `serial`, only that one matches. */
declare function waitForContainer(opts?: {
    serial?: number;
    ms?: number;
}): Promise<ContainerEvent>;

/** Tunable weights/thresholds for createThreatMeter (all optional). */
interface ThreatMeterOpts {
    radius?: number;        // tiles to scan around us (default 12)
    sampleMs?: number;      // re-evaluation cadence (default 350)
    wary?: number;          // score -> "wary" (default 2.5)
    danger?: number;        // score -> "danger" (default 6.0)
    wProx?: number;         // closeness weight (0..1 * this)
    wClosing?: number;      // per tile/sec a mob closes on us (the key signal)
    wFacing?: number;       // mob oriented at us
    wWar?: number;          // mob in war mode
    wAnim?: number;         // mob played an action animation recently
    adj?: number;           // flat bonus while a mob is in melee range
    wConfirmed?: number;    // flat bonus for a mob that has actually attacked us
    wHpDrop?: number;       // * fraction of max HP lost in a hit
    wLowHp?: number;        // * (0.5 - hp/hpMax) below half health
    dmgMemoryMs?: number;   // how long a hit keeps adding the HP-drop term
    directScore?: number;   // score floor while a direct attack is fresh
    directMemoryMs?: number;// how long a direct attack pins the meter
    hostileMemoryMs?: number;// how long a mob stays a "confirmed" foe after hitting us
    trackHp?: boolean;      // proactively query each dangerous mob's HP (default true)
    statusRetryMs?: number; // retry the HP query while a mob's HP is still unknown
    animFreshMs?: number;   // an animation counts as "recent" within this
    /** Body ids that are aggressive on sight — only these drive proactive threat;
     *  every other mob is ignored until it actually attacks us. Defaults to the
     *  server's EVIL creature list (DEFAULT_AGGRESSIVE_BODIES in bootstrap.js). */
    aggressiveBodies?: number[];
    /** Override the aggressive test (default: body is in aggressiveBodies). */
    aggressive?: (mob: UoMobile) => boolean;
    /** Journal phrase that means a player is attacking us (PvP). Default /is attacking you/i. */
    attackedPhrase?: RegExp | null;
    /** Map a notoriety (1..7) to a danger weight; 0 = ignore. */
    notoDanger?: (n: number) => number;
    /** Fired when the danger level changes. */
    onLevel?: (level: ThreatLevel, prev: ThreatLevel, score: number, top: number) => void;
    /** Fired every sample while level === 'danger' (top = scariest mob serial). */
    onDanger?: (top: number, score: number) => void;
}

type ThreatLevel = 'calm' | 'wary' | 'danger';

/** A live danger meter. Read .score/.level/.top, or use the opts callbacks. */
interface ThreatMeter {
    readonly score: number;
    readonly level: ThreatLevel;
    readonly top: number;      // serial of the scariest mob (0 = none)
    readonly count: number;    // hostile mobs in range
    cfg: ThreatMeterOpts;
    /** Recompute once now (start() calls this on a timer). */
    sample(): number;
    /** Begin self-sampling and wire the attack signals; returns the meter. */
    start(): ThreatMeter;
    /** Stop sampling and unsubscribe. */
    stop(): void;
    /** Force the meter to max for directMemoryMs (an explicit attack on us). Pass
     *  the attacker serial to also mark it as the confirmed foe. */
    markDirectAttack(serial?: number): void;
    /** Mark a mob as a confirmed attacker (counts its proactive score, makes it
     *  eligible as the top foe) for hostileMemoryMs. */
    markHostile(serial: number): void;
}

/** Create a shared multi-factor danger meter (scoring lives in bootstrap.js). */
declare function createThreatMeter(opts?: ThreatMeterOpts): ThreatMeter;

// ===== lib/bt.js — priority behaviour runner (auto-loaded) =====

/** Thrown through a step when it is preempted. Let it propagate; don't swallow it. */
declare const CANCELLED: unique symbol;

/** Cancellation token handed to each running behaviour step. */
interface CancelToken {
    /** True once the step has been preempted. */
    readonly cancelled: boolean;
    /** Register a cleanup callback run when the step is preempted. */
    onCancel(fn: () => void): void;
    /** Throw CANCELLED if preempted (call between awaits). */
    check(): void;
    /** Await a promise, unwinding with CANCELLED the instant we are preempted.
     *  A normal rejection of `promise` propagates unchanged. Use for every await. */
    wait<T>(promise: Promise<T>): Promise<T>;
    /** Cancellable sleep. */
    sleep(ms: number): Promise<void>;
    /** Cancellable retry: run `fn` up to `retries` times (retries+1 attempts).
     *  check() runs before each attempt and the gap between attempts is a
     *  cancellable sleep(intervalMs), so a preempt unwinds at once. CANCELLED is
     *  never retried. The token-aware retry helper for BT steps. */
    retry<T>(fn: () => Promise<T>, retries?: number, intervalMs?: number): Promise<T>;
}

/** One entry in a behaviour runner's priority list. */
interface Behavior {
    /** Identity used to detect a priority switch (preemption). */
    name: string;
    /** Guard: this behaviour may run when it returns true. Evaluated every tick. */
    when: () => boolean;
    /** The work, as a plain async function. Runs across ticks until it returns;
     *  thread `token` through every long await so a preempt can unwind it. */
    step: (token: CancelToken) => Promise<void>;
}

interface BehaviorRunner {
    start(): BehaviorRunner;
    stop(): void;
    /** Name of the currently running behaviour, or null. */
    readonly current: string | null;
}

/** Make a standalone cancellation token (rarely needed; runners make their own). */
declare function makeToken(): CancelToken;

/** Create a priority behaviour runner (behaviours highest-priority first). Each
 *  tick the highest-priority behaviour whose `when()` is true owns the body. A
 *  running step is preempted (its token fires, then `onPreempt` is called) ONLY by
 *  a higher-priority behaviour; if its own guard goes false with nothing higher
 *  ready, it runs to completion. */
declare function createBehaviorRunner(opts: {
    tickMs?: number;
    behaviors: Behavior[];
    onError?: (e: unknown) => void;
    onPreempt?: () => void;
    /** Lifecycle hook: 'start' a behaviour took the body, 'done' it returned,
     *  'preempt' a higher-priority behaviour cut it off. */
    onTransition?: (name: string, event: 'start' | 'done' | 'preempt') => void;
}): BehaviorRunner;

// ===== native primitives (used by bootstrap; rarely needed in scripts) =====

declare function __stdout(text: string): void;
declare function __stderr(text: string): void;
declare function __setTimeout(cb: () => void, ms: number): number;
declare function __clearTimeout(id: number): void;


type MethodNames<T> = {
    [K in keyof T]: T[K] extends (...args: any[]) => any ? K : never
}[keyof T] & string;

declare class BehaviorScript {
    token: CancelToken | null;
    runner: BehaviorRunner | null;
    tickMs?: number;
    onError?: (e: unknown) => void;

    // Tunable defaults (override as class fields in the subclass).
    BANK_AT_WEIGHT: number;
    WALLET: number;
    EAT_INTERVAL_MS: number;
    FOOD: string | string[];
    BANDAGE: string;

    /** Wrap a single method (by name) as a behaviour step bound to the token. */
    step(methodName: MethodNames<this>): (token: CancelToken) => Promise<void>;

    /** Compose a step from several methods run in order — e.g.
     *  sequence('goToBank', 'deposit'). Sugar over awaited steps; a preempt
     *  partway propagates CANCELLED and skips the rest. */
    sequence(...methodNames: MethodNames<this>[]): (token: CancelToken) => Promise<void>;

    /** The bot's priority list, highest-priority first. Implement in the subclass. */
    behaviors(): Behavior[];

    start(): this;
    stop(): void;

    /** Synchronous one-time setup, run before the tick loop (e.g. start the
     *  threat meter). No token here — for async prep use onStartup. */
    onStart(): void;
    /** Async one-time prelude, AWAITED before the first tick (so walkTo/rest/
     *  restock work; this.token is set for it). Runs before the runner ticks, so
     *  it CANNOT be preempted by a behaviour — keep it to short, safe prep
     *  (rest, restock), not open-ended work. Default no-op. */
    onStartup(): Promise<void>;
    onStop(): void;
    onPreempt(): void;
    /** Behaviour lifecycle logging (override to customise/silence). 'step' is a
     *  sub-step of a sequence(...); the rest come from the runner. */
    onTransition(name: string, event: 'start' | 'step' | 'preempt' | 'done'): void;

    // --- universal skills (lib/bot.js) ---
    /** True when the backpack is full enough to bank (maxWeight-aware). */
    full(): boolean;
    /** Current HP fraction 0..1 (1 when max is unknown). */
    hpFrac(): number;
    /** First backpack item whose name contains `name`, or undefined. */
    findInPack(name: string): UoItem | undefined;
    /** Total amount of backpack items matching any of `names`. */
    backpackCount(names: string | string[]): number;
    /** Walk to `target`. range>0 loops until within N tiles; range==0 steps onto
     *  the target (or, with adjacent, the nearest surrounding cell). terrain=false
     *  drops the road/grass bias. Returns true on arrival. */
    walkTo(target: { x: number; y: number },
        opts?: { adjacent?: boolean; range?: number; terrain?: boolean }): Promise<boolean>;
}

/** Banking skill (lib/bank.js). Mix in with
 *  `Object.assign(Bot.prototype, BankSkill)`. withdrawGold reads this.BANK and
 *  this.WALLET. */
interface BankSkill {
    /** Open a container by serial; resolves with the container_open event. */
    openContainer(serial: number): Promise<ContainerEvent>;
    /** Say "bank" and resolve with the bank box once it opens. */
    openBank(): Promise<ContainerEvent>;
    /** Drop one backpack stack into a container, retrying past the cooldown. */
    depositStack(item: UoItem, container: number): Promise<boolean>;
    /** Deposit every backpack stack whose name contains `nameFilter`. */
    deposit(nameFilter: string): Promise<void>;
    /** Withdraw gold until the backpack holds `amount` (default this.WALLET). */
    withdrawGold(amount?: number): Promise<void>;
}

/** Keep-alive skill (lib/survival.js). Mix in with
 *  `Object.assign(Bot.prototype, SurvivalSkill)`. Reads this.BANDAGE / this.FOOD
 *  / this.threat. */
interface SurvivalSkill {
    /** Apply one bandage to ourselves; true if it started. */
    bandageSelf(): Promise<boolean>;
    /** Bandage until full HP, bailing the moment a threat appears. */
    rest(): Promise<void>;
    /** Eat food from the backpack (matched by this.FOOD) until full. */
    eatFood(): Promise<void>;
    /** Find a vendor near `coords` whose paperdoll title contains `title` (guild
     *  masters excluded). Double-clicks nearby mobiles to learn titles. */
    findVendor(title: string, coords: { x: number; y: number }): Promise<UoMobile | null>;
    /** Approach a vendor, say "vendor buy", and buy up to `target` of the matching items. */
    buyFrom(vendor: UoMobile, nameVariants: string[], displayName: string, target: number): Promise<boolean>;
    /** Buy back every consumable the backpack is out of, visiting the needed
     *  vendors along the shortest total route from the current position. */
    restockConsumables(consumables: Record<string, {
        name?: string | string[];
        target: number;
        coords: { x: number; y: number };
        title: string;
    }>): Promise<void>;
}

/** Bots mix the skills onto their prototype, so a subclass exposes both at
 *  runtime; declaration-merge them onto the base for editor support. */
interface BehaviorScript extends BankSkill, SurvivalSkill {}