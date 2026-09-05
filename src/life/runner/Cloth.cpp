#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


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

    // HOW MANY IS ENOUGH IS THE CHARACTER'S NUMBER, NOT A CONSTANT.
    // needCfg_.bandageFull is resolved per life and per purse each planning
    // tick (ResolveConsumableThresholds), and for a life that hunts it sits
    // above the owner's floor of a hundred. kBandagesWanted survives only as
    // the fallback for a character with no profession.
    const i32 want = needCfg_.bandageFull > 0 ? needCfg_.bandageFull
                                              : kBandagesWanted;
    if (obs.bandages >= want) {
        LogLine("bandages: %d is enough to fight on", obs.bandages);
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

    // 2. BOLT -> CLOTH.
    if (const u32 bolt = client.FindBackpackItemByGraphic(kClothBoltGraphic)) {
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

    // 4b. NOTHING IN THE PACK TO WORK WITH, BUT THERE IS A PURSE: BUY CLOTH.
    //
    // Scissors on loose cloth give one bandage per cloth, engine-hardcoded and
    // with no skill check (Source-X CClientTarg.cpp:2135-2184), and loose
    // i_cloth is on the weaver's shelf at {3 38} and the tailor's at four
    // rows of {2 24} for 3 gp (tm_vend.scp:875-887, 899-966). That is the
    // only route to a hundred bandages in one sitting: a healer's shelf holds
    // at most twenty and refills once every ten minutes.
    //
    // NARROW ON PURPOSE. The standing ruling "never buy cloth/thread/yarn
    // from NPCs" (owner, 2026-09-02) is about a TAILOR's supply -- a crafter
    // must not buy the thing she makes, and DoMakeCloth below still obeys it
    // by gathering. This branch is a fighter buying a consumable input for
    // bandages when every counter in town is empty, which is why it is gated
    // on WantsToHunt and on being short of the fighting floor.
    const bool hunts =
        needCfg_.profession && WantsToHunt(*needCfg_.profession);
    if (hunts && obs.bandages < want && obs.gold >= kClothMaxPrice) {
        if (!bandageClothBuy_.Running()) {
            life::BuyRequest req;
            req.graphic = kClothGraphic;
            req.item = "loose cloth";
            // One cloth is one bandage, so the shortfall IS the order.
            req.desiredTotal = want - obs.bandages;
            req.minimumGoldReserve = 0;
            req.maxPricePerUnit = kClothMaxPrice;
            req.Sell("weaver", wm::Service::Tailor);
            req.Sell("tailor", wm::Service::Tailor);
            for (u32 drained : DrainedShelves(obs.nowMs)) req.Avoid(drained);
            bandageClothBuy_.Begin(req);
        }
        const life::ActivityTickResult r = bandageClothBuy_.Tick(client, obs);
        LogErrandReason("bandage cloth", r.reason, obs.nowMs);
        if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
            nextActionMs_ = obs.nowMs + r.delayMs;
        if (!life::IsTerminal(r.status)) {
            if (r.acted) {
                // A LEG THAT LANDED IS PROGRESS: reaching the shop, finding the
                // keeper, getting within reach. Only an ask that went unanswered
                // is a try. Hector (2026-09-05 14:40): trip 1, three scans, trip 2
                // = five attempts and the goal was abandoned before the second
                // counter was even reached.
                const bool legLanded = r.offerOpen ||
                    (r.reason && (std::strstr(r.reason, "found a") ||
                                  std::strstr(r.reason, "within reach") ||
                                  std::strstr(r.reason, "ARRIVED") ||
                                  std::strstr(r.reason, "the shop is open")));
                if (legLanded) planner_.NoteProgress();
                else planner_.NoteAttempt(obs.nowMs);
            }
            return false;
        }
        if (r.status == life::ActivityStatus::Success) {
            planner_.NoteProgress();
            // NO DRAINED-SHELF NOTE HERE, unlike the healer's counter. The
            // weaver's list holds i_cloth in FOUR separate rows (16/16/5/13
            // in Castor's window on 2026-09-05) and the errand buys the
            // first matching row, so a partial buy is a row emptied, not a
            // shop emptied. When the rows really do run out the errand says
            // "does not stock" and the failure path below remembers it.
            const i32 cloth =
                static_cast<i32>(client.BackpackItemCount(kClothGraphic));
            LogLine("bandages: %d cloth bought -- cutting it next pass", cloth);
            return false;                 // step 1 above cuts it
        }
        NoteDrainedShelf(bandageClothBuy_.Keeper(), obs.nowMs);
        LogLine("bandages: no cloth bought (%s) -- back to the wool chain",
                r.reason);
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

    // NO SHEEP IN SIGHT. Go where they are.
    //
    // The pastures are read from the world save rather than assumed: of 246
    // sheep on map 0, the three real flocks are at 572,1098 (15), 669,943 (13)
    // and 669,1175 (11), which is the farmland north-east of Yew. The rest are
    // ones and twos wandering. The owner's recollection was Jhelom and
    // Britain; the save says Yew, and the save is what the bot has to walk to.
    if (client.TravelBusy()) return false;
    if (++bandageTrips_ > kMaxBandageTrips) {
        LogLine("goal_failed=MAKE_BANDAGES reason=\"no sheep found after %d "
                "trips to the pastures\"", bandageTrips_ - 1);
        planner_.Cooldown(GoalKind::MakeBandages, obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "no sheep reachable", obs.nowMs);
        bandageTrips_ = 0;
        return false;
    }
    static const struct { i32 x, y; } kPastures[] = {
        {572, 1098}, {669, 943}, {669, 1175},
    };
    const int which = (bandageTrips_ - 1) % 3;
    LogLine("bandages: no sheep in sight -- walking to the pasture at %d,%d "
            "(trip %d)", kPastures[which].x, kPastures[which].y, bandageTrips_);
    travelInFlight_ =
        client.TravelToPoint(kPastures[which].x, kPastures[which].y, 6, "pasture");
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
    const std::vector<Pasture>& pastures = Pastures();
    if (pastures.empty()) {
        LogLine("goal_failed=%s reason=\"no pasture table -- run "
                "tools/pasturegen.py against the world save\"", GoalKindName(self));
        planner_.Cooldown(self, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no pasture data", obs.nowMs);
        return false;
    }
    if (++clothTrips_ > kMaxClothTrips) {
        LogLine("goal_failed=%s reason=\"no sheep found after %d trips "
                "to the pastures\"", GoalKindName(self), clothTrips_ - 1);
        clothTrips_ = 0;
        clothPastureIdx_ = 0;
        planner_.Cooldown(self, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no sheep reachable", obs.nowMs);
        return false;
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
        LogLine("goal_failed=%s reason=\"no pasture within %d tiles of %s "
                "(%d,%d) -- not walking across the map for wool\"",
                GoalKindName(self), kMaxPastureTilesFromHome, anchorWhat,
                anchorX, anchorY);
        clothTrips_ = 0;
        clothPastureIdx_ = 0;
        planner_.Cooldown(self, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no pasture near home", obs.nowMs);
        return false;
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
