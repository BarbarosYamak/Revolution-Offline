'use strict';
// SurvivalSkill — keep-alive / upkeep methods mixed into a bot prototype:
//     Object.assign(MyBot.prototype, SurvivalSkill);
// Reads `this.token`, `this.BANDAGE`, `this.FOOD`, and the shared `this.threat`
// blackboard; sets `this.lastAteMs`. Uses base helpers walkTo/backpackCount/
// findInPack, so mix it onto a BehaviorScript subclass.
//
// Auto-loaded with the other scripts/js/lib/*.js modules.
(function (g) {
    const VENDOR_SCAN_RADIUS = 18;

    // Order `stops` to minimise the TOTAL travel distance of an open route that
    // starts at `from` and visits every stop once (shortest Hamiltonian path).
    // There is one stop per vendor, so the count is tiny and brute-forcing every
    // permutation is both simplest and exact.
    function orderByShortestRoute(from, stops) {
        if (stops.length <= 1) return stops;
        let bestRoute = stops, bestLength = Infinity;
        const search = (remaining, route, length, at) => {
            if (length >= bestLength) return; // prune: already worse than the best
            if (remaining.length === 0) { bestRoute = route; bestLength = length; return; }
            for (let i = 0; i < remaining.length; i++) {
                const next = remaining[i];
                const rest = remaining.slice(0, i).concat(remaining.slice(i + 1));
                search(rest, route.concat(next), length + tileDistance(at, next.coords), next.coords);
            }
        };
        search(stops, [], 0, from);
        return bestRoute;
    }

    const SurvivalSkill = {
        // Apply one bandage to ourselves; returns true if it started.
        async bandageSelf() {
            const { token } = this;
            const bandage = this.findInPack(this.BANDAGE);
            if (!bandage) return false;
            try {
                Player.use(bandage.serial);
                await token.wait(Player.once('target', 2000));
                Player.target(Player.serial);
                const line = (await token.wait(waitForJournal({ ms: 2000 }))).toLowerCase();
                return line.includes('apply the bandages');
            } catch (error) {
                if (error === CANCELLED) throw error;
                return false;
            }
        },

        // Bandage until full HP, bailing out the moment a threat appears.
        async rest() {
            const { token } = this;
            while (!Player.dead && Player.hpMax > Player.hp) {
                if (this.threat?.exists) return;
                await this.bandageSelf();
                await token.sleep(10_000);
            }
        },

        // Eat food from the backpack (matched by this.FOOD) until full.
        async eatFood() {
            const { token } = this;
            const foodVariants = [].concat(this.FOOD || []).map((name) => name.toLowerCase());
            while (!Player.dead) {
                const food = Player.equipment.backpack.items
                    .find((item) => foodVariants.some((variant) => item.name.toLowerCase().includes(variant)));
                if (!food) break;
                Player.use(food.serial);
                let line;
                try { line = (await token.wait(waitForJournal({ ms: 2000 }))).toLowerCase(); }
                catch (error) { if (error === CANCELLED) throw error; break; }
                if (line.includes('too full') || line.includes('stuffed') || line.includes('quite full')) {
                    this.lastAteMs = Date.now();
                    break;
                }
                await token.sleep(300);
            }
        },

        // Double-click nearby mobiles to learn their paperdoll titles; return the
        // live handle whose title contains `wantedTitle` (never a guild master), or
        // null. Skips ones already known.
        async findVendor(wantedTitle, coords) {
            const { token } = this;
            const wantedLower = wantedTitle.toLowerCase();
            const matchesTitle = (mobile) => {
                const title = (mobile.title || '').toLowerCase();
                return title.includes(wantedLower) && !title.includes('guildm');
            };
            const candidates = Mobiles.all()
                .filter((mobile) => mobile.exists && mobile.serial !== Player.serial)
                .filter((mobile) => tileDistance({ x: mobile.x, y: mobile.y }, coords) <= VENDOR_SCAN_RADIUS)
                .sort((a, b) => tileDistance({ x: a.x, y: a.y }, coords) -
                                tileDistance({ x: b.x, y: b.y }, coords));

            let found = candidates.find(matchesTitle);
            if (found) return found;

            for (const mobile of candidates) {
                token.check();
                await token.sleep(1100);
                Player.doubleClick(mobile.serial);
                // The paperdoll may arrive for ANY nearby mobile, not necessarily
                // this one, so re-scan all candidates after each reply.
                try { await token.wait(Player.once('paperdoll', 2000)); }
                catch (error) { if (token.cancelled) throw CANCELLED; }
                if ((found = candidates.find(matchesTitle))) return found;
            }
            return null;
        },

        // Approach the vendor, say "buy", and buy up to `target` of the items whose
        // name matches `nameVariants`. `displayName` is only for logging.
        async buyFrom(vendor, nameVariants, displayName, target) {
            const { token } = this;
            Player.follow(vendor.serial);
            await token.sleep(2000);

            let offer;
            try {
                offer = await token.retry(async () => {
                    const [window] = await token.wait(Promise.all([
                        Vendor.once('vendor_buy', 4000),
                        Player.say('vendor buy'),
                    ]));
                    return window;
                }, 2, 800);
            } catch (error) { if (token.cancelled) throw CANCELLED; return false; }
            if (!offer) return false;

            console.log(`[restock] ${vendor.title} offers: ${
                offer.items.map((item) => `${item.name} x${item.amount} @${item.price}gp`).join(', ')}`);

            const matching = offer.items.filter((item) =>
                nameVariants.some((variant) => item.name.toLowerCase().includes(variant)));
            if (matching.length === 0) {
                console.warn(`[restock] ${vendor.title || 'vendor'} does not sell "${displayName}"`);
                return false;
            }

            let need = Math.max(0, target - this.backpackCount(nameVariants));
            if (need <= 0) return true;

            const buyList = [];
            for (const row of matching) {
                if (need <= 0) break;
                const qty = Math.min(need, row.amount);
                if (qty <= 0) continue;
                console.log(`[restock] buying ${qty}x ${row.name} @${row.price}gp`);
                buyList.push({ serial: row.serial, qty });
                need -= qty;
            }
            if (buyList.length === 0) {
                console.warn(`[restock] ${vendor.title} out of ${displayName} (stock 0)`);
                return true;
            }
            Vendor.buy(offer.vendor, buyList);
            try { await token.wait(Vendor.once('vendor_done', 4000)); }
            catch (error) { if (token.cancelled) throw CANCELLED; }
            return true;
        },

        // Restock every consumable the backpack is out of, buying up to its
        // `target`. Consumables sharing a vendor title+coords are grouped into one
        // stop; the stops are then ordered into the shortest total route from the
        // current position before visiting them.
        async restockConsumables(consumables) {
            const { token } = this;

            const stops = new Map(); // `${title}|${x}|${y}` -> { title, coords, items }
            for (const key of Object.keys(consumables)) {
                const consumable = consumables[key];
                const names = [].concat(consumable.name || key);
                const nameVariants = names.map((name) => name.toLowerCase());
                if (this.backpackCount(nameVariants) > 0) continue;
                const stopKey = `${consumable.title}|${consumable.coords.x}|${consumable.coords.y}`;
                if (!stops.has(stopKey))
                    stops.set(stopKey, { title: consumable.title, coords: consumable.coords, items: [] });
                stops.get(stopKey).items.push({
                    nameVariants, displayName: names.join('/'), target: consumable.target,
                });
            }
            if (stops.size === 0) return;

            const route = orderByShortestRoute({ x: Player.x, y: Player.y }, [...stops.values()]);
            for (const stop of route) {
                token.check();
                console.log(`[restock] heading to '${stop.title}' @${stop.coords.x},${stop.coords.y}` +
                    ` for ${stop.items.map((item) => item.displayName).join(', ')}`);
                if (!await this.walkTo(stop.coords, { range: 1 })) {
                    console.warn(`[restock] could not reach ${stop.coords.x},${stop.coords.y}`);
                    continue;
                }
                const vendor = await this.findVendor(stop.title, stop.coords);
                if (!vendor) {
                    console.warn(`[restock] no '${stop.title}' near ${stop.coords.x},${stop.coords.y}`);
                    continue;
                }
                for (const item of stop.items) {
                    await this.buyFrom(vendor, item.nameVariants, item.displayName, item.target);
                }
                Player.follow(false);
            }
        },
    };

    g.SurvivalSkill = SurvivalSkill;
})(globalThis);
