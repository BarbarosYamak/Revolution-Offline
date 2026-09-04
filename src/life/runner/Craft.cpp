#include "RunnerInternal.h"

#include <algorithm>

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;

namespace {
// ONE STROKE TAKES AS LONG AS THE SHARD SAYS. Sphere's per-skill DELAY
// (runtime/scripts/skills/skill<N>_*.scp, line "DELAY=") is the time between
// the menu answer and the item landing; the bot must not start the next
// stroke inside it, and a human takes a breath besides. Values in ms,
// Owner ruling 2026-09-04 ("we don't want very quick craft in the game",
// PLAYER_MEMORY; the TNS import had cut most to 1.2): inscription 4.0,
// cartography 4.0, alchemy 4.0, blacksmithing 5.0, carpentry 5.0, tailoring
// 4.0, tinkering 4.0, bowcraft 3.0, cooking 2.0; mining/lumberjacking 3.0
// are gather swings and live in their own paths. .makelast adds a 1s
// repeat timer on top (revolution_makelast.scp).
i64 CraftStrokeMs(const std::string& item) {
    const prod::Recipe* r = prod::FindRecipe(item.c_str());
    i64 delay = 1200;
    if (r) {
        switch (r->skillId) {
            case rules::kAlchemy:       delay = 4000; break;
            case rules::kBlacksmithing: delay = 5000; break;
            case rules::kCarpentry:     delay = 5000; break;
            case rules::kInscription:   delay = 4000; break;
            case rules::kCartography:   delay = 4000; break;
            case rules::kTailoring:     delay = 4000; break;
            case rules::kTinkering:     delay = 4000; break;
            case rules::kBowcraft:      delay = 3000; break;
            case rules::kCooking:       delay = 2000; break;
            default:                    delay = 1200; break;
        }
    }
    return delay + 1200;   // the human pause between strokes
}
}  // namespace


