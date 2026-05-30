'use strict';
// BehaviorScript — base class for priority-runner bots (lib/bt.js). It owns the
// behaviour lifecycle plus the two skills EVERY bot needs: movement (walkTo) and
// backpack inventory. Domain skills are opt-in mixins applied to the subclass
// prototype: BankSkill (lib/bank.js) and SurvivalSkill (lib/survival.js). Compose
// them in the bot, e.g.
//
//     class Lumberjack extends BehaviorScript { ... }
//     Object.assign(Lumberjack.prototype, BankSkill, SurvivalSkill);
//
// Every skill method reads `this.token` (the running step's cancellation token),
// so wrap long awaits in token.wait()/token.sleep() and let CANCELLED propagate.
//
// Auto-loaded: the engine evaluates scripts/js/lib/*.js after bootstrap.js and
// before the bot script, so BehaviorScript is a global at script time.
(function (g) {
    class BehaviorScript {
        // --- tunable defaults (override as class fields in the subclass) ---
        BANK_AT_WEIGHT = 400;       // weight to bank at when maxWeight is unknown
        WALLET = 200;               // gold to keep in the backpack
        EAT_INTERVAL_MS = 60 * 1000;// re-hunger interval for the eat behaviour
        FOOD = [];                  // food name substrings eatFood() looks for
        BANDAGE = 'bandage';        // bandage name substring

        // --- shared blackboard (sensing writes it, behaviours read it) ---
        token = null;
        runner = null;

        // ===== behaviour lifecycle =====

        // Wrap a single method as a behaviour step bound to the running token.
        step(methodName) {
            return (token) => {
                this.token = token;
                const method = this[methodName];
                if (typeof method !== 'function')
                    throw new Error(`Unknown behavior method: ${methodName}`);
                return method.call(this);
            };
        }

        // Compose a step from several methods run in order — e.g.
        // sequence('goToBank', 'deposit'). Sequencing is just async/await, not a
        // node type. If the step is preempted partway, CANCELLED propagates and the
        // remaining methods are skipped.
        sequence(...methodNames) {
            return async (token) => {
                this.token = token;
                for (const methodName of methodNames) {
                    const method = this[methodName];
                    if (typeof method !== 'function')
                        throw new Error(`Unknown behavior method: ${methodName}`);
                    this.onTransition(methodName, 'step');
                    await method.call(this);
                }
            };
        }

        behaviors() {
            throw new Error('behaviors() must be implemented');
        }

        start() {
            this.runner = createBehaviorRunner({
                tickMs: this.tickMs || 20,
                behaviors: this.behaviors(),
                onError: this.onError || reportError,
                onPreempt: () => this.onPreempt(),
                onTransition: (name, event) => this.onTransition(name, event),
            });
            this.onStart();
            this.runner.start();
            return this;
        }

        stop() {
            if (this.runner) this.runner.stop();
            this.onStop();
        }

        onStart() {}
        onStop() {}
        onPreempt() { Player.stop(); }

        // Behaviour lifecycle logging (override to customise or silence). Events:
        //   'start'   a behaviour took over the body
        //   'step'    a sub-step of a sequence(...) began
        //   'preempt' a higher-priority behaviour cut this one off
        //   'done'    the behaviour returned on its own
        onTransition(name, event) {
            if (event === 'start') console.log(`[bt] ==> ${name}`);
            else if (event === 'step') console.log(`[bt]     - ${name}`);
            else if (event === 'preempt') console.log(`[bt] !!! ${name} (preempted)`);
            else if (event === 'done') console.log(`[bt] <== ${name}`);
        }

        // ===== state helpers =====

        // True when the backpack is full enough to bank. Uses the server's
        // maxWeight when known (stop 20 stones short), else BANK_AT_WEIGHT.
        full() {
            const maxWeight = Player.maxWeight;
            return maxWeight > 0 ? Player.weight >= maxWeight - 20 : Player.weight >= this.BANK_AT_WEIGHT;
        }

        // Current HP fraction 0..1 (1 when max is unknown).
        hpFrac() {
            const max = Player.hpMax;
            return max > 0 ? Player.hp / max : 1;
        }

        // ===== inventory =====

        // First backpack item whose name contains `name` (a substring), or undefined.
        findInPack(name) {
            return Player.equipment.backpack.items.find((item) => item.name.includes(name));
        }

        // Total amount of backpack items whose name matches any of `names`
        // (string or array of lowercased substrings).
        backpackCount(names) {
            const variants = [].concat(names).map((name) => name.toLowerCase());
            return Player.equipment.backpack.items
                .filter((item) => variants.some((variant) => item.name.toLowerCase().includes(variant)))
                .reduce((sum, item) => sum + (item.amount || 1), 0);
        }

        // ===== movement =====

        // Walk to `target` ({x,y}). The single movement helper for bots:
        //   range > 0  : loop until within `range` tiles (retries, gives up after 8).
        //   range == 0 : step onto the target, or — with adjacent:true — onto the
        //                nearest of the 8 surrounding cells (used to stand next to a
        //                tree/vendor without occupying its tile).
        // terrain:false drops the road/grass bias (direct route, e.g. between trees).
        // Returns true on arrival, false if every candidate cell was unreachable.
        async walkTo(target, { adjacent = false, range = 0, terrain = true } = {}) {
            const { token } = this;
            if (range > 0) {
                let attempts = 0;
                while (tileDistance({ x: Player.x, y: Player.y }, target) > range) {
                    token.check();
                    try { await token.wait(Player.goto(target.x, target.y, { terrain })); }
                    catch (error) {
                        if (token.cancelled) throw CANCELLED;
                        if (++attempts > 8) return false;
                        await token.sleep(400);
                    }
                }
                return true;
            }

            const here = { x: Player.x, y: Player.y };
            const offsets = adjacent
                ? [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]]
                : [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];
            const cells = offsets.map(([dx, dy]) => ({ x: target.x + dx, y: target.y + dy }))
                .sort((a, b) => tileDistance(here, a) - tileDistance(here, b));
            for (const cell of cells) {
                if (cell.x === here.x && cell.y === here.y) return true;
                try { await token.wait(Player.goto(cell.x, cell.y, { terrain })); return true; }
                catch (error) { if (token.cancelled) throw CANCELLED; }
            }
            return false;
        }
    }

    g.BehaviorScript = BehaviorScript;
})(globalThis);
