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
for (const f of ['bootstrap.js','lib/bot.js','lib/bank.js','lib/survival.js','lib/combat.js'])
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
  check(names==='resurrect>fight>bank>eat>recover>patrol','priority order '+names);
  console.log(`${checks} checks, ${fails} failures`); process.exit(fails?1:0);
});
