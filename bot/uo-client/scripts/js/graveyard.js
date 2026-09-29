'use strict';
// GraveyardHunter: a swordsman who trains by walking the Britain graveyard and
// fighting the undead its spawners put there -- the M4 plan's "wild, hostile,
// legal targets", at a place that is fixed, always hostile and always known.
//
// Owner report: bots "do not wander around the cemetery". They could not: the
// only autonomous script was the lumberjack, which fights only what attacks it
// in the forest, and every cemetery visit so far was a one-shot M3.9 scenario
// that walked in, fought once and ended. Nothing ever PATROLLED a graveyard.
//
// Priority:  resurrect > fight > loot > bank > eat > recover > patrol.
//
//   loot     open the corpse of what we just killed (lib/economy.js).
//   bank     when the pack is full OR we carry more gold than we can afford to
//            lose: sell what vendors will buy, bank the gold, stash junk,
//            restock within budget, print the ledger.
//
//   patrol   pick a random tile inside the graveyard's AREADEF rects, walk
//            there, pause briefly, look for undead in range, repeat. Walking is
//            what wakes and draws the spawns; the pause is short on purpose --
//            M3.9 lost a character to a 60-second dwell among three undead.
//   recover  hurt but not fighting: step OUT of the graveyard to bandage, so
//            the next spawn does not interrupt every bandage.
//   fight    lib/combat.js, the same loop the lumberjack uses.
//
// Everything is an ordinary player action: walk, look, attack, bandage, buy.

class GraveyardHunter extends BehaviorScript {
    // Britain Graveyard, from data/revolution_atlas.txt (REGION
    // a_britain_graveyard_1, built from the shard's own AREADEF). The spawner
    // itself is at 1369,1475 (runtime/scripts/revolution/f_m39_graveyards.scp).
    GRAVEYARD = {
        name: 'Britain Graveyard',
        rects: [
            { x1: 1336, y1: 1443, x2: 1390, y2: 1493 },
            { x1: 1336, y1: 1494, x2: 1375, y2: 1510 },
        ],
        centre: { x: 1369, y: 1475 },
    };
    // Where to bandage: just outside the east edge (x = 1390). Out of the
    // spawner's usual reach, close enough to walk back. UNVERIFIED walkable --
    // walkTo(range) tolerates an unwalkable exact tile, but if the log shows
    // "could not reach" here, move it.
    REST_SPOT = { x: 1398, y: 1480 };
    // atlas PLACE britain_bank_2 and britain_healer.
    BANK = { x: 1425, y: 1690 };
    HEALER = { x: 1471, y: 1611 };

    // Bodies of the graveyard spawns (lib/threat.js aggressive list): zombie,
    // skeleton, bone mage, lich, and the ghost body spectres/wraiths/shades use.
    PREY_BODIES = [0x03, 0x32, 0x38, 0x18, 0x1a];
    SEEK_RADIUS = 10;            // look this far for something to fight
    ENGAGE_MIN_HP_FRAC = 0.8;    // only START a fight at or above this
    DWELL_MIN_MS = 1500;         // pause at each waypoint...
    DWELL_MAX_MS = 4000;         // ...for a random time in this range
    AVOID_RADIUS = 6;            // around a mob we fled from
    AVOID_TTL_MS = 2 * 60 * 1000;

    FOOD = ['bread', 'lamb'];
    // Who this character is (lib/memory.js). riskTolerance 0 = cautious,
    // 1 = reckless; activeHours = local-time windows it plays in ([] = always).
    PERSONA = { riskTolerance: 0.5, activeHours: [] };
    WEAPONS = ['katana', 'broadsword', 'longsword', 'scimitar', 'cutlass', 'viking', 'axe', 'hatchet'];
    // atlas PLACE britain_healer; the healer sells bandages on this shard
    // (lumberjack.js buys them from a healer the same way).
    CONSUMABLES = {
        'bandage': { target: 20, coords: { x: 1471, y: 1611 }, title: 'healer' },
    };

