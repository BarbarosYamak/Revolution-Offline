'use strict';
// Loaded fresh on every `run`. Defines ONLY the minimal JS runtime surface that
// the C++ engine guarantees: console routing, delay, tileDistance, reportError,
// and the journal/container waiters. Player / World / Mobiles / Vendor are
// installed by the C++ binding layer; bot helpers live in scripts/js/lib/*.js.
(function (g) {
    const format = (args) => Array.from(args, (value) => {
        if (typeof value === 'string') return value;
        try { return JSON.stringify(value); }
        catch (error) { return String(value); }
    }).join(' ');

    g.console = {
        log: (...args) => __stdout(format(args)),
        info: (...args) => __stdout(format(args)),
        debug: (...args) => __stdout(format(args)),
        warn: (...args) => __stdout(format(args)),
        error: (...args) => __stderr(format(args)),
    };

    g.delay = (ms) => new Promise((resolve) => __setTimeout(resolve, ms | 0));

    // King-move (Chebyshev) distance between two {x,y} points: max(|dx|, |dy|).
    // This is UO's own notion of distance — a diagonal step counts as one tile —
    // so it matches the server's range checks. Use it for "how many tiles away"
    // tests and to sort candidates (mobs, trees, cells) by nearness.
    g.tileDistance = (a, b) => Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y));

    // Last-resort error reporter. Errors from native Promise rejections already
    // carry a JS stack captured at the call site (ClientBindings.cpp).
    g.reportError = (error) => __stderr(`${error}: \n ${error.stack ?? String(error)}`);

    g.waitForJournal = (opts) => new Promise((resolve, reject) => {
        const matches = (text) => !opts.contains ||
            (opts.contains instanceof RegExp ? opts.contains.test(text) : text.includes(opts.contains));
        const timeout = opts.ms ? __setTimeout(() => {
            Player.off('journal', handler);
            reject('waitForJournal timed out');
        }, opts.ms) : 0;
        const handler = (msg) => {
            if (!matches(msg.text)) return;
            __clearTimeout(timeout);
            Player.off('journal', handler);
            resolve(msg.text);
        };
        Player.on('journal', handler);
    });

    // Wait for a container to open. With opts.serial, only that container matches
    // (so a stray open can not resolve us); without it, the first open wins (e.g.
    // the bank box, whose serial we learn only when it opens).
    g.waitForContainer = (opts = {}) => new Promise((resolve, reject) => {
        const timeout = opts.ms ? __setTimeout(() => {
            World.off('container_open', handler);
            reject('waitForContainer timed out');
        }, opts.ms) : 0;
        const handler = (container) => {
            if (opts.serial != null && container.serial !== opts.serial) return;
            __clearTimeout(timeout);
            World.off('container_open', handler);
            resolve(container);
        };
        World.on('container_open', handler);
    });
})(globalThis);
