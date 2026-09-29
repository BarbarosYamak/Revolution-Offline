'use strict';
// CraftSkill + CustomerSkill — crafters that TRAIN by making things and take
// ORDERS from other players (human or bot), paid and delivered by secure trade.
//     Object.assign(Crafter.prototype, CraftSkill);      // the crafter
//     Object.assign(Warrior.prototype, CustomerSkill);   // anyone who orders
//
// Owner report: crafters "are not training or getting orders -- and an order
// can be one piece or a whole set". Until now no autonomous crafter existed at
// all: crafting was only ever driven by one-shot M3.7/M3.8 scenarios.
//
// THE RULES
//
//   * The live craft menu is the authority (M3.9.1 "craft-menu oracle"). A
//     bot only makes what Sphere's 0x7C menu actually OFFERS it right now --
//     Sphere filters that menu by skill and by materials carried. An order for
//     something the menu does not list is declined, never attempted.
//   * Training is crafting. Skill rises only when the server says so; the bot
//     reads it back with Player.skill().
//   * Orders arrive as SPEECH from a player standing near the crafter -- the
//     way a Revolution customer asked a smith -- and are paid IN ADVANCE and
//     delivered through the secure-trade window. No item or coin moves any
//     other way.
//   * Prices are the crafter's own policy (material cost it has observed plus
//     a margin); Revolution crafter prices are UNKNOWN and not invented here.
//
// ORDER GRAMMAR (said within earshot, crafter's name first):
//     "<crafter> order <item>"             one piece
//     "<crafter> order 10 <item>"          a quantity
//     "<crafter> order <set name> set"     a whole set (see SETS)
//     "<crafter> status" / "<crafter> cancel"
(function (g) {
    const lower = (s) => String(s || '').toLowerCase().trim();

    // Sets a customer can order by name. Pieces are MENU LABELS, matched
    // against what the live menu offers. These are the generic Sphere/UO piece
    // lists -- Revolution's own menu labels are UNVERIFIED, which is exactly
    // why each piece is checked against the live catalogue before an order is
    // accepted: a set containing a piece the menu does not offer is declined.
    const SETS = {
        'ringmail':  ['ringmail tunic', 'ringmail sleeves', 'ringmail leggings', 'ringmail gloves'],
        'chainmail': ['chainmail tunic', 'chainmail leggings', 'chainmail coif'],
        'platemail': ['platemail', 'platemail arms', 'platemail legs', 'platemail gloves', 'platemail gorget', 'plate helm'],
        'tinker tools': ['tinker tools', 'scissors', 'pickaxe'],   // a crafter's starter kit
    };

    // ---- pure helpers (exported for tests) --------------------------------

    // Parse one spoken line. Returns null if it is not addressed to us.
    //   {kind:'order', items:[{name, qty}], set: name|null} | {kind:'status'} | {kind:'cancel'}
    function parseOrder(text, crafterName, sets = SETS) {
        const t = lower(text);
        const me = lower(crafterName);
        if (!me || !t.startsWith(me)) return null;
        let rest = t.slice(me.length).replace(/^[\s,:]+/, '');
        if (/^status\b/.test(rest)) return { kind: 'status' };
        if (/^cancel\b/.test(rest)) return { kind: 'cancel' };
        const m = rest.match(/^order\s+(.+)$/);
        if (!m) return null;
        rest = m[1].trim();

        const setMatch = rest.match(/^(?:a\s+|an\s+)?(.+?)\s+set$/);
        if (setMatch) {
            const name = setMatch[1].trim();
            const pieces = sets[name];
            if (!pieces) return { kind: 'order', items: [], set: name, error: `I do not know a "${name}" set` };
            return { kind: 'order', set: name, items: pieces.map((p) => ({ name: p, qty: 1 })) };
        }
        const q = rest.match(/^(\d{1,3})\s+(.+)$/);
        const qty = q ? Math.max(1, Math.min(100, parseInt(q[1], 10))) : 1;
        let name = (q ? q[2] : rest).replace(/^(?:a|an)\s+/, '').trim();
        if (qty > 1) name = name.replace(/s$/, '');   // "10 pickaxes" -> "pickaxe"
        return { kind: 'order', set: null, items: [{ name, qty }] };
    }

    // Which catalogue label satisfies a requested piece name, or null.
    function catalogueMatch(name, catalogue) {
        const want = lower(name);
        const labels = catalogue.map((c) => ({ c, l: lower(c.label) }));
        return (labels.find((x) => x.l === want) ||
                labels.find((x) => x.l.startsWith(want)) ||
                labels.find((x) => x.l.includes(want)) || {}).c || null;
    }

    // Can every piece be made from what the live menu offers?
    function checkOrder(items, catalogue) {
        const missing = items.filter((it) => !catalogueMatch(it.name, catalogue)).map((it) => it.name);
        return { ok: missing.length === 0 && items.length > 0, missing };
    }

    // The crafter's own price: observed material cost per piece plus margin,
    // with a floor per piece so a cheap item is still worth the walk.
    function quote(items, unitCost, { margin = 0.5, minPerPiece = 10 } = {}) {
        let total = 0;
        for (const it of items) {
            const cost = unitCost(it.name);
            const each = Math.max(minPerPiece, Math.ceil((cost > 0 ? cost : 0) * (1 + margin)));
            total += each * it.qty;
        }
        return total;
    }

    // Order lifecycle. Deliberately a plain object so the crafter's persistent
    // memory can hold it later.
    //   quoted -> paid -> ready -> delivered     (or cancelled)
    function createOrder(id, customer, parsed, price, nowMs) {
        return {
            id, customer: { serial: customer.serial, name: customer.name },
            set: parsed.set, price, status: 'quoted', createdMs: nowMs,
            items: parsed.items.map((it) => ({ name: it.name, qty: it.qty, made: 0 })),
        };
    }

    function nextPiece(order) {
        return order.items.find((it) => it.made < it.qty) || null;
    }

    function orderDone(order) { return order.items.every((it) => it.made >= it.qty); }

    g.OrderPolicy = { ADVERT_RE: /^(.+?) the (\w+) takes orders/i, SETS, parseOrder, catalogueMatch, checkOrder, quote, createOrder, nextPiece, orderDone };

    // ---- the crafter ------------------------------------------------------

    const CraftSkill = {
        MAX_OPEN_ORDERS: 3,
        QUOTE_TTL_MS: 3 * 60 * 1000,        // an unpaid quote expires
        CATALOGUE_TTL_MS: 20 * 60 * 1000,   // re-read the menu as skill changes
        CRAFT_RESULT_MS: 8000,

        craftInit() {
            this.orders = [];
            this.nextOrderId = 1;
            this.catalogue = [];            // [{category, label}]
            this.catalogueMs = 0;
            this.catalogueSkill = -1;
            this.madeCount = 0;
            Player.on('journal', (msg) => this.onSpeech(msg));
        },

        findTool() { return this.findInPack(this.CRAFT_TOOL); },

        // Open the craft menu with the tool. Tinker tools open it on the
        // double-click itself (proven, m38_tinker_craft.txt). A tool that asks
        // for a TARGET first (a smith hammer) is answered by the bot's
        // craftTarget() hook -- that flow is UNVERIFIED on this shard.
        async openMenu() {
            const { token } = this;
            const tool = this.findTool();
            if (!tool) return null;
            const menu = Player.once('dialog', 4000);
            const cursor = Player.once('target', 1500).catch(() => null);
            Player.use(tool.serial);
            const armed = await cursor;
            if (armed && typeof this.craftTarget === 'function') await this.craftTarget();
            try { return await token.wait(menu); }
            catch (error) { if (token.cancelled) throw CANCELLED; return null; }
        },

        closeMenu() { try { Player.dialogRespond(0); } catch (e) { /* none open */ } },

        // Walk every category of the live menu and record what it offers. This
        // IS the crafter's knowledge of what it can make right now.
        async readCatalogue() {
            const { token } = this;
            const top = await this.openMenu();
            if (!top) { console.warn('[craft] craft menu did not open'); return this.catalogue; }
            const categories = top.options.map((o) => o.text);
            const found = [];
            for (let i = 0; i < categories.length; i++) {
                const menu = i === 0 ? top : await this.openMenu();
                if (!menu) break;
                const opt = menu.options.find((o) => lower(o.text) === lower(categories[i]));
                if (!opt) { this.closeMenu(); continue; }
                const sub = Player.once('dialog', 3000);
                Player.dialogRespond(opt.index);
                let inner = null;
                try { inner = await token.wait(sub); } catch (error) { if (token.cancelled) throw CANCELLED; }
                if (inner) for (const o of inner.options) found.push({ category: categories[i], label: o.text });
                this.closeMenu();
                await token.sleep(600);
            }
            this.catalogue = found;
            this.catalogueMs = Date.now();
            this.catalogueSkill = Player.skill(this.CRAFT_SKILL);
            console.log(`[craft] live menu offers ${found.length} item(s): ${found.map((f) => f.label).slice(0, 12).join(', ')}${found.length > 12 ? ' ...' : ''}`);
            return found;
        },

        catalogueStale() {
            const skill = Player.skill(this.CRAFT_SKILL);
            return !this.catalogueMs || Date.now() - this.catalogueMs > this.CATALOGUE_TTL_MS ||
                (skill >= 0 && this.catalogueSkill >= 0 && skill - this.catalogueSkill >= 50);
        },

        // Make one item by its menu label. 'ok' | 'fail' | 'not_offered' | 'no_menu'.
        async craftOne(entry) {
            const { token } = this;
            const top = await this.openMenu();
            if (!top) return 'no_menu';
            const cat = top.options.find((o) => lower(o.text) === lower(entry.category));
            if (!cat) { this.closeMenu(); return 'not_offered'; }
            const sub = Player.once('dialog', 3000);
            Player.dialogRespond(cat.index);
            let inner = null;
            try { inner = await token.wait(sub); } catch (error) { if (token.cancelled) throw CANCELLED; }
            const item = inner && inner.options.find((o) => lower(o.text) === lower(entry.label));
            if (!item) { this.closeMenu(); return 'not_offered'; }

            const before = Player.equipment.backpack.items.length;
            const skillBefore = Player.skill(this.CRAFT_SKILL);
            Player.dialogRespond(item.index);
            let line = '';
            try { line = lower(await token.wait(waitForJournal({ ms: this.CRAFT_RESULT_MS }))); }
            catch (error) { if (token.cancelled) throw CANCELLED; }
            await token.sleep(500);
            const skillAfter = Player.skill(this.CRAFT_SKILL);
            if (skillAfter > skillBefore)
                console.log(`[craft] skill ${(skillBefore / 10).toFixed(1)} -> ${(skillAfter / 10).toFixed(1)}`);
            // Success evidence: a new stack in the pack, or a journal line that
            // says so. Failure wording on Revolution is UNKNOWN; "fail"/"ruin"/
            // "destroy" are the Sphere defaults.
            if (/fail|ruin|destroy|lack|not enough/.test(line)) return 'fail';
            if (Player.equipment.backpack.items.length > before || /you (create|make|put)/.test(line)) {
                this.madeCount++;
                return 'ok';
            }
            return 'fail';
        },

        // What to make to train when no order is waiting: something the menu
        // offers, preferring what a vendor on our route is known to buy
        // (lib/economy.js market memory) so training pays for its materials,
        // then the entry furthest down its category (menus list harder items
        // later; UNVERIFIED as a Revolution rule, so it is only a tie-break).
        chooseTrainingItem() {
            if (!this.catalogue.length) return null;
            // Never train on what the bot KEEPs (its own tools, its material):
            // those would pile up unsold. Spare tools are made on purpose elsewhere.
            const keep = [].concat(this.KEEP || []);
            const usable = this.catalogue.filter((c) => !keep.some((k) => lower(c.label).includes(lower(k))));
            if (!usable.length) return null;
            const sellable = usable.filter((c) => {
                const m = this.market && this.market.get(lower(c.label));
                return m && Object.keys(m.buyers).length > 0;
            });
            const pool = sellable.length ? sellable : usable;
            const byCat = new Map();
            for (const c of pool) byCat.set(c.category, c);   // last of each category
            const picks = [...byCat.values()];
            return picks[Math.floor(Math.random() * picks.length)];
        },

        // ---- orders ---------------------------------------------------------

        openOrders() { return this.orders.filter((o) => o.status === 'quoted' || o.status === 'paid' || o.status === 'ready'); },

        unitCost(name) {
            // Material cost the crafter has itself observed: the ingot price it
            // last paid (ledger) times a per-piece estimate. Without an
            // observation the per-piece floor in quote() applies.
            return (this.MATERIAL_PRICE || 0) * (this.MATERIAL_PER_PIECE || 4);
        },

        onSpeech(msg) {
            if (!msg || msg.system || !msg.serial || msg.serial === Player.serial) return;
            const parsed = parseOrder(msg.text, Player.name, this.SETS || SETS);
            if (!parsed) return;
            const who = Mobiles.get(msg.serial);
            const customer = { serial: msg.serial, name: who.name || '' };
            const mine = this.orders.filter((o) => o.customer.serial === msg.serial &&
                (o.status === 'quoted' || o.status === 'paid' || o.status === 'ready'));

            if (parsed.kind === 'status') {
                if (!mine.length) { Player.say(`${customer.name}, I have no order of yours.`); return; }
                for (const o of mine) {
                    const made = o.items.reduce((a, it) => a + it.made, 0), total = o.items.reduce((a, it) => a + it.qty, 0);
                    Player.say(`${customer.name}, order ${o.id}: ${o.status}, ${made}/${total} made.`);
                }
                return;
            }
            if (parsed.kind === 'cancel') {
                const q = mine.find((o) => o.status === 'quoted');
                if (q) { q.status = 'cancelled'; Player.say(`${customer.name}, order ${q.id} cancelled.`); }
                else Player.say(`${customer.name}, a paid order cannot be cancelled -- I am already working on it.`);
                return;
            }
            if (parsed.error) { Player.say(`${customer.name}, ${parsed.error}.`); return; }
            if (this.openOrders().length >= this.MAX_OPEN_ORDERS) {
                Player.say(`${customer.name}, I have too many orders right now. Come back later.`);
                return;
            }
            const check = checkOrder(parsed.items, this.catalogue);
            if (!check.ok) {
                Player.say(`${customer.name}, I cannot make ${check.missing.join(', ')} yet.`);
                (this.wanted = this.wanted || new Set()).add(check.missing[0]);   // a training goal
                return;
            }
            const price = quote(parsed.items, (n) => this.unitCost(n), this.PRICING || {});
            const order = createOrder(this.nextOrderId++, customer, parsed, price, Date.now());
            this.orders.push(order);
            const what = parsed.set ? `a ${parsed.set} set` : parsed.items.map((i) => `${i.qty} ${i.name}`).join(', ');
            Player.say(`${customer.name}, ${what} will be ${price} gold, paid up front. Trade me the gold.`);
            console.log(`[order] #${order.id} quoted: ${what} for ${price}gp to ${customer.name}`);
        },

        expireQuotes() {
            const now = Date.now();
            for (const o of this.orders)
                if (o.status === 'quoted' && now - o.createdMs > this.QUOTE_TTL_MS) {
                    o.status = 'cancelled';
                    console.log(`[order] #${o.id} quote expired unpaid`);
                }
        },

        // A trade window is open with someone. If they have an unpaid quote
        // and put at least the price in gold on their side, accept -- that is
        // the payment. If their order is READY, put the goods on our side and
        // accept. Anything else: cancel, we do not trade blind.
        async handleTrade() {
            const { token } = this;
            const st = Trade.state;
            if (!st.active) return;
            const orders = this.orders.filter((o) => o.customer.serial === st.partner);
            const unpaid = orders.find((o) => o.status === 'quoted');
            const ready = orders.find((o) => o.status === 'ready');

            if (ready) {
                for (const it of ready.items) {
                    const stacks = Player.equipment.backpack.items.filter((i) => lower(i.name).includes(lower(it.name)));
                    let need = it.qty;
                    for (const s of stacks) { if (need <= 0) break; Trade.offer(s.serial, Math.min(need, s.amount || 1)); need -= (s.amount || 1); await token.sleep(600); }
                }
                Trade.accept(true);
                for (let i = 0; i < 30 && Trade.state.active; i++) await token.sleep(1000);
                if (Trade.state.closeReason === 'both_accepted') {
                    ready.status = 'delivered';
                    Player.say(`${ready.customer.name}, enjoy. Thank you for your order.`);
                    console.log(`[order] #${ready.id} delivered`);
                }
                return;
            }
            if (unpaid) {
                const goldOffered = () => Player.containerItems(st.theirContainer)
                    .filter((i) => lower(i.name).includes('gold')).reduce((a, i) => a + (i.amount || 0), 0);
                for (let i = 0; i < 30 && Trade.state.active && goldOffered() < unpaid.price; i++) await token.sleep(1000);
                if (!Trade.state.active) return;
                if (goldOffered() < unpaid.price) { Player.say(`${unpaid.customer.name}, the price is ${unpaid.price} gold.`); Trade.cancel(); return; }
                const before = this.goldInPack ? this.goldInPack() : 0;
                Trade.accept(true);
                for (let i = 0; i < 30 && Trade.state.active; i++) await token.sleep(1000);
                const gained = (this.goldInPack ? this.goldInPack() : 0) - before;
                if (gained >= unpaid.price) {
                    unpaid.status = 'paid';
                    if (this.ledger) this.ledger.record('sale', gained, `order #${unpaid.id}`);
                    Player.say(`${unpaid.customer.name}, paid. I will start now.`);
                    console.log(`[order] #${unpaid.id} paid ${gained}gp`);
                }
                return;
            }
            Trade.cancel();
        },

        // Work the oldest paid order one piece at a time.
        async workOrder() {
            const order = this.orders.find((o) => o.status === 'paid');
            if (!order) return;
            if (this.catalogueStale()) await this.readCatalogue();
            const piece = nextPiece(order);
            if (!piece) { order.status = 'ready'; return; }
            const entry = catalogueMatch(piece.name, this.catalogue);
            if (!entry) {
                console.warn(`[order] #${order.id}: "${piece.name}" no longer offered -- materials?`);
                await this.restockMaterials();
                return;
            }
            const r = await this.craftOne(entry);
            if (r === 'ok') piece.made++;
            else if (r === 'not_offered') await this.restockMaterials();
            if (orderDone(order)) {
                order.status = 'ready';
                Player.say(`${order.customer.name}, your order ${order.id} is ready. Trade me to collect.`);
                console.log(`[order] #${order.id} ready`);
            }
        },

        // Train by crafting when there is no paid order.
        async train() {
            if (this.catalogueStale()) await this.readCatalogue();
            const entry = this.chooseTrainingItem();
            if (!entry) { await this.restockMaterials(); return; }
            const r = await this.craftOne(entry);
            if (r === 'not_offered' || r === 'no_menu') await this.restockMaterials();
            await this.token.sleep(800);
        },

        // Materials: buy the crafter's raw material from its vendor, within
        // budget (SurvivalSkill.buyFrom respects spendable()).
        async restockMaterials() {
            const m = this.MATERIAL;
            if (!m) return;
            if (this.backpackCount(m.name) >= m.low) return;
            const before = this.goldInPack ? this.goldInPack() : 0;
            await this.restockConsumables({ [m.name[0]]: { name: m.name, target: m.target, low: m.low, coords: m.coords, title: m.title } });
            const have = this.backpackCount(m.name);
            const spent = before - (this.goldInPack ? this.goldInPack() : 0);
            if (spent > 0 && have > 0) this.MATERIAL_PRICE = Math.ceil(spent / Math.max(1, have));
            this.catalogueMs = 0;   // new materials change what the menu offers
        },
    };

    // ---- the customer -----------------------------------------------------

    // Crafters announce themselves ("Ahmet the blacksmith takes orders ...");
    // customers remember who they HEARD, where, and when. That is the only way
    // a bot learns a crafter exists -- no directory, no global list.
    const ADVERT_RE = /^(.+?) the (\w+) takes orders/i;

    const CustomerSkill = {
        listenForCrafters() {
            this.crafters = this.crafters || new Map();   // craft -> {name, serial, x, y, heardMs}
            Player.on('journal', (msg) => {
                if (!msg || msg.system || !msg.serial || msg.serial === Player.serial) return;
                const m = String(msg.text || '').match(ADVERT_RE);
                if (!m) return;
                const who = Mobiles.get(msg.serial);
                const craft = lower(m[2]);
                this.crafters.set(craft, { name: m[1].trim(), serial: msg.serial,
                    x: who.exists ? who.x : Player.x, y: who.exists ? who.y : Player.y, heardMs: Date.now() });
                console.log(`[customer] heard ${m[1].trim()} the ${craft} at (${who.x},${who.y})`);
            });
        },

        knownCrafter(craft) {
            const c = this.crafters && this.crafters.get(lower(craft));
            // A crafter heard more than a day ago may have moved or quit.
            return c && Date.now() - c.heardMs < 24 * 3600 * 1000 ? c : null;
        },

        // Walk to a crafter, say the order, pay when quoted, collect when told.
        //   crafterName  as it appears to us; coords where it works
        async placeOrder(crafterName, coords, orderText) {
            const { token } = this;
            await this.walkTo(coords, { range: 2 });
            const crafter = Mobiles.all().find((m) => m.exists && lower(m.name) === lower(crafterName));
            if (!crafter) { console.warn(`[customer] ${crafterName} is not here`); return false; }
            const quoteLine = new Promise((resolve) => {
                const h = (msg) => {
                    if (msg.serial !== crafter.serial) return;
                    const m = msg.text.match(/will be (\d+) gold/i);
                    if (m) { Player.off('journal', h); resolve(parseInt(m[1], 10)); }
                    else if (/cannot|too many|do not know/i.test(msg.text)) { Player.off('journal', h); resolve(-1); }
                };
                Player.on('journal', h);
                delay(6000).then(() => { Player.off('journal', h); resolve(0); });
            });
            Player.say(`${crafterName} order ${orderText}`);
            const price = await quoteLine;
            if (price <= 0) { console.log(`[customer] no quote (${price})`); return false; }
            const budget = typeof this.spendable === 'function' ? this.spendable(false) : this.backpackCount(['gold']);
            if (price > budget) { Player.say(`${crafterName} cancel`); console.log(`[customer] ${price}gp is over budget ${budget}`); return false; }
            const gold = this.findInPack('gold');
            if (!gold) return false;
            // Trade.start drags ONE coin onto the crafter (that is what opens
            // the window); the rest of the price is then added from the pack,
            // so exactly the quoted amount is offered -- no change needed.
            Trade.start(crafter.serial, gold.serial);
            for (let i = 0; i < 10 && !Trade.state.active; i++) await token.sleep(500);
            if (!Trade.state.active) return false;
            const offered = () => Player.containerItems(Trade.state.myContainer)
                .filter((i) => lower(i.name).includes('gold')).reduce((a, i) => a + (i.amount || 0), 0);
            await token.sleep(800);
            const rest = this.findInPack('gold');
            if (rest && price - offered() > 0) Trade.offer(rest.serial, price - offered());
            await token.sleep(1200);
            if (offered() < price) { console.warn(`[customer] could only offer ${offered()}/${price}`); Trade.cancel(); return false; }
            for (let i = 0; i < 30 && Trade.state.active && !Trade.state.theirCheck; i++) await token.sleep(1000);
            if (Trade.state.active) Trade.accept(true);
            for (let i = 0; i < 10 && Trade.state.active; i++) await token.sleep(1000);
            if (this.ledger) this.ledger.record('buy', price, `order from ${crafterName}`);
            this.pendingOrder = { crafterName, coords, placedMs: Date.now() };
            return true;
        },

        // Collect: stand by the crafter; accept its trade once goods are on it.
        async collectOrder() {
            const { token } = this;
            const p = this.pendingOrder;
            if (!p) return false;
            await this.walkTo(p.coords, { range: 2 });
            for (let i = 0; i < 60 && !Trade.state.active; i++) await token.sleep(1000);
            if (!Trade.state.active) return false;
            for (let i = 0; i < 20 && Player.containerItems(Trade.state.theirContainer).length === 0; i++) await token.sleep(1000);
            if (Player.containerItems(Trade.state.theirContainer).length > 0) Trade.accept(true);
            for (let i = 0; i < 20 && Trade.state.active; i++) await token.sleep(1000);
            this.pendingOrder = null;
            return true;
        },
    };

    g.CraftSkill = CraftSkill;
    g.CustomerSkill = CustomerSkill;
})(globalThis);
