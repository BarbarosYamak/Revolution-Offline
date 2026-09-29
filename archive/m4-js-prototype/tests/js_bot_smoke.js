// JS bot smoke test: the combat/flee/rest decisions and the graveyard patrol,
// run under Node with the C++ bindings (Player, Mobiles, ...) stubbed out.
// No server, no client. Usage: node tests/js_bot_smoke.js [scripts/js dir]
//
// Guards the M4 fixes for "bots disengage way too quickly / always ask for
// bandages / never wander the cemetery", and the Object.assign mixin trap that
// silently replaced each bot's onFlee() with an empty one.
'use strict';
const fs=require('fs'), vm=require('vm');
const path=require('path').join(process.argv[2] || require('path').join(__dirname,'..','scripts','js'),'/');
let fails=0, checks=0, finished=false;
// A promise that never settles lets Node exit quietly with code 0 -- which would
// read as a pass. Treat an unfinished run as a failure.
process.on('beforeExit', () => { if (!finished) { console.error('js_bot_smoke: exited before finishing (a promise never settled)'); process.exit(1); } }); const check=(c,m)=>{checks++; if(!c){fails++;console.log('FAIL',m);}};
const g=globalThis;
g.__stdout=(m)=>{ if(/FAIL|checks/.test(m)) process.stdout.write(m+"\n"); }; g.__stderr=(m)=>console.error(m); g.__setTimeout=setTimeout; g.__clearTimeout=clearTimeout;
const handlers={};
g.Player={hp:50,hpMax:50,x:1380,y:1480,dead:false,warMode:false,serial:1,weight:0,maxWeight:300,
  equipment:{backpack:{items:[]}}, on:(n,f)=>{handlers[n]=f}, off(){}, once(){return new Promise(()=>{})},
  requestStatus(){}, follow(){}, attack(){}, setWarMode(){}, stop(){}, say(){}};
let mobs=[];
g.Mobiles={all:()=>mobs, get:(s)=>mobs.find(m=>m.serial===s)||{serial:s,exists:false,hpPct:-1}};
g.createThreatMeter=()=>({start(){},stop(){},count:0});
g.createBehaviorRunner=()=>({start(){},stop(){}}); g.makeToken=()=>({});
g.CANCELLED={};
// Every lib in the engine's own order: sorted file names (JsEngine.cpp EvalLibs).
const libs = fs.readdirSync(path + 'lib').filter((f) => f.endsWith('.js')).sort();
for (const f of ['bootstrap.js', ...libs.map((l) => 'lib/' + l)])
  vm.runInThisContext(fs.readFileSync(path+f,'utf8'),{filename:f});
g.BehaviorScript.prototype._bootstrap=function(){};
// ---- combat assessFight
const C=Object.assign({hpFrac(){return Player.hp/Player.hpMax}}, CombatSkill);
const foe={serial:9,exists:true,hpPct:1.0,name:'zombie'}; C.threat=foe;
let now=1_000_000; const realNow=Date.now; Date.now=()=>now;
let b={startMs:0,startMyHp:-1,startFoeHp:-1};
C.assessFight(b); // latch
now+=4000; Player.hp=44; // lost 12%, foe untouched, 4s
check(!C.assessFight(b).flee, '4s and 12% lost with foe untouched: do NOT flee yet (old code fled here)');
now+=6000; Player.hp=38; // 10s, 24% lost, foe untouched
check(C.assessFight(b).flee, '10s, 24% lost, foe untouched: cannot dent -> flee');
b={startMs:0,startMyHp:-1,startFoeHp:-1}; Player.hp=50; foe.hpPct=1.0; C.assessFight(b);
now+=10000; Player.hp=35; foe.hpPct=0.7; // me TTD .7/.3=2.33, foe .7/.3=2.33 tie
check(!C.assessFight(b).flee, 'an even trade is not a losing race (old code fled on a tie)');
Player.hp=20; foe.hpPct=0.2; // me 40%, foe nearly dead
check(!C.assessFight(b).flee, 'foe at 20%: finish it');
Player.hp=12; // 24% < floor
check(C.assessFight(b).flee, 'below the 30% floor: flee even from a dying foe');
// ---- rest threshold
Player.hp=47; let bandaged=0; const R=Object.assign({token:{sleep:async()=>{}},threat:null},SurvivalSkill,{REST_UNTIL_FRAC:0.9,bandageSelf:async()=>{bandaged++;Player.hp=50;}});
Date.now=realNow;
R.rest().then(()=>{
  check(bandaged===0, '94% HP: rest() does not spend a bandage on a scratch');
  Player.hp=30; return R.rest();
}).then(()=>{
  check(bandaged===1, '60% HP: rest() bandages');
  // ---- graveyard
  g.BehaviorScript.prototype.start=function(){return this};
  // graveyard.js is now a swordsman on the generic fighter engine.
  g.GH = function () { const b = new FighterBot('swordsman', { grounds: ['britain_graveyard'] });
    b.ground = { id: 'britain_graveyard', ...Archetypes.GROUNDS.britain_graveyard }; return b; };
  const h=new GH();
  let inside=true; for(let i=0;i<500;i++){const p=h.waypoint(); if(!h.inGround(p.x,p.y)) inside=false;}
  check(inside,'every patrol waypoint is inside the graveyard rects');
  mobs=[{serial:20,exists:true,notoriety:3,body:0x03,x:1383,y:1480},
        {serial:21,exists:true,notoriety:1,body:0x190,x:1381,y:1480},   // blue human
        {serial:22,exists:true,notoriety:3,body:0x1a,x:1395,y:1480}];   // ghost, out of range? 15 tiles
  Player.x=1380; Player.y=1480;
  const p=h.findPrey(); check(p && p.serial===20,'hunts the nearby zombie, not the human');
  mobs=[{serial:21,exists:true,notoriety:1,body:0x190,x:1381,y:1480}];
  check(h.findPrey()===null,'never hunts a blue human');
  h.onFlee({x:1383,y:1480}); mobs=[{serial:20,exists:true,notoriety:3,body:0x03,x:1383,y:1480}];
  check(h.findPrey()===null,'does not go straight back to a mob it fled from');
  const names=h.behaviors().map(x=>x.name).join('>');
  check(names==='resurrect>offline>fight>loot>bank>collect>gear>eat>recover>patrol','priority order '+names);
  return economyTests();
}).then(() => {
  finished = true;
  console.log(`${checks} checks, ${fails} failures`); process.exit(fails?1:0);
}).catch((e) => { console.error(e); process.exit(1); });

