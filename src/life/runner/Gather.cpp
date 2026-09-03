#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// --- the work --------------------------------------------------------------

bool Runner::DoGatherLogs(Client& client, const Observation& obs) {
    // THE SHARED DECISION FIRST (section 22). Chopping, mining and fishing
    // all answer the same question -- swing, arm, move, or take the load in
    // -- and the branches below used to answer it three different ways. What
    // stays here is what is genuinely a lumberjack's: which tree, and how to
    // swing at it.
    {
        life::GatherRequest req;
        req.resource = "logs";
        req.loadWorthTaking = needCfg_.logsWorthBanking;
        req.packFullFraction = kGathererPackFullFrac;  // the shared bar
        req.toolMustBeWielded = true;  // skill44_lumberjacking reads SRC.WEAPON

        life::GatherSight sight;
        sight.held = obs.logs;
        sight.weightFraction = obs.WeightFraction();
        sight.toolInPack = obs.axeInPack;
        sight.toolWielded = obs.axeEquipped;
        // The census and the exhaustion flag disagree on purpose: TreeCount
        // still sees trunks here while NearestTree has none left to offer.
        sight.targetInReach = obs.atWorkSite && !areaExhausted_;
        sight.areaWorkedOut = areaExhausted_;

        const life::GatherPlan plan = life::DecideGather(req, sight);
        if (plan.step == life::GatherStep::NeedTool) {
            LogLine("goal_failed=GATHER_LOGS reason=\"%s\"", plan.reason);
            planner_.Finish(false, "no axe", obs.nowMs);
            return false;
        }
        if (plan.step == life::GatherStep::TakeItIn) {
            LogLine("gather: %s (%.0f%% of capacity, %d logs)", plan.reason,
                    obs.WeightFraction() * 100.0, obs.logs);
            return true;
        }
        // LeaveArea, ArmTool and Swing all fall through: the branches below
        // already do those three things, and doing them well is this
        // handler's actual job.
    }

    // Arm the axe. `skill44_lumberjacking.scp` requires SRC.WEAPON, so the axe
    // has to be IN HAND, not merely carried -- and it must be the AXE, not
    // whatever the newbie kit armed. This is also the weapon the character
    // fights with, which is the whole point of the era Lumberjack template.
    if (!obs.axeEquipped) {
        // Arming is progress, not a failed attempt -- counting it against the
        // attempt budget would exhaust the goal before the first swing.
        if (ArmAxe(client, obs)) return false;
        if (!AxeInHand(client)) {
            LogLine("goal_failed=GATHER_LOGS reason=\"no axe to arm\"");
            planner_.Finish(false, "no axe to arm", obs.nowMs);
            return false;
        }
    }

    // --- am I actually where the work is? ---------------------------------
    //
    // `areaExhausted_` overrides the census: TreeCount can still see trunks
    // here while NearestTree has none left to offer, because every one of them
    // has already been worked this visit. Believing the census in that state
    // is what kept the character standing in a clearing it had finished.
    if (!obs.atWorkSite || areaExhausted_) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // Earned knowledge first, then common knowledge, then go looking.
            // The order matters: preferring a merely-remembered spot over a
            // named forest is what kept this character in the scrub.
            const KnownResourceSource* proven =
                state_.memory.BestProvenResource("logs", obs.x, obs.y, obs.nowMs);
            if (proven && IsDeadTarget(proven->x, proven->y)) proven = nullptr;
            const KnownResourceSource* hint =
                proven ? nullptr
                       : state_.memory.BestHint("logs", obs.x, obs.y, obs.nowMs);
            if (hint && IsDeadTarget(hint->x, hint->y)) hint = nullptr;
            if (proven) {
                LogLine("gather: back to a stand that has paid out before at "
                        "%d,%d (%d successes, %d failures)",
                        proven->x, proven->y, proven->successes, proven->failures);
                lastHintX_ = proven->x;
                lastHintY_ = proven->y;
                travelInFlight_ =
                    client.TravelToPoint(proven->x, proven->y, 4, "proven_stand");
            } else if (hint) {
                LogLine("gather: nothing proven yet -- trying %s at %d,%d "
                        "(a lead, %d disappointment(s) so far)",
                        hint->label.empty() ? "a known forest" : hint->label.c_str(),
                        hint->x, hint->y, hint->failures);
                lastHintX_ = hint->x;
                lastHintY_ = hint->y;
                travelInFlight_ =
                    client.TravelToPoint(hint->x, hint->y, 6, "forest_hint");
            } else {
                // WALK OUT AND LOOK, RATHER THAN WAIT FOR AN ATLAS ENTRY
                // THAT DOES NOT EXIST. Project owner, 2026-08-31: "if he
                // left the guard zone at Britain he would see farmable
                // trees" -- and the atlas backs this up literally:
                // data/revolution_atlas.txt has zero PLACE rows with
                // resources=lumber (grep -i "\tlumber$"), so
                // TravelToResource(Lumber) can never succeed here. A real
                // player in this position walks out of town; this is that,
                // bounded (world/GuardZoneAdvance.h -- same shape as
                // DoMine's DeeperMiningTarget, opposite direction).
                i32 stepX = 0, stepY = 0;
                if (client.StepOutOfGuardZone(obs.x, obs.y, &stepX, &stepY)) {
                    LogLine("gather: no stand and no lead left, and this is "
                            "guarded ground -- walking out to where trees can "
                            "actually be worked");
                    travelInFlight_ =
                        client.TravelToPoint(stepX, stepY, 4, "past_guard_line");
                } else {
                    LogLine("gather: no stand and no lead left; asking the "
                            "world for lumber");
                    travelInFlight_ =
                        client.TravelToResource(wm::ResourceKind::Lumber);
                }
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GATHER_LOGS reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
            }
            return false;
        }
        travelInFlight_ = false;
        // A completed journey is a new place, so the exhaustion verdict about
        // the OLD one no longer applies.
        areaExhausted_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE. A journey that reports success
        // and leaves us six tiles short of the trees is a failure here, and
        // saying so is what keeps it out of the "worked fine" column.
        if (client.TreeCount(client.PlayerX(), client.PlayerY(), cfg_.searchRadius) == 0) {
            LogLine("gather: trip reported %s but there are no trees within %d tiles",
                    client.TravelSucceeded() ? "success" : "failure", cfg_.searchRadius);
            // Charge the DESTINATION WE AIMED AT, and refuse to aim there
            // again this session. Charging wherever we happen to stand let a
            // no-op trip -- one that "arrived" without moving -- pile failures
            // onto a stand with three real successes, several times a second.
            if (lastHintX_ != 0 || lastHintY_ != 0) {
                state_.memory.NoteResource("logs", lastHintX_, lastHintY_,
                                           client.PlayerZ(), false, obs.nowMs);
                deadTargets_.emplace_back(lastHintX_, lastHintY_);
                if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
                lastHintX_ = lastHintY_ = 0;
            }
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 3000;
        }
        return false;
    }

    // --- pick a tree and stand next to it ---------------------------------
    if (!chopTargetValid_) {
        Client::TreeHit tree;
        // Ask for the nearest tree we have NOT already worked. The exclusion
        // is the whole point: without it every widening radius hands back the
        // same tree, the area reads as exhausted after ONE tree, and the
        // character loops on it -- 231 swings at a single trunk in one live
        // session.
        bool allGuarded = false;
        const bool found = client.NearestTree(obs.x, obs.y, cfg_.searchRadius,
                                              &tree, &visitedTrees_,
                                              &allGuarded);
        if (!found && allGuarded) {
            // OWNER RULE: no gathering inside guarded zones. Every candidate
            // this scan saw stands inside the guard line (Tarath chopped a
            // tree at 1449,1635 inside guarded a_townBritain before this
            // check existed) -- this is a town square, not an empty forest,
            // so do not dead-list the lead. Fall through to the
            // proven-stand/travel logic above by marking the area exhausted;
            // the NEXT tick re-enters at "am I actually where the work is?"
            // and walks to a stand that has actually paid out.
            LogLine("gather: nothing to take outside the guard line here -- "
                    "going to the proven stand");
            areaExhausted_ = true;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        if (!found) {
            LogLine("gather: every tree within %d tiles is worked out -> "
                    "this area is done for now", cfg_.searchRadius);
            // Charge the FAILURE TO THE LEAD that sent us here, so the next
            // trip picks a different named forest instead of walking back to
            // the same dry one. Without this the hint list never reorders and
            // the character loops on its nearest disappointment.
            if (lastHintX_ != 0 || lastHintY_ != 0) {
                state_.memory.NoteResource("logs", lastHintX_, lastHintY_, obs.z,
                                           false, obs.nowMs);
                lastHintX_ = lastHintY_ = 0;
            }
            visitedTrees_.clear();
            // DO NOT complete the goal here. "This area is done" is a reason
            // to GO SOMEWHERE ELSE, not a reason to hand control back -- and
            // handing it back put the character in a 2.5-second loop: the
            // planner re-picked GATHER_LOGS (still the top need), the
            // character was still standing in the same worked-out clearing,
            // and it said the same sentence again. Forty completions with
            // progress=0 in under two minutes, live.
            //
            // This is also the M4 Session L churn -- 22 goals attempted, 1
            // completed -- finally visible.
            deadTargets_.emplace_back(obs.x, obs.y);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            areaExhausted_ = true;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        chopX_ = tree.x; chopY_ = tree.y; chopZ_ = tree.z;
        chopGraphic_ = tree.graphic;
        chopTargetValid_ = true;
        chopCursorPending_ = false;
        swingsOnTree_ = 0;
    }

    // Adjacency is read from the LIVE position, not from the tick's snapshot:
    // the walk finishes between ticks, and swinging from the stale reading is
    // what earned "You can't reach this." eight times per tree in the first
    // live run. A tree is surrounded by eight cells and some of them are other
    // trees, so try them nearest-first rather than always stepping east.
    // RANGE=2 in skill44_lumberjacking.scp -- the shard's own number. Standing
    // adjacent is not required, and demanding it wastes walks.
    if (TileDist(chopX_, chopY_, client.PlayerX(), client.PlayerY()) > 2) {
        if (client.GotoBusy()) return false;
        if (approachCell_ >= 8) {
            LogLine("cannot stand next to the tree at %d,%d -> moving on",
                    chopX_, chopY_);
            visitedTrees_.emplace_back(chopX_, chopY_);
            chopTargetValid_ = false;
            approachCell_ = 0;
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        static const int kdx[8] = {1, -1, 0,  0, 1,  1, -1, -1};
        static const int kdy[8] = {0,  0, 1, -1, 1, -1,  1, -1};
        // Order the eight cells by how close they are to where we stand now,
        // then walk to the next untried one.
        int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        const i32 px = client.PlayerX(), py = client.PlayerY();
        std::stable_sort(order, order + 8, [&](int a, int b) {
            return TileDist(chopX_ + kdx[a], chopY_ + kdy[a], px, py) <
                   TileDist(chopX_ + kdx[b], chopY_ + kdy[b], px, py);
        });
        const int pick = order[approachCell_++];
        client.ActionGoto(chopX_ + kdx[pick], chopY_ + kdy[pick]);
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    approachCell_ = 0;

    // --- swing --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (chopCursorPending_) {
        if (client.TargetActive()) {
            client.ActionTargetStatic(chopX_, chopY_, chopZ_, chopGraphic_);
            chopCursorPending_ = false;
            lastChopMs_ = obs.nowMs;
            chopSwungJournalMs_ = client.JournalNowMs();
            // LET THE CHOP FINISH. skill44_lumberjacking.scp is DELAY=1.6 and
            // Source-X rolls rand(5)+2 strokes per attempt (CCharSkill.cpp),
            // so one chop runs 3.2 to 9.6 seconds. Re-using the axe before it
            // resolves fires @Abort -- "You decide not to chop wood for now."
            // A live session threw away 191 of 240 swings that way, an 80%
            // loss that looked like bad luck rather than a bug.
            //
            // The wait is cut short the moment the pack gains a log, so a
            // fast success is not paid for twice (see the yield check below)
            // -- and equally by any of Sphere's DEFINITIVE answers, checked
            // on a short poll below.
            nextActionMs_ = obs.nowMs + kChopPollMs;
            return false;
        }
        if (obs.nowMs - lastChopMs_ > 6000) {
            chopCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Did the last swing produce anything? The honest signal a client has is
    // the pack: logs went up, or they did not.
    //
    // BOUNDED PER TREE. The first live run swung at one tree for two minutes
    // straight because the "nothing came out" branch was gated on a timer that
    // the next swing kept resetting. Count swings instead of watching a clock:
    // a counter cannot be reset by the thing it is counting.
    // --- has the swing RESOLVED? -------------------------------------------
    //
    // A flat ten-second sleep after every swing was most of the loop's wall
    // clock, and most of it was spent waiting for an answer the server had
    // already given. "There is nothing here to chop" arrives on the FIRST
    // stroke, and with 60% of trees barren by the shard's own resource table
    // (regionresources.scp: 60.0 mr_nothing against 40.0 mr_tree) that is the
    // common case, not the exception. A live run managed seven swings in three
    // minutes and gathered nothing.
    //
    // So the ten seconds is now a CEILING, not a delay: poll briefly and stop
    // the moment Sphere says something conclusive.
    if (!chopCursorPending_ && lastChopMs_ != 0 &&
        obs.nowMs - lastChopMs_ < kChopResolveMs && obs.logs <= logsSeen_) {
        static const char* kResolved[] = {
            "there is nothing here to chop",       // barren: move on NOW
            "but fail to produce any useable wood",// attempt resolved, no yield
            "you decide not to chop wood for now", // @Abort
            "that is too far away",
            "you can't reach this",
        };
        bool done = false;
        for (const char* line : kResolved) {
            if (client.JournalSaidSince(line, chopSwungJournalMs_)) {
                done = true;
                break;
            }
        }
        if (!done) {
            nextActionMs_ = obs.nowMs + kChopPollMs;
            return false;
        }
        // Resolved with no wood. Fall through: the swing counter below decides
        // whether to try this tree once more or move to the next one.
        lastChopMs_ = 0;
    }

    if (obs.logs > logsSeen_) {
        // The chop resolved early and paid out -- stop waiting out the stroke
        // window and swing again.
        nextActionMs_ = obs.nowMs;
        logsSeen_ = obs.logs;
        swingsOnTree_ = 0;
        planner_.NoteProgress();
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, true, obs.nowMs);
        if (!state_.memory.HasEvent("first_logs")) {
            state_.memory.NoteEvent("first_logs", "first logs gathered", "forest",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("first logs gathered at %d,%d (pack now holds %d)", obs.x, obs.y,
                    obs.logs);
        }
    } else if (swingsOnTree_ >= kMaxSwingsPerTree) {
        // A barren tree is NOT a failure. `regionresources.scp` gives
        // `r_default_tree` 60.0 mr_nothing against 40.0 mr_tree, so three trees
        // in five hold no wood at all and Source-X says so on the first swing
        // ("There is nothing here to chop", CCharSkill.cpp:1682). Counting
        // that against the goal's attempt budget ended GATHER_LOGS every few
        // trees in the live run -- the character was working correctly and
        // being told it had failed.
        LogLine("tree at %d,%d holds no wood -> next tree (%d tried this stand)",
                chopX_, chopY_, static_cast<int>(visitedTrees_.size()) + 1);
        // DO NOT MarkStump here. It rewrites the tree's graphic in OUR OWN
        // statics overlay, so the next TreeCount() sees nothing -- the bot
        // blinds itself, concludes it is not at a work site, "travels" zero
        // tiles to where it already stands, and loops, charging a failure onto
        // a genuinely productive stand every time round. `visitedTrees_`
        // already stops us re-picking this tree, and it does it without
        // lying to the census.
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, false, obs.nowMs);
        visitedTrees_.emplace_back(chopX_, chopY_);
        if (visitedTrees_.size() > 64) visitedTrees_.erase(visitedTrees_.begin());
        chopTargetValid_ = false;
        swingsOnTree_ = 0;
        return false;
    }

    const u32 axe = AxeSerialInHand(client);
    if (!axe) {
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    client.ActionUseObject(axe);
    chopCursorPending_ = true;
    swingsOnTree_++;
    lastChopMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}

// ---------------------------------------------------------------------------
// FISH -- the one gold faucet a character can reach on day one.
//
// Every number here is the shard's own, read out of
// runtime/scripts/skills/skill18_fishing.scp rather than assumed:
//
//   DELAY=8.0     one cast takes eight seconds
//   RANGE=4       water four tiles away is reachable
//   FLAGS=skf_gather
//   @PreStart     refuses outright while mounted
//
// The answers Sphere gives are equally its own, and they are the only honest
// signal that a cast resolved:
//
//   "You fish a while, but fail to catch anything."
//   "You pull your line back in and stop fishing."
//
// The same lesson the axe taught applies: a flat sleep after every cast spends
// most of the loop waiting for an answer the server already gave, so the eight
// seconds is a CEILING and the goal polls for a verdict.
// ---------------------------------------------------------------------------
// Every kind of fish the sea yields, counted as one catch. The graphic table
// listed only i_fish_big_1 for a long while, so a character could pull a fish
// out of the water and its own pack counter would report nothing -- the catch
// was real, the blindness was ours (VendorPolicy.cpp kGraphics).
static i32 FishInPack(const std::vector<market::Stock>& pack) {
    static const char* kKinds[] = {"i_fish_big_1", "i_fish_big_2",
                                   "i_fish_big_3", "i_fish_big_4",
                                   "i_fish_small"};
    i32 n = 0;
    for (const char* k : kKinds) n += market::QtyOf(pack, k);
    return n;
}

bool Runner::DoSmelt(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // Prefer plain iron (hue 0) over a coloured vein when both are in the
    // pack -- see FindIronOrePreferPlain's comment. oreHue is what actually
    // got picked, logged below at the point the ore is targeted.
    u16 oreHue = 0;
    const u32 ore = FindIronOrePreferPlain(client, &oreHue);
    if (!ore) {
        LogLine("smelt: no ore in the pack to melt");
        smeltStartedMs_ = 0;
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // WHAT THIS ORE WILL BECOME (S1). FindIronOrePreferPlain reaches for a
    // COLOURED vein once the plain iron is gone, and the ore's hue is the
    // only thing that says which metal it is -- so the ingot it smelts into
    // is decided here, from that hue, and not assumed to be iron. The ore ->
    // ingot step is the ore ITEMDEF's own TDATA1; see econ::IngotNameForOre.
    const char* pickedOre = econ::ItemNameForGraphicAndHue(kIronOre[0], oreHue);
    const char* pickedIngot = pickedOre ? econ::IngotNameForOre(pickedOre) : nullptr;
    if (!pickedIngot) pickedIngot = "i_ingot_iron";   // honest fallback

    // DID THE LAST DOUBLE-CLICK LAND? The pack is the only honest witness.
    // Both outcomes are clilocs -- 1044270 on success, craft_smelt_fail on a
    // failed roll -- and 0xC1 is an explicit no-op in this client, so there is
    // nothing to read in the journal. Counting metal is the truth. This is the
    // same reasoning DoCraft states for inscription.
    //
    // COUNT THE METAL THAT IS ACTUALLY BEING MADE. This used to read
    // i_ingot_iron unconditionally: melt a bag of valorite ore and the count
    // never moved, so the goal reported no progress while producing the most
    // valuable thing on the shard -- and if it had moved it would have been
    // crediting a number nobody made.
    const i32 metal = market::QtyOf(obs.pack, pickedIngot);
    if (smeltIngotName_ != pickedIngot) {
        // The metal changed under us -- the old baseline counted a different
        // ingot entirely, so retake it rather than compare across metals.
        smeltIngotName_ = pickedIngot;
        smeltIngotsBefore_ = metal;
    }
    if (smeltStartedMs_ != 0 && metal > smeltIngotsBefore_) {
        LogLine("smelt: +%d %s (%d in the pack)", metal - smeltIngotsBefore_,
                pickedIngot, metal);
        planner_.NoteProgress();
        if (!state_.memory.HasEvent("first_smelt")) {
            state_.memory.NoteEvent("first_smelt", pickedIngot, "", obs.x,
                                    obs.y, obs.nowMs);
        }
    }
    smeltIngotsBefore_ = metal;

    // The one smelt message that IS plain ASCII, and so the one the journal
    // can actually see: everything else on this path is a cliloc and 0xC1 is a
    // no-op here. If it appears, the character is not close enough -- say so
    // rather than swinging again from the same spot.
    if (smeltStartedMs_ != 0 &&
        client.JournalSaidSince("must be near a forge", smeltStartedMs_)) {
        // GIVE UP ON A FORGE THAT CANNOT BE STOOD NEXT TO. Some are behind a
        // counter or against a wall, so no walkable tile is ever adjacent --
        // the lone forge beside the Minoc armorer is a candidate. Rather than
        // swing at it forever, strike it off and let NearestForge offer the
        // next one; in Minoc that means The Forgery, which has six of them.
        if (++smeltRefusals_ >= 3) {
            LogLine("smelt: the forge at %d,%d refuses from every tile reached "
                    "-- looking for another", smeltForgeX_, smeltForgeY_);
            deadForges_.emplace_back(smeltForgeX_, smeltForgeY_);
            if (deadForges_.size() > 16) deadForges_.erase(deadForges_.begin());
            smeltRefusals_ = 0;
            travelInFlight_ = false;
        } else {
            LogLine("smelt: refused -- not close enough to the forge yet (%d)",
                    smeltRefusals_);
        }
    }

    // A FORGE WITHIN TWO TILES, or go and find one.
    //
    // FROM THE MAP, NOT FROM THE ITEM LIST. sphereworld.scp and
    // spherestatics.scp contain ZERO forges between them -- every forge here
    // is original UO map content in statics0.mul, so the server never sends
    // one as an item and the first version of this, which asked
    // FindWorldItemByGraphic, stood inside a smithy reporting "no forge within
    // 2 tiles" three times and gave up.
    // AS FAR AS THE SERVER SENDS ITEMS, then walk the last few tiles. There
    // is no point asking for more: a forge outside the item-send radius is not
    // in the item list at all, so a bigger number would only look thorough.
    //
    // This is usually enough on its own. A forge stands beside the Minoc
    // armorer at 2535,571, and another INSIDE the Minoc mine at 2561,501 --
    // four tiles from the rock Corwyn actually swings at -- so a miner very
    // often smelts without going anywhere.
    Client::TreeHit forgeTile;
    const bool sawForge =
        client.NearestForge(obs.x, obs.y, 20, &forgeTile, &deadForges_);
    const i32 forgeDist =
        sawForge ? TileDist(obs.x, obs.y, forgeTile.x, forgeTile.y) : 0;

    if (sawForge && forgeDist > kForgeReach) {
        if (client.TravelBusy()) return false;

        // A FORGE YOU CANNOT GET NEXT TO IS NOT A FORGE YOU CAN USE. Many
        // stand against a wall or behind a counter with no walkable tile
        // adjacent -- the lone one beside the Minoc armorer is exactly that.
        // Walking "to" it then succeeds at two tiles forever, and because the
        // character never gets close enough to CLICK, the refusal message that
        // would otherwise retire the forge never arrives. So count approaches
        // as well as refusals. TWO is enough: the second walk to the same
        // stand tile that did not get us within reach tells us nothing the
        // first did not. Four cost Elvar 11 s of visible pacing at the mine
        // forge (2561,501) every session -- "if it is unreachable then it
        // should be 1 try max 2" (project owner, 2026-09-02).
        if (forgeTile.x == smeltForgeX_ && forgeTile.y == smeltForgeY_) {
            if (++smeltApproaches_ >= 2) {
                LogLine("smelt: cannot get within %d tile of the forge at %d,%d "
                        "after %d tries -- looking for another",
                        kForgeReach, forgeTile.x, forgeTile.y, smeltApproaches_);
                deadForges_.emplace_back(forgeTile.x, forgeTile.y);
                if (deadForges_.size() > 16)
                    deadForges_.erase(deadForges_.begin());
                smeltApproaches_ = 0;
                travelInFlight_ = false;
                // The trip counter is what decides to walk somewhere new, and
                // the skip list above is what makes "somewhere new" mean a
                // different building rather than this one again.
                smeltTrips_ = 0;
                nextActionMs_ = obs.nowMs + 500;
                return false;
            }
        } else {
            smeltForgeX_ = forgeTile.x;
            smeltForgeY_ = forgeTile.y;
            smeltApproaches_ = 1;
        }

        // WALK TO A TILE BESIDE THE FORGE, NEVER TO THE FORGE ITSELF. A forge
        // is solid: asking the planner for its own tile made it search for the
        // best part of a second and then give up --
        //   "no path to (2468,557) avoiding 0 block(s) (search 976432.8us)"
        //   "start (2472,550,5) exits: open=8" -- not enclosed; unreachable.
        // Naming a real standing tile turns that into an ordinary short walk.
        i32 standX = 0, standY = 0;
        bool haveStand = false;
        static const int kdx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int kdy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        for (int i = 0; i < 8 && !haveStand; ++i) {
            const i32 tx = forgeTile.x + kdx[i], ty = forgeTile.y + kdy[i];
            if (!client.TileIsWalkable(tx, ty, forgeTile.z)) continue;
            standX = tx; standY = ty; haveStand = true;
        }
        if (!haveStand) {
            // Nothing to stand on next to it -- that is the whole story for
            // the lone forge beside the Minoc armorer. No point approaching
            // four times to learn it.
            LogLine("smelt: no walkable tile beside the forge at %d,%d -- "
                    "striking it off", forgeTile.x, forgeTile.y);
            deadForges_.emplace_back(forgeTile.x, forgeTile.y);
            if (deadForges_.size() > 16) deadForges_.erase(deadForges_.begin());
            smeltApproaches_ = 0;
            travelInFlight_ = false;
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }

        LogLine("smelt: forge at %d,%d is %d tiles off -- standing at %d,%d",
                forgeTile.x, forgeTile.y, forgeDist, standX, standY);
        travelInFlight_ = client.TravelToPoint(standX, standY, 0, "forge");
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // DO NOT SWING WHILE STILL WALKING. The first version issued the
    // double-click in the same tick it started the last step, so the click
    // went out from two tiles away and the shard refused it -- four times in
    // a row, each one looking like a smelt that simply did nothing.
    if (client.TravelBusy()) return false;

    if (!sawForge) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // Forges stand in smithies, and the atlas files smithies -- and
            // the weaponsmiths beside them -- under `blacksmith`.
            // ADVANCE TO THE NEXT SMITHY, do not keep arriving at the same
            // one. The atlas files two Minoc buildings as `blacksmith`:
            // minoc_armorer at 2533,572, whose single forge stands where no
            // adjacent tile can be reached, and minoc_blackshmith at 2471,564
            // inside The Forgery, which has SIX. Travel quite reasonably picks
            // the nearer, so without a skip list the character walks to the
            // armorer forever and never sees the real smithy.
            // "why dont you go minoc_blacksmith ... the real smithy"
            // (project owner, 2026-08-29, asked twice).
            LogLine("smelt: carrying %d ore with no forge in reach -- walking "
                    "to a smithy (%d already tried)",
                    market::QtyOf(obs.pack, "i_ore_iron"),
                    static_cast<int>(smeltSkipPlaces_.size()));
            travelInFlight_ = client.TravelToServiceSkipping(
                wm::Service::Blacksmith, HomeOrNearest(state_.homeCity), {},
                &smeltSkipPlaces_);
            if (!travelInFlight_) {
                LogLine("goal_blocked=SMELT reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                return HandOff(GoalKind::Smelt, GoalKind::IdleBriefly, 60000,
                               "no forge reachable", obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        // Arrived, but the forge is not within the shard's two tiles. Say so
        // plainly rather than clicking into the void -- a smithy whose forge
        // cannot be stood next to is worth knowing about.
        travelInFlight_ = false;
        if (++smeltTrips_ >= 3) {
            LogLine("smelt: %d trips to a smithy and no forge within %d tiles "
                    "-- giving up for now", smeltTrips_, kForgeReach);
            smeltTrips_ = 0;
            return HandOff(GoalKind::Smelt, GoalKind::IdleBriefly, 120000,
                           "arrived but no forge in reach", obs.nowMs);
        }
        LogLine("smelt: at the smithy but no forge within %d tiles (trip %d)",
                kForgeReach, smeltTrips_);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // A FORGE THAT WORKS ENDS THE SEARCH. Without this the skip list only
    // ever grew: once Minoc's smithies were on it, "the nearest blacksmith not
    // yet tried" became Vesper, and the character walked out of its own city
    // to smelt. ("why corwyn in vesper?")
    smeltTrips_ = 0;
    smeltSkipPlaces_.clear();
    travelInFlight_ = false;
    smeltForgeX_ = forgeTile.x;
    smeltForgeY_ = forgeTile.y;
    smeltApproaches_ = 0;

    // CLICK THE FORGE, THEN THE ORE -- not the ore on its own.
    //
    // "Also forge works I double clicked forge then ore" (project owner,
    // 2026-08-29), and the scripts agree. types_forge.scp:
    //     [TYPEDEF t_forge]
    //     ON=@DCLICK
    //        TARGETF f_craft_blacksmith_smelt_targ
    // so the forge arms a target cursor, and that cursor is then given the
    // ore. f_craft_blacksmith_smelt_targ (crafting_functions.scp) checks the
    // ore is in the pack, checks ISNEARTYPE t_forge 3, and for a t_ore target
    // delegates to the ore's own @dclick.
    //
    // The first version clicked the ore directly. That is the OTHER route --
    // type_ore.scp's @dclick -- and it is the one that answered "You must be
    // near a forge to smelt" from two tiles away, over and over.
    if (smeltCursorPending_) {
        if (client.TargetActive()) {
            // What is actually being melted -- hue-resolved at the top of
            // this function, so a coloured vein is named honestly instead of
            // logged as "i_ore_iron" (S1,
            // docs/CRAFTER_RUN_2026_08_30.md #20). The ore count is read
            // against that same name too: obs.pack now keys a coloured vein
            // by its own defname, so asking it for "i_ore_iron" while melting
            // rusty ore printed "0 ore" with a full pack.
            LogLine("smelt: giving the forge's cursor the ore (%s hue 0x%04X, "
                    "%d ore, %d %s so far)", pickedOre ? pickedOre : "?",
                    oreHue,
                    market::QtyOf(obs.pack, pickedOre ? pickedOre : "i_ore_iron"),
                    metal, pickedIngot);
            client.ActionTargetObject(ore);
            smeltCursorPending_ = false;
            smeltReachFails_ = 0;   // the forge answered; the spot is good
            smeltStartedMs_ = obs.nowMs;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        // The cursor never came up. Do not sit waiting on it forever.
        if (obs.nowMs - smeltClickedMs_ > 6000) smeltCursorPending_ = false;
        return false;
    }

    // THE SERVER SAID NO, AND IT MEANT IT.
    //
    // "You can't reach that." is a definitive refusal, and this goal used to
    // discard it and click again: 311 identical "opening the forge at
    // 2561,501" lines in one session (run_m7/r1b_Corwyn.console.txt), the bot
    // standing at 2560,500 -- ONE DIAGONAL TILE away -- and every click
    // refused in under 20 ms. It is the third path this week to re-issue an
    // action the server had already answered, after the vendor open and the
    // vendor buy, which is what the shared interaction layer exists to end.
    //
    // A diagonal is not always reachable on this shard: a forge is a multi-
    // tile static and the tile the atlas names is not necessarily the one that
    // can be touched. So a refusal means MOVE, not repeat -- walk onto the
    // forge's own entity and try from there. After a few of those the forge
    // itself is the problem, not the standing spot, and the errand stands
    // down so a different forge (or a different goal) gets the turn.
    if (client.ActionKind() == act::Kind::UseObject &&
        client.ActionResult() == act::Result::Rejected &&
        client.CurrentAction().subject == client.LastForgeSerial()) {
        if (++smeltReachFails_ > kMaxSmeltReachFails) {
            // RETIRE IT IN THE LIST THE FORGE FINDER ACTUALLY READS.
            //
            // This wrote to deadTargets_ -- the MINING dead list -- while
            // NearestForge is passed deadForges_ (see the call above). So the
            // unreachable forge was never skipped, and the same one was
            // nominated again on the next pick, and the next:
            //
            //   goal_failed=SMELT reason="the forge at 2561,501 refused every
            //   approach after 3 tries"
            //
            // -- at 17:53 and again at 18:01, the identical tile, with a
            // pack of ore each time. (2561,501) sits just inside Minoc Mine
            // 1, so the forge a miner naturally walks to is the one he cannot
            // stand beside; there are others in Minoc, and now he can reach
            // for them.
            LogLine("smelt: the forge at %d,%d refused every approach after "
                    "%d tries -- writing it off and looking for another",
                    forgeTile.x, forgeTile.y, smeltReachFails_ - 1);
            deadForges_.emplace_back(forgeTile.x, forgeTile.y);
            if (deadForges_.size() > 16) deadForges_.erase(deadForges_.begin());
            smeltReachFails_ = 0;
            smeltForgeX_ = smeltForgeY_ = 0;
            // NOT a goal failure: the errand still has ore to melt and another
            // forge to melt it at. Failing here cooled SMELT down for minutes
            // and sent the character back to mining ore it could not process.
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }
        LogLine("smelt: \"you can't reach that\" from %d,%d -- walking onto "
                "the forge itself (try %d of %d)", obs.x, obs.y,
                smeltReachFails_, kMaxSmeltReachFails);
        travelInFlight_ = client.TravelToEntity(client.LastForgeSerial(), 0);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    LogLine("smelt: opening the forge at %d,%d", forgeTile.x, forgeTile.y);
    client.ActionUseObject(client.LastForgeSerial());
    smeltCursorPending_ = true;
    smeltClickedMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 1500;
    return false;
}

bool Runner::DoFish(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // CUT THE FISH UP. "nessa fishing at same spot constantly -- does he ever
    // cut fishes into raw fish?" (project owner, 2026-08-29). She did not: she
    // fished until "pack full" twenty times in a session and stopped, carrying
    // whole fish that are heavy, unsellable in that form and inedible.
    //
    // One whole fish yields FOUR cut steaks (Production.cpp i_fish_cut_raw,
    // Tool::Blade, no station and no skill), and the gesture is the same
    // use-one-thing-on-another as the bandage chain -- a dagger, which every
    // starter kit now carries as ITEMNEWBIE.
    //
    // Done BEFORE the pack-full check on purpose: cutting is what makes room,
    // so a full pack is a reason to cut rather than a reason to stop.
    // CUT IN BATCHES, BECAUSE CUTTING COSTS THE POLE.
    //
    // Source-X wields a bladed weapon that is double-clicked in the pack, and
    // i_fishing_pole is TWOHANDS=Y -- so every single cut takes the pole out of
    // the hands and puts it in the pack. Cutting one fish the instant it is
    // caught therefore costs a re-arm per fish: wave 2026-09-02 logged 643
    // "arming the pole" lines for Ithion in thirty minutes, one pair per catch.
    // Waiting for a handful of fish (or a pack heavy enough that the weight is
    // the problem) pays for the swap once instead of once per fish.
    //
    // The blade is looked for IN THE HAND AS WELL AS THE PACK (FindBlade). The
    // pack-only lookup was the other half of the ping-pong: once the dagger was
    // wielded the runner could no longer see it, fell through to the arming
    // branch, re-armed the pole, and had to wield the dagger again next tick.
    const i32 wholeFish = CountAny(client, kWholeFish,
                                   sizeof(kWholeFish) / sizeof(kWholeFish[0]));
    const usize kBladeN = sizeof(kBladedGraphics) / sizeof(kBladedGraphics[0]);
    const bool bladeInHand =
        GraphicIsAny(client.EquippedGraphicAt(kLayerHand1), kBladedGraphics,
                     kBladeN) ||
        GraphicIsAny(client.EquippedGraphicAt(kLayerHand2), kBladedGraphics,
                     kBladeN);
    // Once the knife is out, finish the pile: stopping half way would pay the
    // swap twice.
    const bool worthCutting =
        wholeFish >= kFishCutBatch || obs.WeightFraction() >= 0.85 ||
        (bladeInHand && wholeFish > 0);
    if (worthCutting) {
        if (const u32 blade = FindBlade(client)) {
            for (u16 g : kWholeFish) {
                const u32 whole = client.FindBackpackItemByGraphic(g);
                if (!whole) continue;
                LogLine("fish: cutting a whole fish (0x%04X) into steaks -- "
                        "four each, and lighter (%d whole left, blade %s)",
                        g, wholeFish, bladeInHand ? "in hand" : "in pack");
                client.ActionUseItemOn(blade, whole);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
    }

    if (obs.WeightFraction() >= kGathererPackFullFrac) {
        LogLine("fish: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        return true;
    }

    // The pole has to be IN HAND, the same way the axe does: skf_gather reads
    // the character's weapon, and a pole in the pack is not a pole in hand.
    // TAKE THE SERIAL FROM THE LAYER IT WAS FOUND ON.
    //
    // The first version noticed the pole was equipped and then asked the PACK
    // for its serial -- which is 0, because it is not in the pack -- and fell
    // back to hand1 regardless of which hand held it. i_fishing_pole is
    // TWOHANDS=Y so the kit arms it automatically, and the result was a fisher
    // standing beside water reporting REFUSE_MISSING_TOOL while holding one.
    //
    // That also produced a wrong conclusion on the way: the pack dump showed
    // no pole, and [NEWBIE FISHING] was very nearly recorded as BLOCKED_RUNTIME
    // when the kit had worked correctly all along.
    const std::vector<u16> poleGfx = econ::GraphicsForItem("i_fishing_pole");
    u32 pole = 0;
    for (u16 g : poleGfx) {
        if (client.EquippedGraphicAt(kLayerHand1) == g) {
            pole = client.EquippedAtLayer(kLayerHand1);
            break;
        }
        if (client.EquippedGraphicAt(kLayerHand2) == g) {
            pole = client.EquippedAtLayer(kLayerHand2);
            break;
        }
    }
    if (pole) {
        fishArmTries_ = 0;   // it is in hand: whatever we tried, it worked
    } else {
        for (u16 g : poleGfx) {
            const u32 inPack = client.FindBackpackItemByGraphic(g);
            if (!inPack) continue;
            // COUNT THE ATTEMPTS AND BACK OFF.
            //
            // An equip that is refused leaves the item back in the pack, which
            // looks exactly like "never tried" -- so a bare retry is a tight
            // loop with no end (643 of them in one gate). Each failure waits
            // longer, and after six the goal says so instead of spinning.
            if (fishArmTries_ >= 6) {
                LogLine("goal_failed=FISH reason=\"%s\" the pole would not stay "
                        "in hand after %d tries (last serial 0x%08X)",
                        faucet::RefusalName(faucet::Refusal::MissingTool),
                        fishArmTries_, inPack);
                fishArmTries_ = 0;
                planner_.Finish(false, "pole will not arm", obs.nowMs);
                return false;
            }
            ++fishArmTries_;
            LogLine("fish: arming the pole (try %d)", fishArmTries_);
            client.ActionEquip(inPack, kLayerServerChooses);
            nextActionMs_ = obs.nowMs + 1500 * fishArmTries_;
            return false;
        }
        // Say WHAT WAS THERE. "no fishing pole" is not a diagnosis when the
        // question is whether the kit delivered one, whether it is on a layer
        // this code does not read, or whether its graphic is not the one the
        // item table names.
        char seen[192];
        int n = 0;
        seen[0] = 0;
        const u32 pack = client.BackpackSerial();
        const usize count = client.ContainerItemCount(pack);
        for (usize i = 0; i < count && n < 160; ++i) {
            u32 sr = 0; u16 gfx = 0, amt = 0;
            if (!client.ContainerItemAt(pack, i, &sr, &gfx, &amt)) continue;
            n += std::snprintf(seen + n, sizeof(seen) - n, "%s0x%04X",
                               n ? "," : "", gfx);
        }
        LogLine("goal_failed=FISH reason=\"%s\" wanted=0x0DBF/0x0DC0 pack=[%s]",
                faucet::RefusalName(faucet::Refusal::MissingTool), seen);
        planner_.Finish(false, "no fishing pole", obs.nowMs);
        return false;
    }

    // --- has the last cast resolved? --------------------------------------
    // Gated on the target reply having been SENT: fishCastMs_ is also set
    // when the pole is double-clicked, and ungated this branch swallowed the
    // whole 9-second ceiling between cursor and reply doing nothing -- the
    // cast1 live run armed the cursor at :24.6 and answered it at :33.7.
    // The ceiling is NOT one stroke. DELAY=8.0 is per @Stroke, and a round
    // can run more than one before Sphere speaks: the cast3 live run held a
    // line at (1465,1751) for a full 9 seconds with no verdict at all, and
    // @Success (skill18_fishing.scp:37) packs the fish SILENTLY -- so the
    // only signs of a good round are the fish appearing in the pack or,
    // eventually, @Fail's sentence. Twenty-five seconds is three strokes of
    // headroom; on expiry the recast below aborts the stale round cleanly.
    const i64 kFishRoundCeilingMs = 25000;
    if (!fishCursorPending_ && fishCastMs_ != 0 &&
        obs.nowMs - fishCastMs_ < kFishRoundCeilingMs) {
        // Two kinds of answer. A cast that HELD and came up empty means the
        // water is good and the roll said mr_nothing (regionresources.scp:
        // RESOURCES=60.0 mr_nothing); cast there again. A REFUSAL is Sphere
        // saying this tile can never pay -- "There are no fish here." is
        // DEFMSG FISHING_2 (core/messages.scp:158) and came back instantly
        // when the cast1 run targeted near-shore water at (1466,1752) -- so
        // the tile goes on the dead list and the sweep moves on.
        // "You pull your line back in and stop fishing." is DELIBERATELY not
        // a resolution. It is @Abort (skill18_fishing.scp:43-44) -- the echo
        // of this character's own recast cancelling the previous round -- and
        // it arrives moments AFTER the new round's journal mark. Treating it
        // as a verdict resolved every new cast instantly, which recast again,
        // which aborted again: the cast3 run cancelled its own line twice a
        // second for a minute straight.
        static const char* kNoCatch[] = {
            "but fail to catch anything",       // @Fail: resolved, no fish
        };
        static const char* kRefusedHere[] = {
            "there are no fish here",           // no fish resource at the tile
            "try fishing elsewhere",            // FISHING_1, same verdict
            "try fishing in water",             // not water at all
            "can't fish from where you are standing",
            "you can't fish while riding",      // @PreStart refusal
            "cannot fish so close to yourself", // adjacent water is refused
            "target cannot be seen",
            "that is too far away",
        };
        bool done = false;
        for (const char* line : kNoCatch) {
            if (client.JournalSaidSince(line, fishCastJournalMs_)) {
                done = true;
                break;
            }
        }
        if (!done) {
            for (const char* line : kRefusedHere) {
                if (!client.JournalSaidSince(line, fishCastJournalMs_))
                    continue;
                LogLine("fish: refused at %d,%d (\"%s\") -- marking it dead",
                        fishX_, fishY_, line);
                deadTargets_.emplace_back(fishX_, fishY_);
                if (deadTargets_.size() > 32)
                    deadTargets_.erase(deadTargets_.begin());
                done = true;
                break;
            }
        }
        // ALL four fish, not just the first: the region rolls mr_fish1-4
        // (core/regionresources.scp:64-90) and each REAPs its own item, so a
        // counter watching only i_fish_big_1 misses three catches in four --
        // and @Success is silent, so the pack count is the only proof.
        const i32 caught = FishInPack(obs.pack);
        // ...and Sphere announces a catch OUT LOUD as well: "You pull out a
        // fish!" (hardcoded, not in skill18_fishing.scp). In the cast4 live
        // run that sentence arrived at :07:12 and the pack counter never
        // moved -- the round resolved only when the 25 s ceiling expired --
        // so the server's own receipt is trusted directly and the pack count
        // stays as a second witness.
        const bool saidCaught =
            client.JournalSaidSince("you pull out a fish", fishCastJournalMs_);
        if (caught > fishSeen_ || saidCaught) {
            LogLine("fish: caught one at %d,%d (%s)", fishX_, fishY_,
                    saidCaught ? "journal" : "pack count");
            fishSeen_ = caught;
            planner_.NoteProgress();
            // Remember the stand tile (obs.x/obs.y), not the water tile the
            // cast targeted (fishX_/fishY_). BestProvenResource feeds this
            // straight into TravelToPoint on a later trip, and a water tile
            // is never walkable -- that produced Dorvar's "goal (3662,2302)
            // is not walkable" spam (wave 2, 2026-09-01): the character is
            // stationary while fishing, so obs.x/obs.y is exactly the shore
            // tile it is standing on right now.
            state_.memory.NoteResource("fish", obs.x, obs.y, obs.z, true,
                                       obs.nowMs);
            if (!state_.memory.HasEvent("first_fish")) {
                state_.memory.NoteEvent("first_fish", "first fish caught",
                                        "water", obs.x, obs.y, obs.nowMs);
            }
            done = true;
        }
        if (!done) {
            nextActionMs_ = obs.nowMs + kFishPollMs;
            return false;
        }
        fishCastMs_ = 0;
    }

    // --- find water -------------------------------------------------------
    //
    // RANGE=4 is the shard's own number, so a spot further than that is not a
    // spot at all. Searching a wider radius and then walking is the difference
    // between fishing and standing hopefully near a lake.
    // GO TO A DOCK FIRST, and only then look for water.
    //
    // The order used to be the other way round, and it was the whole problem:
    // NearestFishingSpot searches 24 tiles and there is water within 24 tiles
    // of almost anywhere near Britain, so the dock trip only ran when that
    // search FAILED -- which it never did. The character chased the nearest
    // pond from wherever it stood, the route stopped short, it picked again
    // from the new position, and it drifted across the map without once
    // reaching a dock. There is no "Docks ARRIVED" line in any of those runs.
    if (!fishAtDock_) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            ++fishTrips_;
            if (fishTrips_ > kMaxFishTrips) {
                LogLine("goal_failed=FISH reason=\"%s\" no dock reachable "
                        "after %d trips",
                        faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                        fishTrips_);
                planner_.Finish(false, "no dock reachable", obs.nowMs);
                fishTrips_ = 0;
                return false;
            }
            const KnownResourceSource* known =
                state_.memory.BestProvenResource("fish", obs.x, obs.y, obs.nowMs);
            if (known) {
                LogLine("fish: back to a shore that has paid out at %d,%d",
                        known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 4, "fishing spot");
            } else {
                LogLine("fish: going to a dock (trip %d)", fishTrips_);
                travelInFlight_ =
                    client.TravelToResource(wm::ResourceKind::Fishing);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=FISH reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE, and the proof is water nearby.
        Client::FishingSpot probe;
        if (!client.NearestFishingSpot(client.PlayerX(), client.PlayerY(), 12,
                                       &probe)) {
            LogLine("fish: trip reported %s but there is no shore within 12 "
                    "tiles of %d,%d",
                    client.TravelSucceeded() ? "success" : "failure",
                    client.PlayerX(), client.PlayerY());
            deadTargets_.emplace_back(client.PlayerX(), client.PlayerY());
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        LogLine("fish: at the water's edge (%d,%d), shore %d,%d",
                client.PlayerX(), client.PlayerY(), probe.standX, probe.standY);
        fishAtDock_ = true;
        fishTrips_ = 0;
        state_.memory.NoteResource("fish", client.PlayerX(), client.PlayerY(),
                                   client.PlayerZ(), true, obs.nowMs);
        return false;
    }

    // AT A DOCK: cast at whatever water is already in range.
    //
    // Every previous version picked a "stand tile" and then tried to walk to
    // it, and every one of them failed differently -- and the post-mortem
    // found ONE cause under all of them: the client could not see the water
    // it was standing next to. Around Britain's docks the sea is wet STATICS
    // over impassable dry-by-tiledata land (see WaterHit in Client.h), so the
    // land-only water search reported "no water 2-4 tiles" while castable
    // water sat 2 tiles away, and every walk was aimed at either a wet tile
    // (an unwalkable A* goal by definition) or a "dry" tile under a water
    // static (walkable:false -- "goal not walkable"). With both water forms
    // visible, standing at the edge is usually already enough.
    //
    // 2 to 4 tiles: RANGE=4 is the maximum from skill18_fishing.scp, and the
    // minimum is the server's own "You cannot fish so close to yourself."

    // A shore hop is in flight. Its deadline is checked BEFORE the
    // GotoBusy early-return -- checked after, a hung walk means the ceiling
    // can never fire.
    if (fishTargetSet_) {
        if (obs.nowMs > fishWalkMs_) {
            LogLine("fish: shore hop to %d,%d timed out at %d,%d",
                    fishTargetX_, fishTargetY_, obs.x, obs.y);
            fishTargetSet_ = false;
            deadTargets_.emplace_back(fishTargetX_, fishTargetY_);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            fishAtDock_ = false;   // re-approach through the travel path
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (client.GotoBusy()) return false;
        // The walk resolved -- arrived, or A* stopped as close as it could.
        // Either way the next tick asks the only question arrival ever
        // poses, "is there castable water from HERE", against a FRESH
        // observation; scanning this tick's snapshot after a walk is the
        // stale-position bug the chop goal already paid for. The tile is
        // marked tried first so a hop that resolved somewhere useless is
        // never picked again.
        LogLine("fish: shore hop done at %d,%d (wanted %d,%d)",
                client.PlayerX(), client.PlayerY(), fishTargetX_, fishTargetY_);
        deadTargets_.emplace_back(fishTargetX_, fishTargetY_);
        if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
        fishTargetSet_ = false;
        nextActionMs_ = obs.nowMs + 300;
        return false;
    }
    // When Sphere has already refused SEVERAL waters around this stand, the
    // whole near band is fishless and probing the rest of the ring one cast
    // at a time is just slower agreement. Refusals are structural, not luck:
    // an empty roll answers "you fish a while, but fail to catch anything"
    // (RESOURCES=60.0 mr_nothing, core/regionresources.scp:93), while "There
    // are no fish here." is about the TILE. Three of those within casting
    // range is enough evidence to move along the shore instead. Only refused
    // WATER counts -- the dead list also holds failed stand tiles, and those
    // say nothing about fish.
    // EXACT-tile matching for cast bookkeeping. IsDeadTarget deliberately
    // matches within EIGHT tiles (Runner.cpp:766) because it judges travel
    // destinations -- "this clearing is treeless" -- and that radius is
    // right for those. Applied to per-tile casts it is catastrophic: one
    // "There are no fish here." at (1466,1752) blackened the whole dock,
    // and the very next scan reported "no water 2-4 tiles ... and no shore
    // to hop to" from a stand with live water three tiles away (cast2 run).
    auto deadExact = [&](i32 x, i32 y) -> bool {
        for (const auto& d : deadTargets_)
            if (d.first == x && d.second == y) return true;
        return false;
    };

    int refusedNear = 0;
    for (const auto& d : deadTargets_) {
        if (std::max(std::abs(d.first - obs.x), std::abs(d.second - obs.y)) > 4)
            continue;
        Client::WaterHit dw;
        if (client.NearestWater(d.first, d.second, 0, &dw)) ++refusedNear;
    }

    Client::WaterHit water;
    bool haveTarget = false;
    for (int r = 2; r <= 4 && !haveTarget && refusedNear < 3; ++r) {
        for (i32 dy = -r; dy <= r && !haveTarget; ++dy) {
            for (i32 dx = -r; dx <= r && !haveTarget; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                Client::WaterHit w;
                if (!client.NearestWater(obs.x + dx, obs.y + dy, 0, &w)) continue;
                if (deadExact(w.x, w.y)) continue;
                water = w;
                haveTarget = true;
            }
        }
    }
    if (!haveTarget) {
        // No castable water from where we stand. That makes this a SHORT
        // WALK problem, not a search problem: NearestFishingSpot now vets
        // its stand tile with the pathfinder's own walkability query, so a
        // spot it returns is a goal ActionGoto's A* will accept -- the
        // missing property that killed every earlier walk. Commit to the
        // tile ONCE: re-picking from the current position every tick is
        // what made attempt 2's target drift from 9 tiles out to 20.
        // The dead list rides along so refused water is not re-nominated:
        // without it the sweep down the pier proposes the same "no fish
        // here" tiles from every new stand.
        Client::FishingSpot spot;
        if (!client.GotoBusy() &&
            client.NearestFishingSpot(client.PlayerX(), client.PlayerY(), 12,
                                      &spot, &deadTargets_) &&
            !deadExact(spot.standX, spot.standY) &&
            !(spot.standX == client.PlayerX() &&
              spot.standY == client.PlayerY())) {
            LogLine("fish: shore hop %d,%d -> %d,%d (water %d,%d)",
                    client.PlayerX(), client.PlayerY(),
                    spot.standX, spot.standY, spot.waterX, spot.waterY);
            client.ActionGoto(spot.standX, spot.standY);
            fishTargetX_ = spot.standX;
            fishTargetY_ = spot.standY;
            fishTargetSet_ = true;
            // A ceiling, not a wait: the hop is at most 12 tiles of route,
            // and 15 s is roomy even at a walk. Timed-out hops are marked
            // dead above, so this cannot retry the same tile forever, and
            // the planner's own attempt cap bounds the whole goal.
            fishWalkMs_ = obs.nowMs + 15000;
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        // No walkable shore to hop to either: this place is a dud.
        LogLine("fish: no water 2-4 tiles from %d,%d and no shore to hop to "
                "-- finding another dock", obs.x, obs.y);
        fishAtDock_ = false;
        deadTargets_.emplace_back(obs.x, obs.y);
        if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    fishTrips_ = 0;

    // --- cast --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (fishCursorPending_) {
        if (client.TargetActive()) {
            // Answer the cursor the way a classic client answers a click on
            // the water the player SEES. Coastline water is a static and a
            // static reply carries its graphic; open sea is wet land and
            // gets a ground reply. Sphere types both as fishable t_water
            // (items/i_ground_tiles.scp:733 [ITEMDEF 01796] TYPE=T_WATER,
            // DUPELIST through 017b2), so the difference is fidelity, not
            // permission -- and water.z is the surface actually targeted:
            // the static's own z (-5 at the Britain dock), not the land
            // buried under it (-15).
            if (water.graphic != 0) {
                client.ActionTargetStatic(water.x, water.y, water.z,
                                          water.graphic);
            } else {
                client.ActionTargetGround(water.x, water.y, water.z);
            }
            fishCursorPending_ = false;
            fishCastMs_ = obs.nowMs;
            fishCastJournalMs_ = client.JournalNowMs();
            fishX_ = water.x; fishY_ = water.y;
            fishSeen_ = FishInPack(obs.pack);
            nextActionMs_ = obs.nowMs + kFishPollMs;
            return false;
        }
        if (obs.nowMs - fishCastMs_ > 6000) {
            fishCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    client.ActionUseObject(pole);
    fishCursorPending_ = true;
    fishCastMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}

// ---------------------------------------------------------------------------
// MINING.
//
// A miner had no goal at all. GatherLogs wants an axe and a tree, Fish wants a
// pole and water, and ore had neither -- so Corran carried a pickaxe for a
// whole session, picked TRAIN_AT_NPC three times and mined nothing. "add
// mining goal so corran can mine" (project owner, 2026-08-29).
//
// The mechanics are the shard's, from skills/skill45_mining.scp:
//   FLAGS=skf_gather, RANGE=2, PROMPT_MSG="Where would you like to mine?"
//   ON=@PreStart refuses while FINDLAYER.layer_horse -- no mining mounted
//   ON=@PreStart reads SRC.WEAPON.USESCUR -- the pickaxe must be WIELDED,
//     not merely carried, and it wears out with use
//
// WHICH TILE IS ROCK IS THE SERVER'S JUDGEMENT, AND WE NOW MIRROR IT INSTEAD
// OF PROBING FOR IT. Swing-and-learn sounded humble but was wrong at both
// ends: the "unwalkable == rock" pre-filter nominated the RIVER beside the
// Minoc bridge (water is unwalkable too), and the ring-of-8 fallback swung at
// roads mid-journey -- 78 identical "Try mining elsewhere" refusals at
// (2540,503) in one run. The engine's actual gate is knowable from source:
// CheckNaturalResource(pt, IT_ROCK) demands the struck tile ITSELF be
// rock-typed land or a t_rock static (Source-X CWorldMap.cpp:52,721,781-785),
// where "rock-typed" is the shard's own [TYPEDEF t_rock] tables -- so
// Client::RockAt reads the same muls against the same tables, and refusals
// are kept only as per-tile memory (a vein rolled mr_nothing stays barren for
// REGEN=10h, core/regionresources.scp:40-42).
bool Runner::DoMine(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    if (obs.WeightFraction() >= kGathererPackFullFrac) {
        LogLine("mine: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // THE PICKAXE MUST BE IN HAND. skill45_mining reads SRC.WEAPON, so one
    // sitting in the backpack mines nothing and explains nothing.
    u32 pick = 0;
    for (int i = 0; i < 2 && !pick; ++i) {
        if (client.EquippedGraphicAt(kLayerHand1) == kMinePickaxe[i])
            pick = client.EquippedAtLayer(kLayerHand1);
        else if (client.EquippedGraphicAt(kLayerHand2) == kMinePickaxe[i])
            pick = client.EquippedAtLayer(kLayerHand2);
    }
    if (!pick) {
        const u32 inPack = FindAny(client, kMinePickaxe, 2);
        if (!inPack) {
            LogLine("goal_failed=MINE reason=\"no pickaxe carried\"");
            planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
            planner_.Finish(false, "no pickaxe", obs.nowMs);
            return false;
        }
        LogLine("mine: taking the pickaxe in hand -- mining reads SRC.WEAPON");
        client.ActionEquip(inPack, kLayerServerChooses);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Answer the cursor the skill arms -- like a classic client click on what
    // the player SEES. A cave floor is a STATIC and a static reply carries
    // its graphic; mountainside is rock land and gets a ground reply. Both
    // carry the ROCK'S OWN z (NearestMiningSpot filled it), not the
    // character's: the face of a mountain is well above the boots of the
    // miner striking it, and the engine range check is against m_Act_p as
    // sent (CCharSkill.cpp:1424-1441).
    if (mineCursorPending_) {
        if (client.TargetActive()) {
            if (mineGraphic_ != 0) {
                client.ActionTargetStatic(mineX_, mineY_, mineZ_,
                                          mineGraphic_);
            } else {
                client.ActionTargetGround(mineX_, mineY_, mineZ_);
            }
            mineCursorPending_ = false;
            // The attempt is timed from the answer, not the double-click:
            // the strokes start now.
            mineSwungMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + kMinePollMs;
            return false;
        }
        if (obs.nowMs - mineSwungMs_ > 6000) mineCursorPending_ = false;
        return false;
    }

    // A SWING IS IN FLIGHT: WAIT FOR THE VERDICT, DO NOT SWING OVER IT.
    // Mining is multi-stroke: SKTRIG_START rolls 2-6 strokes
    // (CCharSkill.cpp:1463) at DELAY=1.6s each (skill45_mining.scp), so an
    // attempt legitimately takes up to ~10s of silence before ANY journal
    // line. The old 2.5s re-swing restarted the skill mid-strokes every
    // single time -- an attempt never once ran to completion, which is why
    // whole sessions produced swings and no ore.
    if (mineSwungMs_ != 0) {
        // Verdicts about the TILE: dead-list it and move the aim.
        static const char* kBadTile[] = {
            "try mining elsewhere",           // MINING_1: not rock, or the
                                              //   vein rolled mr_nothing
            "there is nothing here to mine",  // MINING_2: vein exhausted
            "there is no ore here to mine",   // MINING_3
            "try mining in rock",             // MINING_4
            "that is too far away",           // positioning slip; re-aim
            // MINING_LOS (CCharSkill.cpp:1442-1444): the last gate before
            // the resource roll. Seen live at the Minoc mine mouth: the
            // nearest rock by ring order was the cliff at z=34 over the z=0
            // path, and its top is not visible from its foot. Dead-list it
            // and the z-aware picker finds the cave floor instead.
            "no line of sight to that location",
        };
        bool resolved = false;
        for (const char* line : kBadTile) {
            if (client.JournalSaidSince(line, mineJournalMs_)) {
                LogLine("mine: %d,%d refused (\"%s\") -- marking it dead",
                        mineX_, mineY_, line);
                deadTargets_.emplace_back(mineX_, mineY_);
                if (deadTargets_.size() > 32)
                    deadTargets_.erase(deadTargets_.begin());
                mineRoam_ = true;   // this vein is done; wander before rescanning
                // Counts toward the first-visit deeper-advance below. A
                // veteran with a remembered vein never gets this far off
                // course; a newborn at the mouth racks these up one entrance
                // rock at a time.
                ++mineConsecRefusals_;
                resolved = true;
                break;
            }
        }
        // @Fail is a verdict about the SKILL ROLL, not the tile ("You loosen
        // some rocks but fail to find any useable ore.",
        // skill45_mining.scp:43). The tile stays live -- dead-listing it here
        // is how a low-skill miner talks himself out of a perfectly good
        // vein. Swing at it again.
        if (!resolved &&
            client.JournalSaidSince("fail to find any useable ore",
                                    mineJournalMs_)) {
            LogLine("mine: failed the roll at %d,%d -- that is how gains "
                    "happen; striking again", mineX_, mineY_);
            // The roll failed, not the tile -- this IS genuine resource
            // ground, so the refusal streak that would send DoMine looking
            // deeper is no longer honest. Clear it.
            mineConsecRefusals_ = 0;
            resolved = true;
        }
        if (!resolved && client.JournalSaidSince("You put", mineJournalMs_)) {
            LogLine("mine: ORE at %d,%d", mineX_, mineY_);
            state_.memory.NoteResource("ore", mineX_, mineY_, mineZ_, true,
                                       obs.nowMs);
            planner_.NoteProgress();
            mineConsecRefusals_ = 0;
            mineAdvances_ = 0;
            resolved = true;
        }
        if (!resolved) {
            if (obs.nowMs - mineSwungMs_ < kMineResolveMs) {
                nextActionMs_ = obs.nowMs + kMinePollMs;
                return false;   // still stroking -- leave the skill alone
            }
            LogLine("mine: no verdict from %d,%d in %ds -- moving on",
                    mineX_, mineY_, (int)(kMineResolveMs / 1000));
        }
        mineSwungMs_ = 0;
    }

    // NEVER PICK TARGETS MID-JOURNEY. The bridge screenshots came from
    // exactly this: travel to the mine was still walking its legs when the
    // old code scanned from wherever the character happened to be and stopped
    // him mid-span to swing at the river. En route, there is nothing for this
    // goal to do but wait.
    if (client.TravelBusy() || client.GotoBusy()) return false;

    // Observation is sampled before movement is pumped. A just-completed
    // journey therefore has a valid current client pose while `obs` can still
    // describe the old leg's starting tile for this one life tick. Mining
    // makes follow-up movement decisions here, so it must use the current
    // pose: using the stale observation sent Draver from the cave floor back
    // to Minoc Mine's entrance immediately after every confirmed arrival.
    const i32 hereX = client.PlayerX();
    const i32 hereY = client.PlayerY();
    const i8  hereZ = client.PlayerZ();

    // GO TO THE ROCK FIRST. "first he needs to go to mining area" (project
    // owner, 2026-08-29).
    //
    // Being at WORK is a district; being able to MINE is a tile. atWorkSite
    // accepts 45 tiles from a resource area's edge, which is the right test
    // for "is this a mining town" and far too loose for "can I swing here":
    // Corwyn stood in Minoc hitting ordinary ground and was told "Try mining
    // elsewhere" every time. Walk into the area, then swing.
    bool atHomeMineInterior = false;
    {
        // A fresh miner knows the mine in their home city.  The generic atlas
        // picker is intentionally nearest-to-current-position, which sent
        // Draver from his Jhelom spawn to a Britain resource centroid even
        // though his seeded home knowledge says Minoc.  Use that known lead
        // first; it is an ordinary journey, never a teleport or global fact.
        const KnownResourceSource* homeMine = nullptr;
        if (!state_.homeCity.empty()) {
            for (const KnownResourceSource& source : state_.memory.Resources()) {
                if (source.resource != "ore" || !source.hinted ||
                    source.label.find(state_.homeCity) == std::string::npos)
                    continue;
                if (!homeMine ||
                    (source.label.find(" Mine ") != std::string::npos &&
                     homeMine->label.find(" Mine ") == std::string::npos))
                    homeMine = &source;
            }
        }
        // A cave resource marker may be at its entrance while the productive
        // mining floor is much deeper in the same cave.  Measure arrival
        // against the actual destination we selected, otherwise a miner who
        // successfully reaches the interior will keep trying to return to the
        // entrance and exhaust the trip budget without ever swinging.
        i32 homeMineX = 0, homeMineY = 0;
        bool homeMineInterior = false;
        if (homeMine) {
            homeMineX = homeMine->x;
            homeMineY = homeMine->y;
            homeMineInterior = client.MiningInteriorTarget(
                homeMine->x, homeMine->y, &homeMineX, &homeMineY);
            // Close to the centroid OR genuinely inside the cave's own RECTs.
            // The centroid-only test flips false the moment a miner walks
            // toward a real rock near the RECT's edge (Minoc Mine 1 is
            // 26x27 tiles; kMineReach is 6), which sends the branch below
            // straight back to "go to the interior" every following tick and
            // undoes the walk to the rock -- Elvar ping-ponged between a rock
            // and the interior anchor for the last 13 minutes of a session
            // and never struck ore again (run_gates/wave15).
            atHomeMineInterior =
                homeMineInterior &&
                (TileDist(homeMineX, homeMineY, hereX, hereY) <= kMineReach ||
                 client.WithinMiningRegion(homeMine->x, homeMine->y, hereX,
                                           hereY));
        }
        if (homeMine &&
            TileDist(homeMineX, homeMineY, hereX, hereY) > kMineReach) {
            if (++mineTrips_ > kMaxMineTrips) {
                LogLine("goal_failed=MINE reason=\"could not reach home mine "
                        "in %d trips\"", kMaxMineTrips);
                planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
                planner_.Finish(false, "home mine unreachable", obs.nowMs);
                mineTrips_ = 0;
                return false;
            }
            i32 mineX = homeMineX, mineY = homeMineY;
            std::string destination = homeMine->label;
            if (homeMineInterior) {
                destination += " interior";
                LogLine("mine: %s resident going directly to the interior of "
                        "%s at %d,%d (trip %d)", state_.homeCity.c_str(),
                        homeMine->label.c_str(), mineX, mineY, mineTrips_);
            } else {
                LogLine("mine: %s resident going to known %s at %d,%d "
                        "(interior unavailable; trip %d)",
                        state_.homeCity.c_str(), homeMine->label.c_str(),
                        mineX, mineY, mineTrips_);
            }
            travelInFlight_ = client.TravelToPoint(mineX, mineY, 3,
                                                   destination.c_str());
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        const i32 d = client.DistanceToResource(wm::ResourceKind::Mining);
        if (d > kMineReach) {
            if (++mineTrips_ > kMaxMineTrips) {
                LogLine("goal_failed=MINE reason=\"could not reach a mining "
                        "area in %d trips\"", kMaxMineTrips);
                planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
                planner_.Finish(false, "no mining area reachable", obs.nowMs);
                mineTrips_ = 0;
                return false;
            }
            LogLine("mine: the ore is %d tiles off -- walking into the mining "
                    "area first (trip %d)", d, mineTrips_);
            client.TravelToResource(wm::ResourceKind::Mining);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        mineTrips_ = 0;
    }

    // FIND GENUINE ROCK, walk to its side if need be, and strike it.
    //
    // The resource area's recorded position is a region CENTROID and a
    // centroid can be anything -- Corwyn's was a wooden bridge over water, and
    // the owner sent a screenshot of him standing on it swinging at planks.
    // Being "in the mining area" is not being at the rock, and "unwalkable" is
    // not rock (the river was unwalkable too). NearestMiningSpot mirrors the
    // server's own rock test (see Client::RockAt) and carries the dead list so
    // a refused tile is never nominated twice -- the previous build re-picked
    // (2540,503) 78 times because the primary path never consulted it.
    // ROAM THE MINE, DON'T CAMP ITS MOUTH. "there is more space in the mine
    // dont only mine at the entrance" (project owner, 2026-08-29). Scanning
    // nearest-first from the character's boots always re-nominates the rock
    // beside the last vein, so a miner would chew along the entrance one tile
    // at a time. When a vein dies, jitter the scan origin up to 8 tiles (the
    // low bits of the clock are noise enough for a stroll, and the dead-list
    // already stops him returning to worked-out ground); the walk to the new
    // stand tile is the wander itself. A jitter that lands where no rock is
    // found falls back to scanning from where he stands.
    i32 scanX = hereX, scanY = hereY;

    // START FROM GROUND THAT HAS ACTUALLY GIVEN ORE.
    //
    // A rock GRAPHIC is not a rock RESOURCE. The server draws the distinction
    // itself: "Try mining elsewhere" (DEFMSG_MINING_1) means
    // CheckNaturalResource returned NULL -- there is no ore region on that
    // tile at all -- which is a different failure from DEFMSG_MINING_2, the
    // depleted vein. Corwyn collected the first kind twice in a row at
    // (2554,500) and (2554,498), because Minoc Mine 1 is
    // RECT=2556,474,2582,501 and both of those are OUTSIDE it: he was hitting
    // the cliff beside the doorway. "because he was at the entrance of the
    // mine" (project owner, 2026-08-30).
    //
    // Scanning from the character's boots always finds that entrance wall
    // first, since it is the nearest thing shaped like rock. But this life
    // already knows where the ore is -- Minoc Mine 1 at 2558,499, twenty-six
    // successes -- so start the search from the remembered spot whenever one
    // is close enough to walk to. The existing jitter still spreads him
    // around inside once he is there, and the dead list still retires worked
    // ground.
    bool haveNearbyMemory = false;
    if (!atHomeMineInterior) {
        if (const KnownResourceSource* known =
                state_.memory.BestResource("ore", hereX, hereY, obs.nowMs)) {
        if (known->successes > 0 &&
            TileDist(known->x, known->y, hereX, hereY) <= kMineKnownSpotWithin) {
            scanX = known->x;
            scanY = known->y;
            haveNearbyMemory = true;
        }
        }
    }

    // THE MOUTH IS PICKED CLEAN -- HEAD DEEPER IN. First-visit only: a
    // newborn has no memory (haveNearbyMemory stays false above), so every
    // scan keeps restarting from wherever he is standing, which is the
    // mouth. Three refusals in a row there ("try mining elsewhere" and kin)
    // means the entrance's rock-graphic tiles are exhausted, and the real
    // vein can sit past the ordinary scan radius -- walking further into
    // the mine's own RECTs is what finds it, not another jittered rescan of
    // the same doorway. Bounded by mineAdvances_ so a genuinely empty cave
    // still fails honestly (owner: "it is not going to mine deep -- that is
    // the problem", 2026-08-31; DeeperMiningTarget only fires inside a
    // Cave-kind region, so open-air rock is unaffected).
    if (!haveNearbyMemory &&
        mineConsecRefusals_ >= kMineRefusalsBeforeAdvance &&
        mineAdvances_ < kMaxMineAdvances) {
        i32 deepX = 0, deepY = 0;
        if (client.DeeperMiningTarget(hereX, hereY, &deepX, &deepY)) {
            LogLine("mine: the mouth is picked clean -- heading deeper in");
            ++mineAdvances_;
            mineConsecRefusals_ = 0;
            mineRoam_ = false;
            travelInFlight_ =
                client.TravelToPoint(deepX, deepY, 2, "deeper into the mine");
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
    }

    if (mineRoam_) {
        scanX += (i32)(obs.nowMs % 17) - 8;
        scanY += (i32)((obs.nowMs / 17) % 17) - 8;
        mineRoam_ = false;
    }
    Client::MiningSpot spot;
    bool allGuarded1 = false, allGuarded2 = false;
    if (!client.NearestMiningSpot(scanX, scanY, hereZ, kMineScanRadius, &spot,
                                  &deadTargets_, &allGuarded1) &&
        !client.NearestMiningSpot(hereX, hereY, hereZ, kMineScanRadius, &spot,
                                  &deadTargets_, &allGuarded2)) {
        // OWNER RULE: no gathering inside guarded zones. Both scans saw rock
        // and rejected every candidate for standing inside the guard line --
        // this is a walled-off cave mouth in town, not an empty vein, so do
        // not cool the goal down or dead-list open ground. The known-vein
        // fallback right below is already the proven-stand/travel logic this
        // rule wants; it just needs the right sentence ahead of it.
        const bool allGuarded = allGuarded1 && allGuarded2;
        if (allGuarded) {
            LogLine("mine: nothing to take outside the guard line here -- "
                    "going to the proven stand");
        }
        // NO ROCK HERE IS A REASON TO WALK, NOT A REASON TO GIVE UP.
        //
        // The gate above measures DistanceToResource, which is the distance
        // to a resource AREA -- a district, not a tile. Standing in Minoc
        // town it reported "the ore is 11 tiles off", the goal walked its
        // short leg, arrived somewhere with no rock in it at all, and failed:
        //
        //   mine: no mineable rock within 24 tiles of 2460,429 -- moving on
        //   goal_failed=MINE reason="no rock in reach"
        //
        // -- a hundred tiles from a mine whose exact position this life had
        // recorded twenty-six successes at. The pickaxe had just broken and
        // been replaced in town (v4_Corwyn, 2026-08-30 17:42), which is
        // precisely when a miner is furthest from the rock and most needs to
        // go back to it.
        //
        // A remembered spot that has actually produced ore beats any area
        // centroid, so walk to that before admitting defeat. Bounded by the
        // same trip counter, so an unreachable memory still fails honestly.
        if (!atHomeMineInterior) {
            if (const KnownResourceSource* known =
                    state_.memory.BestResource("ore", hereX, hereY, obs.nowMs)) {
            const i32 back = TileDist(known->x, known->y, hereX, hereY);
            if (known->successes > 0 && back > kMineReach &&
                ++mineTrips_ <= kMaxMineTrips) {
                LogLine("mine: no rock within %d tiles of %d,%d, but '%s' at "
                        "%d,%d has given ore %d time(s) -- walking the %d "
                        "tiles back (trip %d)",
                        kMineScanRadius, hereX, hereY,
                        known->label.empty() ? "a known vein"
                                             : known->label.c_str(),
                        known->x, known->y, known->successes, back,
                        mineTrips_);
                deadTargets_.clear();
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "ore");
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            }
        }

        if (!allGuarded) {
            LogLine("mine: no mineable rock within %d tiles of %d,%d -- moving on",
                    kMineScanRadius, hereX, hereY);
        }
        deadTargets_.clear();
        mineTrips_ = 0;
        // Give up honestly rather than carry a maxed-out advance count into
        // whatever mine this character tries next.
        mineConsecRefusals_ = 0;
        mineAdvances_ = 0;
        planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
        planner_.Finish(false, "no rock in reach", obs.nowMs);
        return false;
    }

    // STRIKE ONLY WHEN ACTUALLY BESIDE IT. The engine wants the target at
    // least 1 and at most RANGE=2 tiles off (CCharSkill.cpp:1432-1441,
    // skill45_mining.scp RANGE=2); further out, walk to the vetted stand tile
    // and let the NEXT tick re-measure from wherever the walk actually ended.
    const i32 toRock = TileDist(hereX, hereY, spot.rockX, spot.rockY);
    if (toRock < 1 || toRock > 2) {
        LogLine("mine: the rock is at %d,%d, %d tiles off -- standing at "
                "%d,%d to reach it", spot.rockX, spot.rockY, toRock,
                spot.standX, spot.standY);
        client.ActionGoto(spot.standX, spot.standY);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    mineX_ = spot.rockX;
    mineY_ = spot.rockY;
    mineZ_ = spot.rockZ;
    mineGraphic_ = spot.rockGraphic;
    LogLine("mine: striking the rock at %d,%d,%d (%s, %d tiles)",
            mineX_, mineY_, (int)mineZ_,
            mineGraphic_ ? "cave floor" : "rock face", toRock);
    mineJournalMs_ = client.JournalNowMs();
    mineSwungMs_ = obs.nowMs;
    mineCursorPending_ = true;
    // USE THE PICKAXE, DO NOT INVOKE THE SKILL.
    //
    // ActionUseSkill(kMining) is answered by Sphere with "There is no such
    // skill. Please tell support you saw this" -- thirty times in one
    // session, which is what "swinging" amounted to. A gathering skill is
    // not requested by id; it is what the TOOL does. Double-clicking the
    // pickaxe is what arms the "Where would you like to mine?" cursor that
    // skill45_mining.scp declares, and DoFish has always done exactly this
    // with the pole.
    client.ActionUseObject(pick);
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

}  // namespace uo::life
