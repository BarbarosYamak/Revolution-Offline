'use strict';
// Lumberjack: cut trees near FOREST, bank the logs, restock consumables, repeat.
// Structured as a priority behaviour runner (lib/bt.js): the highest-priority
// behaviour whose guard is true owns the body, and a strictly higher-priority one
// preempts it. Sensing (events + threat meter) writes the blackboard (`threat`,
// `fleeing`); the behaviours read it and act.
//
// Priority:  resurrect > fight > bank > eat > chop.
//
// Generic skills come from the base class and mixins (lib/bot.js + lib/bank.js +
// lib/survival.js): movement (walkTo), inventory, banking, healing, eating,
// restocking, finding a vendor. Only the lumberjack-specific chopping and the
// combat policy live here.

class Lumberjack extends BehaviorScript {
    AXE = 'hatchet';
    LOG = 'log';
    // Forest stands to rotate through. After a flee (an unbeatable mob) or running
    // a stand dry, `nextForest()` advances to the next entry so the bot doesn't
    // keep returning to the same mob / depleted trees. Add this shard's real
    // coords; a single-entry list just always returns to that one spot.
    FORESTS = [
        { x: 1776, y: 2774 },
        { x: 1776, y: 2774 }, // TODO: replace with a second forest stand
    ];
    BANK = { x: 1819, y: 2824 };
    HEALER = { x: 1914, y: 2806 };
    SEARCH_RADIUS = 24;

    FOOD = ['bread', 'lamb'];

    // Consumables to keep stocked. When the backpack runs OUT of one (count 0),
    // the `restock` step (end of the bank cycle) walks to coords, finds the vendor
    // whose paperdoll title contains `title`, and buys up to `target`. Fill with
    // this shard's real vendor coords/titles; an empty map makes restock a no-op.
    CONSUMABLES = {
        'bandage': { target: 20, coords: { x: 1914, y: 2806 }, title: 'healer' },
        'bread': { name: ['lamb', 'bread'], target: 10, coords: { x: 1854, y: 2793 }, title: 'provisioner' },
    };

    CHOP_WAIT_MS = 15000;
    MAX_RETRY = 5;

    FLEE_HP_FRAC = 0.4;
    HEAL_HP_FRAC = 0.8;         // bandage mid-fight once HP drops below this
    BANDAGE_INTERVAL_MS = 6000; // min gap between mid-fight bandages (~heal timer)
    COMBAT_TICK_MS = 1100;
    LOSE_ASSESS_MS = 4000;
    RESS_WAIT_MS = 8000;

    AVOID_RADIUS = 18;              // tiles around a fled mob to treat as off-limits
    AVOID_TTL_MS = 5 * 60 * 1000;  // how long an avoid-area stays active

    threat = null;
    fleeing = false;
    forestIndex = 0;
    lastChop = null;
    lastAteMs = 0;
    lastBandageMs = 0;
    avoidAreas = [];   // [{ x, y, until }] transient no-chop squares (see avoidArea)

    constructor() {
        super();

        this.threatMeter = createThreatMeter({
            onLevel: (level, prevLevel, score) =>
                console.warn(`[threat] ${prevLevel} -> ${level}  score=${score.toFixed(1)} mobs=${this.threatMeter.count}`),
            onDanger: (topSerial) => { if (topSerial) this.engage(topSerial, 'threat'); },
        });

        Player.on('attacked', (serial) => this.engage(serial, 'ATTACKED by'));
        Player.on('combat', (serial) => this.engage(serial, 'fighting'));

        Player.on('dialog', (dialog) => {
            if (!/resurrect|come back to life/i.test(dialog.question)) return;
            const yes = dialog.options.find((option) => /^\s*yes/i.test(option.text)) || dialog.options[0];
            if (!yes) return;
            console.log(`[lj] resurrect dialog -> "${yes.text.trim()}"`);
            Player.dialogRespond(yes.index);
        });

        Player.on('resurrect_menu', (event) => {
            console.log(`[lj] resurrect menu (action=${event.action}) -> confirming`);
            Player.resurrect();
        });
    }

