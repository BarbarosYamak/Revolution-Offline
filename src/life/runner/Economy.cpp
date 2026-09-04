#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// The reader for the fact DoTradeWithPlayer has been writing all along. See
// the declaration in Runner.h for why the floor is gated on it.
bool Runner::PlayersDeclined(const std::string& item, i64 nowMs) const {
    if (item.empty()) return false;
    for (const LifeEvent& e : state_.memory.Events()) {
        if (e.kind != "no_player_buyer") continue;
        if (e.detail != item) continue;
        if (nowMs - e.atMs <= kPlayerWindowMemoryMs) return true;
    }
    return false;
}

// The buy half of the same reader. See the declaration in Runner.h.
bool Runner::SellersDeclined(const std::string& item, i64 nowMs) const {
    if (item.empty()) return false;
    for (const LifeEvent& e : state_.memory.Events()) {
        if (e.kind != "no_player_seller") continue;
        if (e.detail != item) continue;
        if (nowMs - e.atMs <= kPlayerWindowMemoryMs) return true;
    }
    return false;
}

// The surplus half of the same ruling. See the declaration in Runner.h.
market::MaterialSaleGate Runner::MaterialSaleGateFor(
    const std::string& item, const Observation& obs) const {
    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        market::MaterialSaleGate g;
        g.reason = "no profession, so no plan to derive a cap from";
        return g;
    }
    // WHAT THE BUILD PLAN STILL HAS TO CLIMB. This is the piece the market
    // layer cannot see: the plan is life-layer state and the current sheet is an
    // observation. A skill at or past its target contributes nothing, which is
    // what makes a trained smith's cap smaller than a green one's.
    std::vector<market::SkillGap> gaps;
    gaps.reserve(state_.plan.skills.size());
    for (const SkillTarget& t : state_.plan.skills) {
        const i32 now = obs.SkillTenths(t.skillId);
        gaps.push_back({t.skillId, std::max(0, t.tenths - now)});
    }
    // PACK PLUS BANK. Unsold stock is banked the moment it has no buyer (see
    // DoBank, "put unsold stock away"), so counting the pack alone reports a
    // smith with six hundred banked ingots as holding none -- and the cap it is
    // measured against is in the hundreds.
    const i32 held = market::QtyOf(obs.pack, item) + market::QtyOf(obs.bank, item);
    return market::MaterialNpcSaleGate(
        *me, item.c_str(), held, PlayersDeclined(item, obs.nowMs),
        needCfg_.craftBatch, obs.gold, gaps,
        market::PolicyForPurse(obs.goldOnHand));
}

