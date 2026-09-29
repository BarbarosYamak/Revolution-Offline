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
for (const f of ['bootstrap.js','lib/bot.js','lib/bank.js','lib/survival.js','lib/combat.js','lib/economy.js'])
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
  check(names==='resurrect>fight>loot>bank>eat>recover>patrol','priority order '+names);
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

  // no mixin method may shadow another or a bot's own
  const mix = { BankSkill, SurvivalSkill, CombatSkill, EconomySkill };
  const seen = {};
  for (const [m, obj] of Object.entries(mix)) for (const k of Object.keys(obj)) {
    if (typeof obj[k] !== 'function') continue;
    check(!seen[k], `mixin method ${k} defined once (${seen[k] || m})`); seen[k] = m;
  }
}
