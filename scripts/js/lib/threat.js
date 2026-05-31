'use strict';
// Shared danger assessment, usable by any bot. createThreatMeter(opts) only
// COUNTS the danger (proximity, closing speed, facing, war mode, recent action
// anim, our HP loss, and explicit "attacked"/"is attacking you" signals) into a
// single score + level. The REACTION stays in the script via callbacks
// (onLevel / onDanger). meter.start() runs a self-sampling timer; read
// meter.score / meter.level / meter.top, or use the callbacks.
//
// Auto-loaded: the engine evaluates every scripts/js/lib/*.js after bootstrap.js
// and before the bot script, so createThreatMeter is a global at script time.
(function (g) {
    const DIR_DX = [0, 1, 1, 1, 0, -1, -1, -1];   // = bot::DirToDelta
    const DIR_DY = [-1, -1, 0, 1, 1, 1, 0, -1];

    const defaultNotoDanger = (n) => {     // weight by hue; 0 = friendly / ignore
        switch (n) {
            case 6: return 2.0;   // red / murderer
            case 5: return 1.5;   // orange / enemy
            case 4: return 1.2;   // criminal (gray)
            case 3: return 1.0;   // attackable (gray wild animals)
            default: return 0;    // 1 blue, 2 green, 7 invulnerable
        }
    };

    const dirToward = (fx, fy, tx, ty) => {   // 0..7 facing from (fx,fy) to (tx,ty); -1 same cell
        const dx = Math.sign(tx - fx), dy = Math.sign(ty - fy);
        for (let d = 0; d < 8; d++) if (DIR_DX[d] === dx && DIR_DY[d] === dy) return d;
        return -1;
    };

    // Body ids that aggress on sight, derived from the ouo server template DB
    // (bank/templatestable.dat: every creature with <alignment EVIL> / <notoriety
    // -125>). Bodies resolved via <type NORMAL ...> + bank/defines. This is shared
    // server data, so it lives here (not in a single bot script). Passive grays
    // (horse, cat, deer, bear, cougar — NEUTRAL on this server) are deliberately
    // absent: they are engaged only after they actually hit us. Body 400 (human)
    // is excluded — harmless NPCs share it, so it is ambiguous from the client.
    const DEFAULT_AGGRESSIVE_BODIES = [
        0x01, // ogre
        0x03, // zombie
        0x04, // gargoyle
        0x07, // orc captain
        0x09, // daemon
        0x0a, // balron
        0x0c, 0x3b, // dragon
        0x0d, // air elemental
        0x0e, // earth elemental
        0x0f, // fire elemental
        0x10, // water elemental
        0x11, // orc
        0x12, // ettin
        0x16, // gazer
        0x18, // lich
        0x1a, // ghoul / ghost
        0x1c, // giant spider
        0x1e, // harpy
        0x1f, // headless one
        0x21, 0x23, 0x24, // lizardman
        0x27, // mongbat
        0x2a, 0x2c, 0x2d, // ratman
        0x30, // giant scorpion
        0x32, 0x38, // skeleton / bone mage
        0x33, // slime
        0x34, // snake
        0x35, 0x36, 0x37, // troll
        0x39, // bone knight
        0x3c, 0x3d, // drake
        0xca, // alligator
        0xd7, // giant rat
        0xe1, // timber / dire wolf, hell hound
        0xee, // sewer rat
    ];

    g.createThreatMeter = function (opts) {
        const cfg = Object.assign({
            radius: 12, sampleMs: 350, wary: 2.5, danger: 6.0,
            wProx: 1.0, wClosing: 1.2, wFacing: 1.0, wWar: 1.5, wAnim: 1.2, adj: 3.0,
            wConfirmed: 4.0,   // flat bonus for a mob that has actually attacked us
            wHpDrop: 12.0, wLowHp: 4.0, dmgMemoryMs: 3000,
            directScore: 12.0, directMemoryMs: 4000, animFreshMs: 1200,
            // Once a mob attacks us it stays a "confirmed" target for this long even
            // if it briefly backs off (so we keep fighting the right foe).
            hostileMemoryMs: 8000,
            // Proactively query each dangerous mob's HP (0x34) so mob.hp/.hpPct is
            // known BEFORE we engage — lets a script decide fight-or-flee up front.
            // The server then auto-pushes 0xA1 updates, so we ask once per mob and
            // only retry (statusRetryMs) while its HP is still unknown.
            trackHp: true,
            statusRetryMs: 4000,
            // Body ids that are aggressive on sight: for THESE the proactive terms
            // (proximity/closing/facing) drive the threat. Every OTHER mob (wandering
            // horse, cat, deer — gray but harmless) scores 0 until it actually attacks
            // us, so the bot never swings first at a passive animal. Defaults to the
            // server's EVIL creature list (DEFAULT_AGGRESSIVE_BODIES above); a script
            // may pass its own to extend/replace it. Unknown hits log their body id.
            aggressiveBodies: DEFAULT_AGGRESSIVE_BODIES,
            aggressive: null,   // optional (mob)=>bool; default = aggressiveBodies set
            // a player attacking us posts this to the journal (PvP only; mobs don't)
            attackedPhrase: /is attacking you/i,
            notoDanger: defaultNotoDanger,
            onLevel: null,    // (level, prevLevel, score, topSerial)
            onDanger: null,   // (topSerial, score) every sample while level === 'danger'
        }, opts || {});

        const aggroSet = new Set(cfg.aggressiveBodies || []);
        const isAggressive = cfg.aggressive || ((mob) => aggroSet.has(mob.body));

        const meter = {
            cfg, score: 0, level: 'calm', top: 0, count: 0,
            prevDist: new Map(), hostileUntil: new Map(), statusReqAt: new Map(), hpLast: -1,
            dmgUntil: 0, dmgFrac: 0, directUntil: 0,
            timer: 0, onAttackedHandler: null, onJournalHandler: null,
        };

        // Mark a mob as a confirmed attacker (so its proactive score counts and it
        // is eligible as the top foe). Also pins the overall meter to max briefly.
        meter.markHostile = function (serial) {
            if (serial) meter.hostileUntil.set(serial, Date.now() + cfg.hostileMemoryMs);
        };
        // An explicit, unambiguous attack on us: pin the level to max and, when we
        // know who, mark that mob as the confirmed foe.
        meter.markDirectAttack = function (serial) {
            meter.directUntil = Date.now() + cfg.directMemoryMs;
            meter.markHostile(serial);
        };

        meter.sample = function () {
            const now = Date.now();
            const me = { x: Player.x, y: Player.y };
            const seen = new Set();      // in-range serials (for prevDist pruning)
            const nearby = [];           // {serial, body, dist, animFresh} for HP-drop blame
            let score = 0, topScore = 0, topSerial = 0, count = 0;

            for (const mob of Mobiles.all()) {
                if (!mob.exists) continue;
                const dist = Math.max(Math.abs(mob.x - me.x), Math.abs(mob.y - me.y));
                if (dist > cfg.radius) continue;
                seen.add(mob.serial);
                const animFresh = (mob.animMsAgo >= 0 && mob.animMsAgo < cfg.animFreshMs);
                nearby.push({ serial: mob.serial, body: mob.body, dist, animFresh });

                // Closing speed needs last sample's distance; track it for EVERY
                // in-range mob (even passive ones) so the term is ready the moment
                // one turns hostile.
                let closing = 0;   // tiles/sec toward us; running amplifies
                const prev = meter.prevDist.get(mob.serial);
                if (prev) { const dt = (now - prev.t) / 1000; if (dt >= 0.05) closing = (prev.dist - dist) / dt; }
                meter.prevDist.set(mob.serial, { dist, t: now });

                const nw = cfg.notoDanger(mob.notoriety);
                if (nw === 0) continue;          // friendly hue -> never a threat

                // Gate the proactive danger: only inherently-aggressive species OR a
                // mob that has actually attacked us contributes. A wandering gray
                // animal scores 0 here, so the bot never swings first at it.
                const confirmed = now < (meter.hostileUntil.get(mob.serial) || 0);
                if (!confirmed && !isAggressive(mob)) continue;
                count++;

                // Learn this dangerous mob's HP up front: ask once (0x34); the
                // server then pushes 0xA1 updates. Retry only while still unknown.
                if (cfg.trackHp && mob.hp < 0 && typeof Player.requestStatus === 'function') {
                    if (now - (meter.statusReqAt.get(mob.serial) || 0) >= cfg.statusRetryMs) {
                        Player.requestStatus(mob.serial);
                        meter.statusReqAt.set(mob.serial, now);
                    }
                }

                const prox = Math.max(0, (cfg.radius - dist) / cfg.radius);
                const closeTerm = Math.max(0, closing) * (mob.running ? 1.5 : 1.0);

                let facing = 1;    // is it oriented at us?
                const want = dirToward(mob.x, mob.y, me.x, me.y);
                if (want >= 0) { let a = Math.abs(mob.dir - want) % 8; a = Math.min(a, 8 - a); facing = a <= 1 ? 1 : (a === 2 ? 0.5 : 0); }

                let s = cfg.wProx * prox + cfg.wClosing * closeTerm + cfg.wFacing * facing
                    + (mob.warMode ? cfg.wWar : 0) + cfg.wAnim * (animFresh ? 1 : 0);
                if (dist <= 1) s += cfg.adj;
                if (confirmed) s += cfg.wConfirmed;
                s *= nw;

                score += s;
                if (s > topScore) { topScore = s; topSerial = mob.serial; }
            }

            for (const serial of meter.prevDist.keys()) if (!seen.has(serial)) meter.prevDist.delete(serial);
            for (const serial of meter.statusReqAt.keys()) if (!seen.has(serial)) meter.statusReqAt.delete(serial);
            for (const serial of meter.hostileUntil.keys()) if (meter.hostileUntil.get(serial) <= now) meter.hostileUntil.delete(serial);

            // HP: a landed hit is unambiguous "we are under attack" (no rabbit false
            // positive). Blame the most likely attacker so we confirm + engage it,
            // and log its body id — that is the list to add to aggressiveBodies.
            const hp = Player.hp, hpMax = Player.hpMax || 1;
            if (meter.hpLast >= 0 && hp < meter.hpLast) {
                meter.dmgFrac = (meter.hpLast - hp) / hpMax;
                meter.dmgUntil = now + cfg.dmgMemoryMs;
                meter.directUntil = now + cfg.directMemoryMs;
                let blame = nearby.filter((n) => n.dist <= 1);          // adjacent = melee
                if (!blame.length) blame = nearby.filter((n) => n.animFresh);  // just swung
                if (!blame.length && nearby.length)                     // else the nearest
                    blame = [nearby.reduce((b, n) => (!b || n.dist < b.dist ? n : b), null)];
                for (const n of blame) {
                    if (!meter.hostileUntil.has(n.serial)) {
                        // Only nag to extend the list for bodies we did NOT already
                        // class as aggressive; a known body is just confirmed.
                        const hint = aggroSet.has(n.body) ? '' : ' (add to aggressiveBodies if it attacks on sight)';
                        console.warn(`[threat] hit -> confirmed foe 0x${n.serial.toString(16)} body=0x${n.body.toString(16)}${hint}`);
                    }
                    meter.markHostile(n.serial);
                }
            }
            meter.hpLast = hp;
            if (now < meter.dmgUntil) score += cfg.wHpDrop * meter.dmgFrac;
            const hpFr = hp / hpMax;
            if (hpFr < 0.5) score += cfg.wLowHp * (0.5 - hpFr);

            if (now < meter.directUntil) score = Math.max(score, cfg.directScore);

            const level = score >= cfg.danger ? 'danger' : (score >= cfg.wary ? 'wary' : 'calm');
            const prevLevel = meter.level;
            meter.score = score; meter.level = level; meter.top = topSerial; meter.count = count;
            if (level !== prevLevel && cfg.onLevel) cfg.onLevel(level, prevLevel, score, topSerial);
            if (level === 'danger' && cfg.onDanger) cfg.onDanger(topSerial, score);
            return score;
        };

        meter.start = function () {
            if (meter.timer) return meter;
            meter.hpLast = Player.hp;
            // explicit "attacked" signals -> confirm the attacker + pin to max:
            meter.onAttackedHandler = (serial) => meter.markDirectAttack(serial);  // 0x2F swing at us
            Player.on('attacked', meter.onAttackedHandler);
            if (cfg.attackedPhrase) {                                // PvP "X is attacking you!"
                meter.onJournalHandler = (j) => { if (cfg.attackedPhrase.test(j.text)) meter.markDirectAttack(j.serial || 0); };
                Player.on('journal', meter.onJournalHandler);
            }
            const tick = () => {
                try { meter.sample(); } catch (error) { console.warn('[threat] sample error:', error && error.message); }
                meter.timer = __setTimeout(tick, cfg.sampleMs);
            };
            meter.timer = __setTimeout(tick, cfg.sampleMs);
            return meter;
        };

        meter.stop = function () {
            if (meter.timer) { __clearTimeout(meter.timer); meter.timer = 0; }
            if (meter.onAttackedHandler) { Player.off('attacked', meter.onAttackedHandler); meter.onAttackedHandler = null; }
            if (meter.onJournalHandler) { Player.off('journal', meter.onJournalHandler); meter.onJournalHandler = null; }
        };

        return meter;
    };
})(globalThis);