bool Runner::DoEarnGold(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        // A life that predates the catalogue (the M4 lumberjack) has no
        // `produces` list, so there is nothing this goal can honestly sell.
        // Also a stand-down rather than a completion, for the same reason:
        // an uncatalogued life would otherwise spin here identically.
        LogLine("earn_gold: '%s' is not in the catalogue -- nothing to sell",
                state_.plan.family.c_str());
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNothingToSellCooldownMs);
        planner_.Finish(false, "not in the catalogue", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // --- did the last sale actually pay? ----------------------------------
    //
    // The purse is the proof, not the fact that a packet was sent. Sphere
    // answers a refused sale with silence, and a sale that "worked" without
    // gold arriving is the same silent failure that made a working trainer
    // purchase read as a failure earlier in M5.
    if (sellSent_) {
        // BOTH HALVES, through the shared check (section 18).
        // AGAINST WHAT WAS ACTUALLY OFFERED. A surplus sweep sells whatever
        // the vendor listed, not the item the goal is named after.
        const std::string& soldItem =
            sellVerifyItem_.empty() ? sellItem_ : sellVerifyItem_;

        life::Expectation want;
        want.itemBefore = sellItemBefore_;
        want.itemLoss = 1;          // one leaving proves the sale happened
        want.goldBefore = sellGoldBefore_;
        want.goldGainMin = 1;

        life::Observed seen;
        seen.itemNow = market::QtyOf(obs.pack, soldItem);
        seen.goldNow = obs.gold;

        const life::ProgressCheck sale = life::Verify(want, seen);
        if (sale.verdict == life::Verdict::Confirmed) {
            const i32 paid = sale.goldDelta;
            const i32 sold = -sale.itemDelta;
            const i32 each = sold > 0 ? paid / sold : paid;
            LogLine("earn_gold: sold %d %s for %d gold (%d each) to a '%s'",
                    sellWanted_, soldItem.c_str(), paid, each,
                    sellTrade_.c_str());

            // What it was worth, as OBSERVED. This is the only kind of price
            // this project lets a character know.
            market::PriceObservation po;
            po.item = soldItem;
            po.pricePerUnit = each;
            po.source = market::PriceSource::NpcVendorBuys;
            po.who = sellTrade_;
            po.x = obs.x; po.y = obs.y;
            po.whenMs = obs.nowMs;
            state_.prices.Note(po);

            // Selling to an NPC CREATES gold. Recording it as a source is what
            // makes the anti-arbitrage invariant checkable afterwards.
            state_.ledger.Note(market::GoldFlow::CreatedVendor, paid,
                               soldItem.c_str(), obs.nowMs);

            KnownSupplier sup;
            sup.need = std::string("buyer:") + soldItem;
            sup.name = sellTrade_;
            sup.sourceType = "npc_vendor";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = each;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);

            state_.memory.NoteEvent("sold_to_vendor", soldItem.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            planner_.NoteProgress();
            sellSent_ = false;
            sellAsked_ = false;
        sellReachChecked_ = false;
            sellTrips_ = 0;
            sellLotCap_ = 0;   // this buyer could pay; stop rationing
            sellSweepGold_ += paid;
            Checkpoint(client, obs.nowMs, "sold to a vendor");

            // ONE VISIT, EVERYTHING SPARE.
            //
            // The old code returned success here, and that is the whole of
            // v4_Corwyn's 366 lost gold: Curtis had ALREADY named the six
            // heater shields he would buy, in the same 0x9E that listed the
            // two daggers. Corwyn took 72 gold for the daggers, reported the
            // errand done, and walked to the forge to make more daggers.
            //
            // A player empties their pack at the counter they are already
            // standing at. So re-ask the same vendor -- the sold items are
            // gone from the refreshed list, and whatever else is surplus is
            // still on it -- and only finish when nothing is left that this
            // buyer will take.
            if (++sellSweeps_ < kMaxSellSweeps) {
                LogLine("earn_gold: %d gold so far at this counter -- asking "
                        "the '%s' what else it will take before leaving",
                        sellSweepGold_, sellTrade_.c_str());
                sellVerifyItem_.clear();
                nextActionMs_ = obs.nowMs + 1200;
                return false;
            }
            LogLine("earn_gold: %d gold at this counter over %d sales -- "
                    "enough for one visit", sellSweepGold_, sellSweeps_);
            return true;
        }
        if (obs.nowMs - sellAskedMs_ > 12000) {
            LogLine("earn_gold: offered %d %s and the purse did not move "
                    "(still %d) -- this buyer did not take them",
                    sellWanted_, sellItem_.c_str(), obs.gold);
            state_.memory.NoteEvent("sale_refused", sellItem_.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            sellSent_ = false;
            sellAsked_ = false;
        sellReachChecked_ = false;
            // A VENDOR'S PURSE IS FINITE. OFFER FEWER BEFORE GIVING UP.
            //
            // Alenne bought 5 poison scrolls for 125 gold and then refused 11
            // of the same scroll at the same quoted 25 each -- 275 gold she no
            // longer had. The offer is still LISTED, so nothing about the shop
            // says no; only the silent purse does. The old code went straight
            // to the next trade, found poison scrolls have exactly one buyer
            // trade, failed the goal, and was re-picked ten seconds later to
            // offer the identical 11 again. Gold sat at 135 for the rest of
            // the run while eleven saleable scrolls sat in the pack
            // (run_m5/p0gate3).
            //
            // A player offers half. Halve until the lot is empty, and only
            // then decide this buyer is no use -- the same "a refusal is
            // information, act on it" rule as the banker and trainer paths.
            if (sellLotCap_ <= 0) sellLotCap_ = sellWanted_;
            sellLotCap_ /= 2;
            if (sellLotCap_ > 0) {
                LogLine("earn_gold: trying a smaller lot -- %d %s this time "
                        "(a vendor's own purse runs out)",
                        sellLotCap_, sellItem_.c_str());
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
            sellLotCap_ = 0;
            ++sellBuyerIndex_;      // try the next trade that buys this
            sellTrade_.clear();
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- what is there to sell? -------------------------------------------
    //
    // MID-SWEEP, THIS QUESTION IS ALREADY ANSWERED -- BY THE VENDOR.
    //
    // market::Surplus only knows what this life PRODUCES, and that is a
    // narrower question than "what will this counter take off my hands". The
    // first sweep proved it: Corwyn sold his daggers, said he would ask what
    // else Curtis wanted, and this chooser answered "nothing spare to sell"
    // and stood the goal down -- with six saleable shields in his pack -- one
    // second later. The 0x9E list is the authority once we are standing at
    // the counter, so keep the errand aimed where it is and let the sell
    // stage read it.
    const bool sweeping = sellSweeps_ > 0 && sellSweeps_ < kMaxSellSweeps &&
                          sellSweepGold_ > 0 && sellVendorSerial_ != 0;

    // The threshold bends when the purse is empty: see PolicyForPurse.
    const market::TradePolicy tp = market::PolicyForPurse(obs.goldOnHand);
    const std::vector<market::Offer> offers =
        market::Surplus(*me, obs.pack, tp);
    if (offers.empty() && !sweeping) {
        // THE STOCK MAY BE IN THE BOX. The need layer scores this errand from
        // pack AND bank on purpose -- goods in the bank are still this
        // character's stock, "it just has to go and fetch them, which is a
        // step in the errand" (Needs.cpp) -- but this goal counted the pack
        // alone. A fisher with fish in the bank therefore scored NeedGold at
        // 0.45, won the scoring, entered here, found the pack empty, completed
        // with progress 0, and was re-picked sixty milliseconds later. It did
        // that for the whole session and never fished once, because the errand
        // that outranked fishing could never finish.
        //
        // Fetching the stock IS the errand, so do that rather than refuse.
        std::vector<market::Stock> holdings = obs.pack;
        for (const market::Stock& b : obs.bank) {
            bool merged = false;
            for (market::Stock& h : holdings) {
                if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
            }
            if (!merged) holdings.push_back(b);
        }
        const std::vector<market::Offer> banked =
            market::Surplus(*me, holdings, tp);
        if (banked.empty()) {
            // AND THIS IS A FAILURE, NOT A COMPLETION.
            //
            // The comment above describes this exact bug for the fisher whose
            // stock was in the bank -- "completed with progress 0, and was
            // re-picked sixty milliseconds later" -- and fixed only that
            // branch. The terminal branch still returned true, so a character
            // with genuinely nothing to sell reported success, freed the
            // planner, and was handed the same errand again immediately.
            //
            // Kaelen did it 13,111 times in one session: died, lost everything
            // to full loot, woke with no gold and no goods, and spent 25
            // minutes completing EARN_GOLD at 60ms intervals --
            //
            //   goal_completed=EARN_GOLD progress=0
            //   goal=EARN_GOLD reason="no goal was running"
            //
            // -- while a graveyard full of things worth killing sat outside.
            // A goal that cannot act must stand down and let another have the
            // turn, exactly as GET_FOOD and GET_TOOL learned to.
            LogLine("earn_gold: nothing spare to sell (neither the pack nor "
                    "the bank holds a surplus of what this life makes) -- "
                    "standing down so something that CAN earn gets a turn");
            planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNothingToSellCooldownMs);
            planner_.Finish(false, "nothing to sell", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }

        // Only chase stock a buyer would actually take; a bank full of
        // player-market goods is not a reason to walk to the bank.
        const market::Offer* fetch = nullptr;
        for (const market::Offer& o : banked) {
            if (market::QtyOf(obs.bank, o.item) <= 0) continue;
            if (!market::MaySellToNpc(*me, o.item.c_str(), state_.ledger,
                                      PlayersDeclined(o.item, obs.nowMs))
                     .allowed) {
                continue;
            }
            // AND IS THERE ACTUALLY MORE OF IT THAN THE PLAN WANTS? Withdrawing
            // banked material to walk it to a counter is the exact trip the
            // 2026-09-02 ruling calls a last resort; the cap decides whether
            // this is one. Below it, the stock stays in the box.
            const market::MaterialSaleGate gate =
                MaterialSaleGateFor(o.item, obs);
            if (!gate.allowed) {
                LogLine("earn_gold: leaving %d %s in the bank -- %s "
                        "(held=%d cap=%d: plan %d + training %d + market %d)",
                        market::QtyOf(obs.bank, o.item), o.item.c_str(),
                        gate.reason, gate.held, gate.cap, gate.detail.ownPlan,
                        gate.detail.training, gate.detail.market);
                continue;
            }
            fetch = &o;
            break;
        }
        if (!fetch) {
            // AND THAT IS A STAND-DOWN, NOT A COMPLETION. This returned true
            // -- success with progress 0 -- so the planner freed the goal and
            // handed it straight back, which is the same shape as the 13,111
            // completions documented above. The stock has not moved and the
            // player market has not been asked, so the useful next act is a
            // WTS cycle, and this goal has to get out of the way for one.
            LogLine("earn_gold: the bank holds a surplus but no NPC route for "
                    "it -- that is the player market's job, not this goal's; "
                    "standing down so TRADE_WITH_PLAYER gets a turn");
            planner_.Cooldown(GoalKind::EarnGold,
                              obs.nowMs + kNothingToSellCooldownMs);
            planner_.Finish(false, "no NPC route for the banked surplus",
                            obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }

        if (client.BankContainer() == 0) {
            // ARRIVING IS NOT ENOUGH -- the box has to be OPENED, by asking a
            // banker for it. Travelling and then re-testing "am I at the bank"
            // loops forever the moment the trip completes instantly because
            // the character is already standing there, which is exactly what
            // it did: eight identical "going to fetch it" lines in twelve
            // seconds, never once opening the box. Same shape as the no-op
            // travel loop that pinned GATHER_LOGS.
            // THE SAME ERRAND AS DoBank'S, not a second hand-written one.
            //
            // This was the last unported bank-open, and it spun live within
            // three minutes of being left alone: "earn_gold: the stock is in
            // the bank (1 i_dagger) -- opening the box", every 2.5 seconds
            // against an 8-second deadline, so each ask cancelled the one
            // before it (v3_Corwyn, 15:08). The commit that left it here
            // predicted exactly that -- "no rotation, no deadline discipline
            // and no check that a box ever opened" -- which is an argument
            // for porting a known defect rather than annotating it.
            if (!bankErrand_.Running()) bankErrand_.Begin();
            const life::BankErrandResult br = bankErrand_.Tick(client, obs);
            if (!br.why.empty())
                LogLine("earn_gold: fetching %s -- %s", fetch->item.c_str(),
                        br.why.c_str());
            if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
                nextActionMs_ = obs.nowMs + br.delayMs;

            if (br.status == life::ActivityStatus::Success) {
                i32 bx = 0, by = 0; i8 bz = 0;
                if (br.banker && client.MobilePosition(br.banker, &bx, &by, &bz))
                    state_.memory.NotePlace("bank", "bank", bx, by, bz,
                                            obs.nowMs);
                return false;   // the box is open; the fetch continues below
            }
            if (life::IsTerminal(br.status)) {
                LogLine("goal_failed=EARN_GOLD status=%s reason=\"%s\"",
                        life::ActivityStatusName(br.status), br.why.c_str());
                planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kShortRestMs);
                planner_.Finish(false, "no banker opened a box", obs.nowMs);
                return false;
            }
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }

        // BY NAME, NOT BY GRAPHIC (S1). `take` is read from obs.bank, which
        // has been hue-resolved since S1 -- but the SERIAL was still found by
        // graphic, and the ore and iron-ingot graphics are shared by every
        // metal. A ledger line for i_ingot_iron could therefore pick up the
        // valorite stack sitting beside it in the box and withdraw THAT,
        // then hand it to a vendor at the iron price.
        i32 inBox = 0;
        const u32 serial = FindContainerItemByName(
            client, client.BankContainer(), fetch->item.c_str(), &inBox);
        if (!serial) {
            LogLine("earn_gold: the bank ledger says %s but the open box does "
                    "not show it -- the ledger is stale", fetch->item.c_str());
            return true;
        }
        const i32 take = std::min(market::QtyOf(obs.bank, fetch->item), inBox);
        LogLine("earn_gold: withdrawing %d %s to sell", take,
                fetch->item.c_str());
        client.TakeFromContainer(serial, static_cast<u16>(take));
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Take the first offer this life may legitimately sell.
    const market::Offer* chosen = nullptr;
    for (const market::Offer& o : offers) {
        const market::SellRuling r = market::MaySellToNpc(
            *me, o.item.c_str(), state_.ledger,
            PlayersDeclined(o.item, obs.nowMs));
        if (!r.allowed) {
            LogLine("earn_gold: will NOT sell %d %s -- %s", o.qty,
                    o.item.c_str(), r.reason);
            state_.memory.NoteEvent("sale_refused_policy", o.item.c_str(),
                                    r.reason, obs.x, obs.y, obs.nowMs);
            continue;
        }
        // MATERIALS EXIST TO BE CRAFTED (owner ruling, 2026-09-02). MaySellToNpc
        // answers whether the SHARD will take it; this answers whether this
        // CHARACTER can spare it. Both, or the counter does not get the trip.
        const market::MaterialSaleGate gate = MaterialSaleGateFor(o.item, obs);
        if (!gate.allowed) {
            LogLine("earn_gold: will NOT sell %d %s to an NPC -- %s "
                    "(held=%d cap=%d: plan %d + training %d + market %d)",
                    o.qty, o.item.c_str(), gate.reason, gate.held, gate.cap,
                    gate.detail.ownPlan, gate.detail.training,
                    gate.detail.market);
            state_.memory.NoteEvent("sale_below_surplus_cap", o.item.c_str(),
                                    gate.reason, obs.x, obs.y, obs.nowMs);
            continue;
        }
        chosen = &o;
        break;
    }
    if (!chosen && !sweeping) {
        // Same correction as the banked-surplus branch: returning true here
        // completed the goal with progress 0 and it was re-picked at once.
        // Nothing about the answer changes on the next tick -- the pack, the
        // policy and the registry are all identical -- so stand down and let
        // the player market or the workbench have the turn.
        LogLine("earn_gold: everything spare is barred from an NPC sale; "
                "banking it and standing down");
        planner_.Cooldown(GoalKind::EarnGold,
                          obs.nowMs + kNothingToSellCooldownMs);
        planner_.Finish(false, "nothing spare may be sold to an NPC",
                        obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Mid-sweep there is no new item to choose: the errand stays aimed at the
    // counter it is standing at, and the vendor's own list decides what goes.
    if (chosen) {
        if (sellItem_ != chosen->item) {
            sellItem_ = chosen->item;
            sellBuyerIndex_ = 0;
            sellTrade_.clear();
            sellTrips_ = 0;
            sellLotCap_ = 0;
            // A new item to sell is a new visit; what the last counter paid
            // says nothing about this one.
            sellSweeps_ = 0;
            sellSweepGold_ = 0;
            sellVerifyItem_.clear();
        }
        sellWanted_ = chosen->qty;
    }

    // --- who buys it? ------------------------------------------------------
    const std::vector<const market::NpcBuyer*> buyers = market::NpcBuyersFor(
        sellItem_.c_str(), PlayersDeclined(sellItem_, obs.nowMs));
    if (buyers.empty()) {
        // A real answer, not a failure. The character stays resource-rich and
        // wealth-poor, which is a legitimate state on this shard -- and it is
        // the case the owner's 2026-09-02 ruling names explicitly: "if no NPC
        // class buys the item, bank it and cool the goal down". Most materials
        // land here, because the runtime's own tm_vend.scp has the log, board,
        // ore, iron-ingot and hide BUY rows commented out (see kNpcBuyers).
        //
        // COOLING IT DOWN IS THE HALF THAT WAS MISSING. `return true` reported
        // success with no gold earned, so the planner handed EARN_GOLD back
        // immediately and the same lookup failed again forever.
        LogLine("earn_gold: no NPC trade on this shard buys %s; banking it "
                "and standing down for %llds", sellItem_.c_str(),
                static_cast<long long>(kNoBuyerCooldownMs / 1000));
        state_.memory.NoteEvent("no_buyer", sellItem_.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNoBuyerCooldownMs);
        planner_.Finish(false, "no NPC trade buys it", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }
    if (sellBuyerIndex_ >= buyers.size()) {
        LogLine("goal_failed=EARN_GOLD reason=\"tried all %zu trades that buy "
                "%s\" -- standing down for %llds", buyers.size(),
                sellItem_.c_str(),
                static_cast<long long>(kNoBuyerCooldownMs / 1000));
        // STAND DOWN, do not re-decide. Failing here without a cooldown put
        // EARN_GOLD straight back at the top of the list and the whole walk
        // began again 2.6 seconds later: run_m5/p0gate3 logged this same line
        // every few seconds with gold pinned at 135 and eleven saleable
        // scrolls in the pack. The buyers have not changed in that time; the
        // vendor's purse needs a restock cycle, and something else can be
        // done meanwhile.
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNoBuyerCooldownMs);
        planner_.Finish(false, "no buyer took the goods", obs.nowMs);
        sellBuyerIndex_ = 0;
        sellLotCap_ = 0;
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }
    const market::NpcBuyer* buyer = buyers[sellBuyerIndex_];
    if (sellTrade_ != buyer->trade) {
        sellTrade_ = buyer->trade;
        sellService_ = ServiceForTrade(buyer->trade);
        sellAsked_ = false;
        sellReachChecked_ = false;
    }

    if (client.TravelBusy()) return false;

    // --- get to one ---------------------------------------------------------
    const u32 vendor = client.NearestShopkeeperWithTrade(sellTrade_.c_str(),
                                                        sellService_);
    if (!vendor) {
        if (sellTrips_ >= kMaxSellTrips) {
            LogLine("earn_gold: no '%s' reachable after %d trips; trying the "
                    "next trade that buys %s", sellTrade_.c_str(), sellTrips_,
                    sellItem_.c_str());
            ++sellBuyerIndex_;
            sellTrade_.clear();
            sellTrips_ = 0;
            return false;
        }
        if (!travelInFlight_) {
            ++sellTrips_;
            const KnownSupplier* known = state_.memory.BestSupplier(
                (std::string("buyer:") + sellItem_).c_str());
            // A REMEMBERED BUYER IS ONLY WORTH RETURNING TO IF IT IS NEARBY.
            //
            // This branch had no distance test at all, and it is how Corwyn
            // kept dying between the cities. Standing in Britain after a
            // resurrection, his only remembered buyer:i_dagger was the Minoc
            // blacksmith, so EARN_GOLD sent him:
            //
            //   [travel] buyer -> (2473,562) r=2 from (1450,1617)
            //
            // A thousand tiles on foot, unmounted, through open country, to
            // sell daggers at eighteen gold. Britain has blacksmiths -- he had
            // just walked past them -- but familiarity outranked distance.
            //
            // Same shape as the wind-down bank: "nearest known" is not
            // "nearest". Below the threshold a familiar counter is worth the
            // walk; beyond it, ask the atlas for whatever is close, which is
            // what HomeOrNearest already does by returning nullptr.
            const i32 knownDist =
                known ? TileDist(known->x, known->y, obs.x, obs.y) : -1;
            if (known && known->name == sellTrade_ &&
                knownDist <= kReturnToKnownBuyerWithin) {
                LogLine("earn_gold: back to a buyer we have used before, "
                        "'%s' at %d,%d (%d tiles)", known->name.c_str(),
                        known->x, known->y, knownDist);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "buyer");
            } else {
                if (known && known->name == sellTrade_)
                    LogLine("earn_gold: the '%s' we know is %d tiles away -- "
                            "looking for a nearer one instead",
                            known->name.c_str(), knownDist);
                LogLine("earn_gold: looking for a '%s' to buy %d %s (trip %d)",
                        sellTrade_.c_str(), sellWanted_, sellItem_.c_str(),
                        sellTrips_);
                travelInFlight_ = client.TravelToService(sellService_, HomeOrNearest(state_.homeCity));
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=EARN_GOLD reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("earn_gold: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // --- stand next to the one we mean to deal with -------------------------
    //
    // Sphere routes a vendor keyword to whoever is NEAREST in earshot, not to
    // the name spoken. The first live sale said "Weston sell" three tiles from
    // Weston the carpenter, and JOSHUA THE ARCHITECT answered -- with "You
    // have nothing I'm interested in", because architects do not buy logs.
    // Walking up first is what makes the intended vendor the nearest listener.
    if (sellVendorSerial_ != vendor) {
        sellVendorSerial_ = vendor;
        sellApproached_ = false;
        // A different counter. Its purse and its buy list are its own.
        sellSweeps_ = 0;
        sellSweepGold_ = 0;
    }
    if (!sellApproached_) {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(vendor, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if (d > 1 || dz > 3) {
                LogLine("earn_gold: the '%s' is %d tiles and %d z away -- "
                        "walking up before speaking, or the nearest other "
                        "vendor answers instead", sellTrade_.c_str(), d, dz);
                // WALK TO THE MOBILE, NOT TO ITS FOOTPRINT.
                //
                // TravelToPoint zeroes travelEntitySerial_ and passes no Z at
                // all (ClientTravel.cpp:183-186), so A* is free to finish on
                // whichever floor of that column it reaches first. In a
                // multi-storey Britain mage shop that is the wrong storey:
                // "the 'mage' is 1 tiles and 40 z away", arrived by every 2D
                // measure and out of speech range by the server's, so the buy
                // list never came and the character could not sell a thing for
                // a whole session (run_m5/p0gate4). A UO storey is about 20 z
                // and the same-floor tolerance is 12, so this is never a near
                // miss -- it is a different room.
                //
                // TravelToEntity keeps the serial, re-aims at the live
                // position as it closes, and pins the goal Z on the final leg
                // (ClientTravel.cpp:644-651) -- which is exactly what chasing
                // a wandering NPC needs.
                travelInFlight_ = client.TravelToEntity(vendor, 1);
                sellApproached_ = true;   // one approach, then talk regardless
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        sellApproached_ = true;
    }

    // --- ask what it will take ---------------------------------------------
    if (!sellAsked_) {
        LogLine("earn_gold: asking the '%s' what it buys", sellTrade_.c_str());
        client.ActionVendorSellOpen(vendor);
        sellAsked_ = true;
        sellAskedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    if (client.VendorSellFrom() != vendor) {
        if (obs.nowMs - sellAskedMs_ > 10000) {
            LogLine("earn_gold: the '%s' never showed a buy list",
                    sellTrade_.c_str());
            ++sellBuyerIndex_;
            sellTrade_.clear();
            sellAsked_ = false;
        sellReachChecked_ = false;
            sellVendorSerial_ = 0;
            sellApproached_ = false;
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- close in before handing anything over -------------------------------
    //
    // The buy list arrives at speech range; the sale itself is CanTouch
    // (Chebyshev <= kVendorReach). A vendor that wandered a step while the
    // list was read, or an approach that stopped short, answers every lot
    // with "You can't reach the Vendor" -- Elara offered 16, 8 and 4 poison
    // potions to Corbey from three tiles and was refused all three times
    // (g_Elara 01:47:09-01:47:41, 2026-09-04). One more step in, once per
    // list, before the first lot goes out.
    if (!sellSent_ && !sellReachChecked_) {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(vendor, &vx, &vy, &vz) &&
            TileDist(obs.x, obs.y, vx, vy) > kVendorReach && !client.TravelBusy()) {
            LogLine("earn_gold: the '%s' is %d tiles off -- stepping in "
                    "before the sale", sellTrade_.c_str(),
                    TileDist(obs.x, obs.y, vx, vy));
            travelInFlight_ = client.TravelToEntity(vendor, 1);
            sellReachChecked_ = true;
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        sellReachChecked_ = true;
    }
    if (client.TravelBusy()) return false;

    // --- sell, matching by NAME -------------------------------------------
    //
    // The 0x9E list carries the serials of OUR OWN items, so this is a join
    // against the pack rather than against the vendor's stock.
    //
    // AND THE JOIN IS BY NAME, NOT GRAPHIC (S1). 0x9E carries a graphic and
    // no hue, and ore is ONE graphic for sixteen metals while the iron ingot
    // is one graphic for thirteen. Asking to sell "i_ingot_iron" therefore
    // matched the valorite stack in the same pack and handed it over at the
    // iron price -- an irreversible loss of the rarest thing a miner owns,
    // silently, at the moment the bot thought it was doing its job. So each
    // offer line is resolved back through the PACK, where the hue lives.
    //
    // The fail-safe when a serial cannot be found in the pack (a sub-bag, a
    // stale cache) splits on whether the graphic is ambiguous at all:
    // GraphicNeedsHue() is true only for the two shared families, and for
    // those an unresolvable line is REFUSED rather than guessed. Every other
    // graphic names itself, so it is matched as before.
    const std::vector<u16> mine = econ::GraphicsForItem(sellItem_.c_str());
    auto isMine = [&](const Client::VendorItem& item) {
        bool graphicMatches = false;
        for (u16 g : mine) { if (item.graphic == g) { graphicMatches = true; break; } }
        if (!graphicMatches) return false;
        const char* name = PackItemNameBySerial(client, item.serial);
        if (name) return sellItem_ == name;
        return !econ::GraphicNeedsHue(item.graphic);
    };
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        if (!isMine(v)) continue;

        // THE WHOLE LOT, NOT ONE PIECE. "yes dont sell one buy one" (project
        // owner, 2026-08-29). A dagger does not stack, so the vendor's buy
        // list holds sixteen separate entries of amount 1 -- and
        // min(sellWanted_, v.amount) is therefore always 1. Corwyn sold a
        // dagger, walked back to the forge, made another, and returned.
        //
        // The 0x9F packet has always carried an item COUNT; only the caller
        // was passing one. So gather every entry of this item the vendor will
        // take, up to what this life wants to be rid of, and sell them in a
        // single transaction.
        i32 remaining = sellWanted_;
        if (sellLotCap_ > 0) remaining = std::min<i32>(remaining, sellLotCap_);
        if (remaining <= 0) continue;

        std::vector<std::pair<u32, u16>> lot;
        i32 lotQty = 0;
        for (const Client::VendorItem& w : client.VendorSellOffer()) {
            if (lotQty >= remaining) break;
            if (!isMine(w) || w.amount <= 0) continue;
            const i32 take =
                std::min<i32>(remaining - lotQty, static_cast<i32>(w.amount));
            lot.emplace_back(w.serial, static_cast<u16>(take));
            lotQty += take;
        }
        if (lot.empty()) continue;

        LogLine("earn_gold: '%s' offers %u gold each for %s; selling %d in "
                "%u lot(s)", sellTrade_.c_str(), v.price, sellItem_.c_str(),
                lotQty, static_cast<unsigned>(lot.size()));
        sellWanted_ = lotQty;
        sellGoldBefore_ = obs.gold;
        // AND WHAT THE PACK HELD. A sale is gold arriving AND goods leaving;
        // gold alone also rises from loot, a player trade and a bank
        // withdrawal, and crediting this sale for one of those teaches the
        // price book a number nobody paid. See interaction/progress.h.
        sellVerifyItem_ = sellItem_;
        sellItemBefore_ = market::QtyOf(obs.pack, sellVerifyItem_);
        sellAskedMs_ = obs.nowMs;
        client.ActionVendorSellMany(vendor, lot);
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // NOTHING IT MAKES -- BUT MAYBE SOMETHING IT FOUND.
    //
    // Loot is income for a warrior and a mage, and the sell path could not see
    // it: market::Surplus only ever considers `produces`, which means WHAT I
    // MAKE. Loot is WHAT I FOUND, and the model had no word for it, so a mage
    // with a pack of graveyard drops had nothing the economy recognised.
    //
    // The fix needs no item table and no guessing. The 0x9E list IS the
    // server's own answer: it enumerates OUR items this vendor will buy, with
    // its prices. So offer whatever is in that list that this life has no use
    // for. A graphic table would have been the wrong foundation anyway --
    // ItemNameForGraphic maps 63 graphics, nearly all crafting materials and
    // not one weapon or piece of armour.
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        if (v.amount <= 0 || v.price == 0) continue;

        // HOW MANY OF IT STAYS, not whether any of it is wanted. The role
        // says what it is for; disposal.h says how many that means keeping.
        const ItemRole role = RoleOfGraphic(v.graphic);

        // Every entry of this graphic the vendor listed, and every one in the
        // pack -- a dagger does not stack, so one entry is one dagger and the
        // count has to be gathered rather than read off a single row.
        i32 listed = 0;
        for (const Client::VendorItem& w : client.VendorSellOffer())
            if (w.graphic == v.graphic && w.amount > 0)
                listed += static_cast<i32>(w.amount);

        life::DisposalSight see;
        see.role = role;
        // The 0x9E list is built FROM the backpack, so what it lists is what
        // is carried. An equipped shield is not in it, which is exactly why
        // selling every spare cannot strip a character of the one it wears.
        see.carried = listed;
        see.vendorTakes = listed;
        see.pricePerUnit = static_cast<i32>(v.price);
        see.lotCap = sellLotCap_;

        const life::DisposalPlan plan = life::DecideDisposal(see, disposal_);
        if (plan.step != life::DisposalStep::Sell) continue;

        const i32 qty = plan.quantity;

        LogLine("earn_gold: selling %d of 0x%04X at %u each to a '%s' "
                "(%s -- %s)",
                qty, v.graphic, v.price, sellTrade_.c_str(),
                life::ItemRoleName(role), plan.reason);
        sellWanted_ = qty;
        sellGoldBefore_ = obs.gold;
        // AND WHAT THE PACK HELD -- OF THE THING ACTUALLY BEING SOLD. Counting
        // the goal's item while offering shields would credit a sale of
        // daggers that never happened. econ names only 63 graphics; when this
        // one is not among them the purse alone decides, which Verify()
        // already expresses as itemBefore = -1.
        const char* def = econ::ItemNameForGraphic(v.graphic);
        sellVerifyItem_ = def ? def : "";
        sellItemBefore_ = sellVerifyItem_.empty()
                              ? -1
                              : market::QtyOf(obs.pack, sellVerifyItem_);
        sellAskedMs_ = obs.nowMs;

        // THE WHOLE LOT, IN ONE TRANSACTION -- the same rule the primary path
        // learnt. Armour and weapons do not stack, so `qty` spans that many
        // separate serials and sending one row of amount N sells exactly one.
        std::vector<std::pair<u32, u16>> lot;
        i32 taken = 0;
        for (const Client::VendorItem& w : client.VendorSellOffer()) {
            if (taken >= qty) break;
            if (w.graphic != v.graphic || w.amount <= 0) continue;
            const i32 take = std::min<i32>(qty - taken, static_cast<i32>(w.amount));
            lot.emplace_back(w.serial, static_cast<u16>(take));
            taken += take;
        }
        if (lot.empty()) continue;
        client.ActionVendorSellMany(vendor, lot);
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // A SWEEP THAT RAN OUT OF THINGS TO SELL HAS SUCCEEDED, NOT FAILED. If
    // gold changed hands at this counter, the pack is as empty as this buyer
    // can make it; marching on to the next trade would be looking for a
    // buyer for nothing.
    if (sellSweepGold_ > 0) {
        LogLine("earn_gold: %d gold from this '%s' over %d sale(s), and it "
                "will take nothing else we carry -- done here",
                sellSweepGold_, sellTrade_.c_str(), sellSweeps_);
        return true;
    }

    LogLine("earn_gold: this '%s' does not take %s after all, nor anything "
            "spare we are carrying; trying the next trade",
            sellTrade_.c_str(), sellItem_.c_str());
    state_.memory.NoteEvent("buyer_list_lacks_item", sellItem_.c_str(),
                            sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
    ++sellBuyerIndex_;
    sellTrade_.clear();
    sellAsked_ = false;
    sellReachChecked_ = false;
    return false;
}

// ---------------------------------------------------------------------------
// TRADE_WITH_PLAYER -- the only goal that needs somebody else to exist.
//
// Both halves run in the same body, because a character is whichever one the
// situation makes it: it announces what it has spare, and it answers what it
// hears. A fleet of twenty is mostly listeners at any moment.
//
//   stand where players gather (the home bank)
//   -> announce "WTS 20 i_log 2gp", occasionally, not every tick
//   -> hear "WTB i_log" -> walk to the speaker -> open the trade window
//   -> put the goods in -> accept -> verify against the PACK, not the packet
//
// The listener half is symmetrical: hear a WTS, decide with ConsiderOffer,
// answer with WTB, and wait to be traded with.
//
// S5: AND BOTH HALVES GO TO THE SAME PLACE.
//
// The bank this walked to was the NEAREST one (TravelToService(Banker,
// nullptr)), which is correct for every other errand and fatal for this one.
// Home is set once from homeCities.front(), so miner_smith always lives in
// Minoc and lumberjack_swordsman always in Britain -- 1,500 tiles and two
// different banks apart -- and the producer and the consumer of the one live
// trade edge in the catalogue could never be inside kTradeEarshot of each
// other. A market has to be ONE place. It is market::kMarketBankPlaceId,
// which cites the atlas line and the forum evidence for why that one.
// ---------------------------------------------------------------------------

bool Runner::MarketPlaceUsable(Client& client) {
    if (marketPlaceOk_ >= 0) return marketPlaceOk_ == 1;
    // Not resolved yet -- but the atlas is only there once the world knowledge
    // has loaded, so do not cache a "no" that is really a "not yet".
    if (!client.WorldKnowledgeReady()) return false;

    const wm::Place* p = client.KnownPlace(market::kMarketBankPlaceId);
    const char* bad = nullptr;
    if (!p)                                  bad = "the atlas has no such place";
    else if (!p->Offers(wm::Service::Banker)) bad = "it offers no banker";
    else if (!client.PlaceGuarded(*p))        bad = "it is not in a guarded region";

    if (bad) {
        // DEGRADE, NEVER SUBSTITUTE. atlasgen slugs place ids, so a
        // regenerated atlas could renumber britain_bank_2 -- and the answer to
        // that is the nearest bank the world actually knows about, not a pair
        // of literal coordinates baked into the bot.
        LogLine("market: no usable market place '%s' (%s) -- falling back to "
                "the nearest bank", market::kMarketBankPlaceId, bad);
        marketPlaceOk_ = 0;
        return false;
    }
    LogLine("market: the market is %s (%s) at %d,%d, radius %d, guarded",
            market::kMarketBankPlaceId, p->name.c_str(), p->position.x,
            p->position.y, p->radius);
    marketPlaceOk_ = 1;
    return true;
}

bool Runner::AtMarketBank(const Client& client) const {
    const wm::Place* p = client.KnownPlace(market::kMarketBankPlaceId);
    if (!p) return false;
    // The place's own radius plus two. Arriving is a pathfinder result, not a
    // tile equality: the walker stops on whatever legal tile it can reach, and
    // twenty bots converging on one bank cannot all stand on the same one.
    return TileDist(client.PlayerX(), client.PlayerY(), p->position.x,
                    p->position.y) <= p->radius + 2;
}

// A DIFFERENT QUESTION FROM AtMarketBank: that one asks "am I at THE market
// bank" (market::kMarketBankPlaceId, the one designated trade rendezvous);
// this asks "am I near A bank at all" -- the nearest one the atlas knows of,
// full stop. DoBank's own arrival test, so a fresh character's first BANK
// goal knows whether it has to travel before BankErrand's mobile scan has
// anything to find.
bool Runner::NearAnyBank(Client& client, const Observation& obs) const {
    const wm::Place* p = client.NearestServicePlace(wm::Service::Banker);
    if (!p) return false;
    // The place's own radius plus a few tiles of slack -- same shape as
    // AtMarketBank's "+2", widened a little because BankErrand's own mobile
    // scan (NearestMobileWithTrade) needs the banker in view, not merely the
    // place's rim.
    return TileDist(obs.x, obs.y, p->position.x, p->position.y) <=
           p->radius + 4;
}

void Runner::ForgetBankedStock(const char* item) {
    if (!item || !item[0]) return;
    for (usize i = 0; i < state_.bank.size(); ++i) {
        if (state_.bank[i].item != item) continue;
        state_.bank.erase(state_.bank.begin() +
                          static_cast<std::ptrdiff_t>(i));
        return;
    }
}

bool Runner::DoTradeWithPlayer(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // A SECOND SELLER TURNED UP AND WAS CLOSED ON THE WIRE (Client.h
    // TakeDeclinedTrade). Say so out loud too: the packet ends their window,
    // the words end their errand -- otherwise they walk back and try again.
    {
        u32 declSerial = 0;
        std::string declName;
        if (client.TakeDeclinedTrade(&declSerial, &declName)) {
            LogLine("trade: declined a second window from %s -- already sorted "
                    "with %s", declName.c_str(), tradePartnerName_.c_str());
            client.ActionSay(market::FormatDecline(declName).c_str());
            tradeDeclined_.push_back(declSerial);
        }
    }

    // --- a trade is already open: drive it to a conclusion ------------------
    const trade::TradeState& tr = client.Trade();
    if (tr.Active()) {
        return DriveOpenTrade(client, obs);
    }
    if (tr.CurrentPhase() == trade::Phase::Completed) {
        LogLine("trade: window closed complete with %s", tradePartnerName_.c_str());
        // The PACK is the proof. A completed window means the server moved
        // the goods; believing the packet without checking is how a "sale"
        // that moved nothing gets recorded as income.
        const i32 now = market::QtyOf(obs.pack, tradeItem_);
        if (tradeSellingQty_ > 0 && now < tradePackBefore_) {
            const i32 moved = tradePackBefore_ - now;
            const i32 paid = obs.gold - tradeGoldBefore_;
            LogLine("trade: gave %d %s to %s for %d gold", moved,
                    tradeItem_.c_str(), tradePartnerName_.c_str(), paid);
            if (paid > 0) {
                market::PriceObservation po;
                po.item = tradeItem_;
                po.pricePerUnit = paid / moved;
                po.source = market::PriceSource::PlayerTraded;
                po.who = tradePartnerName_;
                po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
                state_.prices.Note(po);
                state_.ledger.Note(market::GoldFlow::TransferPlayerTrade, paid,
                                   tradeItem_.c_str(), obs.nowMs);
            }
            state_.memory.NoteEvent("traded_with_player", tradeItem_.c_str(),
                                    tradePartnerName_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            planner_.NoteProgress();
        } else if (tradeSellingQty_ == 0 && now > tradePackBefore_) {
            const i32 got = now - tradePackBefore_;
            const i32 spent = tradeGoldBefore_ - obs.gold;
            LogLine("trade: got %d %s from %s for %d gold", got,
                    tradeItem_.c_str(), tradePartnerName_.c_str(), spent);
            if (spent > 0) {
                market::PriceObservation po;
                po.item = tradeItem_;
                po.pricePerUnit = spent / got;
                po.source = market::PriceSource::PlayerTraded;
                po.who = tradePartnerName_;
                po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
                state_.prices.Note(po);
                state_.ledger.Note(market::GoldFlow::TransferPlayerTradeOut, spent,
                                   tradeItem_.c_str(), obs.nowMs);
            }
            planner_.NoteProgress();
        } else {
            LogLine("trade: window completed but nothing moved");
        }
        client.TradeForget();
        ResetTradeState();
        Checkpoint(client, obs.nowMs, "traded with a player");
        return true;
    }
    if (tr.CurrentPhase() == trade::Phase::Cancelled) {
        LogLine("trade: %s cancelled (%s)", tradePartnerName_.c_str(),
                trade::CloseReasonName(tr.Reason()));
        state_.memory.NoteEvent("trade_cancelled", tradeItem_.c_str(),
                                tradePartnerName_.c_str(), obs.x, obs.y,
                                obs.nowMs);
        // A CANCEL IS ACKNOWLEDGED ONCE.
        //
        // TradeState latches Phase::Cancelled until something clears it, and
        // this branch used only to reset the Runner's own bookkeeping -- so it
        // re-fired on every tick. g_Odessa.console.txt 09:12:14.785 onwards:
        // ~200 "trade:  cancelled (partner_cancelled)" lines at 60ms, the
        // partner name already blanked by the first pass, until the anti-spin
        // backstop noticed. Forgetting the window, ending the goal and resting
        // is what closes that loop.
        client.TradeForget();
        ResetTradeState();
        planner_.NoteAttempt(obs.nowMs);
        planner_.Cooldown(GoalKind::TradeWithPlayer,
                          obs.nowMs + kTradeRetryRestMs);
        planner_.Finish(false, "the trade window was cancelled", obs.nowMs);
        return false;
    }

    // --- listen ------------------------------------------------------------
    //
    // Done BEFORE announcing, so a character that can answer somebody else's
    // offer does that rather than adding its own to the noise.
    //
    // AN OFFER GOES STALE. `tradeHeardMs_` only ever advances inside this
    // handler, so a life that trades, spends ten minutes mining, and comes
    // back would answer a WTS that expired nine minutes ago -- walk to a
    // seller who has left, and burn one of its three trips doing it. Clamp the
    // window to two announce intervals before now. THE JOURNAL CLOCK, not the
    // tick clock: JournalHeardSince and Heard::timeMs are both on it, and the
    // two are different clocks (the same mistake made a ten-second training
    // verification expire in 8.7s).
    const i64 journalNow = client.JournalNowMs();
    if (tradeHeardMs_ < journalNow - 2 * kAnnounceIntervalMs)
        tradeHeardMs_ = journalNow - 2 * kAnnounceIntervalMs;
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(tradeHeardMs_, heard);
    if (!heard.empty()) tradeHeardMs_ = heard.back().timeMs;

    // Our own name, so a line addressed to another player can be recognised as
    // not ours to answer. The IDENTITY's name, which is the one this character
    // logged in under and the one other bots hear on the wire. Empty is the
    // tolerant direction: it reads as "said to the room".
    const std::string& myName = state_.identity.characterName;

    for (const Client::Heard& h : heard) {
        // THE BUYER SORTED IT WITH SOMEBODY ELSE. Losing the race is a normal
        // outcome of a room where two sellers hold the same goods; hearing so
        // is much cheaper than waiting out the 25s give-up at the bank.
        if (tradePartner_ != 0 && h.speaker == tradePartner_ &&
            market::ParseDecline(h.text, nullptr) &&
            market::AddressedTo(h.text, myName)) {
            LogLine("trade: %s sorted it with somebody else -- standing down",
                    h.name.c_str());
            state_.memory.NoteEvent("trade_declined", tradeItem_.c_str(),
                                    h.name.c_str(), obs.x, obs.y, obs.nowMs);
            ResetTradeState();
            planner_.NoteAttempt(obs.nowMs);
            planner_.Cooldown(GoalKind::TradeWithPlayer,
                              obs.nowMs + kTradeRetryRestMs);
            planner_.Finish(false, "the buyer sorted it with somebody else",
                            obs.nowMs);
            return false;
        }
        // WHICH KIND OF WTB IS THIS? A reply to our own standing offer, or
        // somebody announcing demand to the room? The two branches below mean
        // opposite things and both used to fire on the same line: a full
        // "WTB 8 i_ingot_iron 52gp" broadcast reached the reply branch first,
        // so a seller committed to the buyer WITHOUT ever saying a WTS back,
        // and the buyer had no deal of its own to fund. See
        // market::ClassifyBuyLine for the evidence.
        market::TradeIntent wtb;
        const market::BuyLineKind wtbKind =
            market::ClassifyBuyLine(h.text, &wtb);

        // Somebody answered OUR offer -- and named US doing it. Without the
        // addressee test one bare "WTB i_ingot_iron" was an answer to every
        // seller in earshot at once (2026-09-02: Kharain and Elvar both took
        // Odessa's reply as their own).
        if (wtbKind == market::BuyLineKind::Reply &&
            !tradeOffer_.item.empty() && tradePartner_ == 0 &&
            wtb.item == tradeOffer_.item &&
            market::AddressedTo(h.text, myName)) {
            LogLine("trade: %s wants our %s", h.name.c_str(), wtb.item.c_str());
            tradePartner_ = h.speaker;
            tradePartnerName_ = h.name;
            tradeItem_ = tradeOffer_.item;
            tradeSellingQty_ = tradeOffer_.qty;
            return false;   // next tick walks over and opens the window
        }
        // A COLD WTB -- somebody wants something we happen to be carrying, and
        // we never announced it.
        //
        // This is the half of the market that did not exist. The branch above
        // only ever recognised an answer to THIS character's own offer, so a
        // crafter could shout for logs all day beside a lumberjack holding two
        // hundred of them and neither would ever hear the other. Demand has to
        // be able to start a trade, not only accept one.
        //
        // Guarded on having no partner yet: once committed to a deal, other
        // people's shopping is not this errand's business -- and without the
        // guard the buyer's own "WTB i_log" confirmation would look like a
        // second, competing request.
        //
        // ANNOUNCE, not Reply: a line that carries a quantity and a price and
        // names nobody is demand talking to the room. That classification is
        // the whole fix -- this branch never ran while the reply branch above
        // was swallowing the same line.
        if (wtbKind == market::BuyLineKind::Announce && tradePartner_ == 0) {
            market::TradeIntent fill;
            if (market::AnswerBuyWant(*me, obs.pack, state_.prices,
                                      tradePolicy_, wtb, &fill)) {
                // SAY IT OUT LOUD, in the seller's own form. The buyer is
                // already listening for a WTS (the branch below), so this both
                // closes the loop mechanically and reads, to a human watching
                // the bank, like one player answering another.
                const std::string line = market::FormatSellOffer(fill);
                LogLine("trade: %s wants %d %s -- answering '%s'",
                        h.name.c_str(), wtb.qty, wtb.item.c_str(), line.c_str());
                client.ActionSay(line.c_str());
                tradePartner_ = h.speaker;
                tradePartnerName_ = h.name;
                tradeItem_ = fill.item;
                tradeSellingQty_ = fill.qty;
                tradeOffer_ = fill;
                // The seller opens the window, so this is the clock the walk
                // and the 20s wait below both run against.
                tradeAnnouncedMs_ = obs.nowMs;
                return false;
            }
        }
        // Somebody is selling something we need.
        market::TradeIntent offer;
        if (!market::ParseSellOffer(h.text, &offer)) continue;
        // ONE SELLER PER WANT. Already committed: tell the other one so, out
        // loud and once, and go no further. Without this the loop happily
        // re-pointed tradePartner_ at a second seller mid-deal, and the two of
        // them raced to open a window on a buyer who could pay for one.
        if (tradePartner_ != 0) {
            const bool told =
                std::find(tradeDeclined_.begin(), tradeDeclined_.end(),
                          h.speaker) != tradeDeclined_.end();
            if (!told && h.speaker != tradePartner_ && offer.item == tradeItem_) {
                LogLine("trade: %s also offers %s -- already sorted with %s",
                        h.name.c_str(), offer.item.c_str(),
                        tradePartnerName_.c_str());
                client.ActionSay(market::FormatDecline(h.name).c_str());
                tradeDeclined_.push_back(h.speaker);
            }
            continue;
        }
        // S4: what is in the hands, so a life already armed does not buy a
        // second sword. Layers 1 and 2 are the weapon and shield hands
        // (Client.cpp:51-52); nothing else is a trade decision today.
        const std::vector<market::WornItem> wornNow = {
            {1, client.EquippedGraphicAt(1)}, {2, client.EquippedGraphicAt(2)}};
        // PACK COIN, NOT THE STATUS-BAR FIGURE. obs.gold counts the bank box
        // (PayFromPackOnly=0 is a buy-from-vendor rule, not a player-trade
        // one); DriveOpenTrade below only ever offers
        // FindBackpackItemByGraphic(kGoldCoin), so accepting against obs.gold
        // let a life commit to a deal its pack could not actually pay for,
        // then stand there offering nothing once the window opened.
        const market::BuyDecision d = market::ConsiderOffer(
            *me, obs.pack, obs.goldOnHand, tradePolicy_, offer, wornNow);
        LogLine("trade: heard '%s' from %s -> %s (%s)", h.text.c_str(),
                h.name.c_str(), d.accept ? "want it" : "no", d.reason);
        if (!d.accept) continue;
        // Say so out loud. The seller is listening for exactly this, and
        // saying it is also what makes the deal visible to a human watching.
        // NAME THE SELLER. See market.h FormatBuyReply: an unaddressed reply
        // is an answer to everybody holding that item.
        client.ActionSay(market::FormatBuyReply(offer.item, h.name).c_str());
        tradePartner_ = h.speaker;
        tradePartnerName_ = h.name;
        tradeItem_ = offer.item;
        tradeSellingQty_ = 0;            // we are the BUYER
        tradeWantQty_ = d.qty;
        tradeOfferPrice_ = offer.pricePerUnit;
        // STAMP THE CLOCK A BUYER ACTUALLY OWNS. The "seller never opened a
        // window" timeout below reads tradeAnnouncedMs_, but only the
        // ANNOUNCE path ever wrote it -- a pure buyer left it at 0, and
        // obs.nowMs is steady_clock-since-boot, so `obs.nowMs - 0` was
        // already past 20000 on the very first tick within 2 tiles: the
        // buyer dropped a seller who had just opened for it. This is the
        // moment a buyer commits to the deal, so this is the clock the
        // 20s wait has to start from.
        tradeAnnouncedMs_ = obs.nowMs;
        return false;
    }

    // --- a partner is named: go and open the window -------------------------
    if (tradePartner_ != 0) {
        if (client.TravelBusy()) return false;
        i32 px = 0, py = 0; i8 pz = 0;
        if (!client.MobilePosition(tradePartner_, &px, &py, &pz)) {
            LogLine("trade: lost sight of %s", tradePartnerName_.c_str());
            ResetTradeState();
            return false;
        }
        if (TileDist(obs.x, obs.y, px, py) > 2) {
            if (!travelInFlight_) {
                travelInFlight_ = client.TravelToEntity(tradePartner_, 1);
                nextActionMs_ = obs.nowMs + 1500;
            } else {
                travelInFlight_ = false;
            }
            return false;
        }
        // Only the SELLER opens the window, so both sides do not race to open
        // one and cancel each other. Sphere opens a trade by dropping an item
        // on the partner, which the buyer has nothing to do with yet.
        if (tradeSellingQty_ > 0) {
            const std::vector<u16> gfx = econ::GraphicsForItem(tradeItem_.c_str());
            u32 serial = 0;
            for (u16 g : gfx) {
                serial = client.FindBackpackItemByGraphic(g);
                if (serial) break;
            }
            if (!serial) {
                LogLine("trade: no %s in the pack after all", tradeItem_.c_str());
                ResetTradeState();
                return false;
            }
            tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
            tradeGoldBefore_ = obs.gold;
            LogLine("trade: opening a window with %s for %d %s",
                    tradePartnerName_.c_str(), tradeSellingQty_,
                    tradeItem_.c_str());
            client.ActionTradeStart(tradePartner_, serial);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        // Buyer: stand there and wait for the seller to open it.
        tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
        tradeGoldBefore_ = obs.gold;
        if (obs.nowMs - tradeAnnouncedMs_ > 20000) {
            LogLine("trade: %s never opened a window", tradePartnerName_.c_str());
            // AND END THE ERRAND. Resetting alone left the goal live with no
            // partner, so the buyer went straight back to listening where it
            // stood -- which is what "Kharain and Elvar stuck at bank" looked
            // like from outside.
            ResetTradeState();
            planner_.NoteAttempt(obs.nowMs);
            planner_.Cooldown(GoalKind::TradeWithPlayer,
                              obs.nowMs + kTradeRetryRestMs);
            planner_.Finish(false, "the seller never opened a window",
                            obs.nowMs);
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- what this life came here for: sell, buy, or neither ----------------
    //
    // A MARKET HAS TWO SIDES and this handler only ever modelled one. The
    // first thing it did was ask ChooseSellOffer, and a life with nothing
    // spare fell straight out of the bottom -- so a smith twenty logs short
    // of the spear it wants to forge could not so much as walk to a bank.
    // The buyer half is the same errand read the other way round, and it is
    // decided here, once, before anything moves.
    //
    // BOTH read the bank as well as the pack. Everything this character ever
    // gathered is in a box, because banking is what it does when the pack
    // fills; a surplus it cannot see is a trip it never makes, and a
    // shortfall it has already banked against is a trip it should not make.
    std::vector<market::Stock> holdings = obs.pack;
    for (const market::Stock& b : obs.bank) {
        bool merged = false;
        for (market::Stock& h : holdings) {
            if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
        }
        if (!merged) holdings.push_back(b);
    }

    market::TradeIntent offer;
    const bool wantsToSell = market::ChooseSellOffer(*me, holdings, state_.prices,
                                                     tradePolicy_, &offer);
    const char* noBuyWhy = nullptr;
    const std::vector<market::Want> buyable =
        market::PlayerMarketWants(*me, holdings, obs.goldOnHand, tradePolicy_,
                                  &noBuyWhy);
    const bool wantsToBuy = !buyable.empty();

    if (!wantsToSell && !wantsToBuy) {
        // Nothing worth announcing -- most often because this character has
        // never seen a price for what it carries and refuses to invent one --
        // AND nothing it is short of that a player could supply and it could
        // pay for.
        // SAME DEAD END, SAME COOLDOWN. Returning plain success here let the
        // need score identically on the very next tick and the goal was
        // re-picked sixteen times a second -- a lumberjack logged
        // goal=TRADE_WITH_PLAYER eight times in half a second and did nothing
        // else all session. An errand that cannot even be started is the
        // market being unavailable, not a goal that succeeded.
        LogLine("trade: nothing to announce (no observed price for what is "
                "spare) and nothing to buy (%s)",
                noBuyWhy ? noBuyWhy : "nothing short");
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        // AND COOL THE GOAL, not only the need -- same reasoning as every
        // other stand-down below (fleet7.console.txt: a 244ms re-pick without
        // it). marketQuietUntilMs_ only blanks NeedTrade on the next Observe;
        // this dead end returned `true` (success) straight past the planner
        // without ever telling it to rest, so it was the one stand-down in
        // this handler still exposed to the instant re-pick.
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        return true;
    }
    if (wantsToSell) tradeOffer_ = offer;

    // A BACK, NOT A BANK, CARRIES THE OFFER. i_log is WEIGHT=2.0
    // (runtime/scripts/items/i_provisions_logs.scp:64): 113 banked logs are 226
    // stones against a 40-STR cap of 180, so the withdrawal below would ask for
    // all of them, the server refuses, and the block re-issues it every 2 s for
    // the whole goal. Two stones per unit is the floor; four fifths of the
    // headroom is margin.
    if (wantsToSell) {
        const i32 fits = ((std::max(0, obs.maxWeight - obs.weight) * 4) / 5) / 2 +
                         market::QtyOf(obs.pack, offer.item);
        if (offer.qty > fits) offer.qty = std::max(0, fits);
    }

    // --- go to the market ---------------------------------------------------
    //
    // ONE PLACE FOR THE WHOLE FLEET. The nearest bank is the right answer for
    // every other errand and the wrong one for this: a rendezvous where each
    // party picks its own nearest bank is not a rendezvous. See
    // market::kMarketBankPlaceId for which bank and why.
    //
    // ARRIVAL IS GEOMETRY, NOT AN OPEN BOX. `obs.atBank` means the bank
    // container is open (Observe), which a buyer has no reason to do -- and
    // gating the journey on it would have a buyer standing at the market
    // re-issuing the walk forever.
    const bool haveMarket = MarketPlaceUsable(client);
    const bool arrived = haveMarket ? AtMarketBank(client)
                                    : (obs.atBank || client.BankContainer() != 0);
    if (!arrived) {
        // NOT AT THE MARKET: whatever box was open is behind us. The serial
        // and its cached contents both survive the walk (Client keeps them),
        // so this flag is the only thing that remembers the box was opened
        // somewhere else and must be asked for again on arrival.
        marketBoxOpened_ = false;
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // DO NOT START A TRIP THE CLOCK CANNOT FINISH. A market attempt is
            // 250s out + 60s listening + 250s back (all three legs measured,
            // docs/S5_MARKET_TRIP_PLAN.md section 3), and wind-down needs its
            // own budget on top. Without this a life spends its last eight
            // minutes walking and wind-down finds it in open country -- which
            // is exactly the Corwyn death loop recorded above in the WindDown
            // phase: logged out in the wild, killed where it stood, full loot.
            const i64 leftMs = cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_);
            if (cfg_.sessionLimitMs > 0 && leftMs < kMarketTripBudgetMs) {
                LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"not enough "
                        "session left for the trip\" left=%llds need=%llds",
                        static_cast<long long>(leftMs / 1000),
                        static_cast<long long>(kMarketTripBudgetMs / 1000));
                planner_.Cooldown(GoalKind::TradeWithPlayer,
                                  obs.nowMs + kMarketQuietMs);
                planner_.Finish(false, "not enough session left for the trip",
                                obs.nowMs);
                marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
                return false;
            }
            // BOUND THE TRIPS. This walk was unbounded, and the flag below
            // clears on the very next tick, so a character that never arrives
            // re-issues the journey every two seconds until the goal's limit
            // kills it -- and is then handed the same errand again.
            // Brannoc logged "taking 30 i_ingot_iron to the Vesper market" 145
            // times in one session and reached no market, while training,
            // eating and crafting all waited their turn behind it. Every other
            // travelling goal already counts its trips; this one did not.
            if (++tradeTrips_ > kMaxTradeTrips) {
                LogLine("goal_failed=TRADE_WITH_PLAYER reason=\"no market "
                        "reached after %d trips\"", tradeTrips_ - 1);
                planner_.Cooldown(GoalKind::TradeWithPlayer,
                                  obs.nowMs + kMarketQuietMs);
                planner_.Finish(false, "no market reachable", obs.nowMs);
                tradeTrips_ = 0;
                nextActionMs_ = obs.nowMs + 5000;
                return false;
            }
            if (wantsToSell)
                LogLine("market: taking %d %s to %s (trip %d)", offer.qty,
                        offer.item.c_str(),
                        haveMarket ? market::kMarketBankPlaceId
                                   : "the nearest bank",
                        tradeTrips_);
            else
                LogLine("market: going to %s to buy %d %s (trip %d)",
                        haveMarket ? market::kMarketBankPlaceId
                                   : "the nearest bank",
                        buyable.front().qty, buyable.front().item.c_str(),
                        tradeTrips_);
            travelInFlight_ =
                haveMarket ? client.TravelToPlace(market::kMarketBankPlaceId)
                           : client.TravelToService(wm::Service::Banker, nullptr);
            if (!travelInFlight_) {
                LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        return false;
    }
    tradeTrips_ = 0;  // arrived: reset the trip allowance for the errand ahead.

    // --- collect the stock before selling it --------------------------------
    //
    // Announcing goods that are in a box on the other side of town is an offer
    // it cannot honour, so the withdrawal is part of the errand -- and it is
    // the ONLY reason this goal ever opens a bank box. A seller with the goods
    // already in its pack, and every buyer, stands at the market with the box
    // shut. Opening it "just in case" is what bank_errand.h:16-19 warns about:
    // an empty box sends no 0x3C, nothing ever flips, and the character
    // re-opens the bank every 2.5 seconds forever.
    if (wantsToSell) {
        const i32 inPack = market::QtyOf(obs.pack, offer.item);
        const i32 inBank = market::QtyOf(obs.bank, offer.item);
        if (inPack < offer.qty && inBank > 0) {
            // A LIFT IS AN ACTION AND YOU STAND STILL TO MAKE ONE.
            //
            // Neither guard was here, and both are the reason the withdrawal
            // below became a metronome: run_r4/pair_Durnholde.console.txt:4382
            // onwards logs "market: withdrawing 20 i_ingot_iron from the bank
            // to sell" seventy-six times between 20:37:43 and 20:40:27, once
            // every two seconds, each one answered immediately by
            // `drag_cancel: reason=0 cannot lift that`. The first of them was
            // issued at 20:37:43.245 -- 645 ms BEFORE `travel_done` at
            // 20:37:43.890 -- i.e. while still walking, which is the same
            // "the open container does not survive being walked away from"
            // lesson DoBank already carries for coin (see kCoin above).
            if (client.TravelBusy()) return false;
            if (client.ActionBusy()) return false;
            // (a) THE BOX MUST BE OPENED *HERE*, AT THE MARKET.
            //
            // `obs.atBank` is a CACHE test, not a proximity one --
            // `BankContainer() != 0 && ContainerKnown(...)` (Runner::Observe)
            // -- and neither half expires when the character walks away. So a
            // box opened in one town is still "open" a thousand tiles later:
            // Durnholde opened 0x40014400 at 20:34:17.896, walked to the
            // blacksmith guild and back, and arrived at the market with
            // obs.atBank still true and a cached stock list the server had
            // long stopped honouring. BankErrand::Tick returns Success
            // immediately whenever BankContainer() is set (bank_errand.h: the
            // box serial IS the success condition), so the inherited box has
            // to be dropped before asking, or the errand rubber-stamps it.
            if (!marketBoxOpened_) {
                if (!bankErrand_.Running()) {
                    if (client.BankContainer()) {
                        LogLine("market: the bank box was opened somewhere "
                                "else -- asking a banker here before trusting "
                                "what it says it holds");
                        client.ForgetBankContainer();
                    }
                    bankErrand_.Begin();
                }
                const life::BankErrandResult br = bankErrand_.Tick(client, obs);
                if (!br.why.empty())
                    LogLine("market: the stock is in the bank (%d %s) -- %s",
                            inBank, offer.item.c_str(), br.why.c_str());
                if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
                    nextActionMs_ = obs.nowMs + br.delayMs;
                if (br.status == life::ActivityStatus::Success) {
                    // Opened HERE. Cleared again the moment the character
                    // leaves for the market (the !arrived branch above) or the
                    // errand ends (ResetTradeState).
                    marketBoxOpened_ = true;
                    marketBoxReopens_ = 0;
                    return false;   // the box is open; the fetch runs next tick
                }
                if (life::IsTerminal(br.status)) {
                    LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"no banker "
                            "opened a box at the market (%s)\"", br.why.c_str());
                    bankErrand_.Cancel();
                    planner_.Cooldown(GoalKind::TradeWithPlayer,
                                      obs.nowMs + kMarketQuietMs);
                    planner_.Finish(false, "no banker at the market", obs.nowMs);
                    marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
                    return false;
                }
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            // BY NAME, NOT BY GRAPHIC (S1) -- same reasoning as the bank
            // fetch in DoEarnGold: `inBank` is hue-resolved from obs.bank,
            // so the serial has to be found the same way or a shared metal
            // graphic hands over the wrong stack.
            i32 inBox = 0;
            const u32 serial = FindContainerItemByName(
                client, client.BankContainer(), offer.item.c_str(), &inBox);
            // (b) THE BOX IS THE TRUTH; THE LEDGER IS ONLY A MEMORY OF IT.
            //
            // `inBank` comes from obs.bank, which away from a box is
            // state_.bank -- what this character last SAW in its own box
            // (Runner::LearnFromObservation). A box that has been opened here
            // and shows none of it says the memory is wrong, and the memory is
            // what has to give. Silently falling through, as this did, left
            // the goal to reach the announce below and shout an offer it could
            // not honour -- or, with the offer still unsatisfied, to be handed
            // straight back and try the same lookup again.
            if (!serial) {
                LogLine("market: the bank ledger says %d %s but the box shows "
                        "none -- trusting the box", inBank,
                        offer.item.c_str());
                ForgetBankedStock(offer.item.c_str());
                planner_.Cooldown(GoalKind::TradeWithPlayer,
                                  obs.nowMs + kNoAudienceMs);
                planner_.Finish(false, "the bank does not hold the stock",
                                obs.nowMs);
                nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                return false;
            }
            {
                i32 take = std::min(std::min(inBank, offer.qty - inPack), inBox);
                // CARRY WEIGHT IS A HARD LIMIT, NOT A SUGGESTION. `take` used
                // to be bounded only by what the box held and what the offer
                // asked for -- Tarath's own bank held 113 spare logs, and a
                // withdrawal of all of them on top of his working stock left
                // him unable to move. This handler has no per-item weight
                // table wired to it (tiledata::StaticTile::weight lives
                // behind Client, unreached here), so it bounds the COUNT by
                // the raw stones of headroom the status packet already
                // reports -- obs.maxWeight - obs.weight -- the same fields
                // DoMine's 0.95-full gate reads. Every tradeable good on this
                // shard costs at least one stone, so this can only ever
                // UNDER-admit units, never overload the pack; it is a floor,
                // not a measured per-item weight, and should be replaced with
                // real tiledata weight if that ever becomes reachable here.
                const i32 roomStones = std::max(0, obs.maxWeight - obs.weight);
                if (take > roomStones) {
                    LogLine("market: clamping withdrawal of %s from %d to %d "
                            "-- only %d stone(s) of carry room left (%d/%d)",
                            offer.item.c_str(), take, roomStones, roomStones,
                            obs.weight, obs.maxWeight);
                    take = roomStones;
                }
                if (take <= 0) {
                    LogLine("market: no carry room left for %s (%d/%d) -- "
                            "leaving it in the bank for now", offer.item.c_str(),
                            obs.weight, obs.maxWeight);
                    planner_.Cooldown(GoalKind::TradeWithPlayer,
                                      obs.nowMs + kNoAudienceMs);
                    planner_.Finish(false, "no carry room for the stock", obs.nowMs);
                    nextActionMs_ = obs.nowMs + 5000;
                    return false;
                }
                // A LIFT THE SERVER REFUSES IS AN ANSWER, NOT A HICCUP.
                //
                // The box serial outlives the visit and the cached contents
                // outlive the box, so a stale pair looks exactly like a full
                // one until the drag comes back "cannot lift that". Count the
                // refusals and re-ask a banker, which is precisely what
                // DoBank already does for coin ("the box will not give up its
                // coin -- reopening", coinLiftFails_ above). Progress -- any
                // change in what the pack holds -- resets the count.
                if (marketLiftItem_ != offer.item || marketLiftPack_ != inPack) {
                    marketLiftItem_ = offer.item;
                    marketLiftPack_ = inPack;
                    marketLiftFails_ = 0;
                }
                if (marketLiftFails_ >= kMaxMarketLiftFails) {
                    marketLiftFails_ = 0;
                    if (++marketBoxReopens_ > kMaxMarketBoxReopens) {
                        // Asked twice, opened twice, refused every time. The
                        // remembered stock is not really there.
                        LogLine("market: the bank ledger says %d %s but the "
                                "box will not give it up after %d reopenings "
                                "-- trusting the box", inBank,
                                offer.item.c_str(), marketBoxReopens_ - 1);
                        ForgetBankedStock(offer.item.c_str());
                        marketBoxReopens_ = 0;
                        planner_.Cooldown(GoalKind::TradeWithPlayer,
                                          obs.nowMs + kNoAudienceMs);
                        planner_.Finish(false,
                                        "the bank does not hold the stock",
                                        obs.nowMs);
                        nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                        return false;
                    }
                    LogLine("market: the box will not give up its %s -- "
                            "asking a banker to open it again",
                            offer.item.c_str());
                    client.ForgetBankContainer();
                    marketBoxOpened_ = false;
                    nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                    return false;
                }
                LogLine("market: withdrawing %d %s from the bank to sell",
                        take, offer.item.c_str());
                ++marketLiftFails_;   // cleared above the moment the pack moves
                // THROUGH THE ACTION SYSTEM, not around it. TakeFromContainer
                // sends 0x07/0x08 raw: no deadline, no ActionBusy, no verdict,
                // so a refused lift left nothing behind for the next tick to
                // read and the handler re-issued it every 2 s, seventy-six
                // times (run_r4/pair_Durnholde.console.txt:4382-4672).
                // ActionMoveItem is the same two packets with a 4 s deadline
                // (Client.cpp kMoveTimeoutMs) and a Rejected verdict on 0x27.
                client.ActionMoveItem(serial, static_cast<u16>(take),
                                      client.BackpackSerial());
                // (c) RETRY LONGER THAN THE DEADLINE. 2 s was shorter than the
                // 4 s move deadline, so every retry only ever superseded its
                // own predecessor and no attempt was ever allowed to resolve.
                nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                return false;
            }
        }
        // The stock is in the pack: nothing is being lifted any more.
        marketLiftFails_ = 0;
        marketLiftItem_.clear();
        marketBoxReopens_ = 0;
    }

    // --- the buyer ASKS -----------------------------------------------------
    //
    // It used to only listen. Its whole errand at the market was to BE PRESENT
    // while somebody else announced, so a trade could start only when a
    // gatherer happened to shout the exact thing this life happened to need,
    // in the three minutes it happened to be standing here. Supply had a voice
    // and demand did not, which is half a market again.
    //
    // Now it says what it wants, on the same schedule and with the same bound
    // as the seller's WTS: item, quantity, and the most it will pay -- a number
    // from ITS OWN purse and its own observed prices, never a market rate,
    // because this fleet has no such thing.
    if (!wantsToSell) {
        market::TradeIntent want;
        const bool haveWant = market::ChooseBuyWant(
            *me, holdings, state_.prices, tradePolicy_, obs.goldOnHand, &want);
        if (marketListenFromMs_ == 0) {
            marketListenFromMs_ = obs.nowMs;
            LogLine("market: at the market to buy %d %s -- asking for %llds",
                    buyable.front().qty, buyable.front().item.c_str(),
                    static_cast<long long>(kListenMs / 1000));
        }
        if (haveWant && obs.nowMs - tradeAnnouncedMs_ >= kAnnounceIntervalMs) {
            const std::string line = market::FormatBuyWant(want);
            LogLine("trade: announcing '%s'", line.c_str());
            client.ActionSay(line.c_str());
            tradeAnnouncedMs_ = obs.nowMs;
            // THE ANNOUNCEMENT IS THE PLAN, and it has to outlive this tick.
            //
            // A seller answers a WTB by walking over and OPENING A WINDOW.
            // From that moment DoTradeWithPlayer short-circuits into
            // DriveOpenTrade and this listen loop never runs again, so
            // whatever the buyer knew about the deal had to be written down
            // before the window appeared. It never was: tradeWantQty_ and
            // tradeOfferPrice_ are set only in the "heard a WTS" branch, the
            // buyer computed owed = 0 and put nothing in
            // (g_Odessa.console.txt:257-284, 2026-09-02). Saying it out loud
            // and not remembering it is the whole defect.
            tradeWant_ = want;
            tradeWantAskedMs_ = obs.nowMs;
        }
        if (obs.nowMs - marketListenFromMs_ >= kListenMs) {
            LogLine("market: nobody answered %s in %llds -- back to work",
                    buyable.front().item.c_str(),
                    static_cast<long long>(kListenMs / 1000));
            marketListenFromMs_ = 0;
            state_.memory.NoteEvent("no_player_seller",
                                    buyable.front().item.c_str(), "", obs.x,
                                    obs.y, obs.nowMs);
            marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
            // GO AND MAKE IT YOURSELF, if this life can. RouteForInput is
            // catalogue reasoning -- "is this something my own profession
            // gathers or processes" -- and a miner_smith short of ingots has
            // exactly that answer: mine, then smelt. Standing down into a
            // ten-minute cooldown with a gather route available is a bot
            // waiting for a delivery it could have dug up.
            //
            // Nothing here buys the material from an NPC. Most materials are
            // WorldProcessed and the vendor policy refuses them, so the honest
            // fallback for everyone else is bank-and-wait -- which is what the
            // plain stand-down below already is.
            const std::string& shortOf = buyable.front().item;
            // WHICH gather goal depends on what this life gathers. "not ore,
            // therefore chop wood" was fine while only miners and lumberjacks
            // reached here; a tailor gathers WOOL, and handing it an axe would
            // have sent it to the forest for a bolt of cloth.
            const GoalKind gatherGoal =
                me->gathers == "ore"    ? GoalKind::Mine
                : me->gathers == "wool" ? GoalKind::MakeCloth
                                        : GoalKind::GatherLogs;
            if (!me->gathers.empty() &&
                market::RouteForInput(*me, shortOf.c_str(),
                                      /*npcTradeKnown=*/false) ==
                    market::SupplyRoute::SelfProduce) {
                return HandOff(GoalKind::TradeWithPlayer, gatherGoal,
                               kMarketQuietMs,
                               "nobody was selling it, and this life can "
                               "gather it itself",
                               obs.nowMs);
            }
            planner_.Cooldown(GoalKind::TradeWithPlayer,
                              obs.nowMs + kMarketQuietMs);
            planner_.Finish(false, "nobody was selling", obs.nowMs);
            return false;
        }
        nextActionMs_ = obs.nowMs + 1000;
        return false;
    }
    // NOTE: `marketListenFromMs_` is a BUYER-only clock now. It used to be
    // shared with a seller's empty-room wait (see the removed
    // PlayersNearby(kTradeEarshot)==0 gate below, dropped 2026-08-30): a
    // seller used to hold off announcing until a headcount saw somebody, but
    // that headcount only counts a mobile whose paperdoll TITLE is already
    // known, and nothing here proactively asked for one -- two bots stood
    // five tiles apart for three minutes each, both silent, because neither
    // had ever been double-clicked. A per-tick scan would fix that but does
    // not scale (300 bots at one bank double-clicking the whole room is an
    // O(N^2) paperdoll storm), so the seller no longer waits for a headcount
    // at all: it announces on schedule like a human at the bank, and
    // whoever answers is identified from the SPEECH packet's own speaker
    // serial, not from this cache.

    // ANNOUNCE ONLY WHAT IS IN THE HAND.
    //
    // `offer` was chosen from the pack AND the bank, because a surplus in a
    // box is a perfectly good reason to make the trip. An OFFER is a promise,
    // and a promise has to be honourable without a second errand: "WTS 113
    // i_log" from a character carrying none is a deal that cancels itself in
    // the trade window. If the withdrawal above did not move the goods, say so
    // and stand down rather than shouting a number that is not true.
    market::TradeIntent announce;
    if (!market::ChooseSellOffer(*me, obs.pack, state_.prices, tradePolicy_,
                                 &announce)) {
        LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"the stock is still in "
                "the bank (%d %s) and the box did not give it up\"",
                market::QtyOf(obs.bank, offer.item), offer.item.c_str());
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "the goods never left the bank", obs.nowMs);
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        return false;
    }
    tradeOffer_ = announce;

    // ANNOUNCE ON SCHEDULE, NOT ON A HEADCOUNT (design change, 2026-08-30).
    //
    // This used to hold off announcing until PlayersNearby(kTradeEarshot) > 0
    // and otherwise wait up to kListenMs for somebody to walk into range --
    // "dont try to sell with WTS if no one around" (project owner,
    // 2026-08-29). But PlayersNearby only counts a mobile whose paperdoll
    // TITLE is already known (Client.cpp PaperdollTitle), and a title only
    // arrives after a double-click (0x88); nothing in this wait loop ever
    // issued one, so two bots standing five tiles apart at the same bank
    // waited out the full three minutes each, silently, having never asked
    // who was there (run_r4/pair2). A per-tick scan would answer that but
    // does not scale: 300 bots at one bank each double-clicking the room is
    // an O(N^2) paperdoll storm.
    //
    // So this now behaves like a human at the counter: say the offer on the
    // normal kAnnounceIntervalMs/kMaxAnnounces schedule and let whoever is
    // listening answer it. A respondent is identified from the SPEECH
    // packet's own speaker serial (the `heard` loop above, via
    // JournalHeardSince), not from a nearby-mobile headcount, so no scan is
    // needed to close a sale -- only PlayersNearby/AudienceFingerprint, used
    // below purely to avoid repeating an offer to the same known audience,
    // still depend on titles, and those arrive incidentally (BankErrand's
    // own scan during a withdrawal, radius now matched to kTradeEarshot --
    // see Client.cpp:4234) rather than from anything this errand asks for.
    //
    // Other BOTS count: they are the market. NPCs do not, which is why the
    // fingerprint below still asks for players rather than for mobiles.

    // AND NOT AT THE SAME PEOPLE WHO ALREADY IGNORED IT. The earshot test is
    // working correctly -- the "players" standing in the Minoc bank are the
    // owner's own observer characters, "Observer, Apprentice Archer" and "The
    // Eminent Owner Observer", which are real player bodies with no " the " in
    // their titles. They are an audience that never buys, so a character kept
    // shouting WTS at them and Jarvinia answered "Um... um?" each time.
    //
    // A player asks once and waits. Announcing again is only sensible when
    // somebody NEW is in the room.
    const u32 audience = client.AudienceFingerprint(kTradeEarshot);
    // ZERO MEANS "NOBODY'S TITLE IS KNOWN", NOT "THE SAME EMPTY ROOM". With
    // no proactive scan, an unscanned room fingerprints as 0 far more often
    // than not, and tradeAudienceIgnored_ starts at 0 too (never declined).
    // Matching those would stand this errand down before its first-ever
    // announcement, and again every cycle whose room happened not to get
    // scanned by something else. Only suppress when the SAME KNOWN audience
    // declined -- an unknown audience is not evidence of anything.
    if (audience != 0 && audience == tradeAudienceIgnored_) {
        LogLine("trade: the same people who ignored the last offer are still "
                "here -- not repeating it");
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "audience already declined", obs.nowMs);
        return false;
    }

    if (obs.nowMs - tradeAnnouncedMs_ >= kAnnounceIntervalMs) {
        const std::string line = market::FormatSellOffer(announce);
        LogLine("trade: announcing '%s'", line.c_str());
        client.ActionSay(line.c_str());
        tradeAnnouncedMs_ = obs.nowMs;
        ++tradeAnnounceCount_;
    }
    if (tradeAnnounceCount_ >= kMaxAnnounces) {
        LogLine("trade: nobody answered %d offers of %s -- back to work",
                tradeAnnounceCount_, announce.item.c_str());
        tradeAudienceIgnored_ = client.AudienceFingerprint(kTradeEarshot);
        state_.memory.NoteEvent("no_player_buyer", announce.item.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        tradeAnnounceCount_ = 0;
        marketListenFromMs_ = 0;   // the wait is over; do not inherit it
        // AND STOP SCHEDULING IT for a while. Finishing the goal was not
        // enough: the need scored the same on the very next tick, the errand
        // was re-picked, and a lumberjack spent whole sessions announcing logs
        // to an empty Yew while its own training and hunting needs -- which it
        // could actually have finished -- sat underneath it.
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        // AND COOL THE GOAL, not only the need. This line was missing while
        // both sibling stand-downs above it ("no audience", "audience already
        // declined") had it, and the gap is measured: fleet7.console.txt:3245
        // stood down at 16:24:10.031 and the planner re-selected
        // TRADE_WITH_PLAYER at 16:24:10.275 -- 244 ms later, reason "previous
        // goal abandoned: nobody wanted it", NeedTrade 0.55 x 145 = 79.8. The
        // whole cycle repeated end-to-end every 50.9s. marketQuietUntilMs_
        // only blanks the NEED on the next Observe; the planner needed telling
        // too. A wasted market trip now costs 8m20s of walking, so the rest
        // after one has to exceed it: kMarketQuietMs is 10 minutes.
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "nobody wanted it", obs.nowMs);
        return false;
    }
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

// Put the goods (or the gold) in the window, then accept. Kept separate
// because it is the half that runs on BOTH sides of the same deal.
bool Runner::DriveOpenTrade(Client& client, const Observation& obs) {
    const trade::TradeState& tr = client.Trade();

    // WHO IS ON THE OTHER SIDE -- from the packet, not from the journal.
    //
    // 0x6F SECURE_TRADE_OPEN carries the partner's serial AND name (trade.h),
    // which is authoritative. Heard::name comes from MobileName() and is empty
    // until something has learned the mobile's name, so a partner met purely
    // through speech logged as `trade:  put nothing in after 25s`
    // (g_Odessa.console.txt:280) and `trade: opening a window with  for 28
    // i_ingot_iron` (g_Elvar.console.txt:365). Backfilling from the window
    // costs nothing and makes every line below name somebody.
    if (tradePartner_ == 0 && tr.PartnerSerial() != 0)
        tradePartner_ = tr.PartnerSerial();
    if (tradePartnerName_.empty() && !tr.PartnerName().empty())
        tradePartnerName_ = tr.PartnerName();

    // A WINDOW THIS SIDE DID NOT PLAN, opened by a seller answering the WTB we
    // broadcast. There is no committed price or quantity because the "heard a
    // WTS" branch never ran for this deal -- but the want we said out loud a
    // few seconds ago is a plan, and it is the only honest basis for funding.
    //
    // FRESHNESS IS THE GUARD. `tradeWant_` is not cleared by every stand-down
    // path, so an old announcement must not fund a window opened half an hour
    // later for something else. The bound is the handshake's own turn times:
    // the listening window plus one announce turn.
    if (!tradeOffered_ && tradeSellingQty_ == 0 && tradeWantQty_ <= 0 &&
        tradeWant_.Valid() &&
        obs.nowMs - tradeWantAskedMs_ <= kListenMs + kAnnounceIntervalMs) {
        const i32 reserve =
            needCfg_.profession ? needCfg_.profession->goldReserve : 0;
        const market::FundingDecision fd =
            market::FundOpenWindow(tradeWant_, obs.goldOnHand, reserve);
        LogLine("trade: %s opened a window for the %d %s we asked for -- %s",
                tradePartnerName_.c_str(), tradeWant_.qty,
                tradeWant_.item.c_str(), fd.reason);
        if (fd.accept) {
            tradeItem_ = tradeWant_.item;
            tradeWantQty_ = fd.qty;
            tradeOfferPrice_ = tradeWant_.pricePerUnit;
            // The proof this trade moved anything is the pack and the purse,
            // and both have to be sampled BEFORE the coin goes in -- the
            // committed path samples them when it names a partner, and this
            // path has no such moment.
            tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
            tradeGoldBefore_ = obs.gold;
        }
        // A refusal is left to time out on the window's own give-up clock
        // below rather than retried: there is no second answer to give.
    }

    if (!tradeOffered_) {
        if (tradeSellingQty_ > 0) {
            const std::vector<u16> gfx = econ::GraphicsForItem(tradeItem_.c_str());
            for (u16 g : gfx) {
                const u32 serial = client.FindBackpackItemByGraphic(g);
                if (!serial) continue;
                client.ActionTradeOffer(serial,
                                        static_cast<u16>(tradeSellingQty_));
                break;
            }
        } else {
            const i32 owed = tradeWantQty_ * tradeOfferPrice_;
            const u32 gold = client.FindBackpackItemByGraphic(kGoldCoin);
            if (gold && owed > 0) {
                client.ActionTradeOffer(gold, static_cast<u16>(owed));
                LogLine("trade: offering %d gold for %d %s", owed,
                        tradeWantQty_, tradeItem_.c_str());
            }
        }
        tradeOffered_ = true;
        tradeOpenedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Accept once the partner has put something in. Accepting an EMPTY window
    // is how a character gives its goods away for nothing.
    if (tr.CurrentPhase() == trade::Phase::Open && !tr.TheirOffer().empty() &&
        !tr.CheckSent()) {
        LogLine("trade: partner offered %zu line(s); accepting",
                tr.TheirOffer().size());
        client.ActionTradeAccept(true);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    if (obs.nowMs - tradeOpenedMs_ > kTradeGiveUpMs) {
        LogLine("trade: %s put nothing in after %llds -- cancelling",
                tradePartnerName_.c_str(),
                static_cast<long long>(kTradeGiveUpMs / 1000));
        client.ActionTradeCancel();
        state_.memory.NoteEvent("trade_timeout", tradeItem_.c_str(),
                                tradePartnerName_.c_str(), obs.x, obs.y,
                                obs.nowMs);
        // ActionTradeCancel latches Phase::Cancelled locally; forget it here
        // so the Cancelled branch in DoTradeWithPlayer does not then report the
        // same close a second time, and end the goal rather than dropping back
        // into the listening loop beside a partner that just went silent.
        client.TradeForget();
        ResetTradeState();
        planner_.NoteAttempt(obs.nowMs);
        planner_.Cooldown(GoalKind::TradeWithPlayer,
                          obs.nowMs + kTradeRetryRestMs);
        planner_.Finish(false, "the partner put nothing in the window",
                        obs.nowMs);
        return false;
    }
    nextActionMs_ = obs.nowMs + 1000;
    return false;
}

void Runner::ResetTradeState() {
    tradePartner_ = 0;
    tradePartnerName_.clear();
    tradeItem_.clear();
    tradeOffer_ = market::TradeIntent{};
    tradeSellingQty_ = 0;
    tradeWantQty_ = 0;
    tradeOfferPrice_ = 0;
    // The broadcast want dies with the errand too -- a plan is only a plan
    // while the goal that made it is running. DriveOpenTrade additionally
    // bounds it by age, because not every stand-down reaches here.
    tradeWant_ = market::TradeIntent{};
    tradeWantAskedMs_ = 0;
    tradeOffered_ = false;
    tradePackBefore_ = 0;
    tradeGoldBefore_ = 0;
    tradeAnnounceCount_ = 0;
    tradeDeclined_.clear();
    travelInFlight_ = false;
    // THE TRIP ALLOWANCE IS PER ERRAND, NOT PER LIFE. It was reset only in the
    // failure branch, so a session that made one successful trip and later
    // wanted a second started from 1 of 3 and burned the whole allowance
    // across the day rather than across the errand.
    tradeTrips_ = 0;
    marketListenFromMs_ = 0;
    marketBoxOpened_ = false;
    marketLiftFails_ = 0;
    marketLiftPack_ = -1;
    marketLiftItem_.clear();
    marketBoxReopens_ = 0;
    // `bankErrand_` is deliberately NOT cancelled here: it is shared with the
    // bank and earn-gold errands, and an open box is useful to whatever runs
    // next. Only the market's own failure path cancels it.
}

// ---------------------------------------------------------------------------
// CRAFTING -- the half of the economy a gatherer never needed.
//
// A crafter's day is two errands. BUY_SUPPLIES fetches what it cannot make;
// CRAFT makes what it can sell. They are separate goals because they fail
// differently, and "I cannot craft" when the truth is "nobody has sold me
// nightshade yet" is a bot lying about its own state.
// ---------------------------------------------------------------------------

// Who sells a craft input, as the paperdoll names them. Read off this shard's
// own vendor templates, never guessed: the mage shop carries both halves of
// the Inscription chain -- SELL=i_scroll_blank,{10 15} and every Magery
// reagent (templates/tm_vend.scp:633-656) -- which is why a scribe's whole
// shopping trip is one stop.
const char* SupplierTradeFor(const std::string& item) {
    if (item.rfind("i_reag_", 0) == 0) return "mage";
    if (item == "i_scroll_blank")      return "mage";
    if (item == "i_bottle_empty")      return "alchemist";
    if (item == "i_feather")           return "provisioner";
    // KINDLING, which is what a campfire is made of and therefore what
    // cooking needs. Marla caught fish, cut them into steaks and then SOLD
    // the steaks raw at 2 gold because she could not cook: NeedCraft never
    // appeared in her list at all, since the recipe wanted a fire and she had
    // nothing to light. Cooked steaks are worth 6 (i_fish_cut_cooked
    // VALUE=6), so the missing gap was threefold value on every fish.
    //
    // The provisioner stocks it -- her own vendor window showed "kindling
    // gfx=0x0DE1 qty=36 price=1" while she stood there buying bread.
    if (item == "i_kindling")          return "provisioner";
    return nullptr;
}

// HOW MANY OF ONE INPUT A STOCKING TRIP BUYS.
//
// Not `craftBatch`, and not a constant. "buying should be bulk as well not one
// buy one" (project owner, 2026-08-30) and, for a crafter, "the brew batch is
// both the training and the stock to sell" (2026-09-04) -- a sitting's
// shortfall of five is neither a training batch nor a market stall.
//
// Every term is something the character can actually see at the counter:
//
//   budget  = the purse ABOVE this life's own reserve (Profession::goldReserve)
//   share   = budget split evenly across the recipe inputs it still has to buy,
//             so the second half of the recipe is still affordable after the
//             first -- two nightshade are worthless without the bottle
//   ratio   = how many of THIS input one output eats (2 : 1 for poison), which
//             is what turns a gold budget into a number of potions
//   quote   = the price the vendor has this moment named, so the exposure is
//             known rather than guessed
//   room    = what is left of the carry limit, at a deliberately pessimistic
//             one stone per unit. Reagents weigh a tenth of that; bottles do
//             not. An order that cannot be carried home is not a bulk buy, it
//             is an overloaded character standing in a shop.
//
// Never returns less than `atLeast`, the next sitting's own shortfall: this
// can only ever ENLARGE an order. The caller still clamps to the shelf and to
// spendable gold afterwards, and both of those are the harder limits in
// practice (VENDOR_S_ALCHEMIST stocks 250).
static i32 BulkSupplyQty(const char* output, const std::string& input,
                         i32 unitPrice, i32 gold, i32 reserve, i32 weight,
                         i32 maxWeight, i32 atLeast) {
    if (!output || unitPrice <= 0) return atLeast;   // no quote, no bulk
    const prod::Recipe* r = prod::FindRecipe(output);
    if (!r) return atLeast;

    i32 perOutput = 0;
    i32 buyableInputs = 0;
    for (const prod::Ingredient& in : r->inputs) {
        if (!in.item || in.qty <= 0) continue;
        if (!econ::CanBuyFromNPC(in.item).allowed) continue;
        ++buyableInputs;
        if (input == in.item) perOutput = in.qty;
    }
    if (perOutput <= 0 || buyableInputs <= 0) return atLeast;

    const i32 budget = gold - reserve;
    if (budget <= 0) return atLeast;   // the reserve is kept, by the brief
    const i32 share = budget / buyableInputs;
    const i32 outputs = share / (perOutput * unitPrice);
    i32 bulk = outputs * perOutput;

    // A fifth of the carry limit left free for what comes OUT of the mortar.
    if (maxWeight > 0) {
        const i32 room = static_cast<i32>(maxWeight * 0.8) - weight;
        if (bulk > room) bulk = room;
    }
    return bulk > atLeast ? bulk : atLeast;
}

// The craft-menu route table (kCraftMenus) and CraftMenuFor now live in
// Identity.cpp, next to ChooseCraft, so a no-server test can assert a route
// without linking the whole runner. Declaration: uo/life.h.

bool Runner::DoBuySupplies(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // SETTLE THE PREVIOUS ASK FIRST, from the gold the server actually took.
    // The ledger is the economy's own books; it must record purchases that
    // HAPPENED. Noting the flow at request time counted one on every
    // superseded retry against a vendor that had walked out of reach.
    if (!pendingBuyItem_.empty() && !client.ActionBusy()) {
        const i32 spent = pendingBuyGoldBefore_ - obs.gold;
        if (spent > 0) {
            state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase, spent,
                               pendingBuyItem_.c_str(), obs.nowMs);
            LogLine("supplies: the server took %d gold for %s (purse %d -> %d)",
                    spent, pendingBuyItem_.c_str(), pendingBuyGoldBefore_,
                    obs.gold);
            planner_.NoteProgress();   // THIS is progress: goods changed hands
            supplyReachFails_ = 0;
        } else {
            LogLine("supplies: asked to buy %s and the purse did not move -- "
                    "nothing was bought", pendingBuyItem_.c_str());
            // A REFUSED REACH IS ANSWERED BY WALKING, NOT BY ASKING AGAIN.
            // Owner rule: unreachable = 1 try, max 2. The window is stale
            // (the shopkeeper is where he was; the buyer is not), so drop it
            // and let the approach below walk to him afresh; after the second
            // refusal the goal stands down instead of asking a third time.
            if (client.ActionResult() == act::Result::Rejected) {
                ++supplyReachFails_;
                client.ForgetVendorOffer();
                if (supplyReachFails_ >= kMaxSupplyReachFails) {
                    LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" the '%s' "
                            "refused reach twice for %s",
                            faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                            supplyTrade_.c_str(), pendingBuyItem_.c_str());
                    pendingBuyItem_.clear();
                    pendingBuyGoldBefore_ = 0;
                    supplyReachFails_ = 0;
                    planner_.Cooldown(GoalKind::BuySupplies,
                                      obs.nowMs + kCraftStuckCooldownMs);
                    planner_.Finish(false, "vendor out of reach twice", obs.nowMs);
                    return false;
                }
                LogLine("supplies: the '%s' is out of reach -- forgetting the "
                        "stale window and walking to him (reach %d of 2)",
                        supplyTrade_.c_str(), supplyReachFails_);
            }
        }
        pendingBuyItem_.clear();
        pendingBuyGoldBefore_ = 0;
    }

    // THE REAGENT POUCH COMES FIRST.
    //
    // PRACTICE_SKILL leaves a list here when the pack cannot pay for any spell
    // its book holds (see DoPracticeSkill and include/uo/spellcast.h). It is
    // the same errand as any other input -- a mage shop stocks every Magery
    // reagent, tm_vend.scp:633-656, which is why SupplierTradeFor already
    // answers "mage" for the i_reag_ family -- so it reuses this whole path
    // rather than growing a second vendor flow. It jumps the queue because a
    // mage with an empty pouch cannot practise at all.
    // Anything the pack has since acquired is off the list -- bought here a
    // moment ago, looted, or carried all along.
    for (usize i = 0; i < reagentWants_.size();) {
        if (market::QtyOf(obs.pack, reagentWants_[i]) > 0) {
            LogLine("supplies: %s is in the pack now -- off the reagent list",
                    reagentWants_[i].c_str());
            reagentWants_.erase(reagentWants_.begin() +
                                static_cast<std::ptrdiff_t>(i));
            // The reason PRACTICE_SKILL stood down has just been carried out
            // of the shop. Let it have its turn back rather than exploring
            // with a full pouch for the rest of the cooldown.
            if (reagentWants_.empty())
                planner_.ClearCooldown(GoalKind::PracticeSkill);
        } else {
            ++i;
        }
    }

    prod::Ingredient want;
    // prod::Ingredient::item is a const char*, so the string it points at has
    // to outlive the rest of this function -- hence the local copy.
    std::string reagentPick;
    // WHAT THE INPUT IS FOR, so the quantity can be sized off the recipe's own
    // ratio at the counter (BulkSupplyQty). Empty for the spell-reagent path
    // above, whose quantity PRACTICE_SKILL already decided.
    std::string craftOutput;
    if (!reagentWants_.empty()) {
        reagentPick = reagentWants_.front();
        want.item = reagentPick.c_str();
        want.qty = reagentWantQty_ > 0 ? reagentWantQty_ : 1;
    } else {
        // THE SAME SITTING THE NEED MODEL COSTED. Asking at needCfg_.craftBatch
        // here while AssessNeeds asked at the stocked size makes the goal
        // report "nothing short after all" for the very shortfall that
        // selected it. See CraftBatchFromStock (uo/life.h).
        const CraftIntent intent =
            ChooseCraft(*me, obs,
                        CraftBatchFromStock(*me, obs, needCfg_.craftBatch,
                                            &craftFocus_),
                        &craftFocus_);
        if (!intent.item || intent.missing.empty()) {
            LogLine("supplies: nothing short after all");
            supplyItem_.clear();
            return true;
        }
        want = intent.missing.front();
        craftOutput = intent.item;
    }
    if (supplyItem_ != want.item) {
        supplyItem_ = want.item;
        supplyTrips_ = 0;
        supplySkipPlaces_.clear();
        const char* trade = SupplierTradeFor(supplyItem_);
        supplyTrade_ = trade ? trade : "";
        // The service the trade word maps to, so a shopkeeper wearing a
        // different title for the same job is still recognised.
        supplyService_ = ServiceForTrade(supplyTrade_.c_str());
    }

    if (supplyTrade_.empty()) {
        // NO NPC SELLS IT IS NOT THE END OF THE ERRAND -- ask where it really
        // comes from. See market::RouteForInput (include/uo/market.h) for the
        // three characters this cost in the 2026-09-01 wave.
        const market::SupplyRoute route =
            market::RouteForInput(*me, supplyItem_.c_str(),
                                  /*npcTradeKnown=*/false);
        if (route == market::SupplyRoute::SelfProduce) {
            const GoalKind make = ProducingGoalFor(supplyItem_);
            LogLine("supplies: no NPC sells %s and this life makes it -- "
                    "handing the errand to %s instead of shopping for it",
                    supplyItem_.c_str(), GoalKindName(make));
            return HandOff(GoalKind::BuySupplies, make, kCraftStuckCooldownMs,
                           "this life produces its own input", obs.nowMs);
        }
        if (route == market::SupplyRoute::PlayerMarket) {
            LogLine("supplies: %s is a player-market good -- no NPC may sell "
                    "it, so this is a rendezvous, not a shopping trip",
                    supplyItem_.c_str());
            return HandOff(GoalKind::BuySupplies, GoalKind::TradeWithPlayer,
                           kCraftStuckCooldownMs,
                           "another profession makes this, not a shopkeeper",
                           obs.nowMs);
        }
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s route=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownSupplier),
                supplyItem_.c_str(), market::SupplyRouteName(route));
        // STAND DOWN, like the two failure paths below already do. CRAFT hands
        // off to BUY_SUPPLIES on kCraftStuckCooldownMs (Runner.cpp, "nothing
        // carried or worn to open the menu with"), so with Craft on a two
        // minute brake and this path finishing with none, Finish(false) alone
        // lets the planner re-pick BUY_SUPPLIES on the very next tick and the
        // pair alternates for the rest of the session. No trade sells this
        // input; that verdict is a table lookup and will not change today.
        // (audit 2026-08-30, finding 5.)
        planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "no trade known to sell it", obs.nowMs);
        return false;
    }

    // THE POLICY DECIDES, not the shop. An NPC that technically stocks a thing
    // is not thereby a legitimate source for it -- that is the whole point of
    // the vendor matrix, and buying a player-market good from a vendor would
    // cut a real player out of the economy this project exists to simulate.
    const econ::VendorRuling ruling = econ::CanBuyFromNPC(supplyItem_.c_str());
    if (!ruling.allowed) {
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s class=%s (%s)",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                supplyItem_.c_str(), econ::VendorClassName(ruling.klass),
                ruling.reason ? ruling.reason : "");
        state_.memory.NoteEvent("policy_refused", supplyItem_.c_str(),
                                econ::VendorClassName(ruling.klass), obs.x,
                                obs.y, obs.nowMs);
        // Same brake, and this is the case the audit named: an input the
        // vendor matrix refuses is refused for the whole session, so retrying
        // it next tick is not a retry, it is a character standing still.
        planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "the vendor policy refuses this input", obs.nowMs);
        return false;
    }

    if (client.TravelBusy()) {
        VetoTripOverSessionBudget(client, obs, GoalKind::BuySupplies,
                                  "BUY_SUPPLIES", kCraftStuckCooldownMs);
        return false;
    }

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        const u32 keeper = client.NearestShopkeeperWithTrade(supplyTrade_.c_str(),
                                                             supplyService_);
        if (keeper) {
            i32 vx = 0, vy = 0; i8 vz = 0;
            if (client.MobilePosition(keeper, &vx, &vy, &vz)) {
                const i32 d = TileDist(obs.x, obs.y, vx, vy);
                const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
                if (d > 1 || dz > 3) {
                    travelInFlight_ = client.TravelToEntity(keeper, 1);
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
            }
            client.ActionVendorOpen(keeper);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        if (!travelInFlight_) {
            if (++supplyTrips_ > kMaxSupplyTrips) {
                LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" no '%s' found "
                        "after %d trips",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        supplyTrade_.c_str(), supplyTrips_);
                planner_.Finish(false, "no supplier reachable", obs.nowMs);
                supplyTrips_ = 0;
                nextActionMs_ = obs.nowMs + 30000;
                return false;
            }
            LogLine("supplies: looking for a '%s' to sell %d %s (trip %d, %zu "
                    "place(s) already tried)",
                    supplyTrade_.c_str(), want.qty, supplyItem_.c_str(),
                    supplyTrips_, supplySkipPlaces_.size());
            // SKIP WHAT WAS ALREADY SENT TO. Without a persistent skip list
            // every retry re-ran PickServicePlace with an empty one and
            // picked the SAME shop -- trip 2 walked Dorvar right back to the
            // Ocllo provisioner whose transit had just stalled, instead of
            // falling through to the next-best candidate. See
            // supplySkipPlaces_.
            travelInFlight_ = client.TravelToServiceSkipping(
                ServiceForTrade(supplyTrade_.c_str()),
                HomeOrNearest(state_.homeCity), {}, &supplySkipPlaces_);
            if (!travelInFlight_) {
                LogLine("goal_blocked=BUY_SUPPLIES reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("supplies: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // AN OPEN SHOP WINDOW IS NOT A VENDOR STILL IN REACH.
    //
    // The approach check above only runs while no offer is open, so once the
    // window came up the character stopped watching where the shopkeeper went
    // -- and Britain's shopkeepers walk. In run_m5/p0gate1 Ysolde bought once,
    // Nightshade wandered off, and every buy after that was answered "You
    // can't reach the Vendor" while the goal re-issued it every 2.5 seconds
    // for the rest of the session. Re-measure before each purchase and walk
    // back if the shop has moved.
    {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (!client.MobilePosition(vendor, &vx, &vy, &vz)) {
            // The window is open but the shopkeeper is not even on screen:
            // the buyer went to the bank and came back to another street
            // (Elara 01:08 -> 01:09, 2026-09-04). Nothing to measure against,
            // so the window is stale by definition. Forget it; the block
            // above walks to a shopkeeper it can see.
            LogLine("supplies: the shop window is open but no '%s' is in "
                    "sight -- forgetting it and walking back to the shop",
                    supplyTrade_.c_str());
            client.ForgetVendorOffer();
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if ((d > kVendorReach || dz > 3) &&
                ++vendorChases_ <= kMaxVendorChases) {
                LogLine("supplies: the '%s' has moved to %d,%d (%d tiles) -- "
                        "walking back before buying (chase %d of %d)",
                        supplyTrade_.c_str(), vx, vy, d, vendorChases_,
                        kMaxVendorChases);
                travelInFlight_ = client.TravelToEntity(vendor, 1);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            if (d > kVendorReach && vendorChases_ > kMaxVendorChases) {
                // Chasing has failed; ask anyway and let Sphere answer. A
                // refusal is information and ends the goal honestly, where
                // another lap ends nothing.
                LogLine("supplies: the '%s' keeps moving (%d tiles after %d "
                        "chases) -- asking from here and letting the server "
                        "decide", supplyTrade_.c_str(), d, kMaxVendorChases);
            }
        }
    }

    // ONE BUY IN FLIGHT AT A TIME. kVendorTimeoutMs is 8 s and this used to
    // re-issue every 2.5 s, so each attempt was superseded before it could
    // resolve -- the identical defect the bank ask had.
    if (client.ActionBusy()) return false;

    // A shop window is open: find the input in it and buy the shortfall.
    //
    // WHOSE window, though. An offer outlives the goal that opened it, so this
    // loop used to search whatever shop was last visited. Ysolde walked from a
    // baker to a mage and searched the BAKER's window for blank scrolls,
    // failing 746 times with "this 'mage' does not stock i_scroll_blank" while
    // standing in a mage shop that sells them.
    if (!OfferBelongsTo(client, vendor)) {
        LogLine("supplies: the open shop window is not this vendor's -- "
                "asking '%s' for their own list", supplyTrade_.c_str());
        client.ActionVendorOpen(vendor);
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }
    const std::vector<u16> gfx = econ::GraphicsForItem(supplyItem_.c_str());
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : gfx) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        const i32 unit = static_cast<i32>(v.price);
        // A STOCKING TRIP, NOT A SITTING'S SHORTFALL. want.qty is what the
        // next batch at the bench is short of; this is what a crafter who
        // makes her living from the bench actually walks out of the shop with.
        // Sized here rather than at the door because the quote is only known
        // once the window is open. See BulkSupplyQty above for every term.
        i32 take = BulkSupplyQty(craftOutput.empty() ? nullptr
                                                     : craftOutput.c_str(),
                                 supplyItem_, unit, obs.gold, me->goldReserve,
                                 obs.weight, obs.maxWeight, want.qty);
        // AND NEVER MORE THAN THE SHELF HOLDS. Sphere refuses the WHOLE order
        // when the quantity exceeds stock, so one over-ask buys nothing at
        // all rather than buying what is there -- the defect that stalled the
        // bandage errand (a healer with 19, asked for 20). Same guard here,
        // where it had not bitten yet only because the batch sizes are small.
        if (v.amount > 0 && take > static_cast<i32>(v.amount))
            take = static_cast<i32>(v.amount);
        // WORKING CAPITAL IS NOT THE DEATH RESERVE.
        //
        // goldReserve is what a life keeps back to replace a tool after it
        // dies. Charging the working stock against it deadlocked a scribe
        // outright: blank scrolls cost 3 gold, the purse held 781, the
        // reserve was 900, and the character stood in the mage shop
        // refusing to buy three scrolls -- every fifteen seconds, for the
        // whole session. A reserve that forbids the only activity which
        // refills it is not caution, it is a trap.
        //
        // So inputs are bought out of working capital: everything above a
        // small hard floor, and never more than a quarter of the purse in one
        // trip, so a bad price cannot empty a character either.
        constexpr i32 kHardFloor = 100;      // never end a trip broke
        const i32 above = obs.gold - kHardFloor;

        // BUY THE BATCH IN ONE GO. The quarter-purse cap was applied on every
        // pass, so a life bought three bottles, then two, then one, shrinking
        // as its own purse shrank -- three vendor trips for one errand:
        //   the server took 36 gold for i_bottle_empty (purse 180 -> 144)
        //   the server took 24 gold for i_bottle_empty (purse 144 -> 120)
        //   the server took 12 gold for i_bottle_empty (purse 120 -> 108)
        // "buying should be bulk as well not one buy one" (project owner,
        // 2026-08-30).
        //
        // The quarter rule exists so an UNKNOWN price cannot empty a purse in
        // one go, and that reasoning only holds while the price is unknown.
        // Here the vendor has already quoted `unit`, so the exposure is known
        // exactly -- the floor is the protection that matters, and it still
        // stands. With a quoted price a life may spend everything above it.
        const bool priceIsKnown = unit > 0;
        const i32 spendable =
            priceIsKnown ? above
                         : ((above < obs.gold / 4) ? above : obs.gold / 4);
        if (unit > 0 && take * unit > spendable) take = spendable / unit;
        if (take <= 0) {
            // SAY WHAT IS BEING WAITED FOR, AND STAND DOWN SO IT CAN HAPPEN.
            //
            // This used to log "blocked" and retry every fifteen seconds
            // forever without ever finishing, so the planner was never asked
            // again -- and the thing that would have unblocked it was sitting
            // in the same pack. Voris stood outside the alchemist with 108
            // gold, unable to afford a 12 gold bottle (the floor of 100 plus
            // the quarter-purse rule leaves 8 spendable), while carrying five
            // poison potions worth about a hundred. "it should see what is it
            // waiting?" (project owner, 2026-08-30).
            //
            // What it is waiting for is GOLD, so name that, and finish so
            // EARN_GOLD gets a turn. If there is genuinely nothing to sell the
            // planner will come back here and the cooldown paces the retry.
            const std::vector<market::Offer> couldSell =
                needCfg_.profession
                    ? market::Surplus(*needCfg_.profession, obs.pack,
                                      market::PolicyForPurse(obs.goldOnHand))
                    : std::vector<market::Offer>{};
            i32 sellable = 0;
            for (const market::Offer& o : couldSell) sellable += o.qty;
            LogLine("supplies: %s costs %d each and only %d of %d gold is "
                    "spendable -- waiting on GOLD, and there %s %d thing(s) "
                    "in the pack to sell; standing down so that can happen",
                    supplyItem_.c_str(), unit, spendable, obs.gold,
                    sellable == 1 ? "is" : "are", sellable);
            planner_.NoteAttempt(obs.nowMs);
            planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + 45000);
            planner_.Finish(false, "cannot afford the inputs yet", obs.nowMs);
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }

        // COIN IN THE PACK, NOT IN THE BOX. obs.gold is the status-bar total
        // and counts the bank on this shard, so "spendable" above can be a
        // comfortable number while the purse is empty -- and the vendor is the
        // one who notices:
        //   supplies: buying 5 i_scroll_blank at 6 each from 'blank scrolls'
        //   Shunnar: Begging thy pardon, but thou canst not afford that.
        //   supplies: asked to buy i_scroll_blank and the purse did not move
        // Ysolde repeated that 42 times in one run holding 409 gold, all of it
        // banked. GET_TOOL and the trainer fee already fetch their coin first;
        // this path was simply never wired to do the same.
        // ...AND THEN IT WAS WIRED, AND IT WAS WRONG FOR THIS SHARD. sphere.ini
        // PayFromPackOnly=0: a vendor purchase draws on the bank box itself.
        // Proven live 2026-09-04 01:25:59 -- Elara carried 200 gold, banked
        // 9006, and Hyman took 864 for 72 bottles without a word. The fetch
        // then sent her 389 tiles to Magincia for 4 gold she did not need and
        // she spent the rest of the session at that bank. Trainer fees and
        // tools keep their fetch (Train.cpp, Gear.cpp): a fee is gold dropped
        // on the NPC, which really does need the coin in the pack.
        // Ysolde's "canst not afford" above was a purse AND bank short of the
        // sum, which the poverty check in BulkSupplyQty already answers.
        (void)take;

        market::PriceObservation po;
        po.item = supplyItem_;
        po.pricePerUnit = unit;
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);

        LogLine("supplies: buying %d %s at %d each from '%s'", take,
                supplyItem_.c_str(), unit, v.name.c_str());
        client.ActionVendorBuy(vendor, v.serial, static_cast<u16>(take));
        // THE LEDGER RECORDS WHAT THE SERVER DID, not what we asked for.
        // Noting the flow here counted a purchase on every one of those
        // superseded retries, so the economy's own books recorded gold
        // destroyed that never left the purse. What is remembered instead is
        // the ASK; the flow is noted on the next tick, from the gold the
        // server actually took (see `pendingBuy_` below).
        pendingBuyItem_ = supplyItem_;
        pendingBuyGoldBefore_ = obs.gold;
        // BUYING IS AN ATTEMPT, NOT PROGRESS. NoteProgress() cleared the
        // failure ladder on every retry, so a goal that bought nothing for
        // twenty minutes never ran out of attempts.
        planner_.NoteAttempt(obs.nowMs);
        supplyItem_.clear();
        // Longer than kVendorTimeoutMs (8 s, Client.cpp): an ask re-issued
        // inside its own deadline supersedes itself and never resolves.
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" this '%s' does not stock %s",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            supplyTrade_.c_str(), supplyItem_.c_str());
    state_.memory.NoteEvent("vendor_lacks", supplyItem_.c_str(),
                            supplyTrade_.c_str(), obs.x, obs.y, obs.nowMs);
    // STAND DOWN, do not spin. Finish(false) alone re-picks on the next tick,
    // and when a stale shop window made this branch reachable from anywhere it
    // failed sixteen times a SECOND (v_Marla, 23:57:44). The window bug is
    // fixed at the source -- 0x3B now clears the offer -- but a shop that
    // truly lacks the item deserves the same brake GET_TOOL has: the stock
    // will be no different two ticks from now.
    planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + 60000);
    planner_.Finish(false, "this vendor does not stock it", obs.nowMs);
    return false;
}

// ---------------------------------------------------------------------------
// SMELTING. The missing link in "mine smelt smith sell".
//
// "it didnt smelt iron ore" (project owner, 2026-08-29). Corwyn reached the
// Minoc mine, swung a pickaxe, filled his pack with Iron Ore -- and stopped
// there, because no goal in the life could turn ore into metal. Downstream
// everything then failed for the right reason and the wrong cause: EARN_GOLD
// refused to sell ore because a raw material is a player-market good, and
// CRAFT was short of the ingots that were sitting in his pack as ore.
// ---------------------------------------------------------------------------
// COIN IN HAND BEFORE A PURCHASE.
//
// "nobody carry gold on them unless they need to buy something -- always put
// additional items to bank, so they can get it when they need it" (project
// owner). The first half was implemented and the second half was not: a
// character banked everything and then stood in front of a shop with an empty
// purse. "even though you say here carry 1000 gp on him he is not carry 1000
// gp" -- the 1000 in that log is the THRESHOLD, not what is carried.
//
// Two symptoms, one cause: Olin quoted 196 gold for Arms Lore and the payment
// step answered "no gold stack found in the pack", and a smith hammer could
// not be bought for the same reason.
//
// Returns true when it has taken over the tick (walking to the bank, opening
// it, or lifting coin out); the caller should return false and try again.
bool Runner::FetchCoinForPurchase(Client& client, const Observation& obs,
                                  i32 needed) {
    if (needed <= 0) return false;
    if (obs.goldOnHand >= needed) { coinWanted_ = 0; return false; }
    // Nothing banked either -- poverty, not logistics. The caller's own
    // "cannot afford" path is the honest answer.
    if (obs.gold < needed) { coinWanted_ = 0; return false; }

    // DO NOT OPEN THE BANK HERE. An earlier version of this walked to the box
    // and shouted "bank" itself, and it was wrong in four separate ways at
    // once -- it tested obs.atBank (which means the box is already OPEN), it
    // passed banker serial 0, it asked before any paperdoll title had been
    // fetched, and it re-issued inside open_bank's own 3s deadline so every
    // attempt superseded the last. The visible result was a character standing
    // at the bank saying "bank" over and over. ("corwyn spamming bank")
    //
    // There is already a goal that opens the box properly, with a skip list
    // for bankers that do not answer: BANK. And there is already a withdrawal
    // that works -- EARN_GOLD lifts stock out of the box the same way. So this
    // only does the part neither of them does: name the sum wanted, so NeedBank
    // fires, and lift the coin once the box is open.
    // "we withdraw stuff from bank before -- why it is hard for this account"
    // (project owner, 2026-08-29). It was not hard; it was duplicated.
    coinWanted_ = needed;

    const u32 box = client.BankContainer();
    if (box) {
        static const u16 kCoin[] = {kGoldCoin};
        const u32 stack = client.FindContainerItemByGraphic(box, kCoin, 1);
        if (stack && !client.ActionBusy()) {
            const i32 want = (needed - obs.goldOnHand) + 200;
            LogLine("bank: withdrawing %d gold for a purchase (need %d, "
                    "carrying %d)", want, needed, obs.goldOnHand);
            client.ActionMoveItem(stack, static_cast<u16>(want),
                                  client.BackpackSerial());
            nextActionMs_ = obs.nowMs + 3000;
            return true;
        }
    }

    // Box shut: let the BANK goal have the tick. Reporting "not now" rather
    // than steering keeps one goal in charge of one errand.
    // AND STAND DOWN LONG ENOUGH FOR THE BANK TRIP TO HAPPEN. Finishing alone
    // was not enough: NeedTool scores 0.90 against NeedBank's 0.80, so the
    // buying goal won the very next tick, stood down again, and BANK never got
    // a turn -- "500 gold needed and 0 carried" every three seconds while the
    // character stood still.
    LogLine("%s: %d gold needed and %d carried -- standing down so the bank "
            "goal can fetch it",
            GoalKindName(planner_.Current().kind), needed, obs.goldOnHand);
    planner_.Cooldown(planner_.Current().kind, obs.nowMs + 45000);
    planner_.Finish(false, "needs coin from the bank", obs.nowMs);
    nextActionMs_ = obs.nowMs + 1000;
    return true;
}

}  // namespace uo::life
