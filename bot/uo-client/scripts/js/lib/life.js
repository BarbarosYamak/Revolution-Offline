'use strict';
// LifeSkill — the JS half of the persistent life (M4.6).
//
// The C++ client saves the character record (targets, suppliers, deaths,
// routes, logout point) and now also an opaque `memory` blob that belongs to
// the script. This mixin fills that blob with what the bot learned by playing
// and restores it next session, so a life CONTINUES instead of restarting:
//
//   danger map, exhausted spots, hunted timer    (lib/memory.js)
//   market knowledge: buyers, prices, refusals   (lib/economy.js)
//   ledger totals                                (lib/economy.js)
//   crafters heard advertising                   (lib/crafting.js CustomerSkill)
//   open orders, next order id                   (lib/crafting.js CraftSkill)
//   pet serial, gear already ordered             (lib/fighter.js)
//
// Timestamps are Date.now() -- wall clock in QuickJS -- so, unlike the C++
// steady clock, they need no translation across a logout. Everything is
// optional: without --life-dir, Life.record is null and this does nothing.
//
// Also: the current behaviour is written as the life's objective, so a
// session that ends mid-corpse-run starts the next one knowing it.
(function (g) {
    const VERSION = 1;
    const OBJECTIVE_FOR = {
        resurrect: 'recover_corpse', fight: 'survive', eat: 'eat', gear: 'rearm', collect: 'rearm',
        bank: 'unload', sell: 'unload', material: 'earn', loot: 'earn', work: 'earn',
        recover: 'survive', patrol: 'train', craft: 'train', offline: 'none',
    };
    const PERSIST_EVERY_MS = 30 * 1000;
    const hasLife = () => typeof Life !== 'undefined' && Life.record !== null;

    const LifeSkill = {
        lifeInit() {
            this.life = hasLife() ? Life.record : null;
            if (!this.life) return;
            if (this.life.archetype && this.archetypeId && this.life.archetype !== this.archetypeId)
                console.warn(`[life] this character was created as '${this.life.archetype}' but runs '${this.archetypeId}' -- targets come from the record`);
            let mem = null;
            try { mem = Life.memory ? JSON.parse(Life.memory) : null; } catch (e) { console.warn('[life] saved memory unreadable; starting fresh'); }
            if (mem && mem.v === VERSION) this.lifeRestore(mem);
            console.log(`[life] ${this.life.name}: session ${this.life.sessions}, ${this.life.deaths} death(s) on record` +
                `${this.life.objective && this.life.objective.kind !== 'none' ? `, resuming '${this.life.objective.kind}'` : ''}`);
            if (this.life.lastLogout && this.life.lastLogout.valid && !this.life.lastLogout.safe)
                console.warn('[life] last session logged out somewhere UNSAFE');
        },

        lifeRestore(m) {
            const now = Date.now();
            if (m.danger && this.dangerCells) for (const [k, heat, at] of m.danger) this.dangerCells.set(k, { heat, atMs: at });
            if (m.empty && this.emptySpots) for (const [k, until] of m.empty) if (until > now) this.emptySpots.set(k, until);
            if (m.huntedUntil) this.huntedUntil = m.huntedUntil;
            if (m.market && this.market) for (const [name, e] of m.market)
                this.market.set(name, { buyers: e.buyers || {}, refusedBy: new Set(e.refusedBy || []) });
            if (m.ledger && this.ledger) for (const [k, v] of Object.entries(m.ledger)) this.ledger.totals[k] = v;
            if (m.crafters) { this.crafters = this.crafters || new Map(); for (const [k, c] of m.crafters) this.crafters.set(k, c); }
            if (m.orders && Array.isArray(this.orders)) {
                // Paid work survives; an unpaid quote from last session does not.
                for (const o of m.orders) if (o.status === 'paid' || o.status === 'ready') this.orders.push(o);
                this.nextOrderId = Math.max(this.nextOrderId || 1, m.nextOrderId || 1);
            }
            if (m.pet) this.pet = m.pet;
            if (m.gearOrdered) this.gearOrdered = true;
            if (m.pendingOrder) this.pendingOrder = m.pendingOrder;
        },

        lifeSnapshot() {
            const m = { v: VERSION, savedMs: Date.now() };
            if (this.dangerCells) m.danger = [...this.dangerCells].slice(-200).map(([k, d]) => [k, Math.round(d.heat * 100) / 100, d.atMs]);
            if (this.emptySpots) m.empty = [...this.emptySpots].slice(-500);
            if (this.huntedUntil) m.huntedUntil = this.huntedUntil;
            if (this.market) m.market = [...this.market].slice(-300).map(([n, e]) => [n, { buyers: e.buyers, refusedBy: [...e.refusedBy] }]);
            if (this.ledger) m.ledger = { ...this.ledger.totals };
            if (this.crafters) m.crafters = [...this.crafters];
            if (Array.isArray(this.orders)) {
                m.orders = this.orders.filter((o) => o.status === 'paid' || o.status === 'ready');
                m.nextOrderId = this.nextOrderId;
            }
            if (this.pet) m.pet = this.pet;
            if (this.gearOrdered) m.gearOrdered = true;
            if (this.pendingOrder) m.pendingOrder = this.pendingOrder;
            return m;
        },

        lifePersist() {
            if (!hasLife()) return false;
            const json = JSON.stringify(this.lifeSnapshot());
            const ok = Life.setMemory(json);
            if (!ok) console.warn(`[life] memory not stored (${json.length} bytes)`);
            return ok;
        },

        // Runs beside the behaviours for the whole session. The C++ side
        // writes the file (every minute, on death, on logout); this keeps the
        // memory it writes current.
        async lifeLoop() {
            while (!this._stopped) {
                await delay(PERSIST_EVERY_MS);
                if (this._stopped) break;
                try {
                    this.lifePersist();
                    if (typeof this.trainingDue === 'function' && this.trainingDue()) this.applyTrainingPlan();
                } catch (e) { reportError(e); }
            }
        },

        lifeNoteBehavior(name) {
            if (!hasLife()) return;
            const kind = OBJECTIVE_FOR[name];
            if (!kind || kind === this.lastObjectiveKind) return;
            this.lastObjectiveKind = kind;
            const target = name === 'patrol' && this.ground ? this.ground.id : (name === 'craft' ? this.craftName || '' : '');
            Life.setObjective(kind, target);
        },
    };

    g.LifeSkill = LifeSkill;
})(globalThis);
