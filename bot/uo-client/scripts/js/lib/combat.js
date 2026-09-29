'use strict';
// CombatSkill — engage / fight / flee / resurrect, mixed into a bot prototype:
//     Object.assign(MyBot.prototype, CombatSkill);
// Reads `this.token`, `this.threat`, `this.fleeing`, `this.lastBandageMs`,
// `this.HEALER`; uses bandageSelf/findVendor (lib/survival.js) and walkTo /
// hpFrac (lib/bot.js). Call `this.installCombatSensing()` from the constructor.
// Define `onFlee(mob)` on the bot to react to a retreat (the lumberjack rotates
// stands). Mixin methods are copied onto the prototype with Object.assign, which
// OVERWRITES a same-named class method -- so the mixin must never define a
// method a bot is expected to provide.
//
// WHY THIS MOVED OUT OF lumberjack.js, AND WHAT CHANGED (M4)
//
// Owner report: bots "always ask for bandages" and "disengage way too quickly".
// Both were true, and both were in the fight loop:
//
//   * It bandaged mid-melee whenever HP dropped below 80%, every 6 seconds.
//     A fighter spent the whole fight double-clicking bandages, burned through
//     its stock, and then walked to the healer to buy more -- the "trading
//     requests" that never stopped.
//   * It fled after FOUR seconds if it had lost 10% HP and the foe's bar had not
//     moved 2%. Four seconds is two swings; a new character with low
//     Swordsmanship misses most of them, so almost every fight "could not dent
//     the foe" before the first hit landed. And the foe's bar was requested only
//     once, so a stale reading also read as "not dented".
//   * The losing-race check fled on a TIE, i.e. on the first unlucky exchange.
//   * Nothing distinguished a foe on its last legs: bots walked away from kills.
//
// Every number below is DERIVED bot tuning, not a Revolution-documented value,
// and is overridable as a class field on the bot.
(function (g) {
    const CombatSkill = {
        FLEE_HP_FRAC: 0.3,          // hard floor: below this, leave
        FINISH_FOE_FRAC: 0.25,      // a foe at or below this is finished, not fled
        HEAL_HP_FRAC: 0.5,          // mid-fight bandage only below this...
        BANDAGE_INTERVAL_MS: 10000, // ...and at most this often
        COMBAT_TICK_MS: 1100,
        LOSE_ASSESS_MS: 10000,      // read a trend only after this long
        LOSE_MIN_HP_LOST: 0.2,      // and only once we have lost this much
        LOSE_MARGIN: 0.75,          // flee only if clearly losing, not on a tie
        STATUS_REFRESH_MS: 5000,    // re-request the foe's bar so it is not stale
        RESS_WAIT_MS: 8000,

        installCombatSensing() {
            Player.on('attacked', (serial) => this.engage(serial, 'ATTACKED by'));
            Player.on('combat', (serial) => this.engage(serial, 'fighting'));
            Player.on('dialog', (dialog) => {
                if (!/resurrect|come back to life/i.test(dialog.question)) return;
                const yes = dialog.options.find((option) => /^\s*yes/i.test(option.text)) || dialog.options[0];
                if (!yes) return;
                console.log(`[combat] resurrect dialog -> "${yes.text.trim()}"`);
                Player.dialogRespond(yes.index);
            });
            Player.on('resurrect_menu', (event) => {
                console.log(`[combat] resurrect menu (action=${event.action}) -> confirming`);
                Player.resurrect();
            });
        },

        engage(serial, why) {
            if (!serial || serial === Player.serial) return;
            if (this.fleeing) return;   // escaping/resting: don't pick a new fight
            if (this.threat && this.threat.exists) return;
            this.threat = Mobiles.get(serial);
            Player.requestStatus(serial);
            console.warn(`[combat] ${why} ${this.threat.name || '0x' + serial.toString(16)}` +
                ` noto=${this.threat.notoriety} hp=${this.threat.hpPct >= 0 ? (this.threat.hpPct * 100 | 0) + '%' : '?'}` +
                ` at ${this.threat.x},${this.threat.y}`);
        },

        foeNearlyDead() {
            const foeHp = this.threat ? this.threat.hpPct : -1;
            return foeHp >= 0 && foeHp <= this.FINISH_FOE_FRAC;
        },

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

            // Hard floor. Checked first: a nearly dead foe can still land the
            // hit that kills a nearly dead bot.
            if (myHp < this.FLEE_HP_FRAC) return { flee: true, why: `HP ${(myHp * 100) | 0}% < floor` };

            // Above the floor, never turn your back on a foe one swing from dead.
            if (this.foeNearlyDead()) return { flee: false };

            // Trend check, only after the fight has run long enough to mean
            // something, and only once it has actually cost us.
            if (baseline.startMs && now - baseline.startMs >= this.LOSE_ASSESS_MS) {
                const myHpLost = baseline.startMyHp - myHp;
                const foeHpLost = baseline.startFoeHp - foeHp;
                if (myHpLost >= this.LOSE_MIN_HP_LOST) {
                    if (foeHpLost <= 0.02)
                        return { flee: true, why: `cannot dent foe in ${((now - baseline.startMs) / 1000) | 0}s (foe ${(foeHp * 100) | 0}%)` };
                    // Crude time-to-die for each side: remaining HP / loss-so-far.
                    const myTimeToDie = myHp / myHpLost, foeTimeToDie = foeHp / foeHpLost;
                    if (myTimeToDie < foeTimeToDie * this.LOSE_MARGIN)
                        return { flee: true, why: `losing race (me ${myTimeToDie.toFixed(1)} < foe ${foeTimeToDie.toFixed(1)} x${this.LOSE_MARGIN})` };
                }
            }
            return { flee: false };
        },

        async fight() {
            const { token } = this;
            token.onCancel(() => { Player.follow(false); Player.setWarMode(false); });
            let followSerial = 0;
            let lastStatusMs = Date.now();
            const baseline = { startMs: 0, startMyHp: -1, startFoeHp: -1 };
            let lastSeen = null;
            while (this.threat?.exists && !Player.dead) {
                lastSeen = { serial: this.threat.serial, x: this.threat.x, y: this.threat.y };
                const verdict = this.assessFight(baseline);
                if (verdict.flee) {
                    const foeName = this.threat?.name || '0x' + (this.threat?.serial ?? 0).toString(16);
                    console.warn(`[combat] ${verdict.why} -> flee from ${foeName} (hp ${Player.hp}/${Player.hpMax})`);
                    this.fleeing = true;
                    // Optional bot hook. NOT defined here: Object.assign onto
                    // the prototype would overwrite the bot's own method.
                    if (typeof this.onFlee === 'function')
                        this.onFlee(this.threat?.exists ? this.threat : Player);
                    return;
                }
                if (!Player.warMode) Player.setWarMode(true);
                Player.attack(this.threat.serial);

                if (followSerial !== this.threat.serial) { Player.follow(this.threat.serial, 1); followSerial = this.threat.serial; }

                // Keep the foe's health bar fresh: a reading that never moves
                // looks exactly like a foe we cannot hurt.
                if (Date.now() - lastStatusMs >= this.STATUS_REFRESH_MS) {
                    Player.requestStatus(this.threat.serial);
                    lastStatusMs = Date.now();
                }

                // Mid-fight bandage: only when genuinely hurt, never on a foe we
                // are about to kill, and throttled. The gate is set before the
                // await so a no-op (no bandage) still throttles.
                if (this.hpFrac() < this.HEAL_HP_FRAC && !this.foeNearlyDead() &&
                    Date.now() - (this.lastBandageMs || 0) > this.BANDAGE_INTERVAL_MS) {
                    this.lastBandageMs = Date.now();
                    await this.bandageSelf();
                }
                await token.sleep(this.COMBAT_TICK_MS);
            }
            Player.follow(false);
            if (Player.warMode) Player.setWarMode(false);
            // The foe left view while we were alive and not fleeing: it died or
            // it ran. Record where, and let a corpse carrying its serial decide
            // which (lib/economy.js lootKill) -- the corpse is the proof, not
            // our guess.
            if (lastSeen && !Player.dead && !this.fleeing)
                this.lastKill = { ...lastSeen, atMs: Date.now() };
            this.threat = null;
        },

        async resurrect() {
            console.warn('[combat] DEAD -> heading to healer to resurrect');
            this.threat = null;
            this.fleeing = false;
            Player.follow(false);
            const { token } = this;
            while (Player.dead) {
                await this.walkTo(this.HEALER);

                const healer = await this.findVendor('healer', this.HEALER);
                if (healer) await this.walkTo({ x: healer.x, y: healer.y }, { adjacent: true });
                else console.warn('[combat] no healer found near HEALER coords');

                Player.setWarMode(true);
                Player.say('ress');
                for (let waited = 0; waited < this.RESS_WAIT_MS && Player.dead; waited += 1000)
                    await token.sleep(1000);
            }
            console.log('[combat] resurrected; resuming');
            await token.sleep(1000);
        },
    };

    g.CombatSkill = CombatSkill;
})(globalThis);