    // --- economy (lib/economy.js) ---
    // Never sold. Weapons, bandages and food are kept automatically.
    KEEP = [];
    // Where loot is offered, from data/revolution_atlas.txt (Britain PLACEs).
    // Whether each one actually buys a given item is NOT assumed: the vendor's
    // own sell list decides, and the bot remembers the answer.
    SELL_VENDORS = [
        { title: 'blacksmith', coords: { x: 1418, y: 1547 } },  // britain_blacksmith
        { title: 'armorer',    coords: { x: 1481, y: 1584 } },  // britain_armorer
        { title: 'tanner',     coords: { x: 1431, y: 1612 } },  // britain_tanner
        { title: 'provisioner',coords: { x: 1469, y: 1668 } },  // britain_provisioner_2
    ];
    WALLET = 150;           // gold carried for bandages and food
    GOLD_RESERVE = 100;     // never spent on anything but essentials
    MAX_CARRY_GOLD = 800;   // bank before carrying more than death should take

    threat = null;
    fleeing = false;
    lastAteMs = 0;
    lastBandageMs = 0;
    avoidAreas = [];

    constructor() {
        super();
        this.threatMeter = createThreatMeter({
            onLevel: (level, prevLevel, score) =>
                console.warn(`[threat] ${prevLevel} -> ${level}  score=${score.toFixed(1)} mobs=${this.threatMeter.count}`),
            onDanger: (topSerial) => { if (topSerial) this.engage(topSerial, 'threat'); },
        });
        this.installCombatSensing();
        this.economyInit();
        this.memoryInit();
    }

    // ===== helpers =====

    inGraveyard(x, y) {
        return this.GRAVEYARD.rects.some((r) => x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2);
    }

    isAvoided(x, y) {
        const now = Date.now();
        this.avoidAreas = this.avoidAreas.filter((area) => area.until > now);
        return this.avoidAreas.some((area) => tileDistance(area, { x, y }) <= this.AVOID_RADIUS);
    }

    // Several random candidates, then a danger-weighted pick among them: a
    // corner where this character died recently is visited a quarter as often.
    randomWaypoint() {
        const candidates = [];
        for (let i = 0; i < 5; i++) candidates.push(this.randomTile());
        return this.pickByDanger(candidates) || this.GRAVEYARD.centre;
    }

    randomTile() {
        // Pick a rect weighted by area, then a tile in it; skip avoided spots.
        const rects = this.GRAVEYARD.rects;
        const areas = rects.map((r) => (r.x2 - r.x1 + 1) * (r.y2 - r.y1 + 1));
        const total = areas.reduce((a, b) => a + b, 0);
        for (let tries = 0; tries < 20; tries++) {
            let pick = Math.random() * total, i = 0;
            while (pick >= areas[i]) pick -= areas[i++];
            const r = rects[i];
            const p = {
                x: r.x1 + Math.floor(Math.random() * (r.x2 - r.x1 + 1)),
                y: r.y1 + Math.floor(Math.random() * (r.y2 - r.y1 + 1)),
            };
            if (!this.isAvoided(p.x, p.y)) return p;
        }
        return this.GRAVEYARD.centre;
    }

    // Nearest undead we could fight, or null. Wild hostiles only: notoriety
    // gray/orange/red (3..6) AND a graveyard body -- never a blue/green human.
    findPrey() {
        const here = { x: Player.x, y: Player.y };
        return Mobiles.all()
            .filter((m) => m.exists && m.serial !== Player.serial)
            .filter((m) => m.notoriety >= 3 && m.notoriety <= 6)
            .filter((m) => this.PREY_BODIES.includes(m.body))
            .filter((m) => tileDistance(here, { x: m.x, y: m.y }) <= this.SEEK_RADIUS)
            .filter((m) => !this.isAvoided(m.x, m.y))
            .sort((a, b) => tileDistance(here, a) - tileDistance(here, b))[0] || null;
    }

    hasWeaponInPack() {
        return Player.equipment.backpack.items.find((item) =>
            this.WEAPONS.some((w) => item.name.toLowerCase().includes(w)));
    }

    // ===== combat hook =====

    onFlee(mob) {
        // Hunted (fled the same foe twice in a minute): stay away three times longer.
        const ttl = this.isHunted() ? 3 * this.AVOID_TTL_MS : this.AVOID_TTL_MS;
        this.avoidAreas.push({ x: mob.x, y: mob.y, until: Date.now() + ttl });
        console.log(`[gy] avoiding (${mob.x},${mob.y}) r=${this.AVOID_RADIUS} for ${this.AVOID_TTL_MS / 1000}s`);
    }

    // ===== behaviours =====

