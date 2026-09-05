#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// --- banking ---------------------------------------------------------------

void Runner::IssueBankItemMove(Client& client, const Observation& obs,
                               u32 serial, u16 amount, u32 box) {
    client.ActionMoveItem(serial, amount, box);
    bankItemMovePending_ = true;
    nextActionMs_ = obs.nowMs + 1500;
}

bool Runner::SettleBankItemMove(Client& client, const Observation& obs) {
    if (!bankItemMovePending_ || client.ActionBusy()) return false;
    bankItemMovePending_ = false;

    const act::Result r = client.ActionResult();
    if (r == act::Result::Success) {
        bankItemMoveFails_ = 0;
        planner_.NoteProgress();
        return false;
    }

    // A DEPOSIT THAT DID NOT LAND IS NOT PROGRESS, AND THE THIRD ONE IS THE
    // LAST. Source-X refuses every drop into a bank box unless the character
    // is standing on the exact tile the box was opened from
    // (CClientEvent.cpp:448-467) and bounces the item back with a plain 0x25,
    // so a character that walked one step deposits nothing, forever, in
    // silence. Letting the box go forces the next tick to walk to a banker
    // and open a fresh one, which re-stamps that tile.
    ++bankItemMoveFails_;
    client.ForgetBankContainer();
    planner_.NoteAttempt(obs.nowMs);
    if (bankItemMoveFails_ >= kMaxBankItemMoveFails) {
        LogLine("bank: %d deposits in a row did not land (last: %s) -- "
                "standing the bank goal down instead of asking again",
                bankItemMoveFails_, act::ResultName(r));
        bankItemMoveFails_ = 0;
        nextActionMs_ = obs.nowMs + 20000;
    } else {
        LogLine("bank: the deposit did not land (%s, %d of %d) -- opening the "
                "box again before trying",
                act::ResultName(r), bankItemMoveFails_,
                kMaxBankItemMoveFails);
        nextActionMs_ = obs.nowMs + 2500;
    }
    return true;
}

