'use strict';
// MemorySkill — what THIS character has lived through, and who it is.
//     Object.assign(MyBot.prototype, MemorySkill);  then this.memoryInit()
//
//   * danger map    places where it was hurt, fled or died, as "heat" that
//                   decays (half-life DANGER_HALF_LIFE_MS). Only what the bot
//                   itself witnessed -- never a shared or global map.
//   * hunted        fleeing the SAME foe twice within HUNTED_WINDOW_MS means
//                   it is being hunted: stay away longer, heal fully first.
//   * empty spots   a resource spot the server said was exhausted ("not
//                   enough wood") is skipped for EMPTY_SPOT_MS.
//   * persona       riskTolerance (0 cautious .. 1 reckless) and activeHours,
//                   the per-character play schedule of CLAUDE.md.
//   * experience    the flee floor rises for a character whose combat skill is
//                   still low: a novice misses most swings, so its fights run
//                   long and it should leave earlier.
//
// Ideas from Klein187/uo-offline (BotDangerMap heat/decay, "hunted",
// persona phases) and PikkonMG/UOTerm (empty-spot cooldown, persona
// active_hours / risk_tolerance) -- ideas only, no code. Every number is
// DERIVED bot tuning. Note uo-offline scales retreat the OTHER way (veterans
// retreat earlier, having more to lose); ours follows from how long a fight
// lasts, and the persona can override either way.
(function (g) {
    const CELL = 16;   // danger is coarse on purpose: a place, not a tile

    // ---- pure helpers (exported for tests) --------------------------------

    function decayed(heat, ageMs, halfLifeMs) {
        return heat * Math.pow(0.5, ageMs / halfLifeMs);
    }

    // True when `date` falls inside any [startHour, endHour) window; a window
    // may wrap midnight ([22, 2]). No windows = always active.
    function isActiveAt(activeHours, date) {
        if (!activeHours || activeHours.length === 0) return true;
        const h = date.getHours() + date.getMinutes() / 60;
        return activeHours.some(([start, end]) =>
            start <= end ? (h >= start && h < end) : (h >= start || h < end));
    }

    // Flee floor for a character: base, shifted by persona risk (reckless
    // leaves later, cautious earlier) and by inexperience.
    //   skillTenths  combat skill in tenths, or -1 unknown (treated as novice)
    function personalFloor(base, riskTolerance, skillTenths) {
        const risk = Math.min(1, Math.max(0, riskTolerance ?? 0.5));
        const skill = skillTenths >= 0 ? Math.min(1000, skillTenths) / 1000 : 0;
        const floor = base + (0.5 - risk) * 0.2 + (1 - skill) * 0.1;
        return Math.min(0.6, Math.max(0.15, floor));
    }

    g.MemoryPolicy = { decayed, isActiveAt, personalFloor, CELL };

    const MemorySkill = {
        DANGER_HALF_LIFE_MS: 45 * 60 * 1000,
        DANGER_AVOID_HEAT: 3.0,        // above this a place is picked 1/4 as often
        HUNTED_WINDOW_MS: 60 * 1000,
        EMPTY_SPOT_MS: 20 * 60 * 1000,
        COMBAT_SKILL: 40,              // Swordsmanship ([SKILL 40])

        memoryInit() {
            this.dangerCells = new Map();   // "cx,cy" -> {heat, atMs}
            this.fleeLog = new Map();       // foe serial -> [ms...]
            this.emptySpots = new Map();    // "x,y" -> untilMs
            this.huntedUntil = 0;
        },

        // --- danger --------------------------------------------------------
        cellKey(x, y) { return `${Math.floor(x / CELL)},${Math.floor(y / CELL)}`; },

        noteDanger(x, y, amount, why) {
            if (!this.dangerCells) this.memoryInit();
            const key = this.cellKey(x, y), now = Date.now();
            const cur = this.dangerCells.get(key);
            const heat = (cur ? decayed(cur.heat, now - cur.atMs, this.DANGER_HALF_LIFE_MS) : 0) + amount;
            this.dangerCells.set(key, { heat, atMs: now });
            console.log(`[memory] danger +${amount} at (${x},${y}) -> ${heat.toFixed(1)} (${why})`);
        },

        dangerAt(x, y) {
            const cur = this.dangerCells && this.dangerCells.get(this.cellKey(x, y));
            return cur ? decayed(cur.heat, Date.now() - cur.atMs, this.DANGER_HALF_LIFE_MS) : 0;
        },

        // Weighted pick: a hot place keeps a quarter of its chance, it is not
        // banned -- a bot that never returns anywhere it was hurt stops playing.
        pickByDanger(candidates, rand = Math.random) {
            if (!candidates.length) return null;
            const w = candidates.map((c) => (this.dangerAt(c.x, c.y) > this.DANGER_AVOID_HEAT ? 0.25 : 1));
            let r = rand() * w.reduce((a, b) => a + b, 0);
            for (let i = 0; i < candidates.length; i++) { if ((r -= w[i]) < 0) return candidates[i]; }
            return candidates[candidates.length - 1];
        },

        // --- hunted --------------------------------------------------------
        // Call on every flee. Returns true when this is a repeat flee from the
        // same foe inside the window.
        noteFlee(foeSerial, x, y) {
            if (!this.fleeLog) this.memoryInit();
            const now = Date.now();
            const times = (this.fleeLog.get(foeSerial) || []).filter((t) => now - t < this.HUNTED_WINDOW_MS);
            times.push(now);
            this.fleeLog.set(foeSerial, times);
            if (this.fleeLog.size > 64) this.fleeLog.delete(this.fleeLog.keys().next().value);
            this.noteDanger(x, y, 1.5, 'fled');
            if (times.length >= 2) {
                this.huntedUntil = now + 5 * 60 * 1000;
                console.warn(`[memory] hunted by 0x${(foeSerial >>> 0).toString(16)} -- staying away longer`);
                return true;
            }
            return false;
        },

        isHunted() { return Date.now() < (this.huntedUntil || 0); },

        // --- empty resource spots -----------------------------------------
        noteEmptySpot(x, y) {
            if (!this.emptySpots) this.memoryInit();
            this.emptySpots.set(`${x},${y}`, Date.now() + this.EMPTY_SPOT_MS);
            if (this.emptySpots.size > 2000) this.emptySpots.delete(this.emptySpots.keys().next().value);
        },

        isEmptySpot(x, y) {
            const until = this.emptySpots && this.emptySpots.get(`${x},${y}`);
            if (!until) return false;
            if (until <= Date.now()) { this.emptySpots.delete(`${x},${y}`); return false; }
            return true;
        },

        // --- persona / schedule -------------------------------------------
        persona() { return this.PERSONA || {}; },

        isActiveNow(date = new Date()) { return isActiveAt(this.persona().activeHours, date); },

        // The flee floor this character uses before gang pressure.
        baseFleeFloor() {
            const skill = typeof Player.skill === 'function' ? Player.skill(this.COMBAT_SKILL) : -1;
            return personalFloor(this.FLEE_HP_FRAC, this.persona().riskTolerance, skill);
        },

        // Off-schedule: walk somewhere guarded (the bank) and log out -- the
        // "log out somewhere safe" rule of the M4 plan.
        async endSession() {
            console.log('[memory] outside active hours -- heading to the bank to log out');
            Player.follow(false);
            Player.setWarMode(false);
            if (this.BANK) await this.walkTo(this.BANK, { range: 2 });
            if (typeof this.economyReport === 'function') this.economyReport();
            await this.token.sleep(2000);
            Player.logout();
            await this.token.sleep(60 * 1000);   // the session is closing
        },
    };

    g.MemorySkill = MemorySkill;
})(globalThis);
