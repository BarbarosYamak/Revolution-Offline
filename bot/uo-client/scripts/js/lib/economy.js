'use strict';
// EconomySkill — the bot's own economy: loot its kills, sell what it does not
// need, bank what death could take, keep a ledger, and spend within a budget.
//     Object.assign(MyBot.prototype, EconomySkill);
// Reads, from the bot: KEEP (name substrings never sold), SELL_VENDORS
// ([{title, coords}]), WALLET (gold to carry), GOLD_RESERVE (never spent on
// anything but essentials), MAX_CARRY_GOLD (bank above this), BANK, and the
// usual BANDAGE / FOOD / CONSUMABLES / AXE / WEAPONS if the bot has them.
// Uses walkTo/backpackCount (lib/bot.js), findVendor (lib/survival.js),
// openContainer/openBank/deposit/withdrawGold (lib/bank.js).
//
// THE RULES THIS FOLLOWS (CLAUDE.md "a bot must play the game")
//
//   * No global market data. What a vendor will buy, and for how much, is
//     learned ONLY from that vendor's own sell list (0x9E), the same window a
//     player sees after saying "sell". Nothing here hardcodes a price.
//   * No invented demand. An item is "unsellable" only after every vendor on
//     the bot's route has been ASKED and did not list it.
//   * Loot only what the bot killed: a corpse is taken only when the server
//     says it belongs to the mobile we were fighting (corpseOf), or it is the
//     single unclaimed corpse on the spot where that mobile fell.
//   * Gold comes from sales and loot, never from anywhere else. The ledger
//     records every change so a run can be audited.
//
// Tunables are DERIVED bot policy, not Revolution mechanics.
(function (g) {
    const CORPSE_GRAPHIC = 0x2006;

    // ---- pure helpers (exported for tests) --------------------------------

    const lower = (s) => String(s || '').toLowerCase();
    const matchesAny = (name, subs) => [].concat(subs || []).some((sub) => lower(name).includes(lower(sub)));

    // How many of `name` the bot wants to keep, or Infinity for "all of it".
    //   rules.keep        substrings kept entirely (tools, weapon, gold)
    //   rules.stock       [{names, target}] consumables: keep 2x target, sell
    //                     only the surplus above that (a restock trip is not
    //                     worth saving a few coins)
    function keepAmount(name, rules) {
        if (lower(name).includes('gold')) return Infinity;
        if (matchesAny(name, rules.keep)) return Infinity;
        for (const s of rules.stock || [])
            if (matchesAny(name, s.names)) return 2 * s.target;
        return 0;
    }

    // Which rows of a vendor's sell offer to sell, and how many of each.
    //   offerItems   rows of a `vendor_sell` event: {serial, amount, price, name}
    //   packCount    (name) -> how many of that the backpack holds in total
    // Items the vendor pays 0 for are skipped: giving loot away is not selling.
    function planSale(offerItems, rules, packCount) {
        const plan = [];
        const budgetLeft = new Map();   // name -> how many we may still sell
        for (const row of offerItems) {
            if (!row || !row.serial || !(row.amount > 0) || !(row.price > 0)) continue;
            const name = lower(row.name);
            if (!budgetLeft.has(name)) {
                const keep = keepAmount(name, rules);
                const have = packCount ? packCount(name) : row.amount;
                budgetLeft.set(name, keep === Infinity ? 0 : Math.max(0, have - keep));
            }
            const qty = Math.min(row.amount, budgetLeft.get(name));
            if (qty <= 0) continue;
            budgetLeft.set(name, budgetLeft.get(name) - qty);
            plan.push({ serial: row.serial, qty, name, price: row.price });
        }
        return plan;
    }

    // A ledger: every gold movement with a kind, so income and spending can be
    // read back per session. Transfers to/from the bank are not income.
    function createLedger(nowMs) {
        return {
            startMs: nowMs,
            entries: [],
            totals: { sale: 0, loot: 0, buy: 0, training: 0, other: 0 },
            record(kind, amount, note, atMs) {
                if (!amount) return;
                const k = this.totals[kind] === undefined ? 'other' : kind;
                this.totals[k] += amount;
                this.entries.push({ kind: k, amount, note: note || '', atMs: atMs || Date.now() });
                if (this.entries.length > 500) this.entries.shift();
            },
            income() { return this.totals.sale + this.totals.loot; },
            expense() { return this.totals.buy + this.totals.training; },
            net() { return this.income() - this.expense(); },
            perHour(nowMs) {
                const h = Math.max((nowMs - this.startMs) / 3600000, 1 / 60);
                return this.net() / h;
            },
        };
    }

    g.EconomyPolicy = { keepAmount, planSale, createLedger, matchesAny };

    // ---- the mixin --------------------------------------------------------

    const EconomySkill = {
        LOOT_WINDOW_MS: 90 * 1000,  // a kill older than this is not worth walking back for
        LOOT_RADIUS: 3,             // corpse search around where the foe fell
        LOOT_TAKE_GAP_MS: 700,      // between lifts: a player drags one at a time

        economyInit() {
            this.ledger = createLedger(Date.now());
            // name -> { buyers: {title: {price, seenMs}}, refusedBy: Set(title) }
            this.market = new Map();
        },

        economyRules() {
            const keep = [].concat(this.KEEP || [], this.AXE || [], this.WEAPONS || [], this.BANDAGE || []);
            const stock = [];
            for (const key of Object.keys(this.CONSUMABLES || {})) {
                const c = this.CONSUMABLES[key];
                stock.push({ names: [].concat(c.name || key), target: c.target });
            }
            if (this.FOOD && this.FOOD.length) stock.push({ names: [].concat(this.FOOD), target: 5 });
            return { keep, stock };
        },

        goldInPack() { return this.backpackCount(['gold']); },

        // What may be spent right now. Essentials (bandages, food) may dip into
        // GOLD_RESERVE -- that is what the reserve is for; anything else may not.
        spendable(essential = false) {
            const reserve = essential ? 0 : (this.GOLD_RESERVE || 0);
            return Math.max(0, this.goldInPack() - reserve);
        },

        marketEntry(name) {
            const key = lower(name);
            if (!this.market.has(key)) this.market.set(key, { buyers: {}, refusedBy: new Set() });
            return this.market.get(key);
        },

        // Known not to sell to ANY vendor on our route (all asked, none listed it).
        isUnsellable(name) {
            const m = this.market && this.market.get(lower(name));
            if (!m || Object.keys(m.buyers).length) return false;
            const titles = (this.SELL_VENDORS || []).map((v) => v.title);
            return titles.length > 0 && titles.every((t) => m.refusedBy.has(t));
        },

        // Pack items we would sell if someone bought them.
        sellCandidates() {
            const rules = this.economyRules();
            return Player.equipment.backpack.items.filter((item) =>
                keepAmount(item.name, rules) !== Infinity &&
                this.backpackCount([item.name]) > keepAmount(item.name, rules) &&
                !this.isUnsellable(item.name));
        },

        async waitGoldChange(before, ms = 3000) {
            const { token } = this;
            for (let waited = 0; waited < ms; waited += 250) {
                if (this.goldInPack() !== before) break;
                await token.sleep(250);
            }
            return this.goldInPack() - before;
        },

        // Visit each sell vendor on the route; sell what it lists and we do not need.
        async sellLoot() {
            const { token } = this;
            if (!this.ledger) this.economyInit();
            if (this.sellCandidates().length === 0) return;

            const here = { x: Player.x, y: Player.y };
            const route = [...(this.SELL_VENDORS || [])]
                .sort((a, b) => tileDistance(here, a.coords) - tileDistance(here, b.coords));
            for (const stop of route) {
                token.check();
                const candidates = this.sellCandidates();
                if (candidates.length === 0) break;
                // Skip a vendor already known to refuse everything we carry.
                if (candidates.every((item) => this.marketEntry(item.name).refusedBy.has(stop.title)))
                    continue;

                console.log(`[econ] selling at '${stop.title}' @${stop.coords.x},${stop.coords.y}`);
                if (!await this.walkTo(stop.coords, { range: 2 })) {
                    console.warn(`[econ] could not reach '${stop.title}'`);
                    continue;
                }
                const vendor = await this.findVendor(stop.title, stop.coords);
                if (!vendor) { console.warn(`[econ] no '${stop.title}' found`); continue; }
                Player.follow(vendor.serial);
                await token.sleep(1500);

                let offer = null;
                try {
                    const [window] = await token.wait(Promise.all([
                        Vendor.once('vendor_sell', 4000),
                        Player.say('vendor sell'),
                    ]));
                    offer = window;
                } catch (error) { if (token.cancelled) throw CANCELLED; }
                Player.follow(false);
                const rows = (offer && offer.items) || [];

                // Learn: listed => a buyer at that price; carried but unlisted => refused.
                const listed = new Set(rows.map((r) => lower(r.name)));
                for (const r of rows)
                    this.marketEntry(r.name).buyers[stop.title] = { price: r.price, seenMs: Date.now() };
                for (const item of candidates)
                    if (!listed.has(lower(item.name))) this.marketEntry(item.name).refusedBy.add(stop.title);

                const plan = planSale(rows, this.economyRules(), (name) => this.backpackCount([name]));
                if (plan.length === 0) {
                    console.log(`[econ] '${stop.title}' buys nothing we want to sell`);
                    continue;
                }
                const expected = plan.reduce((sum, p) => sum + p.qty * p.price, 0);
                console.log(`[econ] selling ${plan.map((p) => `${p.qty}x ${p.name}@${p.price}`).join(', ')} (~${expected}gp)`);
                const before = this.goldInPack();
                Vendor.sell(offer.vendor, plan.map((p) => ({ serial: p.serial, qty: p.qty })));
                const gained = await this.waitGoldChange(before);
                if (gained > 0) this.ledger.record('sale', gained, `${stop.title}: ${plan.map((p) => p.name).join(',')}`);
                else console.warn(`[econ] sale to '${stop.title}' produced no gold change`);
            }
        },

        // Loot the corpse of the foe we just fought, if the server shows one.
        async lootKill() {
            const { token } = this;
            const kill = this.lastKill;
            this.lastKill = null;
            if (!kill || Date.now() - kill.atMs > this.LOOT_WINDOW_MS) return;
            if (!this.ledger) this.economyInit();

            const corpses = World.items(kill.x, kill.y, this.LOOT_RADIUS)
                .filter((it) => it.corpse || it.graphic === CORPSE_GRAPHIC);
            let corpse = corpses.find((c) => c.corpseOf === kill.serial);
            if (!corpse) {
                // Accept an unattributed corpse only if it is the ONLY one here:
                // two corpses and no attribution means one may be someone else's.
                const unknown = corpses.filter((c) => !c.corpseOf);
                if (unknown.length === 1) corpse = unknown[0];
            }
            if (!corpse) { console.log(`[econ] no corpse for 0x${kill.serial.toString(16)} -- it fled, or someone else's`); return; }

            if (!await this.walkTo({ x: corpse.x, y: corpse.y }, { adjacent: true })) return;
            try { await this.openContainer(corpse.serial); }
            catch (error) { if (error === CANCELLED) throw error; console.warn('[econ] corpse did not open'); return; }
            await token.sleep(500);

            const items = Player.containerItems(corpse.serial);
            let took = 0;
            for (const item of items) {
                token.check();
                if (this.full()) { console.log('[econ] pack full; leaving the rest'); break; }
                const isGold = lower(item.name).includes('gold');
                if (!isGold && this.isUnsellable(item.name)) continue;   // learned junk
                const before = this.goldInPack();
                Player.take(item.serial, 0);
                took++;
                await token.sleep(this.LOOT_TAKE_GAP_MS);
                if (isGold) {
                    const gained = await this.waitGoldChange(before, 1500);
                    if (gained > 0) this.ledger.record('loot', gained, 'corpse gold');
                }
            }
            console.log(`[econ] looted ${took}/${items.length} item(s) from the corpse`);
        },

        // Carrying lots of gold is carrying what death takes (full loot loss).
        carryingTooMuchGold() {
            return this.MAX_CARRY_GOLD > 0 && this.goldInPack() > this.MAX_CARRY_GOLD;
        },

        // At the bank: deposit everything, take back WALLET. Not income.
        async bankSurplusGold() {
            const wallet = this.WALLET || 0;
            if (this.goldInPack() <= wallet) return;
            await this.walkTo(this.BANK, { range: 1 });
            await this.deposit('gold');
            await this.withdrawGold(wallet);
        },

        // Items every vendor on the route refused: into the bank rather than
        // carried forever. (The client has no drop-to-ground action yet.)
        async stashJunk() {
            const junk = Player.equipment.backpack.items.filter((item) => this.isUnsellable(item.name) &&
                keepAmount(item.name, this.economyRules()) !== Infinity);
            if (junk.length === 0) return;
            const names = [...new Set(junk.map((j) => j.name))];
            console.log(`[econ] stashing unsellable: ${names.join(', ')}`);
            for (const name of names) await this.deposit(name);
        },

        // A bank trip that could not bring the pack under the limit (all of it
        // kept gear, say) must not fire again on the next tick. Behaviours gate
        // on this.
        bankTripDue() {
            return !this.lastBankTripMs || Date.now() - this.lastBankTripMs > 2 * 60 * 1000;
        },

        economyReport() {
            this.lastBankTripMs = Date.now();
            if (this.full()) console.warn('[econ] still over the weight limit after banking -- check KEEP');
            if (!this.ledger) return;
            const L = this.ledger, now = Date.now();
            console.log(`[econ] ledger: sales ${L.totals.sale} loot ${L.totals.loot} | ` +
                `bought ${L.totals.buy} trained ${L.totals.training} | net ${L.net()} ` +
                `(${L.perHour(now).toFixed(0)}/h) | carrying ${this.goldInPack()}`);
        },
    };

    g.EconomySkill = EconomySkill;
})(globalThis);
