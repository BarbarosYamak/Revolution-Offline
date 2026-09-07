#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// ONE TICK OF A BANDAGE-INPUT PURCHASE. Declared in Runner.h, which carries
// the reasoning. Both rows of the weaver's shelf -- loose cloth and bolts --
// are bought by the same handshake, and the plumbing around it was already
// thirty lines the second row would have copied verbatim.
bool Runner::TickBandageInputBuy(Client& client, const Observation& obs,
                                 life::BuyActivity& buy, const char* tag,
                                 bool& bought) {
    bought = false;
    const life::ActivityTickResult r = buy.Tick(client, obs);
    LogErrandReason(tag, r.reason, obs.nowMs);
    if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
        nextActionMs_ = obs.nowMs + r.delayMs;
    if (!life::IsTerminal(r.status)) {
        // Finding the keeper and reaching the counter are transitions,
        // even when this tick sends no action. They reset failed attempts.
        const bool legLanded = r.offerOpen ||
            (r.reason && (std::strstr(r.reason, "found a") ||
                          std::strstr(r.reason, "within reach") ||
                          std::strstr(r.reason, "ARRIVED") ||
                          std::strstr(r.reason, "the shop is open")));
        if (legLanded) planner_.NoteProgress();
        else if (r.acted) planner_.NoteAttempt(obs.nowMs);
        return true;
    }
    if (r.status == life::ActivityStatus::Success) {
        planner_.NoteProgress();
        bought = true;
        return false;
    }
    // NO DRAINED-SHELF NOTE ON A PARTIAL BUY, unlike the healer's counter.
    // The weaver's list holds i_cloth in FOUR separate rows (16/16/5/13 in
    // Castor's window on 2026-09-05) and the errand buys the first matching
    // row, so a partial buy is a row emptied, not a shop emptied. Only the
    // terminal failure below -- "does not stock" -- means the shop is out.
    return false;
}

