'use strict';
// Crafter: a Britain tinker who trains Tinkering by making things, sells what
// it makes, and takes ORDERS -- single pieces, quantities, or whole sets --
// from anyone who speaks to it, paid up front and delivered by secure trade.
//
// Why a tinker first: Tinkering is the one craft whose whole menu flow is
// proven live on this shard (m38_tinker_craft.txt, m391_craft_oracle.txt):
// the tool opens the 0x7C menu on double-click, no station needed, and its
// material (iron ingots) is an NPC-purchasable good under our vendor policy.
// A smith follows the same code once its hammer->target flow is proven; set
// CRAFT_TOOL/CRAFT_SKILL/MATERIAL and add a craftTarget() hook.
//
// Priority:
//   resurrect > offline > trade > deliver > sell > order > tools > materials > train
//
// Talk to it (standing within earshot):
//   "<name> order 10 pickaxe"        "<name> order tinker tools set"
//   "<name> status"                  "<name> cancel"

class Crafter extends BehaviorScript {
    CRAFT_TOOL = 'tinker';          // backpack name substring of the tool
    CRAFT_SKILL = 37;               // Tinkering ([SKILL 37])
    TOOL_SPARES = 2;                // tools break; keep this many, make more
    // Iron ingots from the Britain blacksmith (atlas britain_blacksmith). M3.7
    // saw a Britain blacksmith stocking ingots; if this one does not, the
    // buy logs "does not sell" and the crafter trains on what it has.
    MATERIAL = { name: ['ingot'], target: 40, low: 8, coords: { x: 1418, y: 1547 }, title: 'blacksmith' };
    MATERIAL_PER_PIECE = 4;         // ingots, for pricing only (quote floor applies)
    PRICING = { margin: 0.5, minPerPiece: 10 };

    // Where it works and waits for customers: the First Bank of Britain
    // (atlas britain_bank_2) -- guarded, busy, and next to its gold.
    WORK_SPOT = { x: 1425, y: 1690 };
    BANK = { x: 1425, y: 1690 };
    HEALER = { x: 1471, y: 1611 };

    // Economy (lib/economy.js): tools, ingots and anything reserved for an
    // order are never sold; the rest of the training output is.
    KEEP = ['tinker', 'ingot'];
    SELL_VENDORS = [
        { title: 'blacksmith', coords: { x: 1418, y: 1547 } },
        { title: 'provisioner', coords: { x: 1469, y: 1668 } },
    ];
    WALLET = 300;                   // crafters carry more: materials cost money
    GOLD_RESERVE = 150;
    MAX_CARRY_GOLD = 1500;
    FOOD = ['bread', 'lamb'];
    CONSUMABLES = {};
    PERSONA = { riskTolerance: 0.2, activeHours: [] };

    threat = null;
    fleeing = false;
    lastAteMs = 0;

    constructor() {
        super();
        this.installCombatSensing();
        this.economyInit();
        this.memoryInit();
        this.craftInit();
    }

    // Items promised to paid orders: never sold, never used for training.
    // (Named so no mixin defines it -- see the Object.assign note in combat.js.)
    reservedNames() {
        return this.orders.filter((o) => o.status === 'paid' || o.status === 'ready')
            .flatMap((o) => o.items.map((it) => it.name));
    }

    toolCount() { return this.backpackCount([this.CRAFT_TOOL]); }

    readyOrderWithCustomerNear() {
        return this.orders.find((o) => {
            if (o.status !== 'ready') return false;
            const c = Mobiles.get(o.customer.serial);
            return c.exists && tileDistance({ x: c.x, y: c.y }, { x: Player.x, y: Player.y }) <= 3;
        }) || null;
    }

    // ===== behaviours =====

    async atWork() {
        if (tileDistance({ x: Player.x, y: Player.y }, this.WORK_SPOT) > 4)
            await this.walkTo(this.WORK_SPOT, { range: 3 });
    }

    async trade() { await this.handleTrade(); }

    async deliver() {
        const { token } = this;
        const order = this.readyOrderWithCustomerNear();
        if (!order) return;
        const first = Player.equipment.backpack.items.find((i) =>
            i.name.toLowerCase().includes(order.items[0].name.toLowerCase()));
        if (!first) { order.status = 'paid'; order.items[0].made = 0; return; }   // lost it: remake
        Trade.start(order.customer.serial, first.serial);
        for (let i = 0; i < 10 && !Trade.state.active; i++) await token.sleep(500);
        await this.handleTrade();
    }

    async order() {
        this.expireQuotes();
        await this.atWork();
        await this.workOrder();
    }

    // Tools wear out. Make a spare while one still works -- a tinker who lets
    // its last tool break has to buy one, and no Britain shop is known to sell
    // them (the tinker there is a guildmaster, who keeps no shop).
    async tools() {
        if (this.catalogueStale()) await this.readCatalogue();
        const entry = OrderPolicy.catalogueMatch('tinker tools', this.catalogue);
        if (!entry) { console.warn('[craft] cannot make spare tools yet'); this.toolsBlockedUntil = Date.now() + 10 * 60 * 1000; return; }
        const r = await this.craftOne(entry);
        console.log(`[craft] spare tinker tools: ${r}`);
    }

    async materials() {
        await this.restockMaterials();
        this.materialsCheckedMs = Date.now();
    }

    async trainStep() {
        this.expireQuotes();
        await this.atWork();
        await this.train();
    }

    behaviors() {
        const have = (n) => this.backpackCount(n);
        return [
            { name: 'resurrect', when: () => Player.dead, step: this.step('resurrect') },
            { name: 'offline', when: () => !this.isActiveNow() && !Trade.state.active &&
                    !this.orders.some((o) => o.status === 'paid' || o.status === 'ready'),
                step: this.step('endSession') },
            { name: 'fight', when: () => !this.fleeing && Boolean(this.threat?.exists), step: this.step('fight') },
            { name: 'trade', when: () => Trade.state.active, step: this.step('trade') },
            { name: 'deliver', when: () => Boolean(this.readyOrderWithCustomerNear()), step: this.step('deliver') },
            { name: 'sell', when: () => (this.full() || this.carryingTooMuchGold()) && this.bankTripDue(),
                step: this.sequence('sellLoot', 'bankSurplusGold', 'withdrawGold', 'economyReport') },
            { name: 'order', when: () => this.orders.some((o) => o.status === 'paid'), step: this.step('order') },
            { name: 'tools', when: () => this.toolCount() > 0 && this.toolCount() < this.TOOL_SPARES &&
                    Date.now() > (this.toolsBlockedUntil || 0), step: this.step('tools') },
            { name: 'materials', when: () => have(this.MATERIAL.name) < this.MATERIAL.low &&
                    Date.now() - (this.materialsCheckedMs || 0) > 10 * 60 * 1000, step: this.step('materials') },
            { name: 'train', when: () => this.toolCount() > 0, step: this.step('trainStep') },
        ];
    }

    async onStartup() {
        Player.requestSkills();
        await this.token.sleep(1500);
        console.log(`[craft] Tinkering ${(Player.skill(this.CRAFT_SKILL) / 10).toFixed(1)}, ` +
            `${this.toolCount()} tool(s), ${this.backpackCount(this.MATERIAL.name)} ingot(s)`);
        await this.atWork();
    }

    onPreempt() { Player.stop(); }
}

Object.assign(Crafter.prototype, BankSkill, SurvivalSkill, CombatSkill, EconomySkill, MemorySkill, CraftSkill);

new Crafter().start();