bool Runner::DoCraft(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        // A character with no trade cannot craft, and saying "done" would
        // report progress=0 forever. Stand down instead.
        planner_.Cooldown(GoalKind::Craft, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "this character has no trade", obs.nowMs);
        return false;
    }

    const CraftIntent intent = ChooseCraft(*me, obs, 1, &craftFocus_);
    if (!intent.item) {
        LogLine("goal_failed=CRAFT status=no_progress reason=\"nothing this "
                "life can make and sell (%s)\"", intent.why);
        // NOT A COMPLETED ERRAND. This returned true, so the planner logged
        // `goal_completed=CRAFT progress=0` and handed the goal straight back
        // -- the shape the anti-spin backstop exists to catch.
        planner_.Cooldown(GoalKind::Craft, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "nothing to make", obs.nowMs);
        return false;
    }
    if (!intent.missing.empty()) {
        // A BURNING FIRE IS KINDLING ALREADY SPENT: the server consumes it at
        // LIGHTING time, not per steak, so a character cooking beside its own
        // lit fire is not short of anything.
        const bool onlyKindling =
            intent.missing.size() == 1 &&
            std::strcmp(intent.missing.front().item, "i_kindling") == 0;
        const prod::Recipe* fireCheck = prod::FindRecipe(intent.item);
        const bool fireBurning =
            fireCheck && fireCheck->station == prod::Station::Fire &&
            client.FindWorldItemByGraphic(kCampfireGraphic, 3) != 0;
        if (!(onlyKindling && fireBurning)) {
            LogLine("goal_blocked=CRAFT reason=\"%s\" %s short of %d x %s",
                    faucet::RefusalName(faucet::Refusal::RequiredForProduction),
                    intent.item, intent.missing.front().qty,
                    intent.missing.front().item);
            return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                           kCraftStuckCooldownMs, "inputs are short", obs.nowMs);
        }
    }

    // A FIRE IS A STATION YOU CARRY -- lighting kindling turns the piece
    // itself into ITEMID_CAMPFIRE, so a fisher can cook where it fished. The
    // engine citations, and why the raw steak cannot be clicked onto the fire:
    // docs/SHARD_MECHANICS_LEARNED.md section 12.
    if (const prod::Recipe* r = prod::FindRecipe(intent.item)) {
        if (r->station == prod::Station::Fire &&
            !client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
            if (client.ActionBusy()) return false;
            // STAND STILL FIRST, then light what is ALREADY on the ground
            // before dropping another piece: Use_Kindling refuses kindling in
            // a container, and a failed Camping roll leaves the piece lying
            // there. docs/SHARD_MECHANICS_LEARNED.md section 12.
            if (client.TravelBusy()) return false;
            if (const u32 ground =
                    client.FindWorldItemByGraphic(kKindlingGraphic, 3)) {
                LogLine("craft: lighting the kindling on the ground for a "
                        "campfire to cook %s on", intent.item);
                client.ActionUseObject(ground);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 3500;
                return false;
            }
            const u32 kindling = client.FindBackpackItemByGraphic(kKindlingGraphic);
            if (!kindling) {
                LogLine("goal_blocked=CRAFT reason=\"%s\" %s needs a fire and "
                        "there is no kindling to light one",
                        faucet::RefusalName(faucet::Refusal::RequiredForProduction),
                        intent.item);
                return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                               kCraftStuckCooldownMs, "no kindling for a fire",
                               obs.nowMs);
            }
            LogLine("craft: putting one kindling on the ground -- "
                    "Use_Kindling refuses it inside a container");
            client.ActionDropGround(kindling, 1, obs.x, obs.y, obs.z);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
    }

    // A FORGE RECIPE NEEDS THE FORGE, AND THE HAMMER IN HAND.
    //
    // Production.cpp already recorded both, from the engine:
    //   CClientUse.cpp:1273 LayerFind(LAYER_HAND1)   -- tool EQUIPPED, not carried
    //   CClientUse.cpp:1282 IsItemTypeNear(IT_FORGE,3)
    // and nothing acted on either. So Corwyn stood wherever he happened to be,
    // double-clicked an ingot 14 times, and the menu never opened -- "craft:
    // making i_dagger -- using a i_ingot_iron to open the menu", once every
    // four seconds until the run ended. "double click smith hammer maybe"
    // (project owner, 2026-08-29), which is exactly what the engine wants.
    if (const prod::Recipe* fr = prod::FindRecipe(intent.item)) {
        if (fr->station == prod::Station::Forge) {
            Client::TreeHit forgeTile;
            // NOT `near` -- MSVC still defines that as a legacy keyword macro.
            const bool haveForge = client.NearestForge(obs.x, obs.y, 20,
                                                       &forgeTile, &deadForges_);
            const i32 d = haveForge
                              ? TileDist(obs.x, obs.y, forgeTile.x, forgeTile.y)
                              : 999;
            if (!haveForge || d > 2) {
                if (client.TravelBusy()) return false;
                LogLine("craft: %s needs a forge -- %s", intent.item,
                        haveForge ? "walking to the one in sight"
                                  : "going to find a smithy");
                if (haveForge) {
                    i32 sx = 0, sy = 0; bool ok = false;
                    static const int ddx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                    static const int ddy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                    for (int i = 0; i < 8 && !ok; ++i) {
                        const i32 tx = forgeTile.x + ddx[i];
                        const i32 ty = forgeTile.y + ddy[i];
                        if (!client.TileIsWalkable(tx, ty, forgeTile.z)) continue;
                        sx = tx; sy = ty; ok = true;
                    }
                    if (ok) {
                        travelInFlight_ = client.TravelToPoint(sx, sy, 0, "forge");
                        nextActionMs_ = obs.nowMs + 2000;
                        return false;
                    }
                    deadForges_.emplace_back(forgeTile.x, forgeTile.y);
                }
                travelInFlight_ = client.TravelToServiceSkipping(
                    wm::Service::Blacksmith, HomeOrNearest(state_.homeCity), {},
                    &smeltSkipPlaces_);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            // A FORGE THAT WORKS ENDS THE SEARCH -- same rule DoSmelt already
            // follows (search above, "A FORGE THAT WORKS ENDS THE SEARCH").
            // TravelToServiceSkipping records every place it is ever SENT to
            // in smeltSkipPlaces_, success or not (ClientTravel.cpp), and
            // CRAFT shares that list with SMELT. Without clearing it here, a
            // smith who wandered off this forge to bank or sell and then
            // needed it again could never be sent back to it: Durnholde used
            // Minoc's own smithy at 21:18, wandered out of NearestForge's
            // 20-tile sight at 21:20, and because "Minoc blacksmith" was
            // already on the list, TravelToServiceSkipping picked "Sea
            // Market blacksmith" (no walkable ground) and then "Papua
            // weaponsmith" -- 904 tiles and three moongates into the Lost
            // Lands -- for a service Minoc had offered the whole time
            // (docs/CRAFTER_RUN_2026_08_30.md defect 4, run_r4/pair_Durnholde
            // .console.txt 21:16-21:25).
            smeltSkipPlaces_.clear();
        }
        // THE TOOL MUST BE IN HAND1, not merely in the pack.
        if (fr->tool == prod::Tool::SmithHammer) {
            bool held = false;
            for (usize i = 0; i < 2; ++i) {
                if (client.EquippedGraphicAt(kLayerHand1) == kSmithHammerGfx[i]) {
                    held = true; break;
                }
            }
            if (!held) {
                // DO NOT RE-ISSUE INSIDE THE ACTION'S OWN DEADLINE. Without
                // this the equip superseded itself every two seconds --
                // "equip invalid_state took=2091ms superseded" -- forever.
                if (client.ActionBusy()) return false;
                const u32 inPack = FindAny(client, kSmithHammerGfx, 2);
                if (!inPack) {
                    // Tongs will not do here, however much the catalogue likes
                    // them: GET_TOOL has to fetch an actual hammer, and
                    // VENDOR_S_TINKER sells one.
                    LogLine("goal_blocked=CRAFT reason=\"%s\" no smith HAMMER "
                            "to open the forge menu with (tongs cannot be "
                            "wielded)",
                            faucet::RefusalName(faucet::Refusal::MissingTool));
                    return HandOff(GoalKind::Craft, GoalKind::GetTool,
                                   kCraftStuckCooldownMs, "no smith hammer",
                                   obs.nowMs);
                }
                // EMPTY THE HAND FIRST. Naming the layer was still not
                // enough: a miner_smith carries a pickaxe AND tongs, mining
                // leaves the pickaxe wielded, and the server will not put a
                // second thing in an occupied hand -- it answered "You put the
                // tongs in your pack" to every attempt, at layer 0 and at
                // layer 1 alike. Mining re-equips its own pickaxe when it next
                // needs it, so putting it away here costs nothing.
                const u32 hand1 = client.EquippedAtLayer(kLayerHand1);
                const u32 hand2 = client.EquippedAtLayer(kLayerHand2);
                const u32 inTheWay = hand1 ? hand1 : hand2;
                if (inTheWay && inTheWay != inPack) {
                    LogLine("craft: putting away what is in hand to free "
                            "HAND1 for the smith tool");
                    client.ActionUnequip(inTheWay);
                    nextActionMs_ = obs.nowMs + 3000;
                    return false;
                }

                // NAME THE LAYER. Passing kLayerServerChooses (0) does not
                // equip anything. The engine looks in HAND1
                // (CClientUse.cpp:1273), so say HAND1.
                LogLine("craft: taking the smith tool into HAND1 -- the menu "
                        "reads LAYER_HAND1");
                client.ActionEquip(inPack, kLayerHand1);
                nextActionMs_ = obs.nowMs + 4000;
                return false;
            }
        }
    }

    if (craftItem_ != intent.item) {
        makeLastIssued_ = false;   // a different item needs its own first make
        makeLastRemaining_ = 0;
        craftItem_ = intent.item;
        craftWait_.Configure(life::RetryPolicy{kMaxCraftAttempts,
                                               kCraftResolveMs, 1000});
        craftWait_.Reset();
        craftHadBefore_ = market::QtyOf(obs.pack, craftItem_);
        craftJournalMs_ = client.JournalNowMs();
        craftMade_ = 0;
        craftSittingTarget_ = 0;   // measured from the pack on the first plan
    }

    // DID THE LAST SWING LAND? Section 18's craft rule and both of its halves
    // -- the pack count rose, or the shard said in words that it did not --
    // decided in uo/activities/craft_confirm.h, which also carries the shard
    // strings and their evidence. Exhausted() AND out of ActionIssued: the
    // counter rises at NoteIssued, so Exhausted() alone would stand the goal
    // down while the last swing was still inside its own deadline.
    const i32 now = market::QtyOf(obs.pack, craftItem_);
    CraftConfirmInput cin;
    cin.packBefore = craftHadBefore_;
    cin.packNow = now;
    cin.deadlineExpired = craftWait_.Expired(obs.nowMs);
    cin.attemptsExhausted =
        craftWait_.Exhausted() &&
        craftWait_.State() != life::HandshakeState::ActionIssued &&
        craftWait_.State() != life::HandshakeState::WaitingForServer;
    usize nCraftFails = 0;
    const CraftFailure* craftFails = CraftFailures(&nCraftFails);
    for (usize fi = 0; fi < nCraftFails; ++fi) {
        if (client.JournalSaidSince(craftFails[fi].text, craftJournalMs_)) {
            cin.heard = &craftFails[fi];
            break;
        }
    }
    const CraftConfirmResult conf = ConfirmCraft(cin);
    if (conf.verdict == CraftVerdict::ShardRefused ||
        conf.verdict == CraftVerdict::NoProgress) {
        // A goal that achieved nothing says so and stands DOWN, so the planner
        // gives the turn to something else instead of re-picking it in 60 ms.
        LogLine("goal_failed=CRAFT status=no_progress reason=\"%s\" (%s, "
                "attempt %d of %d)", conf.reason, craftItem_.c_str(),
                craftWait_.Attempts(), kMaxCraftAttempts);
        craftWait_.Reset();
        // A fresh window: the refusal that ended THIS sitting must not end the
        // next one before a single click has gone out.
        craftJournalMs_ = client.JournalNowMs();
        return HandOff(GoalKind::Craft, GoalKind::IdleBriefly,
                       kCraftStuckCooldownMs, conf.reason, obs.nowMs);
    }
    if (conf.verdict == CraftVerdict::Spoiled) {
        // A real answer, so the swing is over -- but the trade is not. An
        // ATTEMPT, never progress; ChooseCraft ends the goal when stock runs out.
        LogLine("craft: %s -- taking another swing at %s", conf.reason,
                craftItem_.c_str());
        craftWait_.Reset();
        craftJournalMs_ = client.JournalNowMs();
        planner_.NoteAttempt(obs.nowMs);
    }
    if (conf.verdict == CraftVerdict::Made) {
        craftMade_ += conf.made;
        craftHadBefore_ = now;
        craftWait_.Reset();            // the thing we were waiting for arrived
        planner_.NoteProgress();
        LogLine("craft: made %s pack %d->%d (%d this sitting)",
                craftItem_.c_str(), now - conf.made, now, craftMade_);
        if (makeLastRemaining_ > 0) {
            // One of the shard's repeats landed: count it off and give the
            // rest their time again.
            makeLastRemaining_ -= conf.made;
            if (makeLastRemaining_ < 0) makeLastRemaining_ = 0;
            makeLastDeadlineMs_ =
                obs.nowMs + CraftStrokeMs(craftItem_) * (makeLastRemaining_ + 1);
        }
        if (!state_.memory.HasEvent("first_craft")) {
            state_.memory.NoteEvent("first_craft", craftItem_.c_str(), "",
                                    obs.x, obs.y, obs.nowMs);
        }
        // KEEP GOING WHILE THE MATERIAL LASTS, for ANY trade -- the stock in
        // the pack is the honest limit, and kindling is the one carve-out
        // because the server spends it at lighting time. The evidence and the
        // owner's wording: docs/SHARD_MECHANICS_LEARNED.md section 12.
        //
        // ONE INGREDIENT-SETS WALK, not two. The old moreToUse (bool) and
        // canMake (int) loops both iterated rr->inputs, both carved out lit
        // kindling, both compared against market::QtyOf -- lifted here and
        // fed to DecideCraft, which needs exactly this count for both the
        // continue/stop verdict and the .makelast quantity
        // (S2_WIRING_PLAN.md S2.5).
        i32 inputsAvailable = 0;
        const prod::Recipe* rr = prod::FindRecipe(craftItem_.c_str());
        if (rr) {
            inputsAvailable = 500;
            for (const prod::Ingredient& in : rr->inputs) {
                if (!in.item || in.qty <= 0) continue;
                if (rr->station == prod::Station::Fire &&
                    std::strcmp(in.item, "i_kindling") == 0 &&
                    client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
                    continue;   // the fire is already lit
                }
                const i32 have = market::QtyOf(obs.pack, in.item);
                const i32 fits = have / in.qty;
                if (fits < inputsAvailable) inputsAvailable = fits;
            }
            if (inputsAvailable > 500) inputsAvailable = 500;
        }

        CraftRequest req;
        req.item = craftItem_.c_str();
        // A TOTAL, not a delta. `now` (== held below) already includes
        // whatever was in the pack before this sitting started, and
        // craftMade_ cancels out of desiredTotal-held across every call --
        // the batch target stays craftBatch above the pre-sitting stock
        // regardless of which Made event this is. Re-buying/re-making what
        // is already held is the exact bug craft.h:45-47 and the
        // craftHadBefore_ tracking above both warn about.
        //
        // `now` includes the pre-sitting stock, so the target must be built
        // on `now` too: (now - craftMade_) is exactly the pre-sitting stock
        // whichever Made event this is. The previous `craftMade_ + batch`
        // forgot the stock and ended every sitting after ONE potion once
        // Elara held more than the batch (g_Elara 01:43:11 "1 made -- as
        // many are held as this batch wanted", pack 9->10, 2026-09-04).
        //
        // THE STOCK SETS THE SITTING, craftBatch is only the floor. The
        // supply errand sized itself on the best-stocked input
        // (CraftBatchFromStock) and bought in bulk; a bench that then stops
        // every craftBatch pieces turns 73 nightshade into ten separate
        // sittings (g_Elara 2026-09-04 lines 361..1872). Measured ONCE, on
        // the first plan of the sitting, while the pack still holds the
        // whole batch -- re-reading it after each Made would shrink the
        // target with the material and end the run early. `inputsAvailable`
        // is what the pack can fund right now, capped at 500 like the
        // shard's own .makelast (revolution_makelast.scp:59). Owner rule
        // 2026-09-04: every crafter stocks in bulk, then sits a long time.
        if (craftSittingTarget_ <= 0) {
            craftSittingTarget_ = std::max(needCfg_.craftBatch, inputsAvailable);
            if (craftSittingTarget_ > needCfg_.craftBatch) {
                LogLine("craft: the pack funds %d %s -- one sitting of %d, "
                        "not %d", inputsAvailable, craftItem_.c_str(),
                        craftSittingTarget_, needCfg_.craftBatch);
            }
        }
        req.desiredTotal = (now - craftMade_) + craftSittingTarget_;
        // UNKNOWN: no profession field carries a working reserve
        // (S2_WIRING_PLAN.md S2.5). 0 for gathered inputs is the honest
        // default -- "craft till you are out of iron on your bag" is a
        // reserve of 0 and it applies to ore a miner dug. A reserve for
        // bought inputs is its own measured slice.
        req.minimumMaterialsReserve = 0;
        const CraftPlan plan = DecideCraft(req, /*held=*/now, inputsAvailable);
        if (plan.step != lastCraftPlan_) {
            LogPlan(CraftStepName(plan.step), plan.reason);
            lastCraftPlan_ = plan.step;
        }

        if (plan.step == CraftStep::Done) {
            LogLine("craft: %d %s made -- %s", craftMade_, craftItem_.c_str(),
                    plan.reason);
            // ONE SITTING RECORDED, not one piece. A sitting is the unit a
            // player would recognise as "I spent the morning on boards", and
            // counting pieces instead would flip the focus in the middle of a
            // batch and leave the bench half-worked.
            craftFocus_.NoteMade(craftItem_.c_str(), obs.nowMs);
            LogLine("craft_focus=%s sittings_in_a_row=%d",
                    craftFocus_.Last().c_str(), craftFocus_.Run());
            craftItem_.clear();
            return true;
        }

        if (plan.step == CraftStep::ShortOfInputs ||
            plan.step == CraftStep::ReserveHit) {
            // Not a failure: some progress already happened this call
            // (NoteProgress fired above). Find the first ingredient actually
            // short, so the handoff and any vendor lookup name the real
            // thing rather than the recipe's headline material.
            const char* missing = craftItem_.c_str();
            if (rr) {
                for (const prod::Ingredient& in : rr->inputs) {
                    if (!in.item) break;
                    if (rr->station == prod::Station::Fire &&
                        std::strcmp(in.item, "i_kindling") == 0 &&
                        client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
                        continue;
                    }
                    if (market::QtyOf(obs.pack, in.item) < in.qty) {
                        missing = in.item;
                        break;
                    }
                }
            }
            if (econ::CanBuyFromNPC(missing).allowed) {
                return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                               kCraftStuckCooldownMs, plan.reason, obs.nowMs);
            }
            const std::string gathers =
                needCfg_.profession ? needCfg_.profession->gathers
                                    : std::string("logs");
            const GoalKind gatherGoal =
                gathers == "ore" ? GoalKind::Mine : GoalKind::GatherLogs;
            return HandOff(GoalKind::Craft, gatherGoal, kCraftStuckCooldownMs,
                           plan.reason, obs.nowMs);
        }

        // CraftStep::Make. plan.remaining is the batch target already
        // clamped to what the materials allow -- feed it straight to
        // .makelast rather than the old raw material-availability count,
        // which ignored needCfg_.craftBatch entirely.
        if (!makeLastIssued_ && obs.WeightFraction() < 0.90 &&
            plan.remaining > 1) {
            // REPEAT WITH .makelast RATHER THAN RE-WALKING THE MENU. The
            // first item still goes through the menu -- that is what sets
            // TAG.revo.makelast.item -- and the server re-checks CANMAKE
            // every repetition, so skill, materials, tool and station stay
            // enforced. Why it had never worked before 2026-08-30, and why
            // gathering is untouched: docs/SHARD_MECHANICS_LEARNED.md
            // section 12.
            char cmd[64];
            std::snprintf(cmd, sizeof(cmd), ".makelast %d",
                          static_cast<int>(plan.remaining));
            LogLine("craft: %d %s made -- repeating the other %d with "
                    "'%s' instead of walking the menu again",
                    craftMade_, craftItem_.c_str(),
                    static_cast<int>(plan.remaining), cmd);
            client.ActionSay(cmd);
            makeLastIssued_ = true;
            makeLastRemaining_ = static_cast<i32>(plan.remaining);
            makeLastDeadlineMs_ =
                obs.nowMs + CraftStrokeMs(craftItem_) * (makeLastRemaining_ + 1);
            craftJournalMs_ = client.JournalNowMs();
            nextActionMs_ = obs.nowMs + 3000;
            return false;
        }
        LogLine("craft: %d %s made and the material is not finished -- "
                "carrying on", craftMade_, craftItem_.c_str());
        // A BREATH BETWEEN STROKES, even when the menu is walked by hand.
        nextActionMs_ = obs.nowMs + CraftStrokeMs(craftItem_);
        return false;
    }

    // THE SHARD IS STILL REPEATING -- do not touch the menu (Runner.h,
    // makeLastRemaining_). It says "Make Last complete." / "Make Last
    // stopped" when it is done; the pack count says it per item; the
    // deadline is the floor under a repeat that died silently.
    if (makeLastRemaining_ > 0) {
        if (client.JournalSaidSince("Make Last complete", craftJournalMs_) ||
            client.JournalSaidSince("Make Last stopped", craftJournalMs_)) {
            LogLine("craft: the shard finished repeating %s (%d this sitting)",
                    craftItem_.c_str(), craftMade_);
            makeLastRemaining_ = 0;
        } else if (obs.nowMs >= makeLastDeadlineMs_) {
            LogLine("craft: the repeat of %s went quiet with %d still owed -- "
                    "walking the menu myself", craftItem_.c_str(),
                    makeLastRemaining_);
            makeLastRemaining_ = 0;
        } else {
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }
    }

    const CraftMenuPath* path = CraftMenuFor(craftItem_);
    if (!path) {
        // SOME OUTPUTS ARE NOT MENU CRAFTS AT ALL.
        //
        // Provenance::WorldProcessed means exactly that: "a station transforms
        // it; no craft menu, no skill" (production.h). Ore becomes ingots by
        // double-clicking it beside a forge, a whole fish becomes steaks under
        // a blade, wool becomes yarn at a wheel -- each has its own goal, and
        // no amount of menu-walking will ever reach one. Draver spent the
        // 2026-09-02 wave refusing i_ingot_iron here while SMELT, the goal
        // that actually does it, sat unpicked. Hand it over rather than
        // refuse: nothing is missing, the request was simply addressed to the
        // wrong goal.
        const prod::Recipe* wp = prod::FindRecipe(craftItem_.c_str());
        const GoalKind owner = ProducingGoalFor(craftItem_);
        if (wp && wp->provenance == prod::Provenance::WorldProcessed &&
            owner != GoalKind::Craft) {
            LogLine("craft: %s is not a menu craft (%s) -- %s is the goal that "
                    "makes it", craftItem_.c_str(),
                    prod::ProvenanceName(wp->provenance), GoalKindName(owner));
            return HandOff(GoalKind::Craft, owner, kCraftStuckCooldownMs,
                           "not a menu craft", obs.nowMs);
        }
        craftFocus_.NoteNoRoute(craftItem_.c_str());
        LogLine("goal_failed=CRAFT reason=\"%s\" no menu path known for %s",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no craft menu path known", obs.nowMs);
        return false;
    }

    if (client.ActionBusy()) return false;

    // WAITING ON THE LAST ONE -- enter ONLY while an attempt is outstanding.
    // Not `State() != Idle`: NoteExpiry leaves the handshake in Backoff, which
    // means "the last swing is closed out, take another" and is the
    // fall-through below. Why the pack and a Handshake rather than a timer,
    // and the session Voris spent proving it: uo/activities/craft_confirm.h
    // and uo/activities/craft.h.
    if (craftWait_.State() == life::HandshakeState::ActionIssued ||
        craftWait_.State() == life::HandshakeState::WaitingForServer) {
        if (client.ActionBusy()) return false;

        if (!craftWait_.Expired(obs.nowMs)) {
            nextActionMs_ = obs.nowMs + 700;
            return false;
        }
        craftWait_.NoteExpiry(obs.nowMs);
        LogLine("craft: no result from the last %s in %llds (attempt %d of %d)",
                craftItem_.c_str(),
                static_cast<long long>(kCraftResolveMs / 1000),
                craftWait_.Attempts(), kMaxCraftAttempts);

        if (craftWait_.Exhausted()) {
            // The swing is closed out and there are none left. Come straight
            // back rather than deciding here: the confirmation at the top of
            // this handler is the ONE place that says no_progress, and now
            // that the handshake is out of ActionIssued it can.
            nextActionMs_ = obs.nowMs;
            return false;
        }
    }

    // --- walk the menu -----------------------------------------------------
    if (client.CraftMenuOpen()) {
        // READ THE MENU, DO NOT COUNT STEPS.
        //
        // The first version tracked which level it thought it was on. One
        // failed answer reset that counter to zero while the SUBMENU was
        // still open, so it then hunted for "Spell Circle 3" inside the list
        // that was already offering "poison" -- and said "the craft menu does
        // not offer it" sixteen times a second while printing the very option
        // it wanted. The menu itself says which level it is; ask it.
        // DEEPEST FIRST. The menu itself says which level it is on, so ask it
        // from the bottom up; anything else mistakes a submenu for the top.
        // A step beginning with '^' is anchored at the start of the option.
        auto has = [&client](const char* step) -> bool {
            if (!step) return false;
            return step[0] == '^' ? client.DialogHasPrefix(step + 1)
                                  : client.DialogHasOption(step);
        };
        auto choose = [&client](const char* step) -> bool {
            return step[0] == '^' ? client.ChooseDialogByPrefix(step + 1)
                                  : client.ChooseDialogByName(step);
        };
        const char* want = nullptr;
        if (path->step3 && has(path->step3)) {
            want = path->step3;
        } else if (path->step2 && has(path->step2)) {
            want = path->step2;          // already in the submenu
        } else if (has(path->step1)) {
            want = path->step1;          // the top menu, or a flat one
        }
        if (!want) {
            // Three of these and ChooseCraft stops offering the output at all
            // (CraftFocus::NoteNoRoute). scribe3 said "the menu does not offer
            // 'Spell Circle 3'" hundreds of times in one run; the route table
            // is not going to change mid-session, so say it three times and
            // move on to something this life CAN make.
            craftFocus_.NoteNoRoute(craftItem_.c_str());
            LogLine("goal_failed=CRAFT reason=\"%s\" this menu offers none of "
                    "'%s' / '%s' / '%s'",
                    faucet::RefusalName(faucet::Refusal::MissingRecipe),
                    path->step1, path->step2 ? path->step2 : "(flat)",
                    path->step3 ? path->step3 : "(flat)");
            for (const std::string& o : client.CraftableNow()) {
                LogLine("craft:   offered: %s", o.c_str());
            }
            planner_.Finish(false, "the craft menu does not offer it", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!choose(want)) {
            // DialogHasOption just said it was there, so this is a send
            // failure rather than a missing option. Let it settle and re-read.
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        LogLine("craft: chose '%s'", want);
        // LET THE CRAFT FINISH BEFORE TOUCHING ANYTHING ELSE. The swing now
        // belongs to the Handshake and its answer to uo/activities/
        // craft_confirm.h: the pack is watched until the count rises or the
        // shard says why not, and the deadline is only a floor under a craft
        // that failed silently. "you are not waiting to finish one poison"
        // (project owner, 2026-08-30).
        craftWait_.NoteIssued(obs.nowMs);
        craftJournalMs_ = client.JournalNowMs();   // read the answer from here
        nextActionMs_ = obs.nowMs + 1000;
        return false;
    }

    // --- open it -----------------------------------------------------------
    //
    // The menu opens by USING the thing the craft is made from -- a blank
    // scroll for Inscription, a log for Bowcraft. The recipe's own first
    // input is that thing, so nothing here has to be hardcoded per trade.
    const prod::Recipe* r = prod::FindRecipe(craftItem_.c_str());
    if (!r || !r->inputs[0].item) {
        LogLine("goal_failed=CRAFT reason=\"%s\" %s has no recipe",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no recipe", obs.nowMs);
        return false;
    }
    // WHAT OPENS THE MENU. For most trades it is the material -- a blank
    // scroll, a log. For blacksmithing it is the TOOL: t_weapon_mace_smith is
    // a hardcoded engine type (defs_types_hardcoded.scp) whose double-click
    // opens the smith menu, and an ingot's double-click opens nothing at all.
    // ...except where the material EATS the double-click. For a fire recipe
    // the opener is the t_cooking tool: using the raw steak itself would be
    // answered by Use_Eat (CCharUse.cpp:1862) -- the character would swallow
    // its own stock one click at a time and no menu would ever come.
    // THE TOOL OPENS THE MENU wherever the trade has one -- a mortar for
    // alchemy, tinker tools, a sewing kit, a saw -- and only Inscription
    // (Tool::BlankScroll) and the toolless recipes open from the material.
    // Special-casing the smith hammer alone left alchemy double-clicking a
    // reagent and being told "You can't think of a way to use that item".
    const ToolOpener* opener_tool = OpenerFor(r->tool);
    const std::vector<u16> openGfx =
        r->station == prod::Station::Fire
            ? std::vector<u16>(kCookingToolGfx, kCookingToolGfx + 3)
            : (opener_tool
                   ? std::vector<u16>(opener_tool->gfx,
                                      opener_tool->gfx + opener_tool->n)
                   : econ::GraphicsForItem(r->inputs[0].item));
    // WORN COUNTS. THE TOOL IS USUALLY IN A HAND BY THE TIME WE LOOK.
    //
    // This goal equips the smith tool into HAND1 a few lines above, because
    // the blacksmith menu reads LAYER_HAND1 -- and then looked for it in the
    // BACKPACK, where it no longer is. Corwyn walked to a forge, armed his
    // hammer, and was told "nothing in the pack to open the i_dagger menu
    // with" while holding it (v1_Corwyn.console.txt, 14:42:39). That blocks
    // the whole miner_smith economy: no dagger, so no sale, so no income.
    //
    // The same trap the fishing pole set -- a newbie kit hands out tools the
    // shard then EQUIPS, so "the pole in my pack" finds nothing while the
    // character is holding it. FindItemByGraphic(includeEquipped) exists for
    // exactly this and the scenario engine already uses it.
    u32 opener = 0;
    for (u16 g : openGfx) {
        opener = client.FindItemByGraphic(g, /*includeEquipped=*/true);
        if (opener) break;
    }
    if (!opener) {
        LogLine("goal_blocked=CRAFT reason=\"%s\" nothing carried or worn to "
                "open the %s menu with (%s)",
                faucet::RefusalName(faucet::Refusal::MissingTool),
                craftItem_.c_str(), r->inputs[0].item);
        return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                       kCraftStuckCooldownMs, "no material to start from",
                       obs.nowMs);
    }

    // BLACKSMITHING TAKES THREE ACTIONS, NOT ONE.
    //
    // "how to craft is double click hammer then select ingot then select what
    // do you want to craft" (project owner, 2026-08-29). The hammer arms a
    // TARGET cursor; the cursor is given an ingot; only THEN does the menu
    // appear. This code used the hammer and sat waiting for a menu that was
    // never going to come on its own -- the same shape as the smelt bug, where
    // the forge arms a cursor for the ore.
    //
    // Inscription and bowcraft are genuinely one action (use the material),
    // so the middle step is asked for only where the tool opens a target.
    //
    // THE SEWING KIT IS THE SAME SHAPE. Its double-click arms a cursor
    // (CClientUse.cpp:551) and the TYPE of what the cursor is given picks
    // the root menu -- cloth opens sm_tailor_cloth, leather sm_tailor_leather
    // (CClientTarg.cpp:2383-2399). Aelia and Amara used the kit and waited
    // for a menu that only comes after the cloth is targeted.
    const bool toolArmsCursor = r->tool == prod::Tool::SmithHammer ||
                                r->tool == prod::Tool::SewingKit;
    if (toolArmsCursor) {
        if (craftCursorPending_) {
            if (client.TargetActive()) {
                const std::vector<u16> matGfx =
                    econ::GraphicsForItem(r->inputs[0].item);
                u32 mat = 0;
                for (u16 g : matGfx) {
                    mat = client.FindBackpackItemByGraphic(g);
                    if (mat) break;
                }
                if (!mat) {
                    LogLine("goal_blocked=CRAFT reason=\"%s\" the %s cursor "
                            "is up but there is no %s to give it",
                            faucet::RefusalName(
                                faucet::Refusal::RequiredForProduction),
                            prod::ToolName(r->tool), r->inputs[0].item);
                    craftCursorPending_ = false;
                    planner_.Finish(false, "no material to target", obs.nowMs);
                    return false;
                }
                LogLine("craft: giving the %s cursor %s to open the menu",
                        prod::ToolName(r->tool), r->inputs[0].item);
                client.ActionTargetObject(mat);
                craftCursorPending_ = false;
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            if (obs.nowMs - craftClickedMs_ > 6000) craftCursorPending_ = false;
            return false;
        }
        LogLine("craft: making %s -- double-clicking the %s",
                craftItem_.c_str(), prod::ToolName(r->tool));
        client.ActionUseObject(opener);
        craftCursorPending_ = true;
        craftClickedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    LogLine("craft: making %s -- using %s to open the menu",
            craftItem_.c_str(),
            r->station == prod::Station::Fire
                ? "a cooking tool"
                : (opener_tool ? "its own tool" : r->inputs[0].item));
    client.ActionUseObject(opener);
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

}  // namespace uo::life