// ---------------------------------------------------------------------------
// MAKING BANDAGES.
//
// "for warrior it should create its own bandage on up to 50-100, we know the
// crafting bandages" and "wool can be obtained from sheeps" (project owner,
// 2026-08-29).
//
// This is the answer to the deadlock that cost Kaelen a session: hungry, so no
// HP regeneration; wounded, so under the hunting bar; no bandages, so HEAL was
// blocked; and no gold, so he could not buy any. Bandages ARE sold -- healers
// and vets stock them -- but a fighter with an empty purse cannot use a shop,
// and a sheep costs nothing.
//
// The chain is five gestures and they are all the same gesture: use one thing
// on another. See the constants above for the engine citation behind each.
// Every stage is skipped if its output is already in the pack, so a character
// who loots cloth walks straight to the last step.
bool Runner::DoMakeBandages(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    // The flock table, for the last step. Read from the world save by
    // tools/pasturegen.py, never assumed -- and read HERE rather than at the
    // pasture branch so the distance rule below can be applied before any
    // travel is planned.
    LoadPastures(client.DataDir());

    // HOW MANY IS ENOUGH IS THE CHARACTER'S NUMBER, NOT A CONSTANT.
    // needCfg_.bandageFull is resolved per life and per purse each planning
    // tick (ResolveConsumableThresholds), and for a life that hunts it sits
    // above the owner's floor of a hundred. kBandagesWanted survives only as
    // the fallback for a character with no profession.
    const i32 want = needCfg_.bandageFull > 0 ? needCfg_.bandageFull
                                              : kBandagesWanted;
    if (obs.bandages >= want) {
        LogLine("bandages: %d is enough to fight on", obs.bandages);
        bandageTrips_ = 0;
        bandageBoltsOut_ = false;       // a fresh shortfall gets a fresh look
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // Scissors do every step, so without them there is no chain at all. They
    // are in every starter kit as ITEMNEWBIE and so survive death, which is
    // the point -- this goal exists for characters who have just lost
    // everything else.
    const u32 scissors = client.FindBackpackItemByGraphic(kScissorsGraphic);
    if (!scissors) {
        // GO AND BUY A PAIR. "fencer we added scissor no? if he has none he
        // should go buy one" (owner, 2026-08-29) -- and he is right that
        // giving up was the wrong answer.
        //
        // Scissors are in every starter kit as ITEMNEWBIE now, but a character
        // created before that change has none and can never get any, which is
        // exactly Kaelen: MAKE_BANDAGES fired three times and failed three
        // times on "no scissors" while he idled through the rest of the
        // session. A tailor sells them (VENDOR_S_TAILOR, and the tinker too),
        // and they are cheap.
        //
        // If the purse cannot even manage that, THEN stand down -- but say so
        // as a money problem, which is the thing that can actually change.
        if (obs.gold >= kScissorsMoney) {
            BuyScrollFrom(client, obs, "tailor", wm::Service::Tailor,
                          kScissorsGraphic, false, 1, "a pair of scissors",
                          GoalKind::MakeBandages);
            return false;
        }
        LogLine("goal_failed=MAKE_BANDAGES reason=\"no scissors and only %d "
                "gold to buy a pair with\"", obs.gold);
        return HandOff(GoalKind::MakeBandages, GoalKind::EarnGold,
                       kNoBandageCooldownMs, "no scissors and no money",
                       obs.nowMs);
    }

    // 0. LOOTED CLOTHING -> BANDAGES. The cheapest of the lot: no sheep, no
    //    wheel, no loom, and the garment came off something the character had
    //    to kill anyway. A shirt yields 8, a surcoat 14.
    //
    //    Only what is in the PACK. FindBackpackItemByGraphic never returns a
    //    worn item, so a character cannot cut the clothes off its own back --
    //    and the engine would refuse anyway, since CanUse(item, true) requires
    //    CanMoveItem.
    if (const u32 rag = FindAny(client, kCuttableClothing,
                                sizeof(kCuttableClothing) /
                                    sizeof(kCuttableClothing[0]))) {
        LogLine("bandages: cutting up looted clothing (%d bandages so far, "
                "want %d)", obs.bandages, want);
        client.ActionUseItemOn(scissors, rag);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // 1. CLOTH -> BANDAGES. One bandage per cloth, so this is the step that
    //    actually pays and it runs before anything else.
    //
    //    NOT WHILE A PURCHASE IS STILL BEING COUNTED. The buy errand records
    //    the pack BEFORE it asks and compares afterwards (section 18), so
    //    cutting the cloth up inside that window makes the purchase look
    //    like a theft: Castor, 2026-09-05 11:07:35, bought 16 cloth, cut it
    //    16ms later, and the errand reported "gold left the purse and no
    //    goods arrived (pack +0, purse -48)" and walked off to Yew for
    //    sheep -- carrying the sixteen bandages it had just made.
    const u32 cloth = bandageClothBuy_.Running()
                          ? 0
                          : client.FindBackpackItemByGraphic(kClothGraphic);
    if (cloth) {
        LogLine("bandages: cutting cloth (%d bandages so far, want %d)",
                obs.bandages, want);
        client.ActionUseItemOn(scissors, cloth);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // 2. BOLT -> CLOTH. Fifty cloth in one gesture (ConvertBolttoCloth).
    //    Same purchase-window guard as the cloth above: a bolt cut inside the
    //    errand's own before/after comparison reads as a theft.
    const u32 bolt = bandageBoltBuy_.Running()
                         ? 0
                         : client.FindBackpackItemByGraphic(kClothBoltGraphic);
    if (bolt) {
        LogLine("bandages: cutting a bolt of cloth into cloth");
        client.ActionUseItemOn(scissors, bolt);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // 3. YARN OR THREAD -> LOOM -> BOLT.
    u32 spun = client.FindBackpackItemByGraphic(kYarnGraphic);
    if (!spun) spun = client.FindBackpackItemByGraphic(kThreadGraphic);
    if (spun) {
        const u32 loom = FindLoom(client, kStationSight);
        if (!loom) {
            LogLine("bandages: carrying yarn but no loom in sight -- going to "
                    "a tailor, where the looms are");
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("bandages: weaving yarn into cloth at a loom");
        client.ActionUseItemOn(spun, loom);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 4. WOOL -> SPINNING WHEEL -> YARN.
    if (const u32 wool = client.FindBackpackItemByGraphic(kWoolGraphic)) {
        const u32 wheel = FindSpinWheel(client, kStationSight);
        if (!wheel) {
            LogLine("bandages: carrying wool but no spinning wheel in sight -- "
                    "going to a tailor");
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("bandages: spinning wool into yarn at a wheel");
        client.ActionUseItemOn(wool, wheel);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 4b/4c. NOTHING IN THE PACK TO WORK WITH, BUT THERE IS A PURSE: BUY
    // THE INPUT AT A TAILOR RATHER THAN WALKING TO A FLOCK.
    //
    // Scissors on loose cloth give one bandage per cloth, engine-hardcoded and
    // with no skill check (Source-X CClientTarg.cpp:2135-2184), and loose
    // i_cloth is on the weaver's shelf at {3 38} and the tailor's at four
    // rows of {2 24} for 3 gp (tm_vend.scp:875-887, 899-966). That is the
    // only route to a hundred bandages in one sitting: a healer's shelf holds
    // at most twenty and refills once every ten minutes.
    //
    // THE ROWS RUN OUT AND THE BOLTS DO NOT. Ravan emptied the weaver's four
    // cloth rows in one visit (4 + 18 + 12), heard "this weaver does not
    // stock loose cloth", and set off for the Yew pasture 960 tiles away --
    // with 10,000 gold in hand and three rows of bolts, 5/23/18 at 173 gp,
    // on the shelf he was standing at (2026-09-06 23:00:14-23:01:16). A bolt
    // is fifty cloth in one gesture, so a hundred-bandage shortfall is two
    // purchases, not a cross-map hike. The bigger row goes first when the
    // shortfall is a bolt's worth; loose cloth is the top-up and the fallback.
    //
    // NARROW ON PURPOSE. The standing ruling "never buy cloth/thread/yarn
    // from NPCs" (owner, 2026-09-02) is about a TAILOR's supply -- a crafter
    // must not buy the thing she makes, and DoMakeCloth below still obeys it
    // by gathering. This branch is a fighter buying a consumable input for
    // bandages when every counter in town is empty, which is why it is gated
    // on WantsToHunt and on being short of the fighting floor.
    const bool hunts =
        needCfg_.profession && WantsToHunt(*needCfg_.profession);
    // WHAT IS STILL SHORT AFTER THE PACK IS COUNTED. Cloth and bolts already
    // carried are bandages that have not been cut yet, and the order must not
    // count them twice: Hector bought two bolts, cut one, and bought two more
    // while the first pair was still in his pack -- 200 stones and 70% of his
    // carry weight for a shortfall he had already covered (2026-09-07
    // 00:27:04-00:28:31). Steps 1 and 2 above normally consume these before
    // the tick gets here; this holds during the purchase window, when they
    // are deliberately hidden.
    const i32 convertible =
        static_cast<i32>(client.BackpackItemCount(kClothGraphic)) +
        static_cast<i32>(client.BackpackItemCount(kClothBoltGraphic)) *
            kClothPerBolt;
    const i32 shortfall = want - obs.bandages - convertible;
    if (hunts && shortfall > 0) {
        bool bought = false;
        // 4b. A BOLT, when the shortfall is worth one and the purse can pay
        //     the quote. Never more than kMaxBoltsPerTrip: WEIGHT=50.0 each.
        if (bandageBoltBuy_.Running() ||
            (!bandageBoltsOut_ && shortfall >= kClothPerBolt &&
             obs.gold >= kClothBoltMaxPrice)) {
            if (!bandageBoltBuy_.Running()) {
                life::BuyRequest req;
                req.graphic = kClothBoltGraphic;
                req.item = "a bolt of cloth";
                const i32 bolts =
                    (shortfall + kClothPerBolt - 1) / kClothPerBolt;
                req.desiredTotal = std::min(bolts, kMaxBoltsPerTrip);
                req.minimumGoldReserve = 0;
                req.maxPricePerUnit = kClothBoltMaxPrice;
                req.Sell("weaver", wm::Service::Tailor);
                req.Sell("tailor", wm::Service::Tailor);
                for (u32 drained : DrainedShelves(obs.nowMs)) req.Avoid(drained);
                bandageBoltBuy_.Begin(req);
            }
            if (TickBandageInputBuy(client, obs, bandageBoltBuy_,
                                    "bandage bolt", bought))
                return false;
            if (bought) {
                LogLine("bandages: %d bolt(s) bought -- fifty cloth apiece, "
                        "cutting them next pass",
                        static_cast<i32>(
                            client.BackpackItemCount(kClothBoltGraphic)));
                return false;             // step 2 above cuts it
            }
            // NO DRAINED-SHELF NOTE. That note is keyed by shopkeeper and
            // would hide this same keeper's loose-cloth row, which is exactly
            // where the fallback below is going. A session latch instead.
            bandageBoltsOut_ = true;
            LogLine("bandages: no bolt of cloth to be had -- trying the loose "
                    "cloth row on the same shelf");
        }
        // 4c. LOOSE CLOTH. One cloth is one bandage, so the shortfall IS the
        //     order, and a whole stack cuts in a single gesture (measured:
        //     18 cloth took Ravan from 17 bandages to 35).
        if (bandageClothBuy_.Running() || obs.gold >= kClothMaxPrice) {
            if (!bandageClothBuy_.Running()) {
                life::BuyRequest req;
                req.graphic = kClothGraphic;
                req.item = "loose cloth";
                req.desiredTotal = shortfall;
                req.minimumGoldReserve = 0;
                req.maxPricePerUnit = kClothMaxPrice;
                req.Sell("weaver", wm::Service::Tailor);
                req.Sell("tailor", wm::Service::Tailor);
                for (u32 drained : DrainedShelves(obs.nowMs)) req.Avoid(drained);
                bandageClothBuy_.Begin(req);
            }
            if (TickBandageInputBuy(client, obs, bandageClothBuy_,
                                    "bandage cloth", bought))
                return false;
            if (bought) {
                const i32 clothHeld =
                    static_cast<i32>(client.BackpackItemCount(kClothGraphic));
                LogLine("bandages: %d cloth bought -- cutting it next pass",
                        clothHeld);
                return false;             // step 1 above cuts it
            }
            // The rows really are out at this counter, and this one IS a
            // shop note: nothing on this shelf answers the goal for the ten
            // minutes it takes to restock.
            NoteDrainedShelf(bandageClothBuy_.Keeper(), obs.nowMs);
            LogLine("bandages: nothing bought at the cloth counter -- back to "
                    "the wool chain");
        }
    }

    // 5. A SHEEP -> WOOL. The only free step, and the start of everything.
    //
    // WITH A BLADE, NOT THE SCISSORS. This used to hand the scissors to the
    // sheep, on the strength of the comment above citing CClientTarg.cpp:1878
    // -- but that line sits inside `case IT_WEAPON_SWORD / _AXE / _FENCE`
    // (:1866-1900), not inside `case IT_SCISSORS` (:2135), and a sheep is a
    // CHARACTER so the scissors case never even sees it. The gesture answered
    // "Scissors cannot be used on that to produce anything" every time. See
    // FindBlade for the whole citation.
    const u32 blade = FindBlade(client);
    const u32 sheep = client.NearestMobileWithBody(kSheepBody, 12);
    if (sheep && blade) {
        i32 sx = 0, sy = 0; i8 sz = 0;
        if (client.MobilePosition(sheep, &sx, &sy, &sz)) {
            const i32 d = TileDist(obs.x, obs.y, sx, sy);
            if (d > 1) {
                LogLine("bandages: a sheep %d tiles away -- walking up to shear "
                        "it", d);
                travelInFlight_ = client.TravelToEntity(sheep, 1);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        LogLine("bandages: shearing a sheep for wool");
        client.ActionUseItemOn(blade, sheep);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    if (sheep && !blade) {
        LogLine("goal_failed=MAKE_BANDAGES reason=\"a sheep is here but nothing "
                "bladed is carried -- scissors will not shear\"");
        planner_.Cooldown(GoalKind::MakeBandages,
                          obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "no blade to shear with", obs.nowMs);
        return false;
    }

    // NO SHEEP IN SIGHT, AND A FLOCK IS THE LAST RESORT, NOT THE FIRST.
    //
    // This used to hold three hard-coded points -- 572,1098 / 669,943 /
    // 669,1175, the Yew farmland -- walked in order with no distance test at
    // all. Ravan, a Britain fighter with 10,000 gold, planned 23 legs and
    // ~960 tiles through the Yew moongate for them and the session ended with
    // him standing in the wild at 634,849 (2026-09-06 23:01:16). Yew is never
    // a destination of choice (owner, 2026-08-30), and the same rule
    // DoMakeCloth already obeys applies here: the flock has to be OUR flock.
    //
    // So: the save-derived table (tools/pasturegen.py), ranked by distance
    // from this life's own home bank, and nothing beyond
    // kMaxPastureTilesFromHome of it. That radius is not a number picked
    // here -- it is the atlas gap between a home flock and someone else's:
    // Britain bank to the Britain farmland flock at 1321,1817 is ~120 tiles,
    // while the next rows in the table are the Yew flocks at ~750 and Jhelom
    // at ~1900. 400 admits the first and excludes the rest.
    //
    // The anchor is the seeded home bank, not where the character happens to
    // stand, so a fighter stranded up-country still shears at home.
    if (client.TravelBusy()) return false;
    i32 anchorX = obs.x, anchorY = obs.y;
    const char* anchorWhat = "here";
    if (const KnownPlace* homeBank =
            state_.memory.BestPlace("common_knowledge_bank")) {
        anchorX = homeBank->x;
        anchorY = homeBank->y;
        anchorWhat = "home";
    }
    const std::vector<Pasture>& pastures = Pastures();
    std::vector<usize> nearby;   // `near` is a Windows macro
    for (usize i = 0; i < pastures.size(); ++i)
        if (TileDist(anchorX, anchorY, pastures[i].x, pastures[i].y) <=
            kMaxPastureTilesFromHome)
            nearby.push_back(i);
    if (nearby.empty()) {
        // EVERY ROUTE TO A BANDAGE IS SHUT: the healer's counters are empty
        // (that is why this goal was picked at all), the tailor sold no cloth
        // and no bolt, and there is no flock this character would walk to.
        // Stand down and say so, rather than leaving the planner to re-pick
        // HEAL / REPLACE_EQUIPMENT / GET_FOOD in a ten-second ring the way
        // Hector did at 00:00:47-00:01:07 (2026-09-07).
        bandageTrips_ = 0;
        bandageBoltsOut_ = false;
        return BlockNeed(GoalKind::MakeBandages, life::NeedKind::NeedMakeBandages,
                         life::BlockScope::Session,
                         Fmt2("no bandages to buy, no cloth or bolt on any "
                              "counter, and no flock within %d tiles of %s "
                              "(%d,%d)", kMaxPastureTilesFromHome, anchorWhat,
                              anchorX, anchorY).c_str(),
                         kNoBandageCooldownMs, obs.nowMs);
    }
    if (++bandageTrips_ > kMaxBandageTrips) {
        const std::string why =
            Fmt2("no sheep found after %d trips to the pastures",
                 bandageTrips_ - 1);
        bandageTrips_ = 0;
        bandageBoltsOut_ = false;
        // Window, not Session: a flock walks back, and so does a shelf.
        return BlockNeed(GoalKind::MakeBandages, life::NeedKind::NeedMakeBandages,
                         life::BlockScope::Window, why.c_str(),
                         kNoBandageCooldownMs, obs.nowMs);
    }
    std::stable_sort(nearby.begin(), nearby.end(), [&](usize a, usize b) {
        return TileDist(anchorX, anchorY, pastures[a].x, pastures[a].y) <
               TileDist(anchorX, anchorY, pastures[b].x, pastures[b].y);
    });
    const Pasture& p =
        pastures[nearby[static_cast<usize>(bandageTrips_ - 1) % nearby.size()]];
    LogLine("bandages: no sheep in sight -- walking to the flock of %d at "
            "%d,%d, %d tiles off, nearest to %s (trip %d)", p.count, p.x, p.y,
            TileDist(obs.x, obs.y, p.x, p.y), anchorWhat, bandageTrips_);
    travelInFlight_ =
        client.TravelToPoint(p.x, p.y, std::max(4, p.radius / 2), "pasture");
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

namespace {
// WHAT A WOOL-CHAIN SHORTFALL COSTS IN SHEEP.
//
// The need names one link of the chain -- "20 x i_yarn_ball short" -- and the
// only thing a pasture can supply is wool. Converting one into the other at the
// shard's own rates is what lets the character know when it is done shearing:
// without it the goal left the flock after a single sheep and walked ~880 tiles
// to a spinning wheel carrying 1 wool, which is 3 of the 20 yarn it came for
// (artifacts/tailor_cannot_buy_now_2026-09-02.md, downstream defect 1).
//
// Rates are the server's, not ours: wool -> 3 yarn (CClientTarg.cpp:2053),
// 4 yarn -> 1 bolt (:2230-2245), 1 bolt -> 50 cloth (:2147). Rounding is
// always UP, because half a bolt weaves nothing.
i32 WoolForShortfall(const char* item, i32 qty) {
    if (!item || qty <= 0) return 0;
    if (std::strcmp(item, "i_wool") == 0) return qty;
    i32 yarn = 0;
    if (std::strcmp(item, "i_yarn_ball") == 0) {
        yarn = qty;
    } else if (std::strcmp(item, "i_cloth_bolt") == 0) {
        yarn = qty * kYarnPerBolt;
    } else if (std::strcmp(item, "i_cloth") == 0) {
        yarn = ((qty + kClothPerBolt - 1) / kClothPerBolt) * kYarnPerBolt;
    } else {
        return 0;                       // not a link this pasture can supply
    }
    return (yarn + kYarnPerWool - 1) / kYarnPerWool;
}
}  // namespace

// ---------------------------------------------------------------------------
// MAKING CLOTH.
//
// Owner ruling, 2026-09-02: "buy cloth from players first ... otherwise GATHER
// IT: sheep -> shear (bladed item) -> wool -> spinning wheel -> yarn -> loom ->
// bolt of cloth -> scissors -> cloth. Never buy cloth/thread/yarn from NPCs."
//
// This is the same five gestures DoMakeBandages walks and it deliberately does
// NOT share its body: the two stop at different places (bandages vs cloth),
// they are damped by different needs, and MakeBandages carries a fighter's
// looted-clothing shortcut that a tailor must not take -- shredding a shirt it
// could have sold. What IS shared is the mechanics, and both cite the same
// engine lines.
//
// MEASURED CHAIN NUMBERS, from Source-X and re-read for this change
// (docs/M3_7_RESOURCE_ECONOMY.md section 7 agrees):
//
//   blade on a woolly sheep  -> 1 wool   CClientTarg.cpp:1880 (CREID_SHEEP),
//                                        reached from case IT_WEAPON_SWORD /
//                                        _AXE / _FENCE, NOT from IT_SCISSORS
//   wool on a spinning wheel -> 3 yarn   CClientTarg.cpp:2053 (case IT_WOOL)
//   yarn on a loom           -> 1 bolt   CClientTarg.cpp:2186; the loom holds
//                                        4 and takes them in ONE gesture --
//                                        ConsumeAmount(iNeed) at :2235 eats up
//                                        to four from the stack at once, so a
//                                        stack of 4 yarn is one click, not four
//   scissors on the bolt     -> 50 cloth CClientTarg.cpp:2147 ConvertBolttoCloth
//
// KNOWN BLOCKER, recorded and not worked around: every stock Tailoring recipe
// on this runtime reads `RESOURCES=<n> i_cloth,1 i_thread`
// (items/i_provisions_clothing.scp:47, :71, :93, ...), and THREAD comes from
// COTTON, not from wool -- one cotton spins to six thread
// (CClientTarg.cpp:2078). So this loop produces CLOTH and only cloth, which is
// what the recipes need most of and the only half a wool chain can supply.
// Sewing remains blocked on thread until a cotton source is proven. See
// artifacts/tailor_loop_2026-09-02.md.
//
// EVERY STEP IS MEASURED BY AN INVENTORY DELTA. Issuing a double-click is not
// the same as the server honouring it: the wheel and the loom answer with a
// SysMessage and no menu, so there is no confirmation packet to wait on, and
// claiming progress for the gesture is precisely how four goals in this project
// ended up spinning (goals-that-spin). Three gestures in a row that move
// nothing stand the goal down.
// WALK UP TO THE STATION. The same shape as the forge approach in DoSmelt --
// route to a walkable tile BESIDE the station rather than onto its own solid
// tile, and count approaches so a wheel behind a counter costs two walks and
// not a session ("if it is unreachable then it should be 1 try max 2", project
// owner, 2026-09-02). Returns true only when the click is worth sending.
bool Runner::ReachStation(Client& client, const Observation& obs, u32 station,
                          const char* what) {
    i32 stx = 0, sty = 0;
    i8  stz = 0;
    if (!client.WorldItemPosition(station, &stx, &sty, &stz)) {
        // It was found by graphic a moment ago, so this is a cache race, not a
        // reason to refuse. Let the click go and let the server judge it.
        return true;
    }
    const i32 d = TileDist(obs.x, obs.y, stx, sty);
    if (d <= kStationReach) {
        if (station == clothStationSerial_) clothStationApproaches_ = 0;
        return true;
    }
    if (client.TravelBusy()) return false;

    if (station == clothStationSerial_) {
        if (++clothStationApproaches_ >= 2) {
            LogLine("cloth: cannot get within %d tiles of the %s at %d,%d after "
                    "%d tries -- striking it off", kStationReach, what, stx, sty,
                    clothStationApproaches_);
            clothDeadStations_.push_back(station);
            if (clothDeadStations_.size() > 8)
                clothDeadStations_.erase(clothDeadStations_.begin());
            clothStationSerial_ = 0;
            clothStationApproaches_ = 0;
            travelInFlight_ = false;
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
    } else {
        clothStationSerial_ = station;
        clothStationApproaches_ = 1;
    }

    i32 standX = 0, standY = 0;
    bool haveStand = false;
    static const int kdx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int kdy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int i = 0; i < 8 && !haveStand; ++i) {
        const i32 tx = stx + kdx[i], ty = sty + kdy[i];
        if (!client.TileIsWalkable(tx, ty, stz)) continue;
        standX = tx; standY = ty; haveStand = true;
    }
    if (!haveStand) {
        LogLine("cloth: no walkable tile beside the %s at %d,%d -- striking it "
                "off", what, stx, sty);
        clothDeadStations_.push_back(station);
        if (clothDeadStations_.size() > 8)
            clothDeadStations_.erase(clothDeadStations_.begin());
        clothStationSerial_ = 0;
        clothStationApproaches_ = 0;
        travelInFlight_ = false;
        nextActionMs_ = obs.nowMs + 500;
        return false;
    }

    LogLine("cloth: the %s at %d,%d is %d tiles off -- standing at %d,%d",
            what, stx, sty, d, standX, standY);
    travelInFlight_ = client.TravelToPoint(standX, standY, 0, what);
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

// See the declaration in Runner.h. The arithmetic itself moved to
// life::BandageSaleTarget (life/Needs.cpp) so the need that raises
// BANDAGES_FOR_SALE and the handler that ends it stop on the same number.
i32 Runner::BandageSaleTarget() const {
    return life::BandageSaleTarget(state_.memory, needCfg_.craftBatch);
}

// See the declaration in Runner.h.
bool Runner::CutClothForSale(Client& client, const Observation& obs,
                             i32 cloth) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return false;
    // ONLY A LIFE WHOSE CATALOGUE SAYS BANDAGES ARE ITS PRODUCT. The fighters
    // run this same chain for wool income (HARVEST_WOOL) and their cloth is
    // already spoken for -- it goes to a tailor.
    bool sells = false;
    for (const std::string& made : me->produces)
        if (made == "i_bandage") { sells = true; break; }
    if (!sells) return false;

    const i32 target = BandageSaleTarget();
    const i32 held = static_cast<i32>(client.BackpackItemCount(kBandage));
    if (held >= target) return false;

    // NEVER THE BATCH'S OWN CLOTH. The bench comes first: this runs only on
    // what is left above the sitting this life is funded for, so a tailor can
    // still sew the robe she gathered the wool for. Two bounds, and the goal
    // finishes normally the moment either bites.
    const i32 keep = std::max<i32>(1, needCfg_.craftBatch);
    if (cloth <= keep) return false;

    const u32 scissors = client.FindBackpackItemByGraphic(kScissorsGraphic);
    if (!scissors) return false;      // step 1 below buys a pair; not here.
    const u32 piece = client.FindBackpackItemByGraphic(kClothGraphic);
    if (!piece) return false;

    LogLine("bandages_for_sale: cutting cloth (%d bandages held, target %d, "
            "%d cloth with %d kept for the bench)", held, target, cloth, keep);
    client.ActionUseItemOn(scissors, piece);
    planner_.NoteProgress();
    nextActionMs_ = obs.nowMs + 2500;
    return true;
}

// ---------------------------------------------------------------------------
// CUTTING THE CLOTH SURPLUS UP FOR SALE.
//
// "banked finished cloth beyond keep IS the surplus the tailor should turn
// into bandages" (lead brief, 2026-09-07), and the 4013ad5 intent was that
// tailors supply fighters. CutClothForSale already did the cutting; what it
// could not do was reach the cloth, because it only ever ran from MAKE_CLOTH's
// "the batch is covered" exit and only ever counted the PACK. A tailor's
// finished cloth is in the BOX -- Aelia: 280 banked, 10 carried, and MAKE_CLOTH
// still shearing (artifacts/gate_bandage20_20260907/).
//
// So this goal is the fetch. Its whole content is: get the box open, take out
// the size of cut life::ClothCuttableForSale asked for, cut it, put the stock
// above the sale target back in the box, stand down. ONE withdraw-and-cut batch
// per run, and a cooldown on every exit -- the goal's own need is silenced by
// its own success (the shelf fills), so nothing else would stop a re-pick.
//
// The bench's keep survives in the BOX, not in the pack: a scissors gesture
// deletes the whole targeted stack (Source-X CClientTarg.cpp:2152-2179), so
// there is no such thing as cutting part of what is carried.
bool Runner::DoMakeBandagesForSale(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        planner_.Cooldown(GoalKind::MakeBandagesForSale,
                          obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "no profession", obs.nowMs);
        return false;
    }

    const i32 keep   = std::max<i32>(1, needCfg_.craftBatch);
    const i32 target = BandageSaleTarget();
    const i32 held   = static_cast<i32>(client.BackpackItemCount(kBandage));

    // THE SHELF IS FULL. The one exit that is a success, and it is also the
    // condition that silences the need -- so the cooldown here is belt and
    // braces rather than the thing doing the work.
    if (held >= target) {
        LogLine("bandages_for_sale: %d bandages is the sale stock (target %d) "
                "-- the market's turn now", held, target);
        saleCutMade_ = false;
        saleClothTaken_ = false;
        planner_.Cooldown(GoalKind::MakeBandagesForSale,
                          obs.nowMs + kBandageSaleRestMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // NO SCISSORS, NO TRADE. DoMakeBandages buys a pair for a fighter who has
    // none, because for a fighter this is the difference between healing and
    // not. For a tailor it is a stock errand, so it says so and rests instead
    // of spending the bench's money on a detour.
    const u32 scissors = client.FindBackpackItemByGraphic(kScissorsGraphic);
    if (!scissors) {
        return BlockNeed(GoalKind::MakeBandagesForSale,
                         life::NeedKind::NeedBandagesForSale,
                         life::BlockScope::Window,
                         "no scissors in the pack to cut cloth with",
                         kNoBandageCooldownMs, obs.nowMs);
    }

    i32 packCloth = 0;
    FindBackpackItemByName(client, "i_cloth", &packCloth);
    const i32 bankCloth = market::QtyOf(obs.bank, "i_cloth");

    // 1. CLOTH IN HAND ABOVE THE BENCH'S KEEP -- cut it, no trip needed.
    //    Shares CutClothForSale with MAKE_CLOTH's exit, so there is one cutting
    //    gesture and one "bandages_for_sale: cutting cloth" line in the code.
    if (packCloth > keep) {
        if (CutClothForSale(client, obs, packCloth)) {
            saleCutMade_ = true;
            return false;
        }
        // It refused for a reason of its own (catalogue, target, scissors).
        // Nothing here can improve on that, so rest.
        LogLine("goal_failed=BANDAGES_FOR_SALE reason=\"%d cloth carried but "
                "the cut was refused\"", packCloth);
        planner_.Cooldown(GoalKind::MakeBandagesForSale,
                          obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "the cut was refused", obs.nowMs);
        return false;
    }

    // 2. THE BATCH IS CUT. One withdraw-and-cut per run: the withdrawal
    //    happened, the cloth went under the scissors, and the pack is back down
    //    to the keep. Bank whatever is above the sale target while the box is
    //    still open, then stand down -- a second batch is the NEXT pick's, and
    //    the need will still be there if it is warranted.
    if (saleCutMade_) {
        if (BankSaleBandages(client, obs, held, target)) return false;
        LogLine("bandages_for_sale: the batch is cut -- %d of %d bandages on "
                "the shelf, %d cloth left in the box for the bench",
                held, target, bankCloth);
        saleCutMade_ = false;
        saleClothTaken_ = false;
        planner_.Cooldown(GoalKind::MakeBandagesForSale,
                          obs.nowMs + kBandageSaleRestMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // ONE WITHDRAWAL PER RUN, and this one has already been made without
    // putting enough in hand to cut. Asking the box again on the next tick is
    // how a goal spins; the need is still true, so the NEXT pick may try again
    // after the rest.
    if (saleClothTaken_) {
        saleClothTaken_ = false;
        return BlockNeed(GoalKind::MakeBandagesForSale,
                         life::NeedKind::NeedBandagesForSale,
                         life::BlockScope::Window,
                         Fmt2("the withdrawal left %d cloth in the pack, still "
                              "not above the bench's keep of %d",
                              packCloth, keep).c_str(),
                         kBandageSaleRestMs, obs.nowMs);
    }

    // 3. THE CLOTH IS IN THE BOX. Same size the need armed on, so the trip is
    //    never made for a cut that turns out not to exist.
    const i32 cut = life::ClothCuttableForSale(packCloth, bankCloth, keep,
                                               target - held);
    const i32 take = cut - packCloth;
    if (take <= 0) {
        return BlockNeed(GoalKind::MakeBandagesForSale,
                         life::NeedKind::NeedBandagesForSale,
                         life::BlockScope::Window,
                         Fmt2("%d cloth carried and %d in the box: nothing "
                              "above the bench's keep of %d to cut",
                              packCloth, bankCloth, keep).c_str(),
                         kBandageSaleRestMs, obs.nowMs);
    }

    // Get to a counter and open the box. The travel half is DoBank's -- it is
    // the only code that knows which bank a life should walk to -- so this
    // hands the whole "not at a bank yet" case to BANK rather than growing a
    // second, worse copy of it.
    if (!obs.atBank) {
        if (!NearAnyBank(client, obs)) {
            return HandOff(GoalKind::MakeBandagesForSale, GoalKind::Bank,
                           kBandageSaleTripMs,
                           "the cloth to cut is in the bank box", obs.nowMs);
        }
        if (client.TravelBusy()) return false;
        if (!bankErrand_.Running()) bankErrand_.Begin();
        bankErrand_.SetAtKnownBank(true);
        const life::BankErrandResult br = bankErrand_.Tick(client, obs);
        LogErrandReason("bandages_for_sale", br.why.c_str(), obs.nowMs);
        if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
            nextActionMs_ = obs.nowMs + br.delayMs;
        if (br.status == life::ActivityStatus::Success) {
            planner_.NoteProgress();
            return false;             // the fetch runs next tick
        }
        if (!life::IsTerminal(br.status)) {
            if (br.acted) planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        LogLine("goal_failed=BANDAGES_FOR_SALE reason=\"no banker opened a box "
                "for the cloth (%s)\"", br.why.c_str());
        bankErrand_.Cancel();
        planner_.Cooldown(GoalKind::MakeBandagesForSale,
                          obs.nowMs + kBandageSaleRestMs);
        planner_.Finish(false, "no banker answered", obs.nowMs);
        return false;
    }

    // A LIFT IS AN ACTION AND YOU STAND STILL TO MAKE ONE -- the same rule the
    // market withdrawal pays for in Economy.cpp: the box only answers from the
    // tile it was opened on (Source-X CCharStatus.cpp:1063-1069).
    if (client.TravelBusy()) return false;
    if (!client.BankOpenTileHeld()) {
        LogLine("bandages_for_sale: the box was opened at (%d,%d) and we are "
                "at (%d,%d) -- opening it again from here",
                client.BankOpenX(), client.BankOpenY(), obs.x, obs.y);
        client.ForgetBankContainer();
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 1000;
        return false;
    }
    if (SettleBankItemMove(client, obs)) return false;

    // BY NAME, NOT BY GRAPHIC: `bankCloth` was hue-resolved out of obs.bank,
    // so the serial has to be found the same way (S1).
    i32 inBox = 0;
    const u32 stack = FindContainerItemByName(client, client.BankContainer(),
                                              "i_cloth", &inBox);
    if (!stack || inBox <= 0) {
        return BlockNeed(GoalKind::MakeBandagesForSale,
                         life::NeedKind::NeedBandagesForSale,
                         life::BlockScope::Window,
                         Fmt2("the box was open and held no cloth (remembered "
                              "%d)", bankCloth).c_str(),
                         kBandageSaleRestMs, obs.nowMs);
    }
    const i32 moving = std::min(take, inBox);
    LogLine("bandages_for_sale: withdrawing %d cloth to cut (%d in the box, "
            "%d stays for the bench, %d of %d bandages held)",
            moving, inBox, keep, held, target);
    saleClothTaken_ = true;
    IssueBankItemMove(client, obs, stack, static_cast<u16>(moving),
                      client.BackpackSerial());
    return false;
}

// THE STOCK ABOVE THE SALE SHELF GOES BACK IN THE BOX, while it is open.
//
// Only while it is open: a bandage deposit is not worth a bank trip of its own,
// and the ordinary BANK goal already carries surplus `produces` away. Returns
// true when it issued a move, i.e. when the caller should come back next tick.
bool Runner::BankSaleBandages(Client& client, const Observation& obs, i32 held,
                             i32 target) {
    if (held <= target) return false;
    const u32 box = client.BankContainer();
    if (!box || !obs.atBank) return false;
    if (client.TravelBusy() || client.ActionBusy()) return false;
    if (!client.BankOpenTileHeld()) return false;
    if (SettleBankItemMove(client, obs)) return true;
    i32 carried = 0;
    const u32 stack = FindBackpackItemByName(client, "i_bandage", &carried);
    if (!stack || carried <= target) return false;
    const i32 moving = carried - target;
    LogLine("bandages_for_sale: banking %d bandages, keeping %d on the sale "
            "shelf", moving, target);
    IssueBankItemMove(client, obs, stack, static_cast<u16>(moving), box);
    return true;
}

bool Runner::DoMakeCloth(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    LoadPastures(client.DataDir());

    const i32 wool  = static_cast<i32>(client.BackpackItemCount(kWoolGraphic));
    const i32 yarn  = static_cast<i32>(client.BackpackItemCount(kYarnGraphic));
    const i32 bolts = static_cast<i32>(client.BackpackItemCount(kClothBoltGraphic));
    const i32 cloth = static_cast<i32>(client.BackpackItemCount(kClothGraphic));

    // TWO LIVES RUN THIS CHAIN. A tailor (MAKE_CLOTH) shears for her own
    // batch and stops when the batch has its cloth. A fighter (HARVEST_WOOL)
    // runs it for INCOME -- owner ruling 2026-09-02: "add this part only to
    // warrior so they can sell cloth ... tailor doesn't have attack skill ...
    // not wool, cloth itself" -- so it also kills and carves each sheared
    // sheep (step 4a), and it is done when the whole load is cloth: no wool,
    // yarn or bolt left and some cloth in the pack. The cloth is then
    // Surplus() (i_cloth is in the fighter's `produces`) and sells to a
    // tailor's WTB through the ordinary player-first market.
    const GoalKind self = planner_.Current().kind;
    const bool fighter = self == GoalKind::HarvestWool;

    // NO WOOL MEANS THE LOAD IS SPUN AND THIS TRIP IS OVER. The next visit to
    // a flock starts a fresh one, free to shear to capacity again.
    if (wool == 0) clothHeadingToWheel_ = false;

    // DID THE LAST GESTURE ACTUALLY DO ANYTHING?
    if (clothWoolBefore_ >= 0 &&
        obs.nowMs - clothMarkMs_ > kClothMarkStaleMs) {
        // The turn went elsewhere and came back. Judge nothing on numbers
        // this old; take a fresh gesture and measure that instead.
        clothWoolBefore_ = -1;
    }
    if (clothWoolBefore_ >= 0) {
        const bool moved = wool != clothWoolBefore_ || yarn != clothYarnBefore_ ||
                           bolts != clothBoltBefore_ || cloth != clothClothBefore_;
        if (moved) {
            LogLine("cloth: wool %d->%d yarn %d->%d bolts %d->%d cloth %d->%d",
                    clothWoolBefore_, wool, clothYarnBefore_, yarn,
                    clothBoltBefore_, bolts, clothClothBefore_, cloth);
            planner_.NoteProgress();
            clothEmptySteps_ = 0;
        } else if (++clothEmptySteps_ >= kMaxEmptyClothSteps) {
            LogLine("goal_failed=%s reason=\"%d gestures in a row moved "
                    "nothing (wool %d yarn %d bolts %d cloth %d)\"", GoalKindName(self),
                    clothEmptySteps_, wool, yarn, bolts, cloth);
            clothEmptySteps_ = 0;
            clothWoolBefore_ = -1;
            planner_.Cooldown(self, obs.nowMs + kNoClothCooldownMs);
            planner_.Finish(false, "the chain moved nothing", obs.nowMs);
            return false;
        }
        clothWoolBefore_ = -1;
    }

    // ENOUGH? The honest test is the one the need asked: is the batch this
    // life wants to make still short of cloth? Not a cloth count of our own
    // invention -- the recipe decides how much is enough, and it differs by
    // garment (a bandana takes 2, a cape 14).
    //
    // The same answer also says the LEAST wool worth coming home with: the
    // shortfall converted into sheep, floor one -- a life with no recipe in
    // view still profits from a pile of wool. It is NOT a ceiling any more;
    // shearing runs to carry capacity (step 3) or to a bare flock (step 4b),
    // whichever comes first, because the walk back to a wheel costs the same
    // whatever is in the pack.
    i32 woolTarget = 1;
    const prof::Profession* me = needCfg_.profession;
    if (me) {
        const CraftIntent intent =
            ChooseCraft(*me, obs, needCfg_.craftBatch, &craftFocus_);
        bool stillShort = false;
        for (const prod::Ingredient& ing : intent.missing) {
            if (!IsWoolChainMaterial(ing.item)) continue;
            stillShort = true;
            woolTarget = std::max<i32>(1, WoolForShortfall(ing.item, ing.qty));
            break;
        }
        // ...OR THE THING BEING MADE *IS* THE CLOTH, and the yarn for it is
        // already carried. Same empty-missing-list blind spot the NeedCloth
        // clause in Needs.cpp closes: when the chosen output is itself a
        // wool-chain item, four yarn in the pack make the recipe's missing list
        // empty, which reads here as "enough for the batch" -- so the goal
        // would Finish(true) with the yarn unwoven, before it ever reached the
        // loom (artifacts/cloth_walkup_bolt_route_capacity_2026-09-02.md
        // section 4, consequence 2). Weaving is the work; unwoven yarn is the
        // proof it is not done. Nothing to shear in this state, so the wool
        // minimum drops to one and step 2 takes the turn.
        if (!stillShort && intent.item && IsWoolChainMaterial(intent.item) &&
            yarn >= kYarnPerBolt) {
            i32 held = wool;
            if (std::strcmp(intent.item, "i_cloth_bolt") == 0)      held = bolts;
            else if (std::strcmp(intent.item, "i_cloth") == 0)      held = cloth;
            else if (std::strcmp(intent.item, "i_yarn_ball") == 0)  held = yarn;
            const i32 want = std::max<i32>(1, needCfg_.craftBatch);
            if (held < want) {
                stillShort = true;
                woolTarget = 1;
                // Own errand tag: the "cloth" sentinel is held by the two
                // walk-to-the-tailor lines below, and two reasons alternating
                // under one tag defeat the repeat throttle.
                LogErrandReason("weaving",
                                Fmt2("%d yarn carried and %d of %d %s made -- "
                                     "the loom before anything else", yarn, held,
                                     want, intent.item).c_str(),
                                obs.nowMs);
            }
        }
        if (fighter) {
            // The load is the target, not a batch: the trip is short until
            // the pack has been to the flock and everything it brought back
            // is cloth. woolTarget only labels the log lines here.
            stillShort = cloth == 0 || wool > 0 || yarn > 0 || bolts > 0;
            woolTarget = std::max<i32>(woolTarget, 20);
        }
        if (!stillShort) {
            // THE BATCH IS COVERED -- SO THE SPARE CLOTH CAN BECOME STOCK.
            // A tailor's surplus cloth is worth more as bandages than as
            // cloth right now: the fleet's fighters empty every healer and vet
            // counter in town and have nowhere else to go
            // (docs/BANDAGE_SUPPLY_SPEC.md section 1), and cutting needs no
            // Tailoring at all. Runs before the goal closes, one gesture per
            // tick, and stops at the sale target or at the bench's own keep --
            // whichever comes first -- so it cannot spin. What it makes is
            // `produces`, so the ordinary bank and WTS machinery carries it
            // from here.
            if (!fighter && CutClothForSale(client, obs, cloth)) return false;
            if (fighter)
                LogLine("wool_income: %d cloth cut and nothing left on the "
                        "chain -- the load is ready to sell (%d sheep carved "
                        "this trip)", cloth, clothKillsThisTrip_);
            else
                LogLine("cloth: %d cloth and %d bolts is enough for the batch",
                        cloth, bolts);
            clothTrips_ = 0;
            clothPastureIdx_ = 0;
            clothShornSheep_.clear();
            clothFlockBareMs_ = 0;
            clothKillSheep_ = 0; clothCarveCorpse_ = 0; clothCarved_ = false;
            clothKillsThisTrip_ = 0;
            clothHeadingToWheel_ = false;
            clothStationSerial_ = 0;
            clothStationApproaches_ = 0;
            planner_.Finish(true, nullptr, obs.nowMs);
            return true;
        }
    }

    // 1. BOLT -> CLOTH. Runs first: it is the step that actually produces the
    //    thing the recipe wants, and 50 cloth per bolt is the whole yield of
    //    the chain up to here.
    const u32 scissors = client.FindBackpackItemByGraphic(kScissorsGraphic);
    if (bolts > 0 && scissors) {
        const u32 bolt = client.FindBackpackItemByGraphic(kClothBoltGraphic);
        LogLine("cloth: cutting a bolt into cloth (%d cloth so far)", cloth);
        client.ActionUseItemOn(scissors, bolt);
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }
    if (bolts > 0 && !scissors) {
        // A bolt with nothing to cut it is a shopping errand, not a failure.
        // Scissors are ITEMNEWBIE in every starter kit and cheap at a tailor.
        if (obs.gold >= kScissorsMoney) {
            BuyScrollFrom(client, obs, "tailor", wm::Service::Tailor,
                          kScissorsGraphic, false, 1, "a pair of scissors",
                          self);
            return false;
        }
        LogLine("goal_failed=%s reason=\"%d bolts and no scissors, and "
                "only %d gold to buy a pair with\"", GoalKindName(self), bolts, obs.gold);
        return HandOff(self, GoalKind::EarnGold,
                       kNoClothCooldownMs, "no scissors and no money",
                       obs.nowMs);
    }

    // 2. YARN -> LOOM -> BOLT. The loom takes up to four from the stack in one
    //    gesture and only yields a bolt when it has all four, so fewer than
    //    four is not worth walking to a loom for -- it would consume the yarn
    //    into the loom's own store and hand back nothing.
    if (yarn >= kYarnPerBolt) {
        const u32 loom = FindLoom(client, kStationSight, clothDeadStations_);
        if (!loom) {
            // Throttled: the walk is minutes long and this branch is re-entered
            // every tick of it. Saying the same sentence 110 times is not
            // evidence, it is noise that buries the lines that are.
            LogErrandReason("cloth",
                            Fmt2("%d yarn and no loom in sight -- going to the "
                                 "tailor, where the looms are", yarn).c_str(),
                            obs.nowMs);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToPlace(kTailorWorkshopPlace);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        if (!ReachStation(client, obs, loom, "loom")) return false;
        const u32 spun = client.FindBackpackItemByGraphic(kYarnGraphic);
        LogLine("cloth: weaving %d yarn at the loom", yarn);
        client.ActionUseItemOn(spun, loom);
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 3. WOOL -> WHEEL -> YARN. Three yarn per wool, so once a wheel is at hand
    //    this runs until the wool is gone rather than until some yarn target is
    //    hit.
    //
    //    LEAVING THE FLOCK IS THE EXPENSIVE PART. The wheels are in a town and
    //    the sheep are not: Aelia's walk from the Yew flock back to the Britain
    //    tailor was 22 legs, ~880 tiles, the whole of a five-minute session
    //    (artifacts/tailor_cannot_buy_now_2026-09-02.md). Doing that carrying
    //    one wool buys three yarn of the twenty the batch wants.
    //
    //    SO THE TRIP IS MADE WHEN THE PACK IS AS FULL AS ANY OTHER GATHERER
    //    CARRIES, or when the flock has nothing left to give (step 4b) --
    //    "they should work till carry capacity" (project owner, 2026-09-02).
    //    The batch's wool target is the MINIMUM worth having, not the ceiling:
    //    a character that stopped at seven wool walked the same 880 tiles for
    //    a fifth of the load it could have carried.
    if (wool > 0) {
        const u32 wheel = FindSpinWheel(client, kStationSight, clothDeadStations_);
        if (!clothHeadingToWheel_ &&
            obs.WeightFraction() >= kGathererPackFullFrac) {
            clothHeadingToWheel_ = true;
            LogLine("cloth: the pack is %.0f%% full with %d wool (the batch "
                    "wanted %d) -- that is a load, taking it to the wheel",
                    obs.WeightFraction() * 100.0, wool, woolTarget);
        }
        if (!wheel && !clothHeadingToWheel_) {
            // Room left in the pack and no wheel here: keep shearing. The
            // exits from the flock are a full pack (above) and a bare one
            // (step 4b), never a batch-sized wool count. Fall through.
        } else if (!wheel) {
            LogErrandReason("cloth",
                            Fmt2("%d wool (batch wanted %d) and no spinning "
                                 "wheel in sight -- going to the tailor", wool,
                                 woolTarget).c_str(),
                            obs.nowMs);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToPlace(kTailorWorkshopPlace);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        } else {
        if (!ReachStation(client, obs, wheel, "spinning wheel")) return false;
        const u32 raw = client.FindBackpackItemByGraphic(kWoolGraphic);
        LogLine("cloth: spinning wool into yarn (%d wool, %d yarn)", wool, yarn);
        client.ActionUseItemOn(raw, wheel);
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
        }
    }

    // 4. A SHEEP -> WOOL. Free, and the start of everything.
    const u32 blade = FindBlade(client);
    if (!blade) {
        // Not a failure of the chain -- a missing tool, which is somebody
        // else's errand. Say which, so the log names the fix.
        LogLine("goal_failed=%s reason=\"nothing bladed is carried; a "
                "sheep is sheared with a weapon or a knife, never with "
                "scissors\"", GoalKindName(self));
        planner_.Cooldown(self, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no blade to shear with", obs.nowMs);
        return false;
    }

    // 4a. A SHORN SHEEP IS THREE MORE WOOL -- FOR A FIGHTER.
    //
    // Owner ruling 2026-09-02, verified live by the owner: "killed a sheep
    // after shearing, carved it, gave 3 wool". The shears take one wool
    // (hard-coded, CClientTarg.cpp:1883); the corpse carves as the body the
    // animal HAD before the shear (CItemCorpse.cpp:191 `_iPrev_id`), i.e.
    // c_sheep_woolly's 3 wool + 3 lamb legs. The carve output is added to the
    // CORPSE, not the pack (CCharUse.cpp:187), so: attack, wait for it to
    // die, carve its corpse with the blade, open it, take the wool. Four wool
    // per animal instead of one -- the flock is consumed, which is the real
    // supply pressure (spawner regrows one sheep per 5-10 min).
    //
    // A tailor never enters here: clothKillSheep_ is only set by a fighter's
    // shear (step 4). Every phase is bounded. A sheep that will not die in a
    // minute (it fled, the swings all missed) is written off and the next one
    // taken; a corpse that never shows or never opens likewise.
    if (clothKillSheep_) {
        constexpr i64 kKillTimeoutMs    = 60000;
        constexpr i64 kCarveTimeoutMs   = 15000;
        constexpr i64 kAttackReassertMs = 6000;
        i32 sx = 0, sy = 0; i8 sz = 0;
        const bool alive = client.MobilePosition(clothKillSheep_, &sx, &sy, &sz);
        if (!clothCarveCorpse_ && alive) {
            if (obs.nowMs - clothKillStartMs_ > kKillTimeoutMs) {
                LogLine("wool_income: the shorn sheep would not die in %ds -- "
                        "leaving it, next sheep",
                        static_cast<int>(kKillTimeoutMs / 1000));
                clothKillSheep_ = 0;
                client.ExitWarMode();
                return false;
            }
            if (!client.WarModeOn()) client.EnterWarMode();
            if (lastAttackOrderTarget_ != clothKillSheep_ ||
                obs.nowMs - lastAttackOrderMs_ >= kAttackReassertMs) {
                if (lastAttackOrderTarget_ != clothKillSheep_)
                    LogLine("wool_income: attacking the shorn sheep for its "
                            "carve wool");
                client.ActionAttack(clothKillSheep_);
                lastAttackOrderTarget_ = clothKillSheep_;
                lastAttackOrderMs_ = obs.nowMs;
            }
            const i32 d = TileDist(obs.x, obs.y, sx, sy);
            if (d > 1 && !client.GotoBusy())
                client.ActionGotoMobile(clothKillSheep_, 1);
            nextActionMs_ = obs.nowMs + 1200;
            return false;
        }
        if (!clothCarveCorpse_) {
            // Dead. Its corpse: by the 0xAF link first, by proximity second
            // (it died within a tile of us; the pasture may hold older
            // corpses further off).
            u32 corpse = client.CorpseOfMobile(clothKillSheep_);
            if (!corpse) corpse = client.FindWorldItemByGraphic(0x2006, 2);
            if (!corpse) {
                if (obs.nowMs - clothKillStartMs_ >
                    kKillTimeoutMs + kCarveTimeoutMs) {
                    LogLine("wool_income: the sheep died but no corpse showed "
                            "-- next sheep");
                    clothKillSheep_ = 0;
                    client.ExitWarMode();
                    return false;
                }
                nextActionMs_ = obs.nowMs + 1000;
                return false;
            }
            client.ExitWarMode();
            if (!ReachStation(client, obs, corpse, "sheep corpse")) return false;
            clothCarveCorpse_ = corpse;
            clothCarved_ = false;
            clothCorpseOpened_ = false;
            clothCarveMs_ = obs.nowMs;
            LogLine("wool_income: the sheep is down (corpse 0x%08X) -- carving "
                    "it", corpse);
            client.ActionUseItemOn(blade, corpse);
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        // Carved (or the carve was sent). Open the corpse and take the wool.
        if (obs.nowMs - clothCarveMs_ > kCarveTimeoutMs) {
            LogLine("wool_income: the corpse gave up no wool in %ds -- next "
                    "sheep", static_cast<int>(kCarveTimeoutMs / 1000));
            clothKillSheep_ = 0; clothCarveCorpse_ = 0;
            return false;
        }
        // Sphere only tells a client about a container's new contents while
        // that client has it open (the carve's 0x25 never came, 2026-09-03
        // smoke), so the corpse is opened once after the carve regardless of
        // whether an earlier 0x3C already listed it.
        if (!clothCorpseOpened_ || !client.ContainerKnown(clothCarveCorpse_)) {
            clothCorpseOpened_ = true;
            client.ActionOpenContainer(clothCarveCorpse_);
            nextActionMs_ = obs.nowMs + 1200;
            return false;
        }
        const u16 woolGfx[] = {kWoolGraphic};
        const u32 pile =
            client.FindContainerItemByGraphic(clothCarveCorpse_, woolGfx, 1);
        if (pile) {
            u16 amount = 0;
            const usize n = client.ContainerItemCount(clothCarveCorpse_);
            for (usize i = 0; i < n; ++i) {
                u32 sr = 0; u16 g = 0, a = 0;
                if (client.ContainerItemAt(clothCarveCorpse_, i, &sr, &g, &a) &&
                    sr == pile) {
                    amount = a;
                    break;
                }
            }
            LogLine("wool_income: taking %d wool from the carved sheep (%d "
                    "carried)", amount ? amount : 1, wool);
            client.TakeFromContainer(pile, amount ? amount : 1);
            ++clothKillsThisTrip_;
            planner_.NoteProgress();
            clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
            clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
            clothMarkMs_ = obs.nowMs;
            clothKillSheep_ = 0; clothCarveCorpse_ = 0;
            nextActionMs_ = obs.nowMs + 1200;
            return false;
        }
        // Opened but no wool yet: the carve may still be resolving, or was
        // never sent to a corpse in reach. One re-send, then the timeout.
        if (!clothCarved_) {
            clothCarved_ = true;
            clothCorpseOpened_ = false;
            client.ActionUseItemOn(blade, clothCarveCorpse_);
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // A SHEEP THIS CHARACTER HAS ALREADY SHEARED IS NOT A SHEEP.
    //
    // Sphere flips it to CREID_SHEEP_SHORN (0x00DF) and starts the wool regrow
    // timer (CClientTarg.cpp:1886-1890, g_Cfg.m_iWoolGrowthTime), and answers a
    // second attempt with "wait for the wool to grow back" (:1895). The body
    // change normally drops it out of the body filter on its own; the exclude
    // list covers the tick before the update lands. Refusal means TAKE THE NEXT
    // SHEEP -- the old code cleared the list and walked to another pasture,
    // which is why a flock of fifteen yielded one wool.
    const u32 sheep =
        client.NearestMobileWithBody(kSheepBody, 12, clothShornSheep_);
    if (sheep) {
        clothFlockBareMs_ = 0;
        i32 sx = 0, sy = 0; i8 sz = 0;
        if (client.MobilePosition(sheep, &sx, &sy, &sz)) {
            const i32 d = TileDist(obs.x, obs.y, sx, sy);
            if (d > 1) {
                LogLine("cloth: a sheep %d tiles away -- walking up to it", d);
                travelInFlight_ = client.TravelToEntity(sheep, 1);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        LogLine("cloth: shearing a sheep (%d wool carried, want %d)", wool,
                woolTarget);
        clothTrips_ = 0;
        clothPastureIdx_ = 0;
        client.ActionUseItemOn(blade, sheep);
        clothShornSheep_.push_back(sheep);
        if (fighter) {
            // The shear lands first (3 s below), then step 4a puts the
            // animal down for the other three wool.
            clothKillSheep_ = sheep;
            clothKillStartMs_ = obs.nowMs + 3000;
            clothCarveCorpse_ = 0;
            clothCarved_ = false;
        }
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 4b. THE FLOCK THIS CHARACTER IS STANDING IN HAS NOTHING LEFT.
    //
    // Only reachable after at least one shear here, which is what distinguishes
    // "worked this flock out" from "have not arrived yet". Waiting for the
    // REGROW is not an option -- it is thirty minutes (runtime/sphere.ini:399
    // WoolGrowthTime=30), longer than the session -- but the flock roams, so a
    // bounded minute of standing still often produces another animal. When the
    // bound is spent the character leaves with what it has rather than starting
    // a second cross-map trip for the balance.
    if (!clothShornSheep_.empty() && !client.TravelBusy()) {
        if (clothFlockBareMs_ == 0) {
            clothFlockBareMs_ = obs.nowMs;
            LogLine("cloth: every sheep in reach is shorn (%d wool of %d) -- "
                    "wool regrows in 30 min, so waiting %ds for one to wander "
                    "over, not for the regrow", wool, woolTarget,
                    static_cast<int>(kShornFlockWaitMs / 1000));
        }
        if (obs.nowMs - clothFlockBareMs_ < kShornFlockWaitMs) {
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        clothFlockBareMs_ = 0;
        clothShornSheep_.clear();
        if (wool > 0) {
            LogLine("cloth: the flock is shorn out -- taking %d wool (batch "
                    "wanted %d) to the wheel", wool, woolTarget);
            // The pack is not full and never will be here: latch the trip so
            // arriving in town with the wheel still out of item range does not
            // read as "room left, keep shearing" and send us back.
            clothHeadingToWheel_ = true;
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToPlace(kTailorWorkshopPlace);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
    }

    // 5. NO SHEEP IN SIGHT. Go where the save says they are.
    if (client.TravelBusy()) return false;
    // ARM B OF THE NEED/HANDLER CONTRACT (docs/NEED_HANDLER_CONTRACT.md).
    // The pasture table and kMaxPastureTilesFromHome are runner-private, so
    // NeedWoolIncome and NeedCloth cannot evaluate "is there a flock I would
    // walk to" for themselves -- which is why Hector picked HARVEST_WOOL
    // twice in fifteen minutes and lost two three-minute round trips to the
    // identical refusal (D4). The refusal is now recorded where the need can
    // read it, in these same words, for the rest of the session.
    const life::NeedKind flockNeed = fighter ? life::NeedKind::NeedWoolIncome
                                             : life::NeedKind::NeedCloth;
    const std::vector<Pasture>& pastures = Pastures();
    if (pastures.empty()) {
        return BlockNeed(self, flockNeed, life::BlockScope::Session,
                         "no pasture table -- run tools/pasturegen.py against "
                         "the world save",
                         kNoClothCooldownMs, obs.nowMs);
    }
    if (++clothTrips_ > kMaxClothTrips) {
        const std::string why =
            Fmt2("no sheep found after %d trips to the pastures",
                 clothTrips_ - 1);
        clothTrips_ = 0;
        clothPastureIdx_ = 0;
        // Window, not Session: a flock walks back. This is the one the world
        // undoes on its own, so the need is quiet for the restock window and
        // then free to ask again.
        return BlockNeed(self, flockNeed, life::BlockScope::Window, why.c_str(),
                         kNoClothCooldownMs, obs.nowMs);
    }
    // NEAREST FLOCK TO HOME FIRST, NOT BIGGEST AND NOT NEAREST TO HERE.
    //
    // pasturegen.py:108 sorts its rows by count, and this used to walk them in
    // file order, so every character in the world set off for the same 15-sheep
    // flock at 572,1096 regardless of where it stood. That is a 25-leg, ~1048
    // tile journey from Britain and it ate a whole five-minute session
    // (artifacts/tailor_cannot_buy_now_2026-09-02.md).
    //
    // Ranking by distance from where the character HAPPENS TO STAND then
    // produced the opposite failure: Amara, a Britain tailor left stranded up
    // at Yew by a failed errand, measured from Yew, chose the Yew farmland
    // flock and died on the way to it. "Yew is never a home; tailors gather
    // near Britain" (project owner, 2026-09-02). A player who lives in Britain
    // shears the Britain sheep whatever corner of the map today's mishap left
    // them in -- the walk home is the same walk either way, and it ends
    // somewhere they know.
    //
    // The anchor is this life's own seeded home bank (SeedNewbieKnowledge is
    // anchored on state_.homeCity), so nothing here names a city. With no home
    // knowledge yet, fall back to standing position -- the old behaviour.
    // stable_sort keeps the count order as the tie-break, so two equidistant
    // flocks are still taken biggest first.
    i32 anchorX = obs.x, anchorY = obs.y;
    const char* anchorWhat = "here";
    if (const KnownPlace* homeBank =
            state_.memory.BestPlace("common_knowledge_bank")) {
        anchorX = homeBank->x;
        anchorY = homeBank->y;
        anchorWhat = "home";
    }
    //
    // ONLY FLOCKS NEAR HOME. Wave 2026-09-04: clothPastureIdx_ was never reset
    // between goals, so Aelia and Wren (Britain tailors) walked the whole
    // table in order -- Britain, Yew x3, Jhelom, then the Delucia flock in the
    // Lost Lands -- and the Delucia route runs through Trinsic Passage
    // (a_trinsic_passage_level_2_1): seven deaths each, no wool. A tailor
    // shears the farmland next to their home city and nowhere else; when that
    // flock is bare the answer is the WTB fallback, not a cross-map hike.
    // The index now also resets whenever clothTrips_ does.
    std::vector<usize> order;
    for (usize i = 0; i < pastures.size(); ++i)
        if (TileDist(anchorX, anchorY, pastures[i].x, pastures[i].y) <=
            kMaxPastureTilesFromHome)
            order.push_back(i);
    if (order.empty()) {
        clothTrips_ = 0;
        clothPastureIdx_ = 0;
        // Arm B, as above: the distance rule lives in a runner-private table,
        // so the need is told the answer rather than left to re-ask.
        return BlockNeed(self, flockNeed, life::BlockScope::Session,
                         Fmt2("no pasture within %d tiles of %s (%d,%d) -- not "
                             "walking across the map for wool",
                             kMaxPastureTilesFromHome, anchorWhat, anchorX,
                             anchorY).c_str(),
                         kNoClothCooldownMs, obs.nowMs);
    }
    std::stable_sort(order.begin(), order.end(), [&](usize a, usize b) {
        return TileDist(anchorX, anchorY, pastures[a].x, pastures[a].y) <
               TileDist(anchorX, anchorY, pastures[b].x, pastures[b].y);
    });
    const Pasture& p =
        pastures[order[static_cast<usize>(clothPastureIdx_) % order.size()]];
    ++clothPastureIdx_;
    LogLine("cloth: no sheep in sight -- walking to the flock of %d at %d,%d, "
            "%d tiles off, nearest to %s (trip %d)", p.count, p.x, p.y,
            TileDist(obs.x, obs.y, p.x, p.y), anchorWhat, clothTrips_);
    travelInFlight_ = client.TravelToPoint(p.x, p.y, std::max(4, p.radius / 2),
                                           "pasture");
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

}  // namespace uo::life
