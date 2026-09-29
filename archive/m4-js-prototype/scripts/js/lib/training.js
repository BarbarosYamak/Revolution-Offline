'use strict';
// TrainingSkill — progress toward the character's TARGET build the way a
// Revolution player did: work that uses the skill, plus the skill-gump arrows.
//     up      a build skill below its target: let it rise
//     locked  a build skill at (or past) its target: stop it here
//     down    a skill NOT in the build: let it fall as build skills rise
// On a capped shard the server moves points from "down" skills to rising
// ones once the total reaches the cap -- which is how a 700 build is shaped
// without a GM. Our runtime's own cap is 1000.0 (more permissive than
// Revolution), so the bot also watches the 700.0 sum itself: over it, every
// non-build skill is set down and every build skill at target is locked.
//
// Targets come from the persistent life (Life.record.targetBuild) when there
// is one, else from the archetype table. Skills are READ from the server
// (Player.skill); nothing here writes a value.
(function (g) {
    const CAP = 7000;          // 700.0, Revolution (rules.h totalSkillCapTenths)
    const MAX_SKILL = 58;

    // targets: {index: tenths}; current: {index: tenths}
    function plan(targets, current, cap = CAP) {
        const locks = {};
        const gaps = [];
        let sum = 0;
        for (const v of Object.values(current)) if (v > 0) sum += v;
        for (const [k, t] of Object.entries(targets)) {
            const i = Number(k), cur = current[i] ?? 0;
            if (cur >= t) locks[i] = 'locked';
            else { locks[i] = 'up'; gaps.push({ skill: i, gap: t - cur }); }
        }
        for (const [k, v] of Object.entries(current)) {
            const i = Number(k);
            if (targets[i] === undefined && v > 0) locks[i] = 'down';
        }
        gaps.sort((a, b) => b.gap - a.gap);
        const targetSum = Object.values(targets).reduce((a, b) => a + b, 0);
        return {
            locks, focus: gaps.map((x) => x.skill), sum, cap,
            overCap: sum > cap, nearCap: sum >= cap - 50,
            done: gaps.length === 0, unallocated: cap - targetSum,
        };
    }

    g.TrainingPolicy = { plan, CAP };

    const TrainingSkill = {
        TRAINING_REFRESH_MS: 5 * 60 * 1000,

        trainingTargets() {
            const rec = typeof Life !== 'undefined' ? Life.record : null;
            const t = {};
            if (rec && rec.targetBuild && rec.targetBuild.length) {
                for (const b of rec.targetBuild) t[b.skill] = b.tenths;
                return t;
            }
            const a = this.arch;
            if (a && a.build) for (const [n, v] of Object.entries(a.build.skills)) t[g.Archetypes.SK[n]] = v * 10;
            return t;
        },

        currentSkills() {
            const cur = {};
            if (typeof Player.skill !== 'function') return cur;
            for (let i = 0; i < MAX_SKILL; i++) {
                const v = Player.skill(i);
                if (v >= 0) cur[i] = v;
            }
            return cur;
        },

        // Compute the plan and set any lock that differs from the server's.
        applyTrainingPlan() {
            const p = plan(this.trainingTargets(), this.currentSkills());
            this.trainingPlan = p;
            this.trainingPlanMs = Date.now();
            if (typeof Player.setSkillLock !== 'function') return p;
            let changed = 0;
            for (const [k, state] of Object.entries(p.locks)) {
                const i = Number(k);
                const now = typeof Player.skillLock === 'function' ? Player.skillLock(i) : null;
                if (now === null || now === state) continue;   // unknown skill, or already right
                Player.setSkillLock(i, state);
                changed++;
            }
            console.log(`[train] ${(p.sum / 10).toFixed(1)}/${(p.cap / 10).toFixed(0)}` +
                `${p.overCap ? ' OVER CAP' : ''} -- focus ${p.focus.slice(0, 3).join(',') || 'none (build complete)'}; ${changed} lock(s) changed`);
            return p;
        },

        trainingDue() { return !this.trainingPlanMs || Date.now() - this.trainingPlanMs > this.TRAINING_REFRESH_MS; },

        // Is this skill finished (locked at target)? Unknown counts as not.
        skillDone(index) {
            const p = this.trainingPlan;
            return Boolean(p && p.locks[index] === 'locked');
        },
    };

    g.TrainingSkill = TrainingSkill;
})(globalThis);