bool Runner::DoBank(Client& client, const Observation& obs) {
    if (coinWanted_ > 0 && obs.goldOnHand >= coinWanted_) {
        LogLine("bank: %d gold is in the pack now -- the purchase can go ahead",
                obs.goldOnHand);
        coinWanted_ = 0;
        coinLiftFails_ = 0;
    }
    const u32 box = client.BankContainer();
    // ONLY the serial is needed to deposit. Requiring ContainerKnown -- that
    // the box's CONTENTS have arrived -- was wrong twice over: an EMPTY bank
    // box sends no 0x3C at all, so the flag never flipped, and the character
    // re-opened the bank every 2.5 seconds forever without ever putting
    // anything in it. You do not need to know what is in a container to put
    // something into it.
    if (box) {
        // The box is open, so whoever we asked did answer. Forgiving the
        // bankers we had written off is BankErrand's own business now
        // (NpcRotation::NoteAnswered), which is why nothing is cleared here.
        bankErrand_.Cancel();
        if (client.ActionBusy()) return false;

        // Read the LAST deposit before asking for another one.
        if (SettleBankItemMove(client, obs)) return false;

        // STAND STILL TO USE THE BOX. The bank box only answers from the tile
        // it was opened on (Source-X CClientEvent.cpp:448-467 for drops,
        // CCharStatus.cpp:1063-1069 for lifts); a deposit issued mid-stride is
        // a guaranteed silent bounce. The coin withdrawal below already knew
        // this the hard way -- every branch needs it.
        if (client.TravelBusy()) return false;
        if (!client.BankOpenTileHeld()) {
            LogLine("bank: the box was opened at (%d,%d) and we are at "
                    "(%d,%d) -- opening it again from here",
                    client.BankOpenX(), client.BankOpenY(), obs.x, obs.y);
            client.ForgetBankContainer();
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }

        // Keep one working smithing batch but bank the rest.  The generic
        // keep-list below protects all declared crafting inputs; for miner
        // smiths that previously meant *every* ingot stayed in the pack even
        // after NeedBank had correctly identified it as surplus.
        if (needCfg_.profession && needCfg_.profession->gathers == "ore") {
            constexpr i32 kCarryMetalBatch = 20;
            i32 ingots = 0;
            const u32 ingot = FindBackpackItemByName(client, "i_ingot_iron", &ingots);
            if (ingot && ingots > kCarryMetalBatch) {
                const u16 moving = static_cast<u16>(ingots - kCarryMetalBatch);
                LogLine("banking %u iron ingots, keeping %d as the smithing batch",
                        moving, kCarryMetalBatch);
                IssueBankItemMove(client, obs, ingot, moving, box);
                return false;
            }
        }

        // SETTLE THE GOLD DEPOSIT ASKED FOR LAST TICK, from what actually
        // left the pack -- never from having merely issued the drag. The
        // deposit below used to call NoteProgress() the instant it ISSUED
        // the move, not once it LANDED, which hid a real failure mode:
        // Lyra's box kept answering "item landed in 0x4000C91D, not the
        // destination 0x4000C944" -- the exact "this box is not really
        // open" case the item-deposit loop already recovers from below --
        // fifteen times a minute, each one credited as progress. That both
        // defeated the "progress==0 -> stand down" guard further down this
        // function and meant NeedBank never went quiet, so BANK kept
        // outscoring FILL_SPELLBOOK every ~20 s for the rest of the session
        // (docs/LIFE_GATE_WAVE1.md theme 3, run_gates/g_Lyra.console.txt
        // 00:40:09-00:42:21).
        if (bankGoldDepositPending_) {
            bankGoldDepositPending_ = false;
            const DepositOutcome out = SettleDeposit(
                pendingGoldDepositBefore_, obs.goldOnHand,
                bankGoldDepositTries_, kMaxBankDepositTries);
            if (out.progressed) {
                planner_.NoteProgress();
            } else if (out.giveUp) {
                LogLine("bank: %d attempts to deposit gold all landed "
                        "elsewhere -- this box is not really open",
                        bankGoldDepositTries_);
                client.ForgetBankContainer();
                bankGoldDepositTries_ = 0;
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 3000;
                return false;
            } else {
                LogLine("bank: gold did not leave the pack (attempt %d of "
                        "%d) -- asking the box again",
                        bankGoldDepositTries_, kMaxBankDepositTries);
            }
        }

        // TAKE OUT BEFORE PUTTING IN. A purchase waiting on coin is the reason
        // this trip was made at all, and depositing first would empty the pack
        // it is trying to fill.
        if (coinWanted_ > obs.goldOnHand) {
            // STAND STILL TO LIFT. The box opened while the character was
            // still walking to the banker; he then stepped through a door to
            // 2502,548, and every lift came back "cannot lift that" -- the
            // open container does not survive being walked away from.
            if (client.TravelBusy()) return false;
            static const u16 kCoin[] = {kGoldCoin};
            const u32 stack = client.FindContainerItemByGraphic(box, kCoin, 1);
            if (stack && coinLiftFails_ >= 2) {
                // The reference has gone stale. Ask for the box again rather
                // than dragging at a serial the server no longer honours.
                LogLine("bank: the box will not give up its coin -- reopening");
                client.ForgetBankContainer();
                coinLiftFails_ = 0;
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            if (stack) {
                const i32 want = (coinWanted_ - obs.goldOnHand) + 200;
                LogLine("bank: withdrawing %d gold -- %d is wanted for a "
                        "purchase and %d is carried",
                        want, coinWanted_, obs.goldOnHand);
                client.ActionMoveItem(stack, static_cast<u16>(want),
                                      client.BackpackSerial());
                ++coinLiftFails_;   // cleared below the moment coin arrives
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 3000;
                return false;
            }
            LogLine("bank: %d gold wanted but the open box shows no coin",
                    coinWanted_);
            coinWanted_ = 0;
        }

        // Deposit whatever THIS LIFE produces, not just logs.
        //
        // This used to be hardcoded to kLog, which is the same lumberjack
        // assumption that made a mage want a hatchet. A smith carrying fifty
        // iron ingots reached the bank and deposited nothing, stayed at
        // 165/162 stones, and crawled through hundreds of fatigue rejects for
        // the rest of its life. Five of twenty bots were immobilised by it.
        //
        // One item per tick: each move is a separate action the server may
        // refuse, and batching them hides which one failed.
        // DO NOT BANK WHAT THIS LIFE IS ABOUT TO SELL.
        //
        // The need model already refuses to schedule a deposit for sellable
        // output (Needs.cpp, SellableInstead), but a BANK objective restored
        // from a previous session bypasses that reasoning entirely: Bryn
        // reached the bank carrying 15 fish, deposited all 15, and EARN_GOLD
        // pulled the same 15 straight back out two seconds later. A player
        // does not put its stock in the box on the way to the shop.
        //
        // Weight is the exception the need model already makes, and it is the
        // real reason to bank: a load too heavy to carry to a buyer has to go
        // somewhere.
        // GOLD GOES IN WHATEVER THE PACK WEIGHS.
        //
        // Everything below is gated on the load being heavy, which is the
        // right rule for STOCK -- a smith does not put its ingots in the box
        // on the way to the shop. It is the wrong rule for coin: gold is
        // barely any weight at all, so the gate never opened for it, and
        // Corwyn stood at an open bank box carrying 9,842 gold and deposited
        // nothing. "corwyn didnt put money on bank" (project owner).
        //
        // Coin is a RISK problem, not a weight problem, and this shard has
        // full loot on death. What stays is the profession's own reserve plus
        // working change; the rest goes in before anything else is considered.
        {
            const u32 coin = client.FindBackpackItemByGraphic(kGoldCoin);
            const i32 keep =
                std::min(needCfg_.profession ? needCfg_.profession->goldReserve
                                             : 0,
                         kMaxGoldCarriedRt) +
                kGoldWorthCarryingRt;
            // WHAT IS CARRIED, NOT WHAT IS OWNED. obs.gold is the status-bar
            // figure and counts the bank box, so this asked to deposit 8,785
            // coins one second after withdrawing 700 -- undoing the errand
            // that made the trip.
            const i32 spare = obs.goldOnHand - keep;
            if (coin && spare > 0 && !client.ActionBusy()) {
                LogLine("bank: depositing %d gold, keeping %d for this life's "
                        "own errands", spare, keep);
                client.ActionMoveItem(coin, static_cast<u16>(spare), box);
                // An ASK, not yet progress -- settled at the top of this
                // block on the tick after it lands (or does not).
                pendingGoldDepositBefore_ = obs.goldOnHand;
                bankGoldDepositPending_ = true;
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        const bool loadDemandsIt =
            obs.WeightFraction() >= BankWeightLine(needCfg_) ||
            state_.huntReturnPending;
        if (needCfg_.profession && (loadDemandsIt ||
                                    needCfg_.profession->produces.empty())) {
            for (const std::string& made : needCfg_.profession->produces) {
                // FIND AND COUNT THE SAME THING (S1). This used to take the
                // serial from FindBackpackItemByGraphic -- ONE stack, of
                // whatever hue happened to come first -- and the amount from
                // BackpackItemCount, which sums EVERY hue of that graphic.
                // With ore and the iron ingot shared by a dozen metals that
                // is a request to move N of a stack that is not the one being
                // counted: "banking 60 i_ingot_iron" pointing at valorite.
                i32 amount = 0;
                const u32 serial =
                    FindBackpackItemByName(client, made.c_str(), &amount);
                if (!serial || amount <= 0) continue;
                // HALF-MADE WORK IS NOT A DEPOSIT. The tailor's `produces`
                // opens with i_cloth_bolt, so this branch banked the very bolt
                // MAKE_CLOTH was about to cut. See WoolChainWorkInProgress.
                if (life::WoolChainWorkInProgress(*needCfg_.profession,
                                                  obs.pack,
                                                  needCfg_.craftBatch,
                                                  made.c_str())) {
                    LogLine("bank: keeping %d %s -- it is half-made work, not "
                            "stock; it gets finished, not stored", amount,
                            made.c_str());
                    continue;
                }
                // A DEPOSIT THAT NEVER LANDS MUST NOT BE RETRIED FOREVER.
                //
                // The bank serial outlives the visit: Bryn stood on the
                // Britain dock, seventy tiles from any banker, and pushed the
                // same fifteen fish at a box it could not reach once a second
                // for the rest of the session -- every attempt answered
                // "item landed in a different container", none of them
                // counted, and nothing else could run.
                if (bankDepositItem_ == made) {
                    if (++bankDepositTries_ > kMaxBankDepositTries) {
                        LogLine("bank: %d attempts to deposit %s all landed "
                                "elsewhere -- this box is not really open",
                                bankDepositTries_, made.c_str());
                        client.ForgetBankContainer();
                        bankDepositTries_ = 0;
                        bankDepositItem_.clear();
                        planner_.NoteAttempt(obs.nowMs);
                        nextActionMs_ = obs.nowMs + 3000;
                        return false;
                    }
                } else {
                    bankDepositItem_ = made;
                    bankDepositTries_ = 1;
                }
                LogLine("banking %d %s", amount, made.c_str());
                IssueBankItemMove(client, obs, serial,
                                  static_cast<u16>(amount), box);
                return false;
            }
        }
        // STOCK NOBODY WILL BUY YET, WHATEVER THE PACK WEIGHS.
        //
        // "until they have orders they keep other ingots in the bank"
        // (project owner, 2026-08-30). Every branch around this one is gated
        // on `loadDemandsIt` -- carried weight -- and seventeen iron ingots
        // are not heavy. So Corwyn carried them through three sessions while
        // EARN_GOLD logged, every single time:
        //
        //   BLOCKED_NEED EARN_GOLD: carrying its own output with nobody known
        //   to buy it (17 x i_ingot_iron spare, and no buyer known)
        //
        // Unsellable is not worthless. It is stock waiting for an order, and
        // stock waits in the box. NeedBank scores this same condition (see
        // Needs.cpp, "put unsold stock away") so the need and the action
        // agree -- without that pairing the goal completes having done
        // nothing and is picked again immediately. The two buyer checks below
        // are the whole test, and they are the same two the need uses.
        //
        // A WORKING BATCH STAYS. Ingots are both what a smith makes and what
        // it makes FROM; banking every one would leave it standing at a forge
        // with nothing to hammer.
        if (needCfg_.profession) {
            const i32 keepToWorkWith = needCfg_.craftBatch * 2;
            for (const std::string& made : needCfg_.profession->produces) {
                if (market::MaySellToNpc(*needCfg_.profession, made.c_str(),
                                         state_.ledger).allowed)
                    continue;   // it has an NPC route; selling beats storing
                if (state_.memory.BestSupplier(
                        (std::string("buyer:") + made).c_str()))
                    continue;   // a player buyer is known; that is a sale

                // Find and count the same NAME -- see the produces loop above.
                i32 amount = 0;
                const u32 serial =
                    FindBackpackItemByName(client, made.c_str(), &amount);
                if (!serial || amount <= keepToWorkWith) continue;

                const i32 put = amount - keepToWorkWith;
                LogLine("bank: storing %d %s until there is an order for it "
                        "(keeping %d to work with; no NPC buys it and the "
                        "player market was quiet)",
                        put, made.c_str(), keepToWorkWith);
                IssueBankItemMove(client, obs, serial, static_cast<u16>(put),
                                  box);
                return false;
            }
        }

        // AND THE INPUTS, when the load demands it. DoBank could only ever
        // deposit what a life PRODUCES, so a scribe carrying two hundred and
        // thirty blank scrolls at 97% of its carry limit reached the bank,
        // found nothing it was allowed to put down, completed with progress 0
        // and was re-picked -- five thousand one hundred and sixty-nine times
        // in twenty minutes. Stock is still weight.
        //
        // Keep a working batch and box the rest, so the next errand can
        // actually be walked to.
        if (loadDemandsIt && needCfg_.profession) {
            const i32 keep = needCfg_.craftBatch * 2;
            // WHAT THIS LIFE CONSUMES, from the RECIPES rather than from the
            // hand-written list. The scribe has no `consumes` at all -- its
            // inputs were only ever implied by what it makes -- so a list-only
            // version of this deposited nothing and the bank goal still span.
            // The recipe graph already knows, and it cannot fall out of step
            // with itself.
            std::vector<std::string> inputs = needCfg_.profession->consumes;
            for (const std::string& made : needCfg_.profession->produces) {
                const prod::Recipe* r = prod::FindRecipe(made.c_str());
                if (!r) continue;
                for (const prod::Ingredient& in : r->inputs) {
                    if (!in.item) continue;
                    bool seen = false;
                    for (const std::string& have : inputs) {
                        if (have == in.item) { seen = true; break; }
                    }
                    if (!seen) inputs.emplace_back(in.item);
                }
            }
            for (const std::string& input : inputs) {
                // Find and count the same NAME -- see the produces loop above.
                i32 amount = 0;
                const u32 serial =
                    FindBackpackItemByName(client, input.c_str(), &amount);
                if (!serial || amount <= keep) continue;
                // Wool and yarn reach this loop as declared inputs. Same
                // ruling as the produces branch: a step on the way to cloth is
                // finished, not stored.
                if (life::WoolChainWorkInProgress(*needCfg_.profession,
                                                  obs.pack,
                                                  needCfg_.craftBatch,
                                                  input.c_str()))
                    continue;
                const i32 put = amount - keep;
                LogLine("banking %d spare %s (keeping %d to work with)", put,
                        input.c_str(), keep);
                IssueBankItemMove(client, obs, serial, static_cast<u16>(put),
                                  box);
                return false;
            }
        }

        const u32 logs = loadDemandsIt || !needCfg_.profession
                             ? client.FindBackpackItemByGraphic(kLog)
                             : 0;
        if (logs) {
            const u16 amount = static_cast<u16>(client.BackpackItemCount(kLog));
            LogLine("banking %u logs", amount);
            IssueBankItemMove(client, obs, logs, amount, box);
            return false;
        }
        // AND THE DEAD WEIGHT -- what this life has no name for at all.
        //
        // The need and the action have to agree or the goal cannot terminate.
        // NeedBank is scored from CARRIED WEIGHT; every branch above deposits
        // only what the profession produces, consumes or makes from. When the
        // load is none of those, the need stays at 0.72 and the goal completes
        // having done nothing, forever.
        //
        // Ysolde is the case that proves it. A scribe with STR 10 has a carry
        // limit of 75 stones, and her STARTING KIT alone is 73 of them: two
        // chainmail coifs she cannot wear usefully, two books, a candle, three
        // cast scrolls (runtime/save/spherechars.scp, serial 04001425d). Not
        // one of those is a scroll she wrote or a reagent she buys, so she was
        // full of things she could neither use nor put down.
        //
        // A player empties that into the box. So: anything in the pack that is
        // not gold, not a tool this life declares, not a consumable it stocks,
        // not what it makes and not what it makes it FROM is dead weight, and
        // it goes in -- one per tick, each named in the log so the decision is
        // auditable. Bounded by loadDemandsIt, the same guard the inputs
        // branch uses: below the weight line a character keeps its oddments.
        if (loadDemandsIt && client.BackpackContentsKnown()) {
            // GOLD IS NO LONGER KEPT WHOLESALE. It used to be listed here
            // beside logs as something never deposited, which meant a
            // character banked its goods and walked away still carrying every
            // coin it owned -- into a shard with full loot on death. The
            // surplus goes in the box below; only what the life actually needs
            // stays in the pack.
            // TWO KEEP LISTS, because two kinds of thing are being named.
            //
            // Tools and consumables are declared as GRAPHICS by the
            // profession, and a graphic is all they ever are. Everything a
            // life makes, consumes or makes FROM is declared as a DEFNAME --
            // and turning those into graphics threw the answer away (S1):
            // GraphicsForItem("i_ore_iron") is 019b7..019ba, which is every
            // metal's ore, so a smith with i_ore_iron on its list quietly
            // exempted a pack full of valorite and could never put it down.
            // Named things are therefore matched by hue-resolved NAME.
            std::vector<u16> keepGfx{kLog};
            std::vector<std::string> keepNames;
            auto keepAll = [&keepGfx](const std::vector<u16>& g) {
                keepGfx.insert(keepGfx.end(), g.begin(), g.end());
            };
            auto keepNamed = [&keepNames](const std::string& item) {
                for (const std::string& have : keepNames)
                    if (have == item) return;
                keepNames.push_back(item);
            };
            if (needCfg_.profession) {
                for (const prof::ToolNeed& t : needCfg_.profession->tools)
                    keepAll(t.graphics);
                for (const prof::ConsumableNeed& c : needCfg_.profession->consumables)
                    keepAll(c.graphics);
                for (const std::string& s : needCfg_.profession->consumes)
                    keepNamed(s);
                for (const std::string& made : needCfg_.profession->produces) {
                    keepNamed(made);
                    const prod::Recipe* r = prod::FindRecipe(made.c_str());
                    if (!r) continue;
                    for (const prod::Ingredient& in : r->inputs)
                        if (in.item) keepNamed(in.item);
                }
            }
            const u32 pack = client.BackpackSerial();
            const usize n = client.ContainerItemCount(pack);
            for (usize i = 0; i < n; ++i) {
                u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
                if (!client.ContainerItemAt(pack, i, &serial, &gfx, &amount, &hue))
                    continue;
                if (!serial) continue;
                bool named = false;
                for (u16 k : keepGfx) { if (k == gfx) { named = true; break; } }
                // The hue-resolved name, so a coloured ore or ingot is judged
                // as ITSELF against the keep list rather than as iron.
                const char* itemName = econ::ItemNameForGraphicAndHue(gfx, hue);
                if (!named && itemName) {
                    for (const std::string& k : keepNames)
                        if (k == itemName) { named = true; break; }
                }
                if (named) continue;

                // GOLD IS DEPOSITED IN PART, NOT WHOLESALE.
                //
                // It used to be kept entirely, so a character banked its goods
                // and walked out carrying every coin it owned -- on a shard
                // with full loot. Dropping it from the keep list without this
                // would be the opposite mistake: banking the lot and leaving
                // the life unable to buy bread. What stays is the profession's
                // own reserve plus working change; the rest goes in.
                u16 moving = amount ? amount : 1;
                if (gfx == kGoldCoin) {
                    const i32 keep =
                        (needCfg_.profession ? needCfg_.profession->goldReserve
                                             : 0) + kGoldWorthCarryingRt;
                    const i32 spare = static_cast<i32>(moving) - keep;
                    if (spare <= 0) continue;      // all of it is needed
                    moving = static_cast<u16>(spare);
                    LogLine("banking %d gold, keeping %d for this life's own "
                            "errands -- coin in the pack is coin at risk",
                            spare, keep);
                    IssueBankItemMove(client, obs, serial, moving, box);
                    return false;
                }

                LogLine("banking dead weight: %s (0x%04X hue 0x%04X) x%u -- "
                        "this life has no use for it and the pack is at %.0f%%",
                        itemName ? itemName : "unnamed", gfx, hue, moving,
                        obs.WeightFraction() * 100.0);
                IssueBankItemMove(client, obs, serial, moving, box);
                return false;
            }
        }

        // (Recorded at box-open from the banker's own position, not here:
        // where the character stands after the last deposit is not the bank.)
        if (!state_.memory.HasEvent("first_bank_deposit") && planner_.Current().progress > 0) {
            state_.memory.NoteEvent("first_bank_deposit", "logs", "bank", obs.x,
                                    obs.y, obs.nowMs);
        }

        if (state_.huntReturnPending) {
            state_.huntReturnPending = false;
            state_.memory.NoteEvent("hunt_banked", "loot secured; ready to restock", "bank",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("hunt: bank return complete; resume preparation for next hunt");
            Checkpoint(client, obs.nowMs, "hunt bank return complete");
            return true;
        }

        // A VISIT THAT DEPOSITED NOTHING IS NOT A COMPLETED BANK GOAL.
        //
        // Reporting success here is what produced the churn: Finish(true)
        // clears `active`, the next Select() sees NeedBank still at 0.72 --
        // because nothing left the pack -- and starts BANK again 60 ms later.
        // pair2 did that for five straight minutes, fsyncing state.json on
        // every lap. Say what actually happened and stand down.
        if (planner_.Current().progress == 0) {
            LogLine("bank: the box is open and there is nothing in the pack "
                    "this life may put down (weight %d/%d) -- standing down "
                    "for %llds rather than re-deciding",
                    obs.weight, obs.maxWeight,
                    static_cast<long long>(kBankCooldownMs / 1000));
            planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
            planner_.Finish(false, "nothing to deposit", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        return true;
    }

    if (client.TravelBusy()) return false;

    // GET TO A BANK BEFORE ASKING FOR ONE.
    //
    // BankErrand only ever scans mobiles already in view (NearestMobileWithTrade)
    // -- it never travels anywhere. DoBank used to hand straight to it from
    // wherever the character happened to be standing, which right after
    // creation is nowhere near a counter: Draver and Lyra's very first BANK
    // goal failed "no banker in sight" in two seconds flat
    // (run_gates/g_Draver.console.txt, g_Lyra.console.txt, 00:32:06-00:32:11
    // and 00:32:19-00:33:51). Both only ever banked once an UNRELATED market
    // trip happened to walk them past a real banker five minutes later
    // (g_Draver: "market: taking 10 i_ingot_iron to britain_bank_2" at
    // 00:32:52, banker found at 00:37:35) -- proof the census itself is fine
    // once actually at a bank; the goal simply never travelled there on its
    // own. Every other service errand in this file travels first and asks
    // second (DoHeal, EARN_GOLD's buyer trip, ...); this one now does too.
    if (!bankErrand_.Running() && !NearAnyBank(client, obs)) {
        if (!travelInFlight_) {
            const KnownPlace* proven =
                state_.memory.NearestPlace("bank", obs.x, obs.y);
            // A bank learned during an old cross-city errand must not outrank
            // the home/current-city bank that the atlas already knows.  The
            // previous `proven ? ... : seeded` rule sent Minoc miners back to
            // Britain simply because Britain was the first bank they had
            // opened. Choose the genuinely nearer known counter, regardless
            // of whether it was learned by memory or seeded as city knowledge.
            const KnownPlace* seeded = state_.memory.NearestPlace(
                "common_knowledge_bank", obs.x, obs.y);
            const i32 provenDist = proven
                                       ? TileDist(proven->x, proven->y, obs.x, obs.y)
                                       : 0x7FFFFFFF;
            const i32 seededDist = seeded
                                       ? TileDist(seeded->x, seeded->y, obs.x, obs.y)
                                       : 0x7FFFFFFF;
            // Every player knows every town has a bank. The seeded counter is
            // the HOME one, and a novice fighter's hunting ground is chosen by
            // tier (Britain graveyard), not by home: Castor, homed in Trinsic,
            // killed one skeleton at Britain graveyard and then moongated
            // 1331 tiles to "Bank of Britannia - Trinsic Branch" with Britain's
            // own bank ~190 tiles away (g_Castor 2026-09-05 01:39). When the
            // atlas knows a materially nearer counter than anything remembered,
            // that counter is the one a player would walk to.
            const wm::Place* atlasBank = client.NearestServicePlace(wm::Service::Banker);
            const i32 atlasDist = atlasBank
                                      ? TileDist(atlasBank->position.x,
                                                 atlasBank->position.y, obs.x, obs.y)
                                      : 0x7FFFFFFF;
            const i32 bestKnown = provenDist < seededDist ? provenDist : seededDist;
            if (atlasBank && atlasDist * 2 < bestKnown) {
                LogLine("bank: %s is %d tiles off but %s at %d,%d is only %d -- "
                        "every town has a bank, going to the near one",
                        proven && provenDist <= seededDist ? "the bank we know"
                                                           : "the home bank",
                        bestKnown, atlasBank->name.c_str(), atlasBank->position.x,
                        atlasBank->position.y, atlasDist);
                travelInFlight_ = client.TravelToPoint(
                    atlasBank->position.x, atlasBank->position.y, 5, "nearest_bank");
            } else if (proven && provenDist <= seededDist) {
                LogLine("bank: walking back to a bank we have used before, "
                        "%d,%d (%d tiles; seeded alternative %d)",
                        proven->x, proven->y, provenDist, seededDist);
                travelInFlight_ =
                    client.TravelToPoint(proven->x, proven->y, 3, "known_bank");
            } else if (seeded) {
                LogLine("bank: nothing proven yet -- trying what common "
                        "knowledge says is nearest, %s at %d,%d (%d tiles)",
                        seeded->name.c_str(), seeded->x, seeded->y, seededDist);
                travelInFlight_ = client.TravelToPoint(seeded->x, seeded->y, 5,
                                                       "seeded_bank");
            } else {
                LogLine("bank: nothing known or seeded -- asking the world "
                        "for a bank");
                travelInFlight_ = client.TravelToService(
                    wm::Service::Banker, HomeOrNearest(state_.homeCity));
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=BANK reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
            }
            return false;
        }
        travelInFlight_ = false;
        if (!NearAnyBank(client, obs)) {
            LogLine("bank: trip reported %s but no bank is within reach of "
                    "%d,%d",
                    client.TravelSucceeded() ? "success" : "failure",
                    client.PlayerX(), client.PlayerY());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 3000;
        }
        return false;
    }

    // WAIT FOR THE ASK BEFORE ASKING AGAIN.
    //
    // open_bank's deadline is 6 s (kBankTimeoutMs) and this used to re-issue
    // every 2.5 s, so every attempt was killed by the NEXT one before it could
    // either succeed or time out. The whole of run_m5/pair3 is sixty-three
    // repetitions of
    //
    //   [action] open_bank superseded by open_bank
    //   [ACTION_RESULT] open_bank invalid_state (2520ms) superseded
    //
    // and not one resolved result in twenty minutes. An action already in
    // flight is not a reason to start another one.
    if (client.ActionBusy()) return false;

    // --- THE ERRAND OWNS GETTING THE BOX OPEN ----------------------------
    //
    // Six Runner members existed to open one container: bankerAsked_,
    // bankerCounted_, bankOpenTries_, bankerSilent_, bankShouts_ and
    // bankTitlesAskedMs_. All six are the same counters-on-the-runner pattern
    // that let a gear trip spend the spellbook's trip allowance, and all six
    // are now inside BankErrand where they belong to this errand alone.
    //
    // The two bank-specific truths survive intact, because both were learned
    // live and neither is obvious:
    //   * success is the box SERIAL, not its contents -- an empty box sends
    //     no 0x3C, so waiting for contents re-opened the bank forever;
    //   * the keyword works without a named banker, so a character standing
    //     in a bank may say "bank" aloud and be served.
    //
    // GEOMETRY, NOT EYE CONTACT. We only ever reach this line because the
    // block above already confirmed NearAnyBank -- so tell the errand it is
    // at a known location and let it speak straight away rather than hunt
    // for a banker mobile (owner ruling, 2026-08-31: bankers hear through
    // walls; see bank_errand.h).
    if (!bankErrand_.Running()) bankErrand_.Begin();
    bankErrand_.SetAtKnownBank(true);
    const life::BankErrandResult br = bankErrand_.Tick(client, obs);
    LogErrandReason("bank", br.why.c_str(), obs.nowMs);
    if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
        nextActionMs_ = obs.nowMs + br.delayMs;

    if (br.status == life::ActivityStatus::Success) {
        // Remember where the counter is, now that one has actually served
        // us. A keyword ask never named a banker (br.banker == 0 -- we did
        // not need to see one), so the character's own position is the best
        // evidence of where the bank is.
        i32 bx = obs.x, by = obs.y; i8 bz = obs.z;
        if (br.banker) client.MobilePosition(br.banker, &bx, &by, &bz);
        state_.memory.NotePlace("bank", "bank", bx, by, bz, obs.nowMs);
        // The box is open; the next tick takes the deposit branch above.
        planner_.NoteProgress();
        return false;
    }

    if (!life::IsTerminal(br.status)) {
        // ASKING IS NOT PROGRESS. NoteProgress() here reset the attempt
        // counter on every retry, so Exhausted() never fired and the planner
        // believed a goal that had done nothing for twenty minutes was
        // working. An ask is an attempt; the box opening is the progress.
        //
        // ...and a WAIT is not an ask either. Counting the "an ask is in
        // flight" polls made the tick rate, not the banker, decide when the
        // goal ran out of tries -- the same defect the potion errand paid for
        // in run_r4/w_Bruin.console.txt:317-323.
        if (br.acted) planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Nobody here will open a box. Walking to another bank is the honest next
    // move, but not on this goal and not this second.
    LogLine("goal_failed=BANK status=%s reason=\"%s\"",
            life::ActivityStatusName(br.status), br.why.c_str());
    state_.memory.NoteEvent("bank_no_answer", br.why.c_str(), "",
                            obs.x, obs.y, obs.nowMs);
    planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
    planner_.Finish(false, "no banker answered", obs.nowMs);
    nextActionMs_ = obs.nowMs + 5000;
    return false;
}

}  // namespace uo::life
