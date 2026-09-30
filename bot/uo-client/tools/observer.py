"""Observer dashboard: one page showing what every bot on the shard is doing.

Each client writes <bot-data>/<identity>/status.json every 10 s while live and
once more at logout (Runner::PublishStatus). The population manager writes
population.json into its run directory. This tool only READS those files; it
never talks to Sphere and never touches a bot.

    python tools/observer.py                          # http://127.0.0.1:8765
    python tools/observer.py --run run_gates/live     # include population.json
    python tools/observer.py --snapshot shard.html    # one self-contained page
"""
import argparse
from collections import Counter
import http.server
import json
from pathlib import Path
import time

BOT = Path(__file__).resolve().parents[1]
# A client that says "online" but has not written for this long has crashed
# or lost its connection without a clean logout.
STALE_SECONDS = 60
MAP_W, MAP_H = 5120, 4096          # Felucca/Trammel land, the part bots walk
# Rough town centres, for orientation on the map only (never used by a bot).
TOWNS = {'Britain': (1475, 1645), 'Minoc': (2500, 520), 'Vesper': (2890, 690),
         'Yew': (560, 990), 'Trinsic': (1890, 2780), 'Moonglow': (4440, 1170),
         'Skara Brae': (620, 2200), 'Jhelom': (1400, 3820), 'Cove': (2250, 1210),
         "Buccaneer's Den": (2700, 2150), 'Magincia': (3700, 2200),
         "Nujel'm": (3730, 1270), "Serpent's Hold": (2990, 3420), 'Ocllo': (3660, 2600)}


def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def collect(bot_data, run_dir=None, now=None):
    """Everything the page shows, as one JSON-able dict. Pure over the files."""
    now = time.time() if now is None else now
    bots = []
    for path in sorted(Path(bot_data).glob('*/status.json')):
        status = read_json(path)
        if not status:
            continue
        age = now - status.get('updated_ms', 0) / 1000.0
        status['age_s'] = round(age)
        status['stale'] = bool(status.get('online')) and age > STALE_SECONDS
        status['live'] = bool(status.get('online')) and not status['stale']
        status['gold_delta'] = status.get('gold', 0) - status.get('gold_at_login', 0)
        bots.append(status)
    live = [b for b in bots if b['live']]
    summary = {
        'known': len(bots),
        'online': len(live),
        'stale': sum(b['stale'] for b in bots),
        'dead_now': sum(bool(b.get('dead')) for b in live),
        'in_party': sum(b.get('party_size', 0) > 0 for b in live),
        'kills': sum(b.get('kills', 0) for b in live),
        'deaths': sum(b.get('deaths', 0) for b in live),
        'gold_on_line': sum(b.get('gold', 0) for b in live),
        'gold_earned': sum(b['gold_delta'] for b in live),
        'by_family': dict(Counter(b.get('family', '?') for b in live).most_common()),
        'by_goal': dict(Counter(b.get('goal', '?') for b in live).most_common()),
        'by_goal_family': dict(Counter(b.get('goal_family', '?') for b in live).most_common()),
        'by_city': dict(Counter(b.get('home_city', '?') for b in live).most_common()),
        'era': sorted({b.get('era') for b in live if b.get('era')}),
    }
    population = read_json(Path(run_dir) / 'population.json') if run_dir else None
    return {'generated': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(now)),
            'summary': summary, 'population': population, 'bots': bots,
            'map': {'w': MAP_W, 'h': MAP_H, 'towns': TOWNS}}