    // ===== sensing / combat =====

    engage(serial, why) {
        if (!serial || serial === Player.serial) return;
        if (this.fleeing) return;   // escaping/resting: don't pick a new fight
        if (this.threat && this.threat.exists) return;
        this.threat = Mobiles.get(serial);
        Player.requestStatus(serial);
        console.warn(`[lj] ${why} ${this.threat.name || '0x' + serial.toString(16)}` +
            ` noto=${this.threat.notoriety} hp=${this.threat.hpPct >= 0 ? (this.threat.hpPct * 100 | 0) + '%' : '?'}` +
            ` at ${this.threat.x},${this.threat.y}`);
    }

    assessFight(baseline) {
        const myHp = this.hpFrac();
        const foeHp = this.threat ? this.threat.hpPct : -1;
        const now = Date.now();
        // Latch the start-of-fight snapshot once foe HP is actually known.
        if (baseline.startMs === 0 && foeHp >= 0) {
            baseline.startMs = now;
            baseline.startMyHp = myHp;
            baseline.startFoeHp = foeHp;
        }

        // Hard floor: bail the moment my HP drops below the panic threshold.
        if (myHp < this.FLEE_HP_FRAC) return { flee: true, why: `HP ${(myHp * 100) | 0}% < floor` };

        // Trend check, only after the fight has run long enough to read a trend.
        if (baseline.startMs && now - baseline.startMs >= this.LOSE_ASSESS_MS) {
            const myHpLost = baseline.startMyHp - myHp;
            const foeHpLost = baseline.startFoeHp - foeHp;
            if (myHpLost >= 0.1) {
                if (foeHpLost <= 0.02)
                    return { flee: true, why: `cannot dent foe (foe ${(foeHp * 100) | 0}%)` };
                // Crude time-to-die for each side: remaining HP / loss-so-far.
                const myTimeToDie = myHp / myHpLost, foeTimeToDie = foeHp / foeHpLost;
                if (myTimeToDie <= foeTimeToDie)
                    return { flee: true, why: `losing race (me ${myTimeToDie.toFixed(1)} <= foe ${foeTimeToDie.toFixed(1)})` };
            }
        }
        return { flee: false };
    }

    async fight() {
        const { token } = this;
        token.onCancel(() => { Player.follow(false); Player.setWarMode(false); });
        let followSerial = 0;
        const baseline = { startMs: 0, startMyHp: -1, startFoeHp: -1 };
        while (this.threat?.exists && !Player.dead) {
            const verdict = this.assessFight(baseline);
            if (verdict.flee) {
                const foeName = this.threat?.name || '0x' + (this.threat?.serial ?? 0).toString(16);
                console.warn(`[lj] ${verdict.why} -> flee from ${foeName} (hp ${Player.hp}/${Player.hpMax})`);
                this.fleeing = true;
                // Blacklist this mob's area for a while and rotate to a different
                // stand, so after banking + resting `chop` starts somewhere new
                // instead of walking back into it (which would loop flee -> bank ->
                // return -> flee). The avoid-area also keeps tree-search away from
                // the spot even within the same stand.
                const mob = this.threat?.exists ? this.threat : Player;
                this.avoidArea(mob.x, mob.y);
                this.nextForest();
                return;
            }
            if (!Player.warMode) Player.setWarMode(true);
            Player.attack(this.threat.serial);

            if (followSerial !== this.threat.serial) { Player.follow(this.threat.serial, 1); followSerial = this.threat.serial; }

            // Heal mid-fight: assessFight only flees at FLEE_HP_FRAC, and a burst
            // between ticks can drop us past that before the next check. Throttled
            // to the bandage timer; gate set before the await so a no-op (no
            // bandage) still throttles instead of hammering every tick.
            if (this.hpFrac() < this.HEAL_HP_FRAC && Date.now() - this.lastBandageMs > this.BANDAGE_INTERVAL_MS) {
                this.lastBandageMs = Date.now();
                await this.bandageSelf();
            }
            await token.sleep(this.COMBAT_TICK_MS);
        }
        Player.follow(false);
        if (Player.warMode) Player.setWarMode(false);
        this.threat = null;
    }

