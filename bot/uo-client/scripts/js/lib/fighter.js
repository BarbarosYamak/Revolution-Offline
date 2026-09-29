'use strict';
// FighterBot — every fighter archetype from one engine, configured by
// lib/archetypes.js:
//
//   melee    swordsman, fencer, macer     swing, bandage, finish, flee
//   ranged   archer                       keep range 4, arrows as ammo, sword when out
//   mage     pure_mage                    cast from range 6, meditate, heal by spell
//   warlock  warlock, fencing_warlock,    melee + Poison opener + heal spell
//            double_warlock
//   tamer    tamer                        tame a pet, send it in, heal it, stay back
//   pk       pk                           hunt players in the wild (ALLOW_PVP only)
//
// Shared by all: hunting-ground choice by skill level and memory, patrol, loot,
// sell, bank, restock (bandages / arrows / reagents), orders gear from crafters
// it has HEARD, danger/hunted memory, schedule, persona.
//
// Usage (scripts/js/bots/<archetype>.js):   new FighterBot('archer').start();
//
// DERIVED, not Revolution-documented: spell choice by Magery, level bands,
// ranges, mana thresholds. Revolution Eval/Magery damage is UNKNOWN (CLAUDE.md
// "verify Magery"), so spells are chosen by circle availability only.
(function (g) {
    const AR = () => g.Archetypes;
    const lower = (s) => String(s || '').toLowerCase();

    // Standard Sphere spell numbers (m36_magery.txt casts 29 = Greater Heal).
    const SPELL = { HEAL: 4, MAGIC_ARROW: 5, HARM: 12, FIREBALL: 18, POISON: 20, GREATER_HEAL: 29,
        LIGHTNING: 30, MIND_BLAST: 37, ENERGY_BOLT: 42, FLAMESTRIKE: 51 };
    // Lowest Magery (tenths) at which a circle is worth attempting. DERIVED:
    // the classic ~12.5-per-circle ladder, not a Revolution table.
    const CIRCLE_MIN = { 1: 0, 2: 150, 3: 300, 4: 400, 5: 500, 6: 650, 7: 750, 8: 900 };
    const ATTACK_SPELLS = [
        { id: SPELL.FLAMESTRIKE, circle: 7 }, { id: SPELL.ENERGY_BOLT, circle: 6 },
        { id: SPELL.MIND_BLAST, circle: 5 }, { id: SPELL.LIGHTNING, circle: 4 },
        { id: SPELL.FIREBALL, circle: 3 }, { id: SPELL.HARM, circle: 2 }, { id: SPELL.MAGIC_ARROW, circle: 1 },
    ];

    function bestAttackSpell(mageryTenths) {
        return ATTACK_SPELLS.find((s) => mageryTenths >= CIRCLE_MIN[s.circle]) || ATTACK_SPELLS[ATTACK_SPELLS.length - 1];
    }

    // Hunting level 1..4 from the primary combat skill (tenths). DERIVED bands.
    function levelFor(skillTenths) {
        if (skillTenths < 0) return 1;
        if (skillTenths < 300) return 1;
        if (skillTenths < 550) return 2;
        if (skillTenths < 800) return 3;
        return 4;
    }

    const REAGENTS = ['black pearl', 'blood moss', 'garlic', 'ginseng', 'mandrake', 'nightshade', 'spider', 'sulfur'];
    // Tameable wildlife bodies to try, easiest first. Whether a given animal is
    // tameable at our skill is the SERVER's answer ("no chance"), remembered.
    const TAMEABLE = [0xd3, 0xcd, 0x06, 0xd4, 0xe1, 0x19, 0x3f, 0xd5];
    const HUMAN = [0x190, 0x191];

    // Copy mixins onto a bot class ONCE. A mixin method with the same name as a
    // class method would silently replace it (the onFlee trap, M4.2), so that
    // is refused loudly instead.
    function applyMixins(Cls, names) {
        if (Cls.__mixed) return;
        for (const n of names) {
            const m = g[n];
            if (!m) throw new Error(`${Cls.name}: mixin ${n} is not loaded`);
            for (const k of Object.keys(m)) {
                if (typeof m[k] === 'function' && Object.prototype.hasOwnProperty.call(Cls.prototype, k))
                    throw new Error(`${Cls.name}.${k} would be overwritten by ${n}.${k}`);
            }
            Object.assign(Cls.prototype, m);
        }
        Cls.__mixed = true;
    }
    g.applyMixins = applyMixins;

    class FighterBot extends BehaviorScript {
        constructor(archetypeId, opts = {}) {
            // Libraries load in sorted file order, so memory.js / survival.js
            // do not exist yet when this file is evaluated. Mix in on first use.
            applyMixins(FighterBot, ['BankSkill', 'SurvivalSkill', 'CombatSkill', 'EconomySkill', 'MemorySkill', 'CustomerSkill']);
            super();
            const a = AR().A[archetypeId];
            if (!a || a.kind !== 'fighter') throw new Error(`FighterBot: '${archetypeId}' is not a fighter archetype`);
            const check = AR().validate(archetypeId);
            if (!check.ok) throw new Error(`FighterBot: ${check.why}`);
            this.archetypeId = archetypeId;
            this.arch = a;
            this.style = a.style;
            this.skills = a.build.skills;
            this.canUsePoisonedWeapon = check.canUsePoisonedWeapon;

            const townId = opts.home || a.home;
            this.townId = townId;
            this.town = AR().TOWNS[townId];
            this.BANK = this.town.bank;
            this.HEALER = this.town.healer;
            this.ALLOW_PVP = opts.allowPvp === true || g.ALLOW_PVP === true;
            this.groundIds = opts.grounds || null;

            const W = AR().WEAPONS;
            this.WEAPONS = [].concat(W[a.primary] || [], a.secondary !== undefined ? (W[a.secondary] || []) : []);
            this.PERSONA = opts.persona || { riskTolerance: this.style === 'pk' ? 0.7 : (this.style === 'mage' ? 0.35 : 0.5), activeHours: [] };
            this.COMBAT_SKILL = a.primary;
            this.USES_BANDAGES = Boolean(this.skills.HEAL);
            this.FOOD = ['bread', 'lamb', 'fish steak'];

            // Consumables from the home town's vendors, where they exist.
            const v = this.town.vendors || {};
            const cons = {};
            if (this.skills.HEAL) cons.bandage = { target: 30, coords: this.HEALER, title: 'healer' };
            if (this.style === 'ranged' && v.bowyer) cons.arrow = { target: 150, low: 30, coords: v.bowyer, title: 'bowyer' };
            if (this.skills.MAGERY && (v.mage || v.alchemist)) {
                const at = v.mage || v.alchemist, title = v.mage ? 'mage' : 'alchemist';
                for (const r of REAGENTS) cons[r] = { target: this.style === 'mage' ? 40 : 20, coords: at, title };
            }
            this.CONSUMABLES = cons;
            this.KEEP = Object.keys(cons);
            this.SELL_VENDORS = ['blacksmith', 'armorer', 'tanner', 'provisioner', 'bowyer']
                .filter((t) => v[t]).map((t) => ({ title: t, coords: v[t] }));
            this.WALLET = 200;
            this.GOLD_RESERVE = 150;
            this.MAX_CARRY_GOLD = this.style === 'pk' ? 3000 : 800;
            this.ENGAGE_MIN_HP_FRAC = 0.8;
            this.SEEK_RADIUS = this.style === 'mage' || this.style === 'ranged' ? 12 : 10;
            this.DWELL_MIN_MS = 1500;
            this.DWELL_MAX_MS = 4000;
            this.AVOID_RADIUS = 6;
            this.AVOID_TTL_MS = 2 * 60 * 1000;

            this.threat = null;
            this.fleeing = false;
            this.lastAteMs = 0;
            this.lastBandageMs = 0;
            this.avoidAreas = [];
            this.pet = null;             // serial of our tamed pet
            this.tameRefused = new Set(); // bodies the server said we cannot tame
            this.gearOrdered = false;

            this.threatMeter = createThreatMeter({
                onLevel: () => {},
                onDanger: (top) => { if (top && this.shouldDefend(top)) this.engage(top, 'threat'); },
            });
            this.installCombatSensing();
            this.economyInit();
            this.memoryInit();
            this.listenForCrafters();
        }

        // ===== skill / level / ground =====

        primarySkill() { return typeof Player.skill === 'function' ? Player.skill(this.arch.primary) : -1; }

        huntingLevel() { return levelFor(this.primarySkill()); }

        // Grounds at our level (or the nearest level below), close to home,
        // picked with the danger memory -- never a place far across the world.
        chooseGround() {
            const all = AR().GROUNDS;
            const ids = this.groundIds || Object.keys(all);
            const home = this.BANK;
            const near = ids.filter((id) => all[id] && tileDistance(all[id].centre, home) <= 900);
            const pool = near.length ? near : ids.filter((id) => all[id]);
            const lvl = this.huntingLevel();
            let fit = [];
            for (let l = lvl; l >= 1 && fit.length === 0; l--) fit = pool.filter((id) => all[id].level === l);
            if (!fit.length) fit = pool;
            const cands = fit.map((id) => ({ id, x: all[id].centre.x, y: all[id].centre.y }));
            const pick = this.pickByDanger(cands) || cands[0];
            return pick ? { id: pick.id, ...all[pick.id] } : null;
        }

        currentGround() {
            if (!this.ground || Date.now() - (this.groundPickedMs || 0) > 30 * 60 * 1000) {
                this.ground = this.chooseGround();
                this.groundPickedMs = Date.now();
                if (this.ground) console.log(`[fighter] ${this.archetypeId}: hunting at ${this.ground.id} (level ${this.ground.level})`);
            }
            return this.ground;
        }

        inGround(x, y) {
            const gr = this.ground;
            if (!gr) return false;
            if (gr.rects) return gr.rects.some((r) => x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2);
            return tileDistance({ x, y }, gr.centre) <= gr.radius;
        }

        isAvoided(x, y) {
            const now = Date.now();
            this.avoidAreas = this.avoidAreas.filter((a) => a.until > now);
            return this.avoidAreas.some((a) => tileDistance(a, { x, y }) <= this.AVOID_RADIUS);
        }

        randomTileInGround() {
            const gr = this.ground;
            for (let tries = 0; tries < 20; tries++) {
                let p;
                if (gr.rects) {
                    const areas = gr.rects.map((r) => (r.x2 - r.x1 + 1) * (r.y2 - r.y1 + 1));
                    let pick = Math.random() * areas.reduce((a, b) => a + b, 0), i = 0;
                    while (pick >= areas[i]) pick -= areas[i++];
                    const r = gr.rects[i];
                    p = { x: r.x1 + Math.floor(Math.random() * (r.x2 - r.x1 + 1)), y: r.y1 + Math.floor(Math.random() * (r.y2 - r.y1 + 1)) };
                } else {
                    p = { x: gr.centre.x + Math.round((Math.random() * 2 - 1) * gr.radius),
                          y: gr.centre.y + Math.round((Math.random() * 2 - 1) * gr.radius) };
                }
                if (!this.isAvoided(p.x, p.y)) return p;
            }
            return gr.centre;
        }

        waypoint() {
            const c = [];
            for (let i = 0; i < 5; i++) c.push(this.randomTileInGround());
            return this.pickByDanger(c) || this.ground.centre;
        }

        // ===== targets =====

        isHumanPlayer(m) { return HUMAN.includes(m.body); }

        // PvE prey: wild hostiles only (gray/orange/red), of the ground's kind.
        // PvP (pk, ALLOW_PVP only): blue humans away from every town bank.
        findPrey() {
            const here = { x: Player.x, y: Player.y };
            const gr = this.ground;
            const bodies = gr ? (AR().PREY_BODIES[gr.prey] || []) : [];
            const ok = (m) => {
                if (!m.exists || m.serial === Player.serial || m.serial === this.pet) return false;
                if (this.isAvoided(m.x, m.y)) return false;
                if (tileDistance(here, { x: m.x, y: m.y }) > this.SEEK_RADIUS) return false;
                if (this.style === 'pk' && this.ALLOW_PVP && this.isHumanPlayer(m) && m.notoriety === 1)
                    return Object.values(AR().TOWNS).every((t) => tileDistance(t.bank, { x: m.x, y: m.y }) > 80);
                return m.notoriety >= 3 && m.notoriety <= 6 && !this.isHumanPlayer(m) && bodies.includes(m.body);
            };
            return Mobiles.all().filter(ok).sort((a, b) => tileDistance(here, a) - tileDistance(here, b))[0] || null;
        }

        // Defend against anything hostile that comes for us; never start on a
        // blue unless we are a pk with PvP allowed.
        shouldDefend(serial) {
            const m = Mobiles.get(serial);
            if (!m.exists) return false;
            if (m.notoriety === 1 || m.notoriety === 2) return this.style === 'pk' && this.ALLOW_PVP;
            return true;
        }

        // ===== style hooks read by CombatSkill.fight =====

        meleeInFight() {
            if (this.style === 'mage') return this.manaFrac() < 0.15;   // out of mana: wrestle
            if (this.style === 'tamer') return !this.pet || !Mobiles.get(this.pet).exists;
            return true;
        }

        followRange() {
            if (this.style === 'mage') return 6;
            if (this.style === 'tamer' && this.pet && Mobiles.get(this.pet).exists) return 4;
            if (this.style === 'ranged' && this.backpackCount(['arrow']) > 0) return 4;
            return 1;
        }

        manaFrac() { return Player.manaMax > 0 ? Player.mana / Player.manaMax : 0; }

        async castAt(spell, target) {
            Player.cast(spell, target);
            await this.token.sleep(1500 + 250 * (spell > 40 ? 4 : spell > 24 ? 2 : 1));
        }

        async combatTick() {
            const foe = this.threat;
            if (!foe || !foe.exists) return;
            const magery = Player.skill(AR().SK.MAGERY);
            const now = Date.now();

            if (this.style === 'ranged' && this.backpackCount(['arrow']) === 0 && !this.switchedToSword) {
                const sword = this.findWeaponFor(this.arch.secondary);
                if (sword) { console.log('[fighter] out of arrows -> sword'); Player.equip(sword.serial); this.switchedToSword = true; }
            }

            if (this.style === 'mage' || this.style === 'warlock') {
                // Heal by spell first: faster than a bandage, and a caster has the mana.
                if (this.hpFrac() < 0.55 && this.manaFrac() > 0.2 && now - (this.lastCastMs || 0) > 2500) {
                    this.lastCastMs = now;
                    await this.castAt(magery >= CIRCLE_MIN[4] ? SPELL.GREATER_HEAL : SPELL.HEAL, Player.serial);
                    return;
                }
            }
            if (this.style === 'mage' && this.manaFrac() > 0.15 && now - (this.lastCastMs || 0) > 2500) {
                this.lastCastMs = now;
                await this.castAt(bestAttackSpell(magery).id, foe.serial);
            }
            if (this.style === 'warlock' && this.skills.POI && this.poisonedFoe !== foe.serial && this.manaFrac() > 0.3) {
                // Open with the Poison spell (L4: a warlock poisons by SPELL,
                // never by a poisoned blade above Magery 40).
                this.poisonedFoe = foe.serial;
                this.lastCastMs = now;
                await this.castAt(SPELL.POISON, foe.serial);
            }
            if (this.style === 'tamer' && this.pet && Mobiles.get(this.pet).exists && this.petTarget !== foe.serial) {
                this.petTarget = foe.serial;
                Player.say('all kill');
                try { await this.token.wait(Player.once('target', 3000)); Player.target(foe.serial); }
                catch (e) { if (this.token.cancelled) throw CANCELLED; }
            }
        }

        // ===== gear =====

        findWeaponFor(skill) {
            const names = AR().WEAPONS[skill] || [];
            return Player.equipment.backpack.items.find((i) => names.some((n) => lower(i.name).includes(n)));
        }

        async ensureWeapon() {
            if (this.style === 'mage' || this.style === 'tamer') return;
            const w = this.findWeaponFor(this.switchedToSword ? this.arch.secondary : this.arch.primary);
            if (w) { Player.equip(w.serial); await this.token.sleep(800); }
        }

        // What gear to order, by style. Piece names are menu labels; the
        // crafter checks them against its live menu (UNVERIFIED labels).
        gearOrder() {
            switch (this.style) {
                case 'ranged': return { craft: 'bowyer', text: 'bow' };
                case 'mage': return { craft: 'tailor', text: 'cloth outfit set' };
                case 'tamer': return { craft: 'tailor', text: 'leather set' };
                default: return { craft: 'blacksmith', text: 'ringmail set' };
            }
        }

        needsGear() { return !this.gearOrdered && !this.pendingOrder && this.spendable(false) >= 200; }

        async orderGear() {
            const o = this.gearOrder();
            const c = this.knownCrafter(o.craft);
            this.gearOrdered = true;   // once per life; a refusal is not retried every tick
            if (!c) { console.log(`[fighter] no ${o.craft} heard of yet; will listen in town`); this.gearOrdered = false; this.gearRetryMs = Date.now() + 20 * 60 * 1000; return; }
            console.log(`[fighter] ordering "${o.text}" from ${c.name} the ${o.craft}`);
            await this.placeOrder(c.name, { x: c.x, y: c.y }, o.text);
        }

        // ===== behaviours =====

        onFlee(mob) {
            const ttl = this.isHunted() ? 3 * this.AVOID_TTL_MS : this.AVOID_TTL_MS;
            this.avoidAreas.push({ x: mob.x, y: mob.y, until: Date.now() + ttl });
        }

        async recover() {
            if (this.ground && this.inGround(Player.x, Player.y)) {
                Player.follow(false); Player.setWarMode(false);
                // Step out of the ground toward home before healing.
                const c = this.ground.centre, h = this.BANK;
                const d = Math.max(1, tileDistance(c, h));
                const out = { x: Math.round(c.x + (h.x - c.x) * 30 / d), y: Math.round(c.y + (h.y - c.y) * 30 / d) };
                await this.walkTo(out, { range: 3 });
            }
            this.fleeing = false;
            this.threat = null;
            if (this.style === 'mage' || this.style === 'warlock') {
                while (!Player.dead && this.hpFrac() < 0.9 && this.manaFrac() > 0.2 && !this.threat?.exists) {
                    await this.castAt(Player.skill(AR().SK.MAGERY) >= CIRCLE_MIN[4] ? SPELL.GREATER_HEAL : SPELL.HEAL, Player.serial);
                }
            }
            if (this.USES_BANDAGES) await this.rest();
            if (this.skills.MEDI && this.manaFrac() < 0.8) await this.meditate();
            const until = this.isHunted() ? 0.99 : this.ENGAGE_MIN_HP_FRAC;
            while (!Player.dead && this.hpFrac() < until && !this.threat?.exists) await this.token.sleep(5000);
        }

        async meditate() {
            for (let i = 0; i < 20 && !Player.dead && this.manaFrac() < 0.95 && !this.threat?.exists; i++) {
                Player.useSkill(AR().SK.MEDI);
                await this.token.sleep(4000);
            }
        }

        // No prey in sight: casters train Magery on themselves (m36_magery.txt:
        // meditate, then Greater Heal self), tamers try to tame, others walk.
        async trainIdle() {
            if ((this.style === 'mage' || this.style === 'warlock') && this.manaFrac() > 0.4) {
                await this.castAt(SPELL.GREATER_HEAL, Player.serial);
                return true;
            }
            if (this.style === 'tamer' && (!this.pet || !Mobiles.get(this.pet).exists)) return this.tryTame();
            return false;
        }

        async tryTame() {
            const here = { x: Player.x, y: Player.y };
            const cand = Mobiles.all().filter((m) => m.exists && TAMEABLE.includes(m.body) && !this.tameRefused.has(m.body) &&
                m.notoriety === 3 && tileDistance(here, m) <= 10)
                .sort((a, b) => TAMEABLE.indexOf(a.body) - TAMEABLE.indexOf(b.body))[0];
            if (!cand) return false;
            await this.walkTo({ x: cand.x, y: cand.y }, { adjacent: true });
            Player.useSkill(AR().SK.TAMING, cand.serial);
            let line = '';
            try { line = lower(await this.token.wait(waitForJournal({ ms: 12000, contains: /tame|chance|accept|master/i }))); }
            catch (e) { if (this.token.cancelled) throw CANCELLED; }
            if (/no chance|too difficult|cannot/.test(line)) { this.tameRefused.add(cand.body); return true; }
            if (/accept|master|tame it/.test(line) && !/fail/.test(line)) {
                this.pet = cand.serial;
                console.log(`[fighter] tamed 0x${cand.serial.toString(16)} (body 0x${cand.body.toString(16)})`);
                Player.say('all follow me');
            }
            return true;
        }

        async patrol() {
            const { token } = this;
            const gr = this.currentGround();
            if (!gr) { await token.sleep(5000); return; }
            if (!this.inGround(Player.x, Player.y)) {
                if (!await this.walkTo(gr.centre, { range: 3 })) {
                    console.warn(`[fighter] could not reach ${gr.id}; choosing again`);
                    this.noteDanger(gr.centre.x, gr.centre.y, 1, 'unreachable');
                    this.ground = null;
                    await token.sleep(3000);
                    return;
                }
            }
            await this.ensureWeapon();
            const prey = this.findPrey();
            if (prey && this.hpFrac() >= this.ENGAGE_MIN_HP_FRAC) { this.engage(prey.serial, 'hunting'); return; }
            if (await this.trainIdle()) return;
            if (!await this.walkTo(this.waypoint(), { terrain: false })) return;
            await token.sleep(this.DWELL_MIN_MS + Math.random() * (this.DWELL_MAX_MS - this.DWELL_MIN_MS));
            const after = this.findPrey();
            if (after && this.hpFrac() >= this.ENGAGE_MIN_HP_FRAC) this.engage(after.serial, 'hunting');
        }

        async townTrip() {
            await this.walkTo(this.BANK, { range: 2 });
            this.fleeing = false;
            this.threat = null;
        }

        behaviors() {
            const pvpRed = () => this.style === 'pk';
            return [
                { name: 'resurrect', when: () => Player.dead, step: this.step('resurrect') },
                { name: 'offline', when: () => !this.isActiveNow() && !this.threat?.exists, step: this.step('endSession') },
                { name: 'fight', when: () => !this.fleeing && Boolean(this.threat?.exists), step: this.step('fight') },
                { name: 'loot', when: () => Boolean(this.lastKill) && !this.full(), step: this.step('lootKill') },
                // A murderer is killed by town guards: the pk does not bank.
                { name: 'bank', when: () => !pvpRed() && (this.full() || this.carryingTooMuchGold()) && this.bankTripDue(),
                    step: this.sequence('sellLoot', 'bankSurplusGold', 'stashJunk', 'withdrawGold', 'restock', 'economyReport') },
                { name: 'collect', when: () => Boolean(this.pendingOrder) && Date.now() - this.pendingOrder.placedMs > 10 * 60 * 1000,
                    step: this.step('collectOrder') },
                { name: 'gear', when: () => this.needsGear() && Date.now() > (this.gearRetryMs || 0), step: this.sequence('townTrip', 'orderGear') },
                { name: 'eat', when: () => Date.now() - this.lastAteMs > this.EAT_INTERVAL_MS &&
                        Player.equipment.backpack.items.some((i) => this.FOOD.some((f) => lower(i.name).includes(f))),
                    step: this.step('eatFood') },
                { name: 'recover', when: () => this.fleeing || this.hpFrac() < this.ENGAGE_MIN_HP_FRAC, step: this.step('recover') },
                { name: 'patrol', when: () => true, step: this.step('patrol') },
            ];
        }

        onStart() { this.threatMeter.start(); }

        async onStartup() {
            if (typeof Player.requestSkills === 'function') Player.requestSkills();
            await this.token.sleep(1500);
            console.log(`[fighter] ${this.archetypeId} (${this.arch.build.ref} ${this.arch.build.cls}), ` +
                `primary ${(this.primarySkill() / 10).toFixed(1)}, level ${this.huntingLevel()}`);
            if (this.USES_BANDAGES) await this.rest();
            await this.restock();
        }

        async restock() { await this.restockConsumables(this.CONSUMABLES); }

        onStop() { this.threatMeter.stop(); }
        onPreempt() { Player.stop(); }
    }

    g.FighterBot = FighterBot;
    g.FighterPolicy = { SPELL, CIRCLE_MIN, bestAttackSpell, levelFor, TAMEABLE };
})(globalThis);