PAGE = r'''<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Shard Observer</title>
<style>
:root{--bg:#f6f4ef;--panel:#fff;--ink:#1d1c1a;--muted:#6b675f;--line:#e2ded5;
--fight:#b33a3a;--craft:#2f6f9f;--gather:#3f8a4a;--other:#8a6d2f;--dead:#888}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#15140f;--panel:#1f1d18;
--ink:#ece8df;--muted:#a39d90;--line:#35322b}}
:root[data-theme="dark"]{--bg:#15140f;--panel:#1f1d18;--ink:#ece8df;--muted:#a39d90;--line:#35322b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);
font:14px/1.4 system-ui,-apple-system,Segoe UI,sans-serif}
main{max-width:1400px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}.muted{color:var(--muted)}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:14px 0}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:10px}
.card b{display:block;font-size:22px}
.grid{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:14px}
@media (max-width:900px){.grid{grid-template-columns:1fr}}
section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;min-width:0}
h2{font-size:15px;margin:0 0 8px}canvas{width:100%;height:auto;background:var(--bg);border-radius:6px}
.bars div{display:flex;gap:8px;align-items:center;margin:2px 0}.bars span{width:150px;
overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.bars i{display:block;height:10px;
background:var(--craft);border-radius:3px}
.tablewrap{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:13px}
th,td{padding:5px 6px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{cursor:pointer;color:var(--muted);font-weight:600}tr.off td{color:var(--muted)}
tr.stale td{color:var(--fight)}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:4px}
input{padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--panel);color:var(--ink);width:100%;max-width:280px}
details{margin-top:4px}summary{cursor:pointer}
</style></head><body><main>
<h1>Shard Observer</h1>
<div class="muted" id="when"></div>
<div class="cards" id="cards"></div>
<div class="grid">
<section><h2>Where they are</h2><canvas id="map" width="1024" height="820"></canvas>
<div class="muted"><span class="dot" style="background:var(--fight)"></span>fighter
<span class="dot" style="background:var(--craft)"></span>crafter
<span class="dot" style="background:var(--gather)"></span>gatherer
<span class="dot" style="background:var(--dead)"></span>dead</div></section>
<section><h2>What they are doing</h2><div class="bars" id="goals"></div>
<h2 style="margin-top:12px">Who is on line</h2><div class="bars" id="families"></div></section>
</div>
<section style="margin-top:14px"><h2>Characters</h2>
<input id="filter" placeholder="Filter by name, family, goal, city">
<div class="tablewrap"><table><thead><tr id="head"></tr></thead><tbody id="rows"></tbody></table></div></section>
</main>
<script>
const FIGHT=/fencer|macer|archer|warlock|mage|swords|pk|tamer|treasure|knight/;
const GATHER=/miner|lumber|fisher/;
let DATA=window.__DATA__||null, sortKey='live', sortDir=-1;
const COLS=[['character','Name'],['family','Family'],['live','State'],['goal','Goal'],['home_city','Home'],
 ['hp','HP'],['gold','Gold'],['gold_delta','Earned'],['skill_total','Skills'],['kills','Kills'],
 ['deaths','Deaths'],['party_size','Party'],['last_name','Family'],['rhythm','Plays'],['age_s','Updated']];
function color(b){const css=getComputedStyle(document.documentElement);
 if(b.dead)return css.getPropertyValue('--dead');const f=b.family||'';
 return css.getPropertyValue(FIGHT.test(f)?'--fight':GATHER.test(f)?'--gather':'--craft');}
function esc(s){return String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function bars(el,obj){const max=Math.max(1,...Object.values(obj));
 el.innerHTML=Object.entries(obj).slice(0,14).map(([k,v])=>
 `<div><span title="${esc(k)}">${esc(k)}</span><i style="width:${Math.round(200*v/max)}px"></i>${v}</div>`).join('')||'<span class="muted">nobody on line</span>';}
function render(){if(!DATA)return;const s=DATA.summary,p=DATA.population;
 document.getElementById('when').textContent='Updated '+DATA.generated+(s.era&&s.era.length?' · era '+s.era.join(', '):'')+(p?` · manager: ${p.online.length}/${p.max_online} on line, ${p.wanting_now} want to play`:'');
 const cards=[['On line',s.online],['Known',s.known],['Crashed/stale',s.stale],['In a party',s.in_party],
  ['Kills (session)',s.kills],['Deaths (session)',s.deaths],['Gold carried',s.gold_on_line.toLocaleString()],['Gold earned',s.gold_earned.toLocaleString()]];
 document.getElementById('cards').innerHTML=cards.map(([k,v])=>`<div class="card"><span class="muted">${k}</span><b>${v}</b></div>`).join('');
 bars(document.getElementById('goals'),s.by_goal);bars(document.getElementById('families'),s.by_family);
 const c=document.getElementById('map'),g=c.getContext('2d');g.clearRect(0,0,c.width,c.height);
 g.strokeStyle=getComputedStyle(document.documentElement).getPropertyValue('--line');g.strokeRect(0,0,c.width,c.height);
 g.font='11px system-ui';g.fillStyle=getComputedStyle(document.documentElement).getPropertyValue('--muted');
 for(const [n,[tx,ty]] of Object.entries(DATA.map.towns||{})){const x=tx/DATA.map.w*c.width,y=ty/DATA.map.h*c.height;
  g.fillRect(x-1.5,y-1.5,3,3);g.fillText(n,x+4,y+4);}
 for(const b of DATA.bots){if(!b.live)continue;const x=b.x/DATA.map.w*c.width,y=b.y/DATA.map.h*c.height;
  g.fillStyle=color(b);g.beginPath();g.arc(x,y,5,0,7);g.fill();}
 document.getElementById('head').innerHTML=COLS.map(([k,n])=>`<th data-k="${k}">${n}${k===sortKey?(sortDir>0?' ▲':' ▼'):''}</th>`).join('');
 const q=document.getElementById('filter').value.toLowerCase();
 const rows=DATA.bots.filter(b=>!q||[b.character,b.family,b.goal,b.home_city].join(' ').toLowerCase().includes(q))
  .sort((a,b)=>{const x=a[sortKey],y=b[sortKey];return (x>y?1:x<y?-1:0)*sortDir;});
 document.getElementById('rows').innerHTML=rows.map(b=>{const st=b.stale?'stale':b.live?(b.dead?'dead':'on line'):'off line';
  const recent=(b.recent||[]).slice().reverse().map(r=>`${new Date(r.at_ms).toLocaleTimeString()} ${esc(r.goal)}: ${esc(r.why)}`).join('<br>');
  return `<tr class="${b.stale?'stale':b.live?'':'off'}"><td><span class="dot" style="background:${color(b)}"></span>${esc(b.character)}
  ${recent?`<details><summary class="muted">recent</summary><div style="white-space:normal;max-width:520px">${recent}</div></details>`:''}</td>
  <td>${esc(b.family)}</td><td>${st}</td><td>${esc(b.goal)}</td><td>${esc(b.home_city)}</td>
  <td>${b.hp}/${b.hp_max}</td><td>${b.gold}</td><td>${b.gold_delta}</td><td>${b.skill_total}</td>
  <td>${b.kills}</td><td>${b.deaths}</td><td>${b.party_size||''}</td><td>${esc(b.last_name||'')}${b.family_head?' (head)':''}</td><td title="${esc(b.schedule)}">${esc(b.rhythm)}</td><td>${b.age_s}s ago</td></tr>`;}).join('');}
document.addEventListener('click',e=>{const k=e.target.dataset&&e.target.dataset.k;if(!k)return;
 if(k===sortKey)sortDir=-sortDir;else{sortKey=k;sortDir=-1;}render();});
document.getElementById('filter').addEventListener('input',render);
async function poll(){try{const r=await fetch('api/status');if(r.ok){DATA=await r.json();render();}}catch(e){}}
if(DATA){render();}else{poll();setInterval(poll,5000);}
</script></body></html>
'''


def snapshot(data):
    """A self-contained page with the data embedded (no server needed)."""
    blob = json.dumps(data).replace('</', '<\\/')
    return PAGE.replace('<script>\n', '<script>\nwindow.__DATA__=' + blob + ';\n', 1)


def serve(bot_data, run_dir, host, port):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.split('?')[0].endswith('/api/status'):
                body = json.dumps(collect(bot_data, run_dir)).encode('utf-8')
                kind = 'application/json'
            elif self.path.split('?')[0] in ('/', '/index.html'):
                body, kind = PAGE.encode('utf-8'), 'text/html; charset=utf-8'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', kind)
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer((host, port), Handler)
    print(f'observer: http://{host}:{port}/  (bot data: {bot_data})', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--bot-data', type=Path, default=BOT / 'bot_data')
    parser.add_argument('--run', type=Path, help='population manager run directory')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--snapshot', type=Path, help='write one self-contained page and exit')
    args = parser.parse_args()
    if args.snapshot:
        args.snapshot.write_text(snapshot(collect(args.bot_data, args.run)), encoding='utf-8')
        print('wrote', args.snapshot)
    else:
        serve(args.bot_data, args.run, args.host, args.port)
