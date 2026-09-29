'use strict';
// GatherSkill — getting raw materials by playing: chop logs, mine ore, smelt
// it, fish. Used by crafters whose input the vendor policy refuses to sell
// (logs for carpenters and bowyers, fish for cooks) and by the gatherer bots
// (miner, fisher). The lumberjack keeps its own proven script.
//
// Evidence per activity:
//   logs    PROVEN  hatchet on a tree static (m37_slice_d_carpenter.txt,
//                   lumberjack.js)
//   ore     PROVEN  pickaxe on the mountain tile (m37_slice_b_minoc.txt:
//                   stand 2564,499, target 2563,499,20)
//   smelt   PROVEN  double-click the ore near a forge (same scenario, 2562,501)
//   fish    UNVERIFIED  fishing pole on a water tile at a dock
//
// GathererBot at the bottom runs a miner or a fisher from the same table.
(function (g) {
    const lower = (s) => String(s || '').toLowerCase();

    // Spots from data/revolution_atlas.txt and the proving scenarios.
    const SPOTS = {
        lumber: {
            britain: [{ x: 1416, y: 1904 }, { x: 1419, y: 1900 }, { x: 1425, y: 1908 }],   // m37_slice_d
            trinsic: [{ x: 1776, y: 2774 }],                                              // lumberjack.js
            yew: [{ x: 600, y: 1050 }],        // UNVERIFIED: Yew forest edge, not yet walked
            minoc: [{ x: 2540, y: 620 }],      // UNVERIFIED
            vesper: [{ x: 2780, y: 780 }],     // UNVERIFIED
        },
        // stand = where to stand, tile = the mountain tile to target
        mine: {
            minoc: [{ stand: { x: 2564, y: 499 }, tile: { x: 2563, y: 499, z: 20 }, forge: { x: 2562, y: 501 }, proven: true }],
            britain: [{ stand: { x: 1443, y: 1228 }, tile: null, forge: null, proven: false }],   // atlas brit_mine1
        },
        fish: {
            britain: [{ x: 1479, y: 1757 }], vesper: [{ x: 3009, y: 828 }], jhelom: [{ x: 1502, y: 3989 }],
            skara: [{ x: 617, y: 2276 }], minoc: [{ x: 2255, y: 1191 }],
        },
    };

    const GatherSkill = {
        gatherSpots(kind) {
            const byTown = SPOTS[kind] || {};
            return byTown[this.townId] || Object.values(byTown)[0] || [];
        },

        // Chop until `want` logs are carried (or the pack is full).
        async gatherLogs(want) {
            const { token } = this;
            const axe = this.findInPack('hatchet') || this.findInPack('axe');
            if (!axe) { console.warn('[gather] no hatchet/axe to chop with'); return false; }
            Player.equip(axe.serial);
            await token.sleep(800);
            for (const spot of this.gatherSpots('lumber')) {
                if (this.backpackCount(['log']) >= want || this.full()) break;
                if (!await this.walkTo(spot, { range: 2 })) continue;
                const trees = World.statics(Player.x, Player.y, 12)
                    .filter((s) => lower(s.name).includes('tree') && !lower(s.name).includes('leaves'))
                    .filter((s) => !(this.isEmptySpot && this.isEmptySpot(s.x, s.y)));
                for (const t of trees) {
                    if (this.backpackCount(['log']) >= want || this.full()) break;
                    if (!await this.walkTo(t, { adjacent: true, terrain: false })) continue;
                    for (let i = 0; i < 8; i++) {
                        token.check();
                        Player.use(axe.serial);
                        try { await token.wait(Player.once('target', 4000)); } catch (e) { if (token.cancelled) throw CANCELLED; break; }
                        Player.target(t.x, t.y, t.z, t.graphic);
                        let line = '';
                        try { line = lower(await token.wait(waitForJournal({ ms: 12000 }))); } catch (e) { if (token.cancelled) throw CANCELLED; }
                        if (line.includes('not enough wood')) { if (this.noteEmptySpot) this.noteEmptySpot(t.x, t.y); break; }
                        if (this.backpackCount(['log']) >= want || this.full()) break;
                    }
                }
            }
            return this.backpackCount(['log']) > 0;
        },

        // Mine at a proven spot, then smelt at its forge. Ore is heavy: smelt
        // whenever the pack fills.
        async gatherOre(wantIngots) {
            const { token } = this;
            const spot = this.gatherSpots('mine').find((s) => s.proven) || null;
            if (!spot) { console.warn('[gather] no PROVEN mine spot near home; mining skipped'); return false; }
            const pick = this.findInPack('pickaxe');
            if (!pick) { console.warn('[gather] no pickaxe'); return false; }
            Player.equip(pick.serial);
            await token.sleep(800);
            if (!await this.walkTo(spot.stand, { range: 0 })) return false;
            for (let i = 0; i < 30 && !this.full(); i++) {
                token.check();
                Player.use(pick.serial);
                try { await token.wait(Player.once('target', 4000)); } catch (e) { if (token.cancelled) throw CANCELLED; break; }
                Player.target(spot.tile.x, spot.tile.y, spot.tile.z);
                let line = '';
                try { line = lower(await token.wait(waitForJournal({ ms: 10000 }))); } catch (e) { if (token.cancelled) throw CANCELLED; }
                if (line.includes('no ore') || line.includes('nothing here')) break;
                if (this.backpackCount(['ingot']) + this.backpackCount(['ore']) >= wantIngots) break;
            }
            await this.smeltOre(spot);
            return this.backpackCount(['ingot']) > 0;
        },

        async smeltOre(spot) {
            const { token } = this;
            if (!spot || !spot.forge) return;
            if (!await this.walkTo(spot.forge, { range: 1 })) return;
            for (let i = 0; i < 20; i++) {
                const ore = this.findInPack('ore');
                if (!ore) break;
                Player.use(ore.serial);
                await token.sleep(2500);
            }
        },

        // UNVERIFIED flow: pole on the water beside a dock.
        async gatherFish(want) {
            const { token } = this;
            const pole = this.findInPack('fishing pole') || this.findInPack('pole');
            if (!pole) { console.warn('[gather] no fishing pole'); return false; }
            const dock = this.gatherSpots('fish')[0];
            if (!dock || !await this.walkTo(dock, { range: 1 })) return false;
            Player.equip(pole.serial);
            await token.sleep(800);
            for (let i = 0; i < 25 && !this.full(); i++) {
                token.check();
                Player.use(pole.serial);
                try { await token.wait(Player.once('target', 4000)); } catch (e) { if (token.cancelled) throw CANCELLED; break; }
                // The tile two steps past the dock edge; the server decides if it is water.
                Player.target(dock.x + 2, dock.y + 2, -5);
                let line = '';
                try { line = lower(await token.wait(waitForJournal({ ms: 12000 }))); } catch (e) { if (token.cancelled) throw CANCELLED; }
                if (line.includes('cannot fish here') || line.includes("can't fish")) { console.warn('[gather] not water here -- fishing spot needs live verification'); break; }
                if (this.backpackCount(['fish']) >= want) break;
            }
            return this.backpackCount(['fish']) > 0;
        },
    };

    // ---- GathererBot: miner / fisher --------------------------------------
    class GathererBot extends BehaviorScript {
        constructor(archetypeId, opts = {}) {
            g.applyMixins(GathererBot, ['BankSkill', 'SurvivalSkill', 'CombatSkill', 'EconomySkill', 'MemorySkill', 'GatherSkill']);
            super();
            const a = g.Archetypes.A[archetypeId];
            if (!a || a.kind !== 'gatherer') throw new Error(`GathererBot: '${archetypeId}' is not a gatherer`);
            if (a.gather === 'lumber') throw new Error('GathererBot: use lumberjack.js for lumberjacks (its proven script)');
            this.archetypeId = archetypeId;
            this.arch = a;
            this.kind = a.gather;
            this.townId = opts.home || a.home;
            const town = g.Archetypes.TOWNS[this.townId];
            this.BANK = town.bank;
            this.HEALER = town.healer;
            const v = town.vendors || {};
            this.KEEP = ['pickaxe', 'fishing pole', 'hatchet'];
            this.SELL_VENDORS = ['blacksmith', 'provisioner', 'tanner'].filter((t) => v[t]).map((t) => ({ title: t, coords: v[t] }));
            this.CONSUMABLES = {};
            this.WALLET = 100; this.GOLD_RESERVE = 50; this.MAX_CARRY_GOLD = 800;
            this.PERSONA = opts.persona || { riskTolerance: 0.3, activeHours: [] };
            this.COMBAT_SKILL = g.Archetypes.SK.WREST;
            this.threat = null; this.fleeing = false; this.lastAteMs = 0;
            this.installCombatSensing(); this.economyInit(); this.memoryInit();
        }

        async work() {
            if (this.kind === 'mine') await this.gatherOre(60);
            else if (this.kind === 'fish') await this.gatherFish(40);
            await this.token.sleep(1000);
        }

        async townTrip() { await this.walkTo(this.BANK, { range: 2 }); }

        behaviors() {
            return [
                { name: 'resurrect', when: () => Player.dead, step: this.step('resurrect') },
                { name: 'offline', when: () => !this.isActiveNow() && !this.threat?.exists, step: this.step('endSession') },
                { name: 'fight', when: () => !this.fleeing && Boolean(this.threat?.exists), step: this.step('fight') },
                { name: 'bank', when: () => (this.full() || this.carryingTooMuchGold()) && this.bankTripDue(),
                    step: this.sequence('sellLoot', 'townTrip', 'bankSurplusGold', 'stashJunk', 'economyReport') },
                { name: 'recover', when: () => this.fleeing || this.hpFrac() < 0.6, step: this.step('rest') },
                { name: 'work', when: () => true, step: this.step('work') },
            ];
        }

        onPreempt() { Player.stop(); }
    }

    g.GatherSkill = GatherSkill;
    g.GathererBot = GathererBot;
    g.GatherSpots = SPOTS;
})(globalThis);
