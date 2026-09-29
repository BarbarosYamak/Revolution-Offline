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
let fails=0, checks=0; const check=(c,m)=>{checks++; if(!c){fails++;console.log('FAIL',m);}};
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
for (const f of ['bootstrap.js','lib/bot.js','lib/bank.js','lib/survival.js','lib/combat.js','lib/economy.js','lib/memory.js'])
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
  vm.runInThisContext(fs.readFileSync(path+'graveyard.js','utf8').replace('new GraveyardHunter().start();','globalThis.GH=GraveyardHunter;'),{filename:'graveyard.js'});
  const h=new GH();
  let inside=true; for(let i=0;i<500;i++){const p=h.randomWaypoint(); if(!h.inGraveyard(p.x,p.y)) inside=false;}
  check(inside,'every patrol waypoint is inside the graveyard rects');
  check(!h.inGraveyard(h.REST_SPOT.x,h.REST_SPOT.y),'the rest spot is outside the graveyard');
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
  check(names==='resurrect>offline>fight>loot>bank>eat>recover>patrol','priority order '+names);
  return economyTests();
}).then(() => {
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

  // no mixin method may shadow another or a bot's own
  const mix = { BankSkill, SurvivalSkill, CombatSkill, EconomySkill, MemorySkill };
  const seen = {};
  for (const [m, obj] of Object.entries(mix)) for (const k of Object.keys(obj)) {
    if (typeof obj[k] !== 'function') continue;
    check(!seen[k], `mixin method ${k} defined once (${seen[k] || m})`); seen[k] = m;
  }
}