    async resurrect() {
        console.warn('[lj] DEAD -> heading to healer to resurrect');
        this.threat = null;
        this.fleeing = false;
        Player.follow(false);
        const { token } = this;
        while (Player.dead) {
            await this.walkTo(this.HEALER);

            const healer = await this.findVendor('healer', this.HEALER);
            if (healer) await this.walkTo({ x: healer.x, y: healer.y }, { adjacent: true });
            else console.warn('[lj] no healer found near HEALER coords');

            Player.setWarMode(true);
            Player.say('ress');
            for (let waited = 0; waited < this.RESS_WAIT_MS && Player.dead; waited += 1000)
                await token.sleep(1000);
        }
        console.log('[lj] resurrected; resuming work');
        await token.sleep(1000);
    }

    // ===== banking / upkeep (thin wrappers over the mixins) =====

    async goToBank() {
        Player.follow(false);
        Player.setWarMode(false);
        await this.walkTo(this.BANK, { range: 1 });
        this.threat = null;
        this.fleeing = false;
    }

    async depositLogs() {
        await this.deposit(this.LOG);
    }

    async restock() {
        await this.restockConsumables(this.CONSUMABLES);
    }

    // ===== chopping =====

    isTree(staticTile) {
        return staticTile.name.includes('tree') && !staticTile.name.includes('leaves');
    }

    classify(journalLine) {
        const text = journalLine.toLowerCase();
        if (text.includes('logs in your backpack')) return 'ok';
        if (text.includes('not enough wood')) return 'depleted';
        if (text.includes('fail to produce')) return 'retry';
        if (text.includes('must wait')) return 'wait';
        if (text.includes('too far')) return 'far';
        if (text.includes("can't use")) return 'badtarget';
        return 'other';
    }

    async ensureAxe() {
        const inPack = this.findInPack(this.AXE);
        if (inPack) {
            console.log('[lj] equipping axe from backpack');
            Player.equip(inPack.serial);
            await this.token.sleep(800);
        }
    }

    async chopOnce(tree) {
        const { token } = this;
        return token.retry(async () => {
            Player.use(this.AXE);
            await token.wait(Promise.all([
                waitForJournal({ contains: 'do you want to use' }),
                Player.once('target', 5000),
            ]));
            Player.target(tree.x, tree.y, tree.z, tree.graphic);
            return token.wait(waitForJournal({ ms: this.CHOP_WAIT_MS }));
        });
    }

    async chopTree(tree) {
        const { x, y, z, graphic } = tree;
        if (!await this.walkTo(tree, { adjacent: true, terrain: false })) return 0;
        this.lastChop = { x: Player.x, y: Player.y };
        console.log(`[lj] chopping (${x},${y})`);
        const { token } = this;
        let chopped = 0, retries = 0;
        while (!this.full() && !Player.dead) {
            token.check();
            const kind = this.classify(await this.chopOnce(tree));
            if (kind === 'ok') { chopped++; retries = 0; continue; }
            if (kind === 'wait' || kind === 'retry') {
                if (++retries > this.MAX_RETRY) break;
                await token.sleep(2000);
                continue;
            }
            if (kind === 'depleted') {
                World.markStump(x, y, z, graphic);
                console.log('[lj] depleted -> stump at', x, y);
            }
            break;
        }
        return chopped;
    }

    // Mark a square of AVOID_RADIUS around (x,y) off-limits for AVOID_TTL_MS, so
    // tree-search and stand-rotation skip it. Used when fleeing a mob.
    avoidArea(x, y) {
        this.avoidAreas.push({ x, y, until: Date.now() + this.AVOID_TTL_MS });
        console.log(`[lj] avoiding (${x},${y}) r=${this.AVOID_RADIUS} for ${this.AVOID_TTL_MS / 1000}s`);
    }