    async retreatToRest() {
        Player.follow(false);
        Player.setWarMode(false);
        console.log(`[gy] retreating to rest spot ${this.REST_SPOT.x},${this.REST_SPOT.y}`);
        if (!await this.walkTo(this.REST_SPOT, { range: 2 }))
            console.warn('[gy] could not reach the rest spot; resting where we are');
        this.fleeing = false;
        this.threat = null;
    }

    async recover() {
        if (this.inGraveyard(Player.x, Player.y)) await this.retreatToRest();
        // Fled from outside the graveyard (chased out, or never in): the
        // retreat is already done, so the flag must still be cleared here or
        // `recover` would own the body forever.
        this.fleeing = false;
        await this.rest();
        // Out of bandages and still hurt: go and buy some rather than
        // re-entering the graveyard wounded.
        if (this.backpackCount(this.BANDAGE) === 0) await this.restock();
        // Nothing to heal with: wait it out (natural regeneration).
        // Hunted: come back only at full health.
        const until = this.isHunted() ? 0.99 : this.ENGAGE_MIN_HP_FRAC;
        while (!Player.dead && this.hpFrac() < until && !this.threat?.exists) {
            await this.token.sleep(5000);
        }
    }

    async goToBank() {
        Player.follow(false);
        Player.setWarMode(false);
        await this.walkTo(this.BANK, { range: 1 });
        this.threat = null;
        this.fleeing = false;
    }

    async restock() {
        await this.restockConsumables(this.CONSUMABLES);
    }

    async ensureWeapon() {
        const weapon = this.hasWeaponInPack();
        if (weapon) {
            console.log(`[gy] equipping ${weapon.name}`);
            Player.equip(weapon.serial);
            await this.token.sleep(800);
        }
    }

    async patrol() {
        const { token } = this;
        if (!this.inGraveyard(Player.x, Player.y)) {
            console.log(`[gy] heading to ${this.GRAVEYARD.name}`);
            if (!await this.walkTo(this.GRAVEYARD.centre, { range: 3 })) {
                console.warn('[gy] could not reach the graveyard; retrying shortly');
                await token.sleep(5000);
                return;
            }
        }
        await this.ensureWeapon();

        const prey = this.findPrey();
        if (prey) {
            this.engage(prey.serial, 'hunting');
            return;   // `fight` preempts on the next tick
        }

        const wp = this.randomWaypoint();
        // A random tile may be a headstone or a wall: walkTo (range 0) tries it
        // and its eight neighbours once each; if all reject, pick again.
        if (!await this.walkTo(wp, { terrain: false })) return;

        const dwell = this.DWELL_MIN_MS + Math.random() * (this.DWELL_MAX_MS - this.DWELL_MIN_MS);
        await token.sleep(dwell);
        const after = this.findPrey();
        if (after) this.engage(after.serial, 'hunting');
    }

    behaviors() {
        return [
            { name: 'resurrect', when: () => Player.dead, step: this.step('resurrect') },
            { name: 'offline', when: () => !this.isActiveNow() && !this.threat?.exists, step: this.step('endSession') },
            { name: 'fight', when: () => !this.fleeing && Boolean(this.threat?.exists), step: this.step('fight') },
            { name: 'loot', when: () => Boolean(this.lastKill) && !this.full(), step: this.step('lootKill') },
            { name: 'bank', when: () => (this.full() || this.carryingTooMuchGold()) && this.bankTripDue(),
                step: this.sequence('sellLoot', 'bankSurplusGold', 'stashJunk', 'withdrawGold', 'restock', 'economyReport') },
            { name: 'eat', when: () => Date.now() - this.lastAteMs > this.EAT_INTERVAL_MS &&
                    Player.equipment.backpack.items.some((item) => [].concat(this.FOOD).some((name) => item.name.includes(name))),
                step: this.step('eatFood') },
            // Hurt, or just fled: get out, heal, come back.
            { name: 'recover', when: () => this.fleeing || this.hpFrac() < this.ENGAGE_MIN_HP_FRAC,
                step: this.step('recover') },
            { name: 'patrol', when: () => true, step: this.step('patrol') },
        ];
    }

    onStart() {
        this.threatMeter.start();
    }

    async onStartup() {
        await this.rest();
        if (this.backpackCount(this.BANDAGE) === 0) await this.restock();
    }

    onStop() {
        this.threatMeter.stop();
    }

    onPreempt() {
        Player.stop();
    }
}

Object.assign(GraveyardHunter.prototype, BankSkill, SurvivalSkill, CombatSkill, EconomySkill, MemorySkill);

new GraveyardHunter().start();
