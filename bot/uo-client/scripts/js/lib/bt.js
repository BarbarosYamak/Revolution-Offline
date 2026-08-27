'use strict';
// Lightweight priority behaviour runner with cancellable async steps.
//
// Loaded automatically: the engine evaluates every scripts/js/lib/*.js after
// bootstrap.js and before the bot script, so this is available as a global.
//
// Model (the "lighter priority-tick" we chose over a full behaviour tree): a flat
// list of behaviours in priority order, each a plain async function (a "step").
// On every tick the highest-priority behaviour whose guard `when()` is true owns
// the body.
//
// Preemption is HIGHER-PRIORITY-ONLY: a running step is CANCELLED only when a
// behaviour ABOVE it becomes eligible (a fresh threat or death interrupts work).
// If the running step's own guard goes false and only a LOWER-priority behaviour
// is ready, the step is left to run to completion — it returns on its own, then
// the runner picks the next. This matters because a step often changes the very
// state its guard reads (e.g. `bank` deposits until it's no longer overweight,
// `flee` clears its flag once recovered); without this rule the step would
// preempt ITSELF mid-task the instant its guard flipped. Steps stay imperative
// and readable; the runner only decides which one runs and when to interrupt it.
//
// Cancellation: each step gets a token. Wrap every long await with token.wait()
// (or token.sleep()) so a preempt unwinds it by throwing CANCELLED. Register
// cleanup with token.onCancel(). Movement is cancelled by the runner's onPreempt
// hook (the bot wires it to Player.stop()).
(function (g) {
    // Sentinel thrown to unwind a preempted step. Steps must let it propagate
    // (don't swallow it); token.retry below re-throws it instead of retrying.
    const CANCELLED = g.CANCELLED || Symbol('cancelled');
    g.CANCELLED = CANCELLED;

    // A cancellation token handed to each running step.
    function makeToken() {
        let fire;
        const cancelled = new Promise((_, reject) => { fire = () => reject(CANCELLED); });
        cancelled.catch(() => {});            // never an "unhandled rejection"
        const hooks = [];
        return {
            cancelled: false,
            /** Register a cleanup callback run when the step is preempted. */
            onCancel(fn) { hooks.push(fn); },
            cancel() {
                if (this.cancelled) return;
                this.cancelled = true;
                for (const fn of hooks) { try { fn(); } catch (e) { /* ignore */ } }
                fire();
            },
            /** Throw CANCELLED if we have been preempted (call between awaits). */
            check() { if (this.cancelled) throw CANCELLED; },
            /**
             * Await `promise`, but unwind with CANCELLED the instant we are
             * preempted. Use for EVERY await inside a step. A normal rejection of
             * `promise` (e.g. a goto that aborted) propagates unchanged.
             */
            async wait(promise) {
                try {
                    return await Promise.race([promise, cancelled]);
                } finally {
                    if (this.cancelled) throw CANCELLED;   // robust vs. race ordering
                }
            },
            /** Cancellable sleep. */
            sleep(ms) { return this.wait(g.delay(ms)); },
            /**
             * Cancellable retry: run `fn` up to `retries` times (so retries+1
             * attempts total). check() runs before each attempt and the gap
             * between attempts is sleep(intervalMs), so a preempt unwinds at once
             * instead of waiting out the interval. CANCELLED always propagates and
             * is never retried; `fn` may close over this token for its own awaits.
             */
            async retry(fn, retries = 3, intervalMs = 250) {
                for (let attempt = 0; ; attempt++) {
                    this.check();
                    try {
                        return await fn();
                    } catch (e) {
                        if (e === CANCELLED || this.cancelled) throw CANCELLED;
                        if (attempt >= retries) {
                            const tag = `[retry failed after ${attempt + 1} attempts] `;
                            if (e instanceof Error) { e.message = tag + e.message; throw e; }
                            throw new Error(tag + String(e));
                        }
                        await this.sleep(intervalMs);   // cancellable gap
                    }
                }
            },
        };
    }
    g.makeToken = makeToken;

    /**
     * createBehaviorRunner({ tickMs, behaviors, onError, onPreempt })
     *   behaviors: [{ name, when:()=>bool, step: async (token)=>void }], highest
     *              priority FIRST (priority = array index, lower = higher). `step`
     *              runs across many ticks until it returns.
     *   onPreempt(): called right after the running step is cancelled by a
     *              higher-priority behaviour (the bot uses it to halt movement,
     *              e.g. Player.stop()).
     *   onTransition(name, event): lifecycle hook for logging. event is
     *              'start' (a behaviour took the body), 'done' (it returned on its
     *              own), or 'preempt' (a higher-priority behaviour cut it off).
     * A running step is only ever interrupted by a behaviour ABOVE it; a guard
     * going false (with nothing higher ready) lets the step finish on its own.
     * Returns { start(), stop(), current }.
     */
    g.createBehaviorRunner = function ({ tickMs = 300, behaviors, onError, onPreempt, onTransition }) {
        let curName = null, curToken = null, curDone = null, curIndex = -1, running = false;
        const note = (name, event) => { if (onTransition) try { onTransition(name, event); } catch (e) { /* ignore */ } };

        function startStep(behavior, index) {
            const token = makeToken();
            curName = behavior.name;
            curIndex = index;
            curToken = token;
            note(behavior.name, 'start');
            curDone = (async () => {
                try { await behavior.step(token); }
                catch (e) { if (e !== CANCELLED && onError) onError(e); }
                finally {
                    if (curToken === token) { curName = null; curToken = null; curDone = null; curIndex = -1; }
                    // A natural return reports 'done'; a preempt already reported itself.
                    if (!token.cancelled) note(behavior.name, 'done');
                }
            })();
        }

        async function tick() {
            try {
                if (curToken) {
                    const topIndex = behaviors.findIndex((b) => b.when());
                    // Keep running unless something STRICTLY higher-priority is ready.
                    if (topIndex < 0 || topIndex >= curIndex) {
                        if (running) g.__setTimeout(tick, tickMs);
                        return;
                    }
                    // A higher-priority behaviour wants the body -> preempt.
                    note(curName, 'preempt');
                    curToken.cancel();
                    if (onPreempt) try { onPreempt(); } catch (e) { /* ignore */ }
                    await curDone;                 // let the old step unwind (clears cur*)
                }
                // Idle (or just preempted) -> start the highest-priority eligible step.
                const pickIndex = behaviors.findIndex((b) => b.when());
                if (pickIndex >= 0) startStep(behaviors[pickIndex], pickIndex);
            } catch (e) {
                if (onError) onError(e);
            }
            if (running) g.__setTimeout(tick, tickMs);
        }

        return {
            start() { if (!running) { running = true; g.__setTimeout(tick, 0); } return this; },
            stop() { running = false; if (curToken) curToken.cancel(); },
            get current() { return curName; },
        };
    };
})(globalThis);