    // True if (x,y) is inside an active avoid-area. Prunes expired areas in passing.
    isAvoided(x, y) {
        const now = Date.now();
        this.avoidAreas = this.avoidAreas.filter((area) => area.until > now);
        return this.avoidAreas.some((area) => tileDistance(area, { x, y }) <= this.AVOID_RADIUS);
    }

    // Move to the next forest stand in the rotation and forget the current chop
    // spot, so the next chop() starts somewhere new (dodges a mob / dead trees).
    // Skips stands that currently sit inside an avoid-area; if every stand is
    // avoided it lands on the last one tried (least-bad).
    nextForest() {
        this.lastChop = null;
        for (let tried = 0; tried < this.FORESTS.length; tried++) {
            this.forestIndex = (this.forestIndex + 1) % this.FORESTS.length;
            if (!this.isAvoided(this.FORESTS[this.forestIndex].x, this.FORESTS[this.forestIndex].y)) break;
        }
        console.log(`[lj] rotating to forest #${this.forestIndex} ` +
            `(${this.FORESTS[this.forestIndex].x},${this.FORESTS[this.forestIndex].y})`);
    }

    async chop() {
        await this.walkTo(this.lastChop ?? this.FORESTS[this.forestIndex]);
        await this.ensureAxe();
        const visited = new Set();
        const { token } = this;
        while (!this.full() && !Player.dead) {
            token.check();
            const here = { x: Player.x, y: Player.y };
            const tree = World.statics(here.x, here.y, this.SEARCH_RADIUS)
                .filter((staticTile) => this.isTree(staticTile))
                .filter((candidate) => !visited.has(candidate.x + ',' + candidate.y))
                .filter((candidate) => !this.isAvoided(candidate.x, candidate.y))
                .reduce((best, candidate) =>
                    (!best || tileDistance(here, candidate) < tileDistance(here, best) ? candidate : best), null);

            if (!tree) {
                console.log('[lj] no trees left in range -> rotating stand');
                this.nextForest();
                await token.sleep(3000);
                return;
            }
            await this.chopTree(tree);
            visited.add(tree.x + ',' + tree.y);
        }
    }

    // Priority order matters: a running step is preempted ONLY by a higher one
    // (bt.js). `eat` must sit BELOW `bank` so it can't interrupt the bank
    // sequence mid-restock — otherwise bread lands in the pack, `eat` preempts,
    // and bank (guard already false after deposit) never resumes to buy bandages
    // or rest.
    behaviors() {
        return [
            { name: 'resurrect', when: () => Player.dead, step: this.step('resurrect') },
            { name: 'fight', when: () => !this.fleeing && Boolean(this.threat?.exists), step: this.sequence('fight', 'rest') },
            { name: 'bank', when: () => this.fleeing || this.full(), step: this.sequence('goToBank', 'depositLogs', 'withdrawGold', 'restock', 'rest') },
            { name: 'eat', when: () => Date.now() - this.lastAteMs > this.EAT_INTERVAL_MS &&
                    Player.equipment.backpack.items.some((item) => [].concat(this.FOOD).some((name) => item.name.includes(name))),
                step: this.step('eatFood') },
            { name: 'chop', when: () => true, step: this.step('chop') },
        ];
    }

    onStart() {
        this.threatMeter.start();
    }

    // Rest to full stamina and stock consumables before the first chop. Awaited by
    // the base class before the tick loop starts (see BehaviorScript._bootstrap),
    // so it has a token and runs exactly once, with no behaviour racing it.
    async onStartup() {
        await this.rest();
        await this.restock();
    }

    onStop() {
        this.threatMeter.stop();
    }

    onPreempt() {
        Player.stop();
    }
}

Object.assign(Lumberjack.prototype, BankSkill, SurvivalSkill);

new Lumberjack().start();