async function economyTests() {
  const { planSale, keepAmount, createLedger } = EconomyPolicy;
  const rules = { keep: ['hatchet', 'bandage'], stock: [{ names: ['bread'], target: 5 }] };
  check(keepAmount('gold coin', rules) === Infinity, 'gold is never sold');
  check(keepAmount('hatchet', rules) === Infinity, 'the tool is never sold');
  check(keepAmount('bread loaf', rules) === 10, 'food: keep 2x target');
  check(keepAmount('bone helmet', rules) === 0, 'loot: keep none');
  const pack = { 'bone helmet': 1, 'bread loaf': 14, 'hatchet': 1, 'leather gloves': 2 };
  const plan = planSale([
    { serial: 1, amount: 1, price: 12, name: 'Bone Helmet' },
    { serial: 2, amount: 14, price: 1, name: 'bread loaf' },
    { serial: 3, amount: 1, price: 9, name: 'hatchet' },
    { serial: 4, amount: 2, price: 0, name: 'leather gloves' },
  ], rules, (n) => pack[n] || 0);
  const by = Object.fromEntries(plan.map((p) => [p.serial, p.qty]));
  check(by[1] === 1, 'sells the loot the vendor listed');
  check(by[2] === 4, 'sells only the food surplus above 2x target (14 - 10)');
  check(by[3] === undefined, 'never sells the hatchet even when a vendor offers');
  check(by[4] === undefined, 'does not give loot away for 0 gold');

  const L = createLedger(0);
  L.record('sale', 100, 'x', 1); L.record('loot', 40, 'y', 1); L.record('buy', 30, 'z', 1); L.record('bank', 999, '', 1);
  check(L.income() === 140 && L.expense() === 30 && L.net() === 110, 'ledger: income, expense, net');
  check(L.totals.other === 999, 'unknown kinds are kept apart, not counted as income');

  // --- the mixin on a graveyard hunter: market learning + corpse attribution
  const h = new GH();
  check(typeof h.lootKill === 'function' && typeof h.sellLoot === 'function', 'graveyard bot has the economy skill');
  check(typeof h.onFlee === 'function' && h.onFlee !== CombatSkill.onFlee, 'onFlee still the bot\'s own');
  h.economyInit();
  h.marketEntry('bone helmet').refusedBy.add('blacksmith');
  check(!h.isUnsellable('bone helmet'), 'refused by one vendor is not unsellable');
  for (const v of h.SELL_VENDORS) h.marketEntry('bone helmet').refusedBy.add(v.title);
  check(h.isUnsellable('bone helmet'), 'refused by every vendor on the route is unsellable');
  h.marketEntry('bone helmet').buyers['armorer'] = { price: 5, seenMs: 0 };
  check(!h.isUnsellable('bone helmet'), 'a known buyer overrides refusals');

  // corpse attribution
  let opened = [], taken = [];
  g.World = { items: () => worldItems };
  let worldItems = [];
  Player.containerItems = (s) => s === 500 ? [{ serial: 600, name: 'gold coin', amount: 12 }, { serial: 601, name: 'bone helmet', amount: 1 }] : [];
  Player.take = (s) => taken.push(s);
  h.walkTo = async () => true;
  h.openContainer = async (s) => { opened.push(s); return { serial: s }; };
  h.token = { sleep: async () => {}, check() {}, wait: (p) => p, cancelled: false };
  h.full = () => false;
  h.waitGoldChange = async () => 0;
  worldItems = [{ serial: 500, graphic: 0x2006, corpse: true, corpseOf: 77, x: 1, y: 1 },
                { serial: 501, graphic: 0x2006, corpse: true, corpseOf: 88, x: 1, y: 2 }];
  h.lastKill = { serial: 77, x: 1, y: 1, atMs: Date.now() };
  await h.lootKill();
  check(opened.join() === '500', 'loots the corpse attributed to our kill, not the other');
  check(taken.includes(600) && taken.includes(601), 'takes gold and sellable loot');
  check(h.lastKill === null, 'a kill is looted once');
  opened = []; worldItems = [{ serial: 510, corpse: true, corpseOf: 0, x: 1, y: 1 }, { serial: 511, corpse: true, corpseOf: 0, x: 2, y: 1 }];
  h.lastKill = { serial: 77, x: 1, y: 1, atMs: Date.now() };
  await h.lootKill();
  check(opened.length === 0, 'two unattributed corpses: touch neither (one may be someone else\'s)');
  opened = []; h.lastKill = { serial: 77, x: 1, y: 1, atMs: Date.now() - 10 * 60 * 1000 };
  worldItems = [{ serial: 500, corpse: true, corpseOf: 77, x: 1, y: 1 }];
  await h.lootKill();
  check(opened.length === 0, 'a stale kill is not walked back to');

  // budget
  h.backpackCount = (n) => [].concat(n).some((x) => String(x).includes('gold')) ? 120 : 0;
  h.GOLD_RESERVE = 100;
  check(h.spendable(false) === 20, 'discretionary spending leaves the reserve alone');
  check(h.spendable(true) === 120, 'essentials may use the reserve');

  // --- ideas adopted from other UO bot projects (M4.3 research) ---
  const floorPlan = planSale([{ serial: 9, amount: 3, price: 2, name: 'bone helmet' }],
    { keep: [], stock: [], minPrice: { 'bone': 5 } }, () => 3);
  check(floorPlan.length === 0, 'minPrice: not sold below the floor');
  const big = planSale([{ serial: 1, amount: 200, price: 1, name: 'log' }, { serial: 2, amount: 200, price: 1, name: 'log' }],
    { keep: [], stock: [] }, () => 400);
  check(big.reduce((a, p) => a + p.qty, 0) === EconomyPolicy.VENDOR_MAX_SELL, 'one sale never exceeds VendorMaxSell=255');

  opened = []; h.looted = new Set([500]);
  worldItems = [{ serial: 500, corpse: true, corpseOf: 77, x: 1, y: 1 }];
  h.lastKill = { serial: 77, x: 1, y: 1, atMs: Date.now() };
  await h.lootKill();
  check(opened.length === 0, 'a corpse is never looted twice');
  opened = []; h.looted = new Set();
  mobs = [{ serial: 30, exists: true, notoriety: 3, body: 0x03, x: 2, y: 1 }];
  h.lastKill = { serial: 77, x: 1, y: 1, atMs: Date.now() };
  await h.lootKill();
  check(opened.length === 0 && h.lastKill && h.lastKill.serial === 77,
        'hostile beside the corpse: do not loot now, keep the kill for later');
  mobs = [];

  // gang pressure
  Player.x = 100; Player.y = 100;
  mobs = [];
  const one = h.fleeFloor();
  mobs = [1, 2, 3].map((i) => ({ serial: 40 + i, exists: true, notoriety: 3, body: 0x03, x: 100 + i % 2, y: 100 }));
  const three = h.fleeFloor();
  check(Math.abs(one - h.baseFleeFloor()) < 1e-9, 'one-on-one: the character\'s own floor');
  check(Math.abs(three - Math.min(Math.max(h.GANG_MAX_FLOOR, one), one + 0.2)) < 1e-9, 'three attackers: floor raised by two steps (capped)');
  mobs = Array.from({ length: 12 }, (_, i) => ({ serial: 60 + i, exists: true, notoriety: 6, body: 0x03, x: 101, y: 101 }));
  check(h.fleeFloor() === h.GANG_MAX_FLOOR, 'the raised floor is capped');
  mobs = [];

  // low-mark restock
  let bandages = 6;
  h.backpackCount = (n) => [].concat(n).some((x) => String(x).includes('bandage')) ? bandages : 0;
  check(h.consumableIsLow({ target: 20 }, ['bandage']) === false, '6 of 20 bandages: not low yet (mark 5)');
  bandages = 5;
  check(h.consumableIsLow({ target: 20 }, ['bandage']) === true, '5 of 20: low -> restock before running out');
  check(h.consumableIsLow({ target: 20, low: 8 }, ['bandage']) === true, 'an explicit low mark wins');

  // --- memory / persona (lib/memory.js) ---
  const { decayed, isActiveAt, personalFloor } = MemoryPolicy;
  check(Math.abs(decayed(4, 45 * 60000, 45 * 60000) - 2) < 1e-9, 'danger heat halves every half-life');
  const at = (h) => { const d = new Date(2026, 0, 1, h, 0); return d; };
  check(isActiveAt([], at(3)), 'no schedule = always active');
  check(isActiveAt([[18, 23]], at(20)) && !isActiveAt([[18, 23]], at(12)), 'evening window');
  check(isActiveAt([[22, 2]], at(1)) && !isActiveAt([[22, 2]], at(3)), 'a window can wrap midnight');
  check(personalFloor(0.3, 0.5, 0) > personalFloor(0.3, 0.5, 1000), 'a novice leaves earlier than a GM');
  check(personalFloor(0.3, 0.0, 500) > personalFloor(0.3, 1.0, 500), 'a cautious persona leaves earlier than a reckless one');
  check(personalFloor(0.3, 0, 0) <= 0.6 && personalFloor(0.9, 1, 1000) >= 0.15, 'floor stays within 15..60%');

  h.memoryInit();
  h.noteDanger(1370, 1480, 5, 'test');
  check(h.dangerAt(1370, 1480) > 4.9 && h.dangerAt(1500, 1600) === 0, 'danger is local to where it happened');
  let hot = 0; const rnd = (() => { let i = 0; return () => ((i++ * 0.6180339) % 1); })();
  for (let i = 0; i < 1000; i++) if (h.pickByDanger([{ x: 1370, y: 1480 }, { x: 1500, y: 1600 }], rnd).x === 1370) hot++;
  check(hot > 100 && hot < 350, `a hot place is picked ~1/5 as often, not never (${hot}/1000)`);
  check(!h.noteFlee(0x55, 1, 1) && !h.isHunted(), 'first flee: not hunted');
  check(h.noteFlee(0x55, 1, 1) && h.isHunted(), 'second flee from the same foe within a minute: hunted');
  h.noteEmptySpot(10, 20);
  check(h.isEmptySpot(10, 20) && !h.isEmptySpot(10, 21), 'an exhausted spot is remembered exactly');

  // bandage lock
  h.bandageBusyUntil = Date.now() + 5000;
  check(await h.bandageSelf() === false, 'no second bandage while one is being applied');

  // --- crafting & orders (lib/crafting.js) ---
  const OP = OrderPolicy;
  let po = OP.parseOrder('Ahmet order pickaxe', 'Ahmet');
  check(po && po.kind === 'order' && po.items[0].name === 'pickaxe' && po.items[0].qty === 1 && !po.set, 'order: one piece');
  po = OP.parseOrder('ahmet, order 10 pickaxes', 'Ahmet');
  check(po.items[0].qty === 10 && po.items[0].name === 'pickaxe', 'order: a quantity (plural folded)');
  po = OP.parseOrder('Ahmet order ringmail set', 'Ahmet');
  check(po.set === 'ringmail' && po.items.length === 4 && po.items.every((i) => i.qty === 1), 'order: a whole set expands to its pieces');
  po = OP.parseOrder('Ahmet order dragon set', 'Ahmet');
  check(po.error && po.items.length === 0, 'order: unknown set is refused with a reason');
  check(OP.parseOrder('Mehmet order pickaxe', 'Ahmet') === null, 'order: not addressed to us -> ignored');
  check(OP.parseOrder('Ahmet status', 'Ahmet').kind === 'status' && OP.parseOrder('Ahmet cancel', 'Ahmet').kind === 'cancel', 'status / cancel');
  po = OP.parseOrder('Ahmet order 500 pickaxe', 'Ahmet');
  check(po === null || po.items[0].qty <= 100, 'absurd quantities are capped or rejected');

  const cat = [{ category: 'Tools', label: 'Pickaxe' }, { category: 'Tools', label: 'Scissors' }, { category: 'Tools', label: 'Tinker Tools' },
               { category: 'Parts', label: 'Nails' }];
  check(OP.checkOrder([{ name: 'pickaxe', qty: 3 }], cat).ok, 'the live menu offers it: accept');
  const miss = OP.checkOrder(OP.parseOrder('Ahmet order ringmail set', 'Ahmet').items, cat);
  check(!miss.ok && miss.missing.length === 4, 'a set with pieces the menu does not offer is declined (menu oracle)');
  check(OP.checkOrder(OP.parseOrder('Ahmet order tinker tools set', 'Ahmet').items, cat).ok, 'a set the menu fully covers is accepted');
  check(OP.quote([{ name: 'pickaxe', qty: 3 }], () => 0, { minPerPiece: 10 }) === 30, 'quote: per-piece floor when cost unknown');
  check(OP.quote([{ name: 'pickaxe', qty: 2 }], () => 20, { margin: 0.5 }) === 60, 'quote: observed cost + 50%');
  const ord = OP.createOrder(1, { serial: 9, name: 'Mehmet' }, OP.parseOrder('Ahmet order tinker tools set', 'Ahmet'), 90, 0);
  check(ord.status === 'quoted' && OP.nextPiece(ord).name === 'tinker tools', 'new order: quoted, first piece next');
  ord.items.forEach((it) => { it.made = it.qty; });
  check(OP.orderDone(ord) && OP.nextPiece(ord) === null, 'all pieces made -> done');

  // the crafter bot, end to end on stubs
  g.CR = function () { return new CrafterBot('tinker'); };
  const said = [];
  Player.say = (t) => said.push(t);
  Player.name = 'Ahmet';
  Player.skill = () => 450;
  g.Trade = { state: { active: false }, start() {}, offer() {}, accept() { return true; }, cancel() { return true; } };
  const cr = new CR();
  cr.catalogue = cat; cr.catalogueMs = Date.now(); cr.catalogueSkill = 450;
  mobs = [{ serial: 9, exists: true, name: 'Mehmet', x: 0, y: 0 }];
  cr.onSpeech({ text: 'Ahmet order 3 pickaxes', serial: 9, system: false });
  check(cr.orders.length === 1 && cr.orders[0].items[0].qty === 3 && /will be \d+ gold/.test(said.pop()), 'crafter quotes a spoken order');
  cr.onSpeech({ text: 'Ahmet order platemail', serial: 9, system: false });
  check(cr.orders.length === 1 && /cannot make/.test(said.pop()), 'crafter declines what its menu does not offer, and says why');
  check(cr.wanted && cr.wanted.size === 1, 'a declined piece becomes a training goal');
  cr.onSpeech({ text: 'Ahmet order tinker tools set', serial: 9, system: false });
  cr.onSpeech({ text: 'Ahmet order nails', serial: 9, system: false });
  cr.onSpeech({ text: 'Ahmet order scissors', serial: 9, system: false });
  check(cr.openOrders().length === 3 && /too many orders/.test(said.pop()), 'at most MAX_OPEN_ORDERS open');
  cr.onSpeech({ text: 'Ahmet cancel', serial: 9, system: false });
  check(cr.orders.filter((o) => o.status === 'cancelled').length === 1, 'cancel drops an unpaid quote');
  cr.orders[0].createdMs = Date.now() - 10 * 60 * 1000; cr.expireQuotes();
  check(cr.orders[0].status === 'cancelled', 'an unpaid quote expires');

  // payment through the trade window
  const o2 = cr.orders.find((o) => o.status === 'quoted');
  let gold = 100;
  cr.goldInPack = () => gold;
  cr.token = { sleep: async () => {}, check() {}, wait: (p) => p, cancelled: false };
  g.Trade = { state: { active: true, partner: 9, theirContainer: 77, closeReason: 'none' },
    accept() { gold += o2.price; this.state.active = false; this.state.closeReason = 'both_accepted'; return true; },
    cancel() { this.state.active = false; return true; }, offer() {} };
  Player.containerItems = (s) => s === 77 ? [{ serial: 5, name: 'gold coin', amount: o2.price }] : [];
  await cr.handleTrade();
  check(o2.status === 'paid' && cr.ledger.totals.sale >= o2.price, 'gold >= price in the trade window: accepted, order paid, ledgered');
  check(cr.reservedNames().includes(o2.items[0].name), 'paid order items are reserved from sale');

  g.Trade.state = { active: true, partner: 9, theirContainer: 77, closeReason: 'none' };
  const o3 = cr.orders.find((o) => o.status === 'quoted');
  if (o3) {
    Player.containerItems = () => [{ serial: 5, name: 'gold coin', amount: 1 }];
    let cancelled = false; g.Trade.cancel = function () { cancelled = true; this.state.active = false; return true; };
    await cr.handleTrade();
    check(cancelled && o3.status === 'quoted', 'too little gold: trade cancelled, order stays unpaid');
  }

  // training picks something sellable and never the kept tool/material
  cr.market.set('scissors', { buyers: { blacksmith: { price: 5 } }, refusedBy: new Set() });
  let picks = new Set(); for (let i = 0; i < 30; i++) picks.add(cr.chooseTrainingItem().label);
  check(!picks.has('Tinker Tools'), 'training never makes the kept tool');
  check(picks.size === 1 && picks.has('Scissors'), 'training prefers what a vendor is known to buy');

  // craftOne through a stubbed menu
  const menus = [{ options: [{ index: 1, text: 'Tools' }, { index: 2, text: 'Parts' }] },
                 { options: [{ index: 1, text: 'Pickaxe' }, { index: 2, text: 'Scissors' }] }];
  let mi = 0, chosen = [];
  Player.once = (ev) => ev === 'dialog' ? Promise.resolve(menus[mi++]) : Promise.reject(new Error('timeout'));
  Player.dialogRespond = (i) => chosen.push(i);
  Player.use = () => {};
  Player.equipment.backpack.items = [{ serial: 1, name: "tinker's tools", amount: 1 }];
  g.waitForJournal = () => { Player.equipment.backpack.items.push({ serial: 2, name: 'scissors', amount: 1 }); return Promise.resolve('You create the item.'); };
  const res = await cr.craftOne({ category: 'Tools', label: 'Scissors' });
  check(res === 'ok' && chosen.join() === '1,2', 'craftOne: category then item, chosen BY NAME from the live menu');
  mi = 0; chosen = [];
  check(await cr.craftOne({ category: 'Tools', label: 'Platemail' }) === 'not_offered', 'craftOne: an item the menu lacks is not attempted');

  // ===== every archetype (lib/archetypes.js, fighter.js, crafterbot.js, gathering.js) =====
  const AR = Archetypes;
  check(fs.readFileSync(path + '../../data/revolution_archetypes.tsv', 'utf8') === AR.toTsv(),
        'data/revolution_archetypes.tsv matches lib/archetypes.js (regenerate if you changed a build)');
  const kinds = { fighter: 0, crafter: 0, gatherer: 0 };
  for (const [id, a] of Object.entries(AR.A)) {
    const v = AR.validate(id);
    check(v.ok, `${id}: build is legal (${v.why || v.total + ' pts'})`);
    check(AR.TOWNS[a.home], `${id}: home town ${a.home} exists`);
    check(fs.existsSync(path + id + '.js'), `${id}: has a launcher script ${id}.js`);
    kinds[a.kind]++;
    if (a.kind === 'crafter') check(AR.CRAFTS[a.craft] && AR.CRAFTS[a.craft].skill === AR.SK[Object.keys(AR.SK).find((k) => AR.SK[k] === AR.CRAFTS[a.craft].skill)],
        `${id}: craft ${a.craft} defined`);
    if (a.build.cls === 'HISTORICAL_EXACT') check(v.total === 700, `${id}: an exact historical build spends all 700`);
  }
  check(kinds.fighter >= 10 && kinds.crafter >= 8 && kinds.gatherer >= 3, `all types present (${JSON.stringify(kinds)})`);
  for (const c of ['alchemist', 'scribe', 'bowyer', 'blacksmith', 'tailor', 'carpenter', 'tinker', 'cook'])
    check(AR.CRAFTS[c], `craft ${c} exists`);
  check(!AR.validate('warlock').canUsePoisonedWeapon && AR.validate('swordsman').canUsePoisonedWeapon,
        'L4: Magery>40 warlock may not wield a poisoned blade; the warrior may');
  for (const [gid, gr] of Object.entries(AR.GROUNDS)) check(AR.TOWNS[gr.town], `ground ${gid}: town ${gr.town} exists`);

  const FP = FighterPolicy;
  check(FP.bestAttackSpell(0).id === FP.SPELL.MAGIC_ARROW && FP.bestAttackSpell(1000).id === FP.SPELL.FLAMESTRIKE,
        'spell choice follows Magery: Magic Arrow at 0, Flamestrike at GM');
  check(FP.levelFor(100) === 1 && FP.levelFor(600) === 3 && FP.levelFor(950) === 4, 'hunting level from skill');

  // every fighter constructs, and its style hooks behave
  const mk = (id, opts) => { const f = new FighterBot(id, opts); f.token = h.token; return f; };
  const fighters = {};
  for (const [id, a] of Object.entries(AR.A)) if (a.kind === 'fighter') {
    let ok = true; try { fighters[id] = mk(id); } catch (e) { ok = false; console.log(e.message); }
    check(ok, `fighter ${id} constructs`);
  }
  Player.mana = 50; Player.manaMax = 100;
  check(fighters.pure_mage.followRange() === 6 && !fighters.pure_mage.meleeInFight(), 'mage fights from range 6 and does not swing');
  Player.mana = 5;
  check(fighters.pure_mage.meleeInFight(), 'mage out of mana wrestles');
  const packItems = Player.equipment.backpack.items;
  Player.equipment.backpack.items = [{ serial: 70, name: 'arrow', amount: 50 }];
  fighters.archer.backpackCount = FighterBot.prototype.backpackCount;
  check(fighters.archer.followRange() === 4, 'archer with arrows keeps range 4');
  Player.equipment.backpack.items = [];
  check(fighters.archer.followRange() === 1, 'archer without arrows closes to melee');
  check(fighters.tamer.meleeInFight() && fighters.tamer.followRange() === 1, 'tamer without a pet defends itself');
  check(fighters.swordsman.CONSUMABLES.bandage && !fighters.pure_mage.CONSUMABLES.arrow, 'consumables follow the build');
  check(fighters.archer.CONSUMABLES.arrow && fighters.archer.CONSUMABLES.arrow.title === 'bowyer', 'archer restocks arrows at the bowyer');
  check(fighters.pure_mage.CONSUMABLES['black pearl'], 'mage restocks reagents');

  // combatTick per style
  const casts = []; Player.cast = (sp, t) => casts.push([sp, t]); Player.skill = (i) => (i === AR.SK.MAGERY ? 1000 : 500);
  Player.mana = 90; Player.hp = 50; Player.hpMax = 50;
  mobs = [{ serial: 88, exists: true, notoriety: 3, body: 0x03, x: 1381, y: 1480, hpPct: 1 }];
  const pm = fighters.pure_mage; pm.threat = Mobiles.get(88); pm.lastCastMs = 0;
  await pm.combatTick();
  check(casts.length === 1 && casts[0][0] === FP.SPELL.FLAMESTRIKE && casts[0][1] === 88, 'GM mage opens with Flamestrike on the foe');
  Player.hp = 20; pm.lastCastMs = 0; casts.length = 0;
  await pm.combatTick();
  check(casts[0] && casts[0][0] === FP.SPELL.GREATER_HEAL && casts[0][1] === Player.serial, 'hurt mage heals itself by spell first');
  Player.hp = 50; casts.length = 0;
  const wl = fighters.warlock; wl.threat = Mobiles.get(88); wl.lastCastMs = 0;
  await wl.combatTick(); await wl.combatTick();
  check(casts.filter((c) => c[0] === FP.SPELL.POISON).length === 1, 'warlock opens with the Poison spell once per foe');
  const said2 = []; Player.say = (t) => said2.push(t);
  let targeted = null; Player.target = (x) => { targeted = x; };
  Player.once = (ev) => ev === 'target' ? Promise.resolve({}) : Promise.reject(new Error('t'));
  const tm = fighters.tamer; tm.pet = 99; mobs.push({ serial: 99, exists: true, notoriety: 2, body: 0xe1, x: 1380, y: 1481 });
  tm.threat = Mobiles.get(88);
  await tm.combatTick();
  check(said2.includes('all kill') && targeted === 88, 'tamer sends its pet: "all kill" + target the foe');
  check(!tm.meleeInFight() && tm.followRange() === 4, 'tamer with a pet stays back');

  // ground choice and PvP gating
  const sw = fighters.swordsman;
  Player.skill = () => 150;
  check(sw.chooseGround().level === 1, 'a novice hunts a level-1 ground');
  Player.skill = () => 900;
  check(sw.chooseGround().level >= 3, 'a GM hunts a harder ground near home');
  const pk = fighters.pk; pk.ground = { id: 'x', prey: 'wildlife', centre: { x: 1000, y: 1000 }, radius: 10 };
  Player.x = 1000; Player.y = 1000;
  mobs = [{ serial: 55, exists: true, notoriety: 1, body: 0x190, x: 1002, y: 1000 }];
  check(pk.findPrey() === null, 'pk without ALLOW_PVP never targets a player');
  const pk2 = mk('pk', { allowPvp: true }); pk2.ground = pk.ground;
  check(pk2.findPrey() && pk2.findPrey().serial === 55, 'pk with ALLOW_PVP targets a blue player in the wild');
  mobs[0].x = 1426; mobs[0].y = 1690; Player.x = 1425; Player.y = 1690;
  check(pk2.findPrey() === null, 'pk never targets a player near a town bank');
  check(sw.findPrey() === null && !sw.shouldDefend(55), 'a normal fighter never starts on a player');
  check(!pk2.behaviors().find((x) => x.name === 'bank').when(), 'a pk does not walk into guarded towns to bank');

  // every crafter and gatherer constructs
  const crafters = {};
  for (const [id, a] of Object.entries(AR.A)) if (a.kind === 'crafter') {
    let ok = true; try { crafters[id] = new CrafterBot(id); crafters[id].token = h.token; } catch (e) { ok = false; console.log(e.message); }
    check(ok, `crafter ${id} constructs`);
  }
  for (const id of ['miner', 'fisher']) { let ok = true; try { new GathererBot(id); } catch (e) { ok = false; console.log(e.message); } check(ok, `gatherer ${id} constructs`); }
  let refused = false; try { new GathererBot('lumberjack'); } catch (e) { refused = true; }
  check(refused, 'lumberjack keeps its own proven script');
  check(crafters.blacksmith.MATERIAL && crafters.blacksmith.MATERIAL.title === 'blacksmith', 'blacksmith buys ingots');
  check(crafters.tailor.MATERIAL && crafters.tailor.MATERIAL.name[0] === 'bolt', 'tailor buys cloth BOLTS (cloth itself is not NPC-sold)');
  check(!crafters.carpenter.MATERIAL && crafters.carpenter.craftName === 'carpenter' && AR.CRAFTS.carpenter.material.gather === 'lumber', 'carpenter gathers its own logs');
  check(!crafters.scribe.MATERIAL, 'scribe cannot buy blank scrolls (policy UNKNOWN -> refused)');
  check(crafters.alchemist.MATERIAL, 'alchemist can buy reagents');
  check(crafters.blacksmith.WORK_SPOT === AR.TOWNS.minoc.vendors.blacksmith, 'blacksmith works at the smithy (forge + anvil)');

  // craftTarget: material vs station
  Player.equipment.backpack.items = [{ serial: 301, name: 'iron ingot', amount: 20 }];
  let tgt = null; Player.target = (x) => { tgt = x; };
  crafters.blacksmith.findInPack = FighterBot.prototype.findInPack;
  await crafters.blacksmith.craftTarget();
  check(tgt === 301, 'blacksmith answers the hammer cursor with its ingots');
  g.World = { items: () => [{ serial: 400, name: 'oven', x: 0, y: 0 }], statics: () => [] };
  tgt = null; await crafters.cook.craftTarget();
  check(tgt === 400, 'cook answers with a nearby heat source');

  // tailor turns bolts into cloth with scissors
  const t = crafters.tailor;
  Player.equipment.backpack.items = [{ serial: 510, name: 'scissors', amount: 1 }, { serial: 511, name: 'bolt of cloth', amount: 1 }];
  t.restockMaterials = async () => {};
  const used = []; Player.use = (sr) => { used.push(sr); };
  Player.target = (sr) => { if (sr === 511) Player.equipment.backpack.items = Player.equipment.backpack.items.filter((i) => i.serial !== 511).concat([{ serial: 512, name: 'cloth', amount: 50 }]); };
  await t.getMaterial();
  check(used[0] === 510 && t.backpackCount(['cloth']) === 50, 'tailor: scissors on the bolt -> cloth');

  // an UNVERIFIED flow that opens no menu backs off instead of looping
  const bw = crafters.bowyer; bw.atWork = async () => {}; bw.readCatalogue = async () => { bw.catalogue = []; bw.catalogueMs = Date.now(); return []; };
  await bw.craftStep();
  check(bw.flowFailures === 1 && bw.flowBlockedUntil > Date.now(), 'bowyer: no menu -> reported and backed off');

  // crafters advertise; customers remember who they heard
  const saidAd = []; Player.say = (x) => saidAd.push(x); Player.name = 'Ahmet';
  const bs = crafters.blacksmith; bs.catalogue = [{ category: 'Armor', label: 'Ringmail Tunic' }];
  await bs.advertise();
  const ad = saidAd.pop();
  check(OrderPolicy.ADVERT_RE.test(ad) && ad.includes('ringmail'), 'blacksmith advert names its trade and sets');
  const cust = mk('swordsman'); cust.crafters = new Map();
  let jh = null; Player.on = (n, f) => { if (n === 'journal') jh = f; }; cust.listenForCrafters();
  mobs = [{ serial: 123, exists: true, name: 'Ahmet', x: 2471, y: 564 }];
  jh({ text: ad, serial: 123, system: false });
  check(cust.knownCrafter('blacksmith') && cust.knownCrafter('blacksmith').name === 'Ahmet', 'a fighter who hears the advert knows the blacksmith');
  check(cust.gearOrder().craft === 'blacksmith' && fighters.archer.gearOrder().craft === 'bowyer' && fighters.pure_mage.gearOrder().craft === 'tailor',
        'gear orders go to the right craft per style');
  Player.equipment.backpack.items = packItems;

  // ===== training toward the target build (lib/training.js) =====
  const TP = TrainingPolicy;
  let pl = TP.plan({ 40: 1000, 27: 1000 }, { 40: 1000, 27: 600, 44: 300 });
  check(pl.locks[40] === 'locked' && pl.locks[27] === 'up' && pl.locks[44] === 'down',
        'at target -> locked; below -> up; not in build -> down');
  check(pl.focus[0] === 27 && !pl.done, 'focus is the biggest gap');
  pl = TP.plan({ 40: 1000 }, { 40: 1000, 27: 6100 });
  check(pl.overCap, 'over 700.0 is noticed (the runtime would allow 1000)');
  pl = TP.plan({ 40: 1000, 27: 1000 }, { 40: 1000, 27: 1000 });
  check(pl.done && pl.focus.length === 0 && pl.unallocated === 5000, 'a finished build: done, and the unallocated remainder is reported');

  const sw2 = mk('swordsman');
  const locksSent = [];
  Player.skill = (i) => ({ 40: 1000, 27: 400, 44: 250 })[i] ?? -1;
  Player.skillLock = (i) => ({ 40: 'up', 27: 'up', 44: 'up' })[i] ?? null;
  Player.setSkillLock = (i, st) => locksSent.push([i, st]);
  g.Life = undefined;
  const sp = sw2.applyTrainingPlan();
  check(locksSent.some(([i, st]) => i === 40 && st === 'locked'), 'swordsmanship at 100 gets locked');
  check(locksSent.some(([i, st]) => i === 44 && st === 'down'), 'lumberjacking (not in the build) is set down');
  check(!locksSent.some(([i]) => i === 27), 'tactics already "up": no redundant packet');
  check(sw2.skillDone(40) && !sw2.skillDone(27), 'skillDone reflects the plan');

  // the mage stops self-cast training once Magery is at target
  const pm2 = mk('pure_mage'); casts.length = 0;
  Player.skill = (i) => (i === 25 ? 1000 : 500); Player.skillLock = () => 'up'; Player.mana = 90; Player.manaMax = 100;
  pm2.applyTrainingPlan();
  let medi = 0; pm2.meditate = async () => { medi++; };
  await pm2.trainIdle();
  check(casts.length === 0 && medi === 1, 'Magery at target: no more self-casting; meditation trains instead');

  // ===== persistent life from JS (lib/life.js) =====
  let stored = '', objective = null;
  g.Life = { record: { name: 'Ayse', archetype: 'swordsman', sessions: 3, deaths: 1,
      targetBuild: [{ skill: 40, tenths: 1000 }, { skill: 27, tenths: 900 }], objective: { kind: 'train', target: '', attempts: 0 },
      lastLogout: { valid: true, safe: false } },
    get memory() { return stored; }, setMemory(j) { stored = j; return true; },
    setObjective(k, t) { objective = [k, t]; }, save() {} };
  const a1 = mk('swordsman');
  check(JSON.stringify(a1.trainingTargets()) === JSON.stringify({ 40: 1000, 27: 900 }), 'targets come from the saved life, not the table');
  a1.noteDanger(1370, 1480, 5, 'died');
  a1.noteEmptySpot(9, 9);
  a1.marketEntry('bone helmet').buyers.armorer = { price: 7, seenMs: 1 };
  a1.marketEntry('bone helmet').refusedBy.add('tanner');
  a1.ledger.record('sale', 250, 'x');
  a1.crafters.set('blacksmith', { name: 'Ahmet', serial: 5, x: 1, y: 2, heardMs: Date.now() });
  a1.gearOrdered = true; a1.pet = 777;
  check(a1.lifePersist() && stored.length > 0, 'memory handed to the C++ life');
  const a2 = mk('swordsman');   // next session: fresh bot, same saved life
  check(a2.dangerAt(1370, 1480) > 4.9, 'danger map survives the logout');
  check(a2.isEmptySpot(9, 9), 'exhausted spots survive');
  check(a2.market.get('bone helmet').buyers.armorer.price === 7 && a2.market.get('bone helmet').refusedBy.has('tanner'),
        'market knowledge (buyers and refusals) survives');
  check(a2.ledger.totals.sale === 250, 'ledger totals survive');
  check(a2.knownCrafter('blacksmith') && a2.knownCrafter('blacksmith').name === 'Ahmet', 'crafters heard survive');
  check(a2.gearOrdered && a2.pet === 777, 'pet and gear-ordered survive');
  a2.lifeNoteBehavior('resurrect');
  check(objective && objective[0] === 'recover_corpse', 'behaviour becomes the saved objective (resurrect -> recover_corpse)');
  objective = null; a2.lifeNoteBehavior('resurrect');
  check(objective === null, 'an unchanged objective is not rewritten');

  // a crafter's paid orders survive, unpaid quotes do not
  stored = '';
  const c1 = new CrafterBot('tinker'); c1.token = h.token;
  c1.orders.push({ id: 4, status: 'paid', customer: { serial: 9, name: 'M' }, items: [{ name: 'pickaxe', qty: 2, made: 1 }] });
  c1.orders.push({ id: 5, status: 'quoted', customer: { serial: 9, name: 'M' }, items: [{ name: 'scissors', qty: 1, made: 0 }] });
  c1.nextOrderId = 6; c1.lifePersist();
  const c2 = new CrafterBot('tinker');
  check(c2.orders.length === 1 && c2.orders[0].id === 4 && c2.orders[0].items[0].made === 1, 'a paid order resumes where it stopped');
  check(c2.nextOrderId === 6, 'order numbering continues');
  stored = '{not json';
  let survived = true; try { mk('swordsman'); } catch (e) { survived = false; }
  check(survived, 'unreadable saved memory starts fresh instead of crashing');
  g.Life = undefined;
  const noLife = mk('swordsman');
  check(noLife.lifePersist() === false && noLife.life === null, 'without --life-dir everything is a no-op');

  // no mixin method may shadow another or a bot's own
  const mix = { BankSkill, SurvivalSkill, CombatSkill, EconomySkill, MemorySkill, CraftSkill, CustomerSkill, GatherSkill, TrainingSkill, LifeSkill };
  const seen = {};
  for (const [m, obj] of Object.entries(mix)) for (const k of Object.keys(obj)) {
    if (typeof obj[k] !== 'function') continue;
    check(!seen[k], `mixin method ${k} defined once (${seen[k] || m})`); seen[k] = m;
  }
}
