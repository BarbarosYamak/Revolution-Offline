'use strict';
// CrafterBot — every crafter archetype from one engine, configured by
// lib/archetypes.js CRAFTS:
//
//   blacksmith  PROVEN   hammer -> target ingots -> menu; works at a forge+anvil
//   tinker      PROVEN   tinker tools -> menu
//   tailor      PROVEN   buys cloth BOLTS, scissors bolt -> cloth, kit -> target cloth -> menu
//   carpenter   PROVEN   chops its own logs, saw -> menu
//   bowyer      UNVERIFIED  dagger -> target logs -> menu; chops its own logs
//   alchemist   UNVERIFIED  mortar -> target reagent -> menu; BLOCKED on empty bottles
//   scribe      UNVERIFIED  pen -> menu; BLOCKED on blank scrolls
//   cook        UNVERIFIED  raw food -> target oven/fire; catches its own fish
//
// Each one: trains by crafting what its LIVE menu offers, gets its material the
// way the vendor policy allows (buy / buy-and-process / gather), sells its
// output, advertises, takes orders (piece, quantity, set) paid and delivered
// by secure trade (lib/crafting.js). An UNVERIFIED flow that does not open a
// menu is reported once, loudly, and the bot backs off rather than looping.
//
// Usage (scripts/js/<craft>.js):   new CrafterBot('bowyer').start();
(function (g) {
    const lower = (s) => String(s || '').toLowerCase();
    const ADVERT_EVERY_MS = 4 * 60 * 1000;

    class CrafterBot extends BehaviorScript {
        constructor(archetypeId, opts = {}) {
            g.applyMixins(CrafterBot, ['BankSkill', 'SurvivalSkill', 'CombatSkill', 'EconomySkill', 'MemorySkill', 'CraftSkill', 'GatherSkill']);
            super();
            const AR = g.Archetypes;
            const a = AR.A[archetypeId];
            if (!a || a.kind !== 'crafter') throw new Error(`CrafterBot: '${archetypeId}' is not a crafter archetype`);
            const check = AR.validate(archetypeId);
            if (!check.ok) throw new Error(`CrafterBot: ${check.why}`);
            const c = AR.CRAFTS[a.craft];
            this.archetypeId = archetypeId;
            this.arch = a;
            this.craftName = a.craft;
            this.craft = c;
            this.townId = opts.home || a.home;
            const town = AR.TOWNS[this.townId];
            this.town = town;
            this.BANK = town.bank;
            this.HEALER = town.healer;
            const v = town.vendors || {};

            this.CRAFT_TOOL = c.tool;
            this.CRAFT_SKILL = c.skill;
            this.SETS = c.sets;
            this.PRICING = { margin: 0.5, minPerPiece: 10 };
            this.MATERIAL_PER_PIECE = c.material.perPiece;
            // Buyable material: restocked from the craft's vendor in this town.
            const mv = c.material.vendor && v[c.material.vendor];
            const buyNames = c.material.buy || c.material.name;
            this.MATERIAL = (c.material.policy === 'ALLOWED' || c.material.policy === 'ALLOWED_AS_BOLT') && mv
                ? { name: buyNames, target: c.material.target, low: c.material.low, coords: mv, title: c.material.vendor } : null;
            this.KEEP = [].concat(c.tool, c.material.name, c.material.buy || [], c.material.process ? [c.material.process.tool] : [],
                c.material.gather === 'lumber' ? ['hatchet', 'axe'] : [], c.material.gather === 'fish' ? ['fishing pole'] : []);
            this.SELL_VENDORS = ['blacksmith', 'armorer', 'tailor', 'bowyer', 'carpenter', 'provisioner', 'mage', 'alchemist']
                .filter((t) => v[t]).map((t) => ({ title: t, coords: v[t] }));
            this.CONSUMABLES = {};
            this.WALLET = 300; this.GOLD_RESERVE = 150; this.MAX_CARRY_GOLD = 1500;
            this.FOOD = ['bread', 'lamb', 'fish steak'];
            this.PERSONA = opts.persona || { riskTolerance: 0.2, activeHours: [] };
            this.COMBAT_SKILL = AR.SK.WREST;
            // Where it works: at the station when it needs one, else the bank.
            this.WORK_SPOT = opts.workSpot || (c.station.length && v[c.material.vendor] ? v[c.material.vendor] : town.bank);

            this.threat = null; this.fleeing = false; this.lastAteMs = 0;
            this.flowFailures = 0;
            this.installCombatSensing(); this.economyInit(); this.memoryInit(); this.craftInit();
            if (c.material.blocker) console.warn(`[craft] ${archetypeId}: BLOCKED -- ${c.material.blocker}`);
            if (c.flow !== 'PROVEN') console.warn(`[craft] ${archetypeId}: craft flow UNVERIFIED on this shard (${c.evidence})`);
        }

        // --- hooks read by CraftSkill (bot-defined names, never in a mixin) ---

        reservedNames() {
            return this.orders.filter((o) => o.status === 'paid' || o.status === 'ready')
                .flatMap((o) => o.items.map((it) => it.name));
        }

        // Answer the tool's target cursor: the material, or a nearby station.
        async craftTarget() {
            const t = this.craft.target;
            if (!t) return;
            if (t.material) {
                const m = this.findInPack(t.material);
                if (m) { Player.target(m.serial); return; }
                console.warn(`[craft] no ${t.material} to target`);
            } else if (t.station) {
                const st = World.items(Player.x, Player.y, 3).find((i) => t.station.some((s) => lower(i.name).includes(s)));
                if (st) { Player.target(st.serial); return; }
                console.warn(`[craft] no ${t.station.join('/')} within reach`);
            }
            try { Player.target(0); } catch (e) { /* cancel */ }
        }

        // --- material ---------------------------------------------------------

        materialCount() { return this.backpackCount(this.craft.material.name); }

        materialLow() { return this.materialCount() < this.craft.material.low; }

        async getMaterial() {
            const m = this.craft.material;
            if (m.gather === 'lumber') await this.gatherLogs(m.target);
            else if (m.gather === 'fish') await this.gatherFish(m.target);
            else if (this.MATERIAL) {
                await this.restockMaterials();
                // Tailor: scissors turn bolts into cloth (m37_slice_a_finish).
                if (m.process) {
                    const tool = this.findInPack(m.process.tool);
                    for (let i = 0; i < 20; i++) {
                        const bolt = this.findInPack(m.process.from);
                        if (!bolt || !tool) break;
                        Player.use(tool.serial);
                        try { await this.token.wait(Player.once('target', 3000)); Player.target(bolt.serial); }
                        catch (e) { if (this.token.cancelled) throw CANCELLED; break; }
                        await this.token.sleep(1200);
                    }
                }
            } else if (m.alt === 'mine+smelt' || this.arch.build.skills.MINING) {
                await this.gatherOre(m.target);
            } else {
                console.warn(`[craft] ${this.archetypeId}: no legal source for ${m.name.join('/')} here -- ${m.blocker || 'buy from players'}`);
            }
            this.materialCheckedMs = Date.now();
            this.catalogueMs = 0;
        }

        // --- behaviours -------------------------------------------------------

        async atWork() {
            if (tileDistance({ x: Player.x, y: Player.y }, this.WORK_SPOT) > 3)
                await this.walkTo(this.WORK_SPOT, { range: 2 });
        }

        stationOk() {
            if (!this.craft.station.length) return true;
            const near = World.items(Player.x, Player.y, 3).map((i) => lower(i.name));
            return this.craft.station.every((s) => near.some((n) => n.includes(s)));
        }

        async advertise() {
            const offered = this.catalogue.slice(0, 5).map((c) => c.label).join(', ');
            const sets = Object.keys(this.SETS || {});
            Player.say(`${Player.name} the ${this.craftName} takes orders: ${offered || 'ask me'}${sets.length ? `; sets: ${sets.join(', ')}` : ''}. Say "${Player.name} order <item>".`);
            this.lastAdvertMs = Date.now();
        }

        // Craft once toward an order or for training; notice a dead flow.
        async craftStep() {
            this.expireQuotes();
            await this.atWork();
            if (!this.stationOk()) {
                console.warn(`[craft] ${this.archetypeId}: no ${this.craft.station.join('+')} within 3 tiles of ${this.WORK_SPOT.x},${this.WORK_SPOT.y}`);
                this.flowBlockedUntil = Date.now() + 10 * 60 * 1000;
                return;
            }
            if (this.catalogueStale()) {
                await this.readCatalogue();
                if (!this.catalogue.length) {
                    this.flowFailures++;
                    if (this.craft.flow !== 'PROVEN')
                        console.warn(`[craft] ${this.archetypeId}: UNVERIFIED flow opened no menu (${this.flowFailures}x) -- needs a live check`);
                    this.flowBlockedUntil = Date.now() + Math.min(60, 5 * this.flowFailures) * 60 * 1000;
                    return;
                }
                this.flowFailures = 0;
            }
            if (Date.now() - (this.lastAdvertMs || 0) > ADVERT_EVERY_MS) await this.advertise();
            if (this.orders.some((o) => o.status === 'paid')) await this.workOrder();
            else await this.train();
        }

        async deliver() {
            const { token } = this;
            const order = this.orders.find((o) => o.status === 'ready' && Mobiles.get(o.customer.serial).exists &&
                tileDistance(Mobiles.get(o.customer.serial), { x: Player.x, y: Player.y }) <= 3);
            if (!order) return;
            const first = Player.equipment.backpack.items.find((i) => lower(i.name).includes(lower(order.items[0].name)));
            if (!first) { order.status = 'paid'; order.items[0].made = 0; return; }
            Trade.start(order.customer.serial, first.serial);
            for (let i = 0; i < 10 && !Trade.state.active; i++) await token.sleep(500);
            await this.handleTrade();
        }

        async trade() { await this.handleTrade(); }
        async townTrip() { await this.walkTo(this.BANK, { range: 2 }); }

        behaviors() {
            const readyNear = () => this.orders.some((o) => o.status === 'ready' && Mobiles.get(o.customer.serial).exists &&
                tileDistance(Mobiles.get(o.customer.serial), { x: Player.x, y: Player.y }) <= 3);
            return [
                { name: 'resurrect', when: () => Player.dead, step: this.step('resurrect') },
                { name: 'offline', when: () => !this.isActiveNow() && !Trade.state.active &&
                        !this.orders.some((o) => o.status === 'paid' || o.status === 'ready'), step: this.step('endSession') },
                { name: 'fight', when: () => !this.fleeing && Boolean(this.threat?.exists), step: this.step('fight') },
                { name: 'trade', when: () => Trade.state.active, step: this.step('trade') },
                { name: 'deliver', when: readyNear, step: this.step('deliver') },
                { name: 'sell', when: () => (this.full() || this.carryingTooMuchGold()) && this.bankTripDue(),
                    step: this.sequence('sellLoot', 'townTrip', 'bankSurplusGold', 'stashJunk', 'withdrawGold', 'economyReport') },
                { name: 'material', when: () => this.materialLow() && Date.now() - (this.materialCheckedMs || 0) > 10 * 60 * 1000,
                    step: this.step('getMaterial') },
                { name: 'craft', when: () => Boolean(this.findTool()) && Date.now() > (this.flowBlockedUntil || 0), step: this.step('craftStep') },
                { name: 'idle', when: () => true, step: this.step('atWork') },
            ];
        }

        async onStartup() {
            if (typeof Player.requestSkills === 'function') Player.requestSkills();
            await this.token.sleep(1500);
            console.log(`[craft] ${this.archetypeId} (${this.arch.build.ref} ${this.arch.build.cls}, flow ${this.craft.flow}): ` +
                `skill ${(Player.skill(this.CRAFT_SKILL) / 10).toFixed(1)}, tool ${this.findTool() ? 'yes' : 'NO'}, material ${this.materialCount()}`);
            await this.atWork();
        }

        onPreempt() { Player.stop(); }
    }

    g.CrafterBot = CrafterBot;
})(globalThis);
