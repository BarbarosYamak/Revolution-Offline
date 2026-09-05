#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// --- WHICH COUNTERS ARE ALREADY EMPTY ---------------------------------------
//
// Sphere's shelf is a real container: buying it out leaves it empty until the
// ten-minute restock timer fires (Source-X CCharNPCAct_Vendor.cpp:32-92).
// Reopening it in the meantime shows the drained count, so the only way to a
// hundred bandages through NPCs is the NEXT counter.
void Runner::NoteDrainedShelf(u32 serial, i64 nowMs) {
    if (!serial) return;
    for (DrainedShelf& d : drainedShelves_) {
        if (d.serial != serial) continue;
        d.whenMs = nowMs;
        return;
    }
    DrainedShelf d;
    d.serial = serial;
    d.whenMs = nowMs;
    drainedShelves_.push_back(d);
}

std::vector<u32> Runner::DrainedShelves(i64 nowMs) const {
    std::vector<u32> out;
    for (const DrainedShelf& d : drainedShelves_) {
        if (nowMs - d.whenMs >= kShelfRestockMs) continue;   // restocked by now
        out.push_back(d.serial);
    }
    return out;
}


// --- tools and equipment ---------------------------------------------------

// Which trade sells a given tool, and where the world model files them.
// Read off this shard's own vendor templates rather than guessed, the same
// discipline the trainer and buyer tables follow.
struct ToolVendor {
    const char* tool;      // the profession's own name for it
    const char* trade;     // paperdoll-title substring
    wm::Service service;
};
const ToolVendor kToolVendors[] = {
    // NEITHER OF THESE WAS SOLD WHERE IT SAID. VENDOR_S_BLACKSMITH's entire
    // stock is i_tongs and i_store_ingot (tm_vend.scp) -- no hatchet, no
    // pickaxe. So a lumberjack or a miner who lost a tool walked to a smithy,
    // opened a shop that could not contain the thing, and left; Edrik logged
    // NeedTool(hatchet 0.90) unsatisfied all session. The same "goal addressed
    // to nobody" shape as the missing tongs row below.
    //
    // Who actually sells them, from tm_vend.scp:
    //   i_hatchet   VENDOR_S_WEAPONS_BLADED only
    //   i_pickaxe   VENDOR_S_WEAPONS_BLADED, VENDOR_S_TINKER
    //
    // The pickaxe goes to the tinker: 9 tinker shops are in the atlas, and a
    // miner already visits one for a shovel and tinker tools.
    //
    // The hatchet has one seller and no Service of its own -- the atlas files
    // "Papua weaponsmith" under `blacksmith`, so Blacksmith is the right place
    // to WALK to, while the trade string must say "weaponsmith" because it is
    // matched as a paperdoll-title substring and "weaponsmith" does not
    // contain "blacksmith". 40 c_weaponsmith_* stand in sphereworld.scp.
    {"hatchet",      "weaponsmith", wm::Service::Blacksmith},
    {"pickaxe",      "tinker",      wm::Service::Tinker},
    {"fishing pole", "fisherman",   wm::Service::Fisherman},
    {"mortar",       "alchemist",   wm::Service::Alchemist},
    {"mapmaker's pen", "mapmaker",  wm::Service::Mapmaker},
    {"dagger",       "weaponsmith", wm::Service::Blacksmith},
    {"spellbook",    "mage",        wm::Service::Mage},
    // FOUR TOOLS THE CATALOGUE ASKS FOR AND NOBODY SOLD.
    //
    // Exactly the shape of the missing kTrainers rows: a profession names a
    // tool, VendorForTool returns null, and the goal fails
    // REFUSE_NO_KNOWN_BUYER without ever walking to a shop. Bruin, a full
    // crafter, logged "no known supplier of a tongs" 107 times in one session
    // -- BLOCKED_NEED GET_TOOL and BLOCKED_NEED CRAFT together, so he could
    // neither equip himself nor make anything, and spent 85% of his picks
    // idling with 0 gold.
    //
    // Trades read from this shard's own vendor templates rather than guessed:
    //   i_tongs        VENDOR_S_BLACKSMITH, VENDOR_S_TINKER
    //   i_shovel       VENDOR_S_TINKER
    //   i_sewing_kit   VENDOR_S_TAILOR, VENDOR_S_TINKER
    //   i_tinker_tools VENDOR_S_TINKER
    // Where two sell it, the one whose trade the tool belongs to is named --
    // a smith's tongs from a smith -- since that is also who stands in the
    // shop the rest of that craft's errands already visit.
    {"tongs",        "blacksmith",  wm::Service::Blacksmith},
    // The wieldable half of the smith kit, and it is sold WHERE THE SMITH
    // ALREADY IS. Reading only tm_vend.scp's SELL rows said otherwise --
    // VENDOR_S_BLACKSMITH lists just i_tongs and i_store_ingot -- but a
    // vendor's stock is not only that list: c_blacksmith and c_blacksmith_f
    // in c_vendor_human.scp both carry
    //     ITEM={ i_hammer_sledge 1 i_hammer_smith 1 }
    // in their own CHARDEF. "blacksmith has it as well, it doesnt need to go
    // far away" (project owner, 2026-08-29) -- and a smelting or smithing
    // errand is standing in a smithy already, so this costs no walk at all.
    // (c_armorer and c_weaponsmith_blade carry it too, but Blacksmith travel
    // deliberately steps past armouries.)
    {"smith hammer", "blacksmith",  wm::Service::Blacksmith},
    {"shovel",       "tinker",      wm::Service::Tinker},
    {"sewing kit",   "tailor",      wm::Service::Tailor},
    {"tinker tools", "tinker",      wm::Service::Tinker},
    // And two more the cross-check caught: every name in any profession's
    // p.tools must appear here, and "saw" and "scissors" did not.
    //   i_saw       VENDOR_S_CARPENTER, VENDOR_S_TINKER
    //   i_scissors  VENDOR_S_TAILOR, VENDOR_S_TINKER, VENDOR_S_WEAVER
    {"saw",          "carpenter",   wm::Service::Carpenter},
    {"scissors",     "tailor",      wm::Service::Tailor},
    // The fisher's cooking tool. Sold by the BAKER: the stock-Sphere
    // Scripts-X tm_vend.scp carries SELL=i_rolling_pin,{1 6} in
    // VENDOR_S_BAKER_TEMPLATE, a row the TNS shop-list swap dropped and the
    // runtime file restores. A c_baker stands at Britain's bakery
    // (sphereworld.scp P=1448,1618,20), an easy walk from the dock.
    {"rolling pin",  "baker",       wm::Service::Baker},
};

const ToolVendor* VendorForTool(const std::string& tool) {
    for (const ToolVendor& t : kToolVendors) {
        if (tool == t.tool) return &t;
    }
    return nullptr;
}

bool Runner::DoGetTool(Client& client, const Observation& obs) {
    // WHICH TOOL IS MISSING, from the catalogue rather than from a hardcoded
    // hatchet. This body was written for the lumberjack and never generalised
    // -- the fourth place in this file where that was true, after the needs,
    // the sell path and the bank. A fisher standing next to a lake was being
    // sent to a blacksmith to buy an axe it had no use for.
    std::string toolName;
    std::vector<u16> toolGfx;
    if (needCfg_.profession) {
        for (const prof::ToolNeed& t : needCfg_.profession->tools) {
            // HELD/WORN, ACROSS EVERY GRAPHIC THE TOOL WEARS, and counting
            // the HANDS as well as the pack -- the same question obs.toolsHeld
            // asks a few hundred lines above, answered the same way. Three
            // regressions lived in the narrower version this replaces:
            //
            //  * the layer came from t.graphics[0] ALONE, so a tool whose
            //    first listed graphic has no itemdef layer reported layer 0
            //    for the whole entry;
            //  * `held` counted the PACK only, so a tool already in the hand
            //    read held=0 -- and a flip-graphic tool (kPickaxe and kHatchet
            //    are two graphics each) also read worn != graphics[0], because
            //    the wielded half is the OTHER graphic. Together that is
            //    DecideAcquire's "none held, and it may be bought": the
            //    character walks to a smith and buys a second pickaxe while
            //    swinging the first;
            //  * mustWear was `layer != 0` -- i.e. "anything equippable must
            //    be equipped" -- which ignored ToolNeed::mustBeWielded, the
            //    field the catalogue sets for exactly this and which no code
            //    in src/ read at all. A saw or a sewing kit works from the
            //    pack; only the SRC.WEAPON skills need the hand.
            // (audit 2026-08-30, finding 4.)
            u8 layer = 0;
            for (u16 g : t.graphics) {
                layer = client.ItemEquipLayer(g);
                if (layer) break;
            }
            const u16 canonical = t.graphics.empty() ? 0 : t.graphics[0];
            i32  held = 0;
            u32  have = 0;
            bool wielded = false;
            for (u16 g : t.graphics) {
                if (!g) continue;
                held += static_cast<i32>(client.BackpackItemCount(g));
                if (!have) have = client.FindBackpackItemByGraphic(g);
                if (client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g ||
                    (layer && client.EquippedGraphicAt(layer) == g))
                    wielded = true;
            }
            // A TOOL IN THE HAND IS A TOOL HELD. Reported as the canonical
            // graphic so DecideAcquire's `worn == req.graphic` test sees the
            // tool it asked about rather than whichever flip-frame the server
            // put on the paperdoll -- the same shape the garment scan in
            // DoReplaceEquipment uses.
            if (wielded) ++held;
            const u16 worn = wielded ? canonical : 0;

            life::AcquireRequest req;
            req.graphic = canonical;
            req.item = t.name.c_str();
            req.desiredTotal = 1;
            req.layer = layer;
            req.mustWear = t.mustBeWielded;
            // A profession that names a tool can use it -- unlike armour,
            // nothing here gates which tools this life may wield.
            req.wearable = true;

            const life::AcquirePlan plan = life::DecideAcquire(req, held, worn);
            // Keyed by THIS tool's own name -- see Runner.h's comment on
            // lastToolAcquirePlanByItem_ for the alternating-log bug a single
            // shared sentinel produced here.
            AcquireStep& lastStep = lastToolAcquirePlanByItem_[t.name];
            if (plan.step != lastStep) {
                LogPlan(life::AcquireStepName(plan.step), plan.reason);
                lastStep = plan.step;
            }
            // THE PAPERDOLL IS THE VERIFICATION, AND IT ARRIVES NEXT TICK.
            // An equip is an ask with a server answer; "I sent the packet" is
            // not "it is in my hand" (acquire.h, rule 2). The old code called
            // NoteProgress() the instant it sent ActionEquip, so a shard that
            // silently refused the wield reset the failure ladder every 1.2
            // seconds and the planner's backstop -- the thing that ends a goal
            // doing nothing -- could never fire.
            int& wearTries = toolWearAttemptsByItem_[t.name];
            if (plan.step == life::AcquireStep::Done) {
                // Done AFTER we asked means EquippedGraphicAt now sees it.
                // THAT is the progress, and it is the only place that says so.
                if (wearTries > 0) {
                    LogLine("tool: the %s is in hand now", t.name.c_str());
                    wearTries = 0;
                    planner_.NoteProgress();
                }
                continue;
            }

            if (plan.step == life::AcquireStep::Wear) {
                if (client.ActionBusy()) return false;
                if (wearTries >= kMaxToolWearTries) {
                    LogLine("goal_blocked=GET_TOOL reason=\"the %s is in the "
                            "pack but %d equip attempts left the hand empty\"",
                            t.name.c_str(), wearTries);
                    state_.memory.NoteEvent("wield_refused", t.name.c_str(), "",
                                            obs.x, obs.y, obs.nowMs);
                    wearTries = 0;
                    return HandOff(GoalKind::GetTool, GoalKind::Bank,
                                   kGearCooldownMs,
                                   "the shard refuses to wield it", obs.nowMs);
                }
                ++wearTries;
                LogLine("tool: putting the %s in hand (attempt %d of %d)",
                        t.name.c_str(), wearTries, kMaxToolWearTries);
                client.ActionEquip(have, layer);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 1200;
                return false;
            }
            if (plan.step == life::AcquireStep::Refuse) {
                // Unreachable while every profession tool is `wearable=true`
                // above -- kept so the switch stays exhaustive if that
                // changes (an STR-gated tool, say).
                LogLine("goal_blocked=GET_TOOL reason=\"%s\"", plan.reason);
                state_.memory.NoteEvent("policy_refused", t.name.c_str(), "",
                                        obs.x, obs.y, obs.nowMs);
                return HandOff(GoalKind::GetTool, GoalKind::Bank,
                               kGearCooldownMs, plan.reason, obs.nowMs);
            }
            // Buy: genuinely missing. Fall through to the shop errand below.
            toolName = t.name;
            toolGfx = t.graphics;
            break;
        }
        if (toolName.empty()) return true;   // every tool this life needs
    } else {
        // A life predating the catalogue keeps the original behaviour exactly.
        if (obs.axeInPack || obs.axeEquipped) return true;
        toolName = "hatchet";
        toolGfx.assign(kHatchet, kHatchet + 2);
        toolGfx.push_back(kAxe[0]);
        toolGfx.push_back(kAxe[1]);
    }

    const ToolVendor* tv = VendorForTool(toolName);
    if (!tv) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownBuyer),
                toolName.c_str());
        // STAND DOWN. No cooldown here meant the goal failed and was
        // re-picked on the very next tick: Bruin logged 2,058 GET_TOOL goals
        // in ten minutes at sixty-millisecond intervals (run_m7/f6_Bruin).
        // GET_TOOL sits in the Emergency family, which is exempt from
        // satiation by design -- nobody should get bored of needing an axe --
        // so a cooldown is the ONLY brake it has, and it had none.
        return HandOff(GoalKind::GetTool, GoalKind::IdleBriefly,
                       kNoToolCooldownMs, "no trade known to sell it",
                       obs.nowMs);
    }

    const KnownSupplier* known = state_.memory.BestSupplier(toolName.c_str());

    // A tool purchase is legal under the vendor policy -- a tool is not a
    // resource, and buying one shortcuts no production chain. Verify that
    // here rather than assuming it, because the policy is the thing that
    // keeps the shard's player economy alive.
    const econ::VendorRuling ruling =
        econ::CanBuyFromNPCGraphic(toolGfx.empty() ? 0 : toolGfx[0]);
    if (!ruling.allowed) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s class=%s",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                toolName.c_str(), econ::VendorClassName(ruling.klass));
        state_.memory.NoteEvent("policy_refused", toolName.c_str(),
                                econ::VendorClassName(ruling.klass),
                                obs.x, obs.y, obs.nowMs);
        // Same stand-down. A policy refusal is a settled answer, not a
        // temporary one -- re-asking it sixty times a second changes nothing.
        return HandOff(GoalKind::GetTool, GoalKind::IdleBriefly,
                       kNoToolCooldownMs, "the vendor policy refuses this tool",
                       obs.nowMs);
    }

    // COIN BEFORE THE SHOP TRIP, not after arriving at it. Fetching it later
    // put two destinations in play at once: the coin errand started walking to
    // the bank, the tool goal re-issued its walk to the smithy on the next
    // tick, and the character announced "looking for a blacksmith" every two
    // and a half seconds without ever arriving anywhere.
    if (FetchCoinForPurchase(client, obs, kToolMoneyToCarry)) return false;

    if (client.TravelBusy()) {
        VetoTripOverSessionBudget(client, obs, GoalKind::GetTool, "GET_TOOL",
                                  kNoToolCooldownMs);
        return false;
    }

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        if (!travelInFlight_) {
            if (known) {
                LogLine("get_tool: returning to a remembered supplier '%s' at %d,%d",
                        known->name.c_str(), known->x, known->y);
                travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "supplier");
            } else {
                toolTitlesAskedMs_ = 0;
                LogLine("get_tool: no remembered supplier; looking for a %s to "
                        "sell a %s", tv->trade, toolName.c_str());
                travelInFlight_ =
                    client.TravelToService(tv->service, HomeOrNearest(state_.homeCity));
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GET_TOOL reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 15000;
            }
            return false;
        }
        // LEARN WHO IS STANDING HERE BEFORE DECIDING NOBODY IS.
        //
        // NOTE THE ORDER. travelInFlight_ is cleared AFTER this, not before:
        // clearing it first and then returning early to wait for titles threw
        // away the fact that the character had arrived, so the next tick saw
        // "not travelling" and set off again -- a walk of nought tiles,
        // restarted every two and a half seconds, with two blacksmiths named
        // Olin and Curtis standing in the room.
        //
        // NearestShopkeeperWithTrade matches on the PAPERDOLL TITLE and skips
        // every mobile whose title has not been fetched yet -- and titles only
        // arrive after ActionScanMobiles double-clicks them. This path never
        // called it, so the character arrived, saw a cache full of untitled
        // mobiles, and concluded the trade was absent. At The Forgery that
        // meant "arrived but no blacksmith is here" three times over with
        // c_blacksmith standing at 2474,565 and 2467,567 -- three and four
        // tiles away.
        if (!toolTitlesAskedMs_ || obs.nowMs - toolTitlesAskedMs_ > 20000) {
            client.ActionScanMobiles();
            toolTitlesAskedMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2500;   // let the replies land
            return false;
        }
        travelInFlight_ = false;
        // Arrived (or gave up). Ask whoever is here to show their wares.
        const u32 keeper = client.NearestShopkeeperWithTrade(tv->trade, tv->service);
        if (!keeper) {
            // BOUND THE TRIPS. This was the last travelling goal without a
            // limit, and it cost a whole session: Edrik logged "arrived but no
            // blacksmith is here" TWO HUNDRED AND FIFTY times, 51 GET_TOOL
            // picks at 100% of a full crafter's day, and never went anywhere
            // else. Documented in M4_OPEN_LOOSE_ENDS as waste-not-a-hang, on
            // the grounds the 300s limit caps it -- which was true and still
            // meant one goal owning the character.
            //
            // Three arrivals with nobody there is enough to conclude this
            // trade is not where the atlas says, cool off and let another
            // family have the day.
            if (++toolTrips_ > kMaxToolTrips) {
                LogLine("goal_failed=GET_TOOL reason=\"%d arrivals and no '%s' "
                        "was there to sell a %s\"", toolTrips_ - 1, tv->trade,
                        toolName.c_str());
                planner_.Cooldown(GoalKind::GetTool,
                                  obs.nowMs + kNoToolCooldownMs);
                planner_.Finish(false, "no shopkeeper of that trade", obs.nowMs);
                toolTrips_ = 0;
                return false;
            }
            LogLine("get_tool: arrived but no %s is here (trip %d of %d)",
                    tv->trade, toolTrips_, kMaxToolTrips);
            state_.memory.NoteEvent("vendor_not_observed", toolName.c_str(),
                                    tv->trade, obs.x, obs.y, obs.nowMs);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        toolTrips_ = 0;
        // WALK UP FIRST. Sphere routes a vendor keyword to whoever is nearest
        // in earshot, not to the name spoken -- the lesson a carpenter taught
        // when Joshua the architect answered instead.
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

    // A shop window is open. A supplier exists only once we have READ its
    // stock and seen the item -- never because a place was tagged with a
    // profession (supplier.h, and three journeys that ended at a guildmaster).
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : toolGfx) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        KnownSupplier s;
        s.need = toolName;
        s.name = v.name;
        s.sourceType = "npc_vendor";
        s.serial = vendor;
        s.x = obs.x; s.y = obs.y; s.z = obs.z;
        s.observedQuantity = v.amount;
        s.observedPricePerUnit = static_cast<i32>(v.price);
        s.lastVerifiedMs = obs.nowMs;
        s.policyAllows = true;
        state_.memory.NoteSupplier(s);
        // What a thing COSTS is a price observation like any other, and the
        // character has just read it off an open window with its own eyes.
        market::PriceObservation po;
        po.item = toolName;
        po.pricePerUnit = static_cast<i32>(v.price);
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);
        if (!state_.memory.HasEvent("supplier_learned")) {
            state_.memory.NoteEvent("supplier_learned", v.name.c_str(), "", obs.x,
                                    obs.y, obs.nowMs);
        }
        LogLine("memory_learned=SUPPLIER need=%s name=\"%s\" price=%u qty=%u",
                toolName.c_str(), v.name.c_str(), v.price, v.amount);

        if (obs.gold < static_cast<i32>(v.price)) {
            LogLine("goal_blocked=GET_TOOL reason=\"%s\" %s costs %u, carrying %d",
                    faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                    toolName.c_str(), v.price, obs.gold);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
            return false;
        }
        toolGoldBefore_ = obs.gold;
        client.ActionVendorBuy(vendor, v.serial, 1);
        state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase,
                           static_cast<i32>(v.price), toolName.c_str(),
                           obs.nowMs);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    LogLine("goal_blocked=GET_TOOL reason=\"%s\" this %zu-item list has no %s",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            client.VendorOffer().size(), toolName.c_str());
    planner_.NoteAttempt(obs.nowMs);
    nextActionMs_ = obs.nowMs + 8000;
    return false;
}

// A HORSE FIRST. "create a new tailor, make it buy a horse first, mount, then
// do the rest" (project owner, 2026-09-02).
//
// Shop shape copied from DoGetTool, trimmed: the trade is the animal trainer
// (paperdoll title "animal trainer", atlas Service::Stablemaster; Britain's
// stands at 1391,1655 and stocks VENDOR_S_TRAINER -- tm_vend.scp:1638-1641,
// four riding horses at VALUE={450 500}). Sphere hands an NPC-bought figurine
// straight to Use_Figurine (CClientEvent.cpp:1309), so the horse appears at
// the buyer's feet inside the purchase and never enters the pack. Mounting is
// a double-click on the animal (M3.7.1, CClient::Event_DoubleClick ->
// Horse_Mount); the paperdoll answers with obs.mounted, and that -- not the
// click -- is the progress.
bool Runner::DoBuyMount(Client& client, const Observation& obs) {
    // Riding-horse figurines the trainer sells: i_pet_horse_tan 0x259E,
    // i_pet_horse_gray 0x2599, i_pet_horse_brown_lt 0x2120,
    // i_pet_horse_brown_dk 0x2121 (i_char_icons.scp). Pack animals excluded.
    static const u16 kHorseFigurines[] = {0x259E, 0x2599, 0x2120, 0x2121, 0x211F};
    // c_horse_tan 0xC8, c_horse_brown_dk 0xCC, c_horse_gray 0xE2,
    // c_horse_brown_lt 0xE4 (c_monster_classic.scp).
    static const u16 kHorseBodies[] = {0x00C8, 0x00CC, 0x00E2, 0x00E4};
    constexpr i32 kMaxMountTrips  = 2;   // owner rule: unreachable = 1 try, max 2
    constexpr i32 kMaxMountClicks = 3;
    constexpr i64 kMountCooldownMs = 10 * 60 * 1000;

    if (obs.dead) return false;
    if (obs.mounted) {
        if (mountBoughtMs_) {
            LogLine("mount: in the saddle -- paperdoll shows mounted");
            state_.memory.NoteEvent("mounted", "riding horse", "", obs.x, obs.y,
                                    obs.nowMs);
        }
        mountBoughtMs_ = 0; mountClicks_ = 0; mountTrips_ = 0;
        planner_.NoteProgress();
        return true;
    }
    if (client.ActionBusy()) return false;

    // BOUGHT, STANDING BESIDE IT: climb on. Bounded, because a refused
    // double-click (Sphere says "you dont own that" for someone else's
    // horse) reports nothing this client can read.
    if (mountBoughtMs_) {
        const u32 horse = [&]() -> u32 {
            for (u16 b : kHorseBodies) {
                const u32 h = client.NearestMobileWithBody(b, 3);
                if (h) return h;
            }
            return 0u;
        }();
        if (!horse) {
            if (obs.nowMs - mountBoughtMs_ < 4000) {
                nextActionMs_ = obs.nowMs + 1000;   // let the pet packet land
                return false;
            }
            LogLine("goal_failed=BUY_MOUNT reason=\"paid for a horse and none "
                    "appeared within 3 tiles in 4s\"");
            mountBoughtMs_ = 0; mountClicks_ = 0;
            planner_.Cooldown(GoalKind::BuyMount, obs.nowMs + kMountCooldownMs);
            planner_.Finish(false, "no horse appeared after the purchase", obs.nowMs);
            return false;
        }
        if (mountClicks_ >= kMaxMountClicks) {
            LogLine("goal_failed=BUY_MOUNT reason=\"%d double-clicks on the "
                    "horse and the paperdoll still shows on foot\"", mountClicks_);
            mountBoughtMs_ = 0; mountClicks_ = 0;
            planner_.Cooldown(GoalKind::BuyMount, obs.nowMs + kMountCooldownMs);
            planner_.Finish(false, "the shard would not seat us", obs.nowMs);
            return false;
        }
        ++mountClicks_;
        LogLine("mount: double-clicking the horse 0x%08X to mount (attempt %d of %d)",
                horse, mountClicks_, kMaxMountClicks);
        client.ActionUseObject(horse);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (client.TravelBusy()) {
        VetoTripOverSessionBudget(client, obs, GoalKind::BuyMount, "BUY_MOUNT",
                                  kMountCooldownMs);
        return false;
    }

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        if (!travelInFlight_) {
            mountTitlesAskedMs_ = 0;
            LogLine("mount: looking for an animal trainer to sell a horse");
            travelInFlight_ = client.TravelToService(wm::Service::Stablemaster,
                                                     HomeOrNearest(state_.homeCity));
            if (!travelInFlight_) {
                LogLine("goal_blocked=BUY_MOUNT reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.Cooldown(GoalKind::BuyMount, obs.nowMs + kMountCooldownMs);
                planner_.Finish(false, "no way to an animal trainer", obs.nowMs);
            }
            return false;
        }
        // Titles first, then judge who is here (DoGetTool's lesson).
        if (!mountTitlesAskedMs_ || obs.nowMs - mountTitlesAskedMs_ > 20000) {
            client.ActionScanMobiles();
            mountTitlesAskedMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        travelInFlight_ = false;
        const u32 keeper =
            client.NearestShopkeeperWithTrade("animal", wm::Service::Stablemaster);
        if (!keeper) {
            if (++mountTrips_ >= kMaxMountTrips) {
                LogLine("goal_failed=BUY_MOUNT reason=\"%d arrivals and no animal "
                        "trainer was there\"", mountTrips_);
                mountTrips_ = 0;
                planner_.Cooldown(GoalKind::BuyMount, obs.nowMs + kMountCooldownMs);
                planner_.Finish(false, "no animal trainer at the stable", obs.nowMs);
                return false;
            }
            LogLine("mount: arrived but no animal trainer is here (trip %d of %d)",
                    mountTrips_, kMaxMountTrips);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        mountTrips_ = 0;
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

    // The window is open: a horse is for sale only if the offer shows one.
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : kHorseFigurines) { if (v.graphic == g) { match = true; break; } }
        if (!match || v.amount == 0) continue;

        market::PriceObservation po;
        po.item = "riding horse";
        po.pricePerUnit = static_cast<i32>(v.price);
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);
        LogLine("memory_learned=SUPPLIER need=riding_horse name=\"%s\" price=%u qty=%u",
                v.name.c_str(), v.price, v.amount);

        const i32 reserve = needCfg_.profession ? needCfg_.profession->goldReserve : 0;
        if (obs.gold < static_cast<i32>(v.price) + reserve) {
            LogLine("goal_blocked=BUY_MOUNT reason=\"%s\" horse costs %u, reserve %d, "
                    "holding %d",
                    faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                    v.price, reserve, obs.gold);
            planner_.Cooldown(GoalKind::BuyMount, obs.nowMs + kMountCooldownMs);
            planner_.Finish(false, "cannot afford the horse", obs.nowMs);
            return false;
        }
        LogLine("mount: buying a %s for %u gold", v.name.c_str(), v.price);
        client.ActionVendorBuy(vendor, v.serial, 1);
        state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase,
                           static_cast<i32>(v.price), "riding horse", obs.nowMs);
        mountBoughtMs_ = obs.nowMs;
        mountClicks_ = 0;
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    LogLine("goal_failed=BUY_MOUNT reason=\"%s\" this %zu-item list has no riding "
            "horse in stock",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            client.VendorOffer().size());
    planner_.Cooldown(GoalKind::BuyMount, obs.nowMs + kMountCooldownMs);
    planner_.Finish(false, "the trainer has no horse today", obs.nowMs);
    return false;
}

// SIXTEEN FREE BANDAGES, OFF YOUR OWN BACK.
//
// "unequip the resurrection robe and cut into bandages with scissors" and
// "not any robe only resurrection robe" (project owner, 2026-08-30).
//
// The shard side already works: types/type_scissors.scp cuts t_clothing, and
// a robe's 16.1 weight yields sixteen bandages. It also refuses to cut WORN
// clothing -- the engine calls CanUse(pItemTarg, true) -- which is exactly why
// the robe has to come off first.
//
// WHICH ROBE. Source-X hands out ITEMID_ROBE on LAYER_ROBE at a resurrection
// (CCharSpell.cpp:509-514) and names it "Resurrection Robe". The client reads
// names from tiledata rather than per item, so the name is not available here
// -- but the MOMENT is. Death on this shard is full loot, so in the minutes
// after coming back the only robe a character can possibly be wearing is the
// one the server just conjured. A mage's own robe went to the corpse with
// everything else. Outside that window this does nothing at all, which is the
// point: nobody's real robe gets cut up.
bool Runner::CutResurrectionRobe(Client& client, const Observation& obs) {
    constexpr u16 kResRobe = 0x1F03;      // ITEMID_ROBE
    constexpr u8  kLayerRobe = 22;        // LAYER_ROBE, "robe over all"
    constexpr u16 kScissors[] = {0x0F9E, 0x0DFC};
    // Long enough to walk out of the healer's and find the scissors; far too
    // short to catch a robe bought later in the session.
    constexpr i64 kResRobeWindowMs = 5 * 60 * 1000;

    if (obs.dead || client.ActionBusy()) return false;

    // WHOSE ROBE IS IT? Two ways to be sure, because the robe outlives the
    // session that earned it.
    //
    // 1. This life does not wear cloth. A miner-smith's kit is metal and
    //    leather; a robe on LAYER_ROBE is not part of it and never was, so
    //    whatever put it there, it is spare cloth. This is the case that
    //    matters in practice -- Corwyn resurrected in one process and was
    //    still wearing the robe three sessions later, because a transition
    //    that happened before this program started cannot be observed by it.
    //
    // 2. This life DOES wear cloth (a mage), so a robe might genuinely be
    //    its own -- and only the minutes right after a resurrection are
    //    safe, since full loot means its real robe went to the corpse.
    const bool wearsCloth = needCfg_.profession &&
                            needCfg_.profession->wears == prof::Profession::Wear::Cloth;
    const bool freshlyRaised =
        resurrectedAtMs_ && obs.nowMs - resurrectedAtMs_ <= kResRobeWindowMs;
    if (wearsCloth && !freshlyRaised) return false;

    // Still wearing it: take it off. The server will not let scissors touch
    // worn cloth.
    if (client.EquippedGraphicAt(kLayerRobe) == kResRobe) {
        const u32 worn = client.EquippedAtLayer(kLayerRobe);
        if (worn) {
            LogLine("robe: taking off the resurrection robe -- it is worth "
                    "sixteen bandages");
            client.ActionUnequip(worn);
            nextActionMs_ = obs.nowMs + 1500;
            return true;
        }
    }

    const u32 robe = client.FindBackpackItemByGraphic(kResRobe);
    if (!robe) return false;

    u32 scissors = 0;
    for (u16 g : kScissors) {
        scissors = client.FindBackpackItemByGraphic(g);
        if (scissors) break;
    }
    if (!scissors) {
        // Not a failure worth stalling on -- the robe keeps. Said once per
        // resurrection rather than every tick.
        if (!state_.memory.HasEvent("no_scissors_for_robe")) {
            LogLine("robe: the resurrection robe is in the pack but there are "
                    "no scissors to cut it with");
            state_.memory.NoteEvent("no_scissors_for_robe", "i_scissors", "",
                                    obs.x, obs.y, obs.nowMs);
        }
        return false;
    }

    LogLine("robe: cutting the resurrection robe into bandages");
    client.ActionUseItemOn(scissors, robe);
    nextActionMs_ = obs.nowMs + 2000;
    return true;
}

// SHIRT, TROUSERS, SHOES -- in that order, and out of the pack before the shop.
//
// "nice you did it but now wear your shirt short shoes etc" and "if you have
// your clothing on your bag wear them, you can buy missing parts" (project
// owner, 2026-08-30). Cutting up the resurrection robe leaves a character
// standing in Britain in its underwear, which is not what a player looks
// like.
//
// Wearing costs nothing and needs nobody, so it always comes first; the shop
// is only for what is genuinely absent. The layer comes from tiledata rather
// than a hand-written table, exactly as the armour path does.
bool Runner::WearBasicClothing(Client& client, const Observation& obs) {
    if (obs.dead || client.ActionBusy()) return false;
    for (const ClothingPiece& p : kBasicClothing) {
        bool worn = false;
        const u32 have = ClothingOnHand(client, p, &worn);
        if (worn || !have) continue;
        LogLine("clothes: putting on the %s that was already in the pack",
                p.what);
        // Let the server pick the layer: the pack piece may be any of the
        // slot's variants, not the one this row is named after.
        client.ActionEquip(have, kLayerServerChooses);
        nextActionMs_ = obs.nowMs + 1200;
        return true;
    }
    // Skirts use their own paperdoll layer, not the trousers layer above.
    // Consequently a long skirt sitting in a woman's pack was never seen by
    // the basic-clothing loop and remained invisible even though it could be
    // worn over the starting trousers.  Wear an owned skirt; do not force a
    // shopping trip merely to satisfy a cosmetic choice.
    if (client.PlayerIsFemale()) {
        constexpr u16 kSkirts[] = {0x1516, 0x1531, 0x1537}; // long, short, kilt
        for (u16 graphic : kSkirts) {
            const u8 layer = client.ItemEquipLayer(graphic);
            if (!layer || client.EquippedGraphicAt(layer)) continue;
            const u32 have = client.FindBackpackItemByGraphic(graphic);
            if (!have) continue;
            LogLine("clothes: putting on the skirt that was already in the pack");
            client.ActionEquip(have, kLayerServerChooses);
            nextActionMs_ = obs.nowMs + 1200;
            return true;
        }
    }
    return false;
}

// The weapon-school basic (katana/kryss/club/bow) for a WantsToHunt fighter
// with empty hands -- SchoolWeapon and SchoolWeaponFor live in uo/life.h /
// life/Identity.cpp, pure and Client-free, next to WantsToHunt itself (same
// weapon-skill target, same threshold, so the two never disagree). See there
// for the per-weapon citations.

bool Runner::DoReplaceEquipment(Client& client, const Observation& obs, bool medicineOnly) {
    // FREE BANDAGES BEFORE BOUGHT ONES.
    if (!medicineOnly && CutResurrectionRobe(client, obs)) {
        planner_.NoteProgress();
        return false;
    }
    // AND DRESS FROM THE PACK BEFORE WALKING TO A SHOP.
    if (!medicineOnly && WearBasicClothing(client, obs)) {
        planner_.NoteProgress();
        return false;
    }

    // The cheapest fix first: something usable is already in the pack. The axe
    // is preferred -- it is this build's weapon AND its tool, so arming it
    // solves both needs at once.
    if (!medicineOnly && !obs.weaponEquipped) {
        // WAITING IS NOT PROGRESS.
        //
        // ArmAxe returns true for TWO different things: "I issued an
        // unequip/equip" and "an action is already in flight, come back"
        // (Runner.cpp:1640). Crediting both counted the TICK RATE as work.
        // Measured, wave 2 2026-09-01: Xerxes cut his resurrection robe at
        // 18:08:57.081, the use_item_on stayed in flight until it timed out
        // at 18:09:12.099, and REPLACE_EQUIPMENT completed reporting
        // progress=243 -- one per ~60ms tick of that fifteen-second wait,
        // having armed nothing (run_gates/g_Xerxes.console.txt:72-105).
        // Illyria did the same behind her cast_spell timeouts (progress=101,
        // 110, 70; g_Illyria.console.txt:152,331,461).
        //
        // Worse than the wrong number: NoteProgress() also clears
        // goal_.attempts (Goals.cpp:659), so the failure ladder was reset on
        // every tick and the goal could never run out of tries.
        const bool waiting = client.ActionBusy();
        if (ArmAxe(client, obs)) {
            if (!waiting) planner_.NoteProgress();
            return false;
        }
        const SchoolWeapon* desiredSchool = needCfg_.profession
                                                ? SchoolWeaponFor(*needCfg_.profession)
                                                : nullptr;
        const u32 sword = FindAny(client, kKatana, 2);
        if ((!desiredSchool || desiredSchool->skill == rules::kSwordsmanship) &&
            !AxeInHand(client) && sword) {
            if (client.ActionBusy()) return false;
            LogLine("arming: no axe carried, equipping the sword instead");
            client.ActionEquip(sword, kLayerServerChooses);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }

        // THE BUILD'S OWN SCHOOL WEAPON, ARMED FROM THE PACK OR BOUGHT.
        //
        // Reached only when the axe and the generic katana fallback above
        // both found nothing to ARM. That katana fallback only ever equips
        // one already sitting in the pack -- it never buys -- so a
        // swordsman with none carried falls through to here too, same as a
        // fencer, macefighter or archer. This is the gap the fix closes: the
        // FindAny just below is one more (harmless, redundant) look for a
        // swordsman, and the Buy path below it is the part that was missing
        // for every school, swordsman included.
        if (const SchoolWeapon* school = desiredSchool) {
            const u32 have = FindAny(client, school->graphics, 2);
            if (have) {
                if (client.ActionBusy()) return false;
                LogLine("arming: a %s is in the pack -- equipping it",
                        school->item);
                client.ActionEquip(have, kLayerServerChooses);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }

            // Buy it, or -- decided the same way bandages/garment/potions
            // above decide -- Done (nothing to do) or Refuse. `held` is
            // always 0 here: if it were not, the FindAny check just above
            // would already have armed it and returned.
            life::AcquireRequest req;
            req.graphic = school->graphics[0];
            req.item = school->item;
            req.desiredTotal = 1;
            req.mustWear = false;   // arming is handled above, from the pack
            req.wearable = true;
            req.minimumGoldReserve = 100;
            const life::AcquirePlan plan = life::DecideAcquire(req, 0, 0);

            if (plan.step == life::AcquireStep::Buy || weaponBuy_.Running()) {
                // VENDOR POLICY, ASKED BEFORE THE WALK. Every basic school
                // weapon here is a live smithing/carpentry/bowcraft recipe
                // (SKILLMAKE on each ITEMDEF) -- exactly the "a player craft
                // produces this" class M3.7 exists to keep off an NPC's
                // counter. i_katana/i_kryss/i_club/i_bow carry no
                // VendorPolicy row at all (Unknown, fails safe); i_dagger,
                // the one bladed weapon that IS graded, is PlayerCrafted.
                // Both refuse. This is deliberately NOT loosened here --
                // see the fix report -- so the refusal is asked and obeyed
                // the same way the bandage errand already asks it, rather
                // than silently skipped.
                const econ::VendorRuling ruling =
                    econ::CanBuyFromNPCGraphic(school->graphics[0]);
                if (!ruling.allowed) {
                    LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"the "
                            "vendor policy grades a %s %s, and no player "
                            "supplier is known\"", school->item,
                            econ::VendorClassName(ruling.klass));
                    state_.memory.NoteEvent("policy_refused", school->defname,
                                            econ::VendorClassName(ruling.klass),
                                            obs.x, obs.y, obs.nowMs);
                    planner_.Finish(false, "no legitimate source of a weapon",
                                    obs.nowMs);
                    return false;
                }
                if (client.TravelBusy()) return false;
                if (!weaponBuy_.Running()) {
                    life::BuyRequest breq;
                    breq.graphic = school->graphics[0];
                    breq.item = school->item;
                    breq.desiredTotal = 1;
                    breq.minimumGoldReserve = 100;
                    breq.Sell("weaponsmith", wm::Service::Blacksmith);
                    if (school->bowyerFallback)
                        breq.Sell("bowyer", wm::Service::Bowyer);
                    weaponBuy_.Begin(breq);
                }
                const life::ActivityTickResult wr =
                    weaponBuy_.Tick(client, obs);
                LogErrandReason("weapon", wr.reason, obs.nowMs);
                if (wr.wake == life::Wake::AfterDelay && wr.delayMs > 0)
                    nextActionMs_ = obs.nowMs + wr.delayMs;
                if (!life::IsTerminal(wr.status)) {
                    if (wr.acted) planner_.NoteAttempt(obs.nowMs);
                    return false;
                }
                if (wr.status == life::ActivityStatus::Success) {
                    planner_.NoteProgress();
                    return false;   // armed on the next pass, see FindAny above
                }
                LogLine("weapon: no %s bought (%s)", school->item, wr.reason);
                const i64 rest =
                    (wr.status == life::ActivityStatus::RetryableFailure)
                        ? kShortRestMs : kGearCooldownMs;
                return HandOff(planner_.Current().kind, GoalKind::Bank,
                               rest, "no weapon bought", obs.nowMs);
            }
        }
    }
    // --- BANDAGES, THE MISSING GARMENT, HEAL POTIONS -----------------------
    //
    // One AcquireRequest per item, decided by DecideAcquire instead of three
    // copies of "empty slot, more wanted" (S2_WIRING_PLAN.md S2.7). ALL
    // THREE plans feed the early-out below; dropping one reproduces the
    // regression this shape exists to prevent -- fifteen picks of
    // REPLACE_EQUIPMENT in one session, every one of them goal_completed
    // progress=0, because the old early-out asked about bandages only while
    // the need that selected the goal said "potions=0 low=2 gold=8993".

    // 1. Bandages -- BUT ONLY FOR A LIFE THAT DECLARES THEM.
    //
    // THE ERRAND MUST ASK WHAT THE NEED ASKS. AssessNeeds gates its bandage
    // clause on WantsConsumable(cfg, "bandage") (Needs.cpp) because a
    // crafting life's catalogue entry deliberately drops Bandages() in favour
    // of CrafterHealPotions() -- "you are crafter you dont have heal skill so
    // buy healing potion 3-4" / "so crafter do not buy bandages" (project
    // owner, 2026-08-30). This errand asked nothing, so it built the request
    // unconditionally and a miner_smith bought THIRTY bandages its zero
    // Healing could never make work. Worse, the Buy branch below returns on
    // every path, so the heal-potion branch behind it -- the one thing that
    // WOULD have kept the crafter alive -- was unreachable for exactly the
    // lives that needed it. (audit 2026-08-30, finding 1.)
    //
    // A family with no bandages in `consumables` treats this plan as Done,
    // which is the truth: it is not short of something it does not carry.
    const bool wantsBandages = life::WantsConsumable(needCfg_, "bandage") &&
        (!medicineOnly || obs.SkillTenths(rules::kHealing) >= 300);
    life::AcquirePlan bandagePlan;   // default Done -- vacuously satisfied
    // `low` TRIGGERS THE RESTOCK; `bandageFull` ENDS IT.
    //
    // This used to read "once a partial purchase crosses the safety floor,
    // move on to the other missing kit", and with a floor of eight that was
    // survivable. It is not survivable at the owner's hundred: one healer's
    // shelf is i_bandage {5 20}, so the pack crosses the floor while the
    // errand is barely started. Castor bought twenty from Dale on
    // 2026-09-05, the plan read Done -- "nothing on the list could be
    // replaced this pass" -- and he met a skeleton with 27.
    if (wantsBandages && obs.bandages >= needCfg_.bandageFull)
        bandageTopUp_ = false;
    if (wantsBandages && obs.bandages < needCfg_.bandageLow)
        bandageTopUp_ = true;
    if (wantsBandages && bandageTopUp_) {
        life::AcquireRequest bandageReq;
        bandageReq.graphic = kBandage;
        bandageReq.item = "bandages";
        bandageReq.desiredTotal = needCfg_.bandageFull;
        bandageReq.mustWear = false;
        bandageReq.wearable = true;
        // goldFloor stays ZERO here deliberately: bandages ARE the emergency
        // reserve. A character that will not spend its last coin on the thing
        // that keeps it alive has misunderstood what the reserve is for.
        bandageReq.minimumGoldReserve = 0;
        bandageReq.Sell("healer", wm::Service::Healer);
        bandagePlan = life::DecideAcquire(bandageReq, obs.bandages, 0);
    }
    if (bandagePlan.step != lastBandageAcquirePlan_) {
        LogPlan(life::AcquireStepName(bandagePlan.step), bandagePlan.reason);
        lastBandageAcquirePlan_ = bandagePlan.step;
    }

    // 2. The missing garment. Only what the pack could not supply, one piece
    // per visit: the errand re-runs, and a shirt bought this trip is worn by
    // WearBasicClothing at the top of the next one before anything else is
    // considered. Same scan order (shirt, trousers, shoes) as before.
    const ClothingPiece* garment = nullptr;
    life::AcquirePlan garmentPlan;   // default Done -- vacuously satisfied
    for (const ClothingPiece& p : kBasicClothing) {
        if (medicineOnly) break;
        const u8 layer = client.ItemEquipLayer(p.graphic);
        if (!layer) continue;
        bool worn = false;
        const u32 have = ClothingOnHand(client, p, &worn);
        life::AcquireRequest req;
        req.graphic = p.graphic;
        req.item = p.what;
        req.desiredTotal = 1;
        req.layer = layer;
        req.mustWear = true;
        req.wearable = true;
        const u16 wornGraphic = worn ? p.graphic : 0;
        const life::AcquirePlan plan =
            life::DecideAcquire(req, have ? 1 : 0, wornGraphic);
        if (plan.step == life::AcquireStep::Done) continue;
        garment = &p;
        garmentPlan = plan;
        break;
    }
    if (garment && (garmentPlan.step != lastGarmentAcquirePlan_ ||
                    garment->what != lastGarmentAcquireItem_)) {
        LogPlan(life::AcquireStepName(garmentPlan.step), garmentPlan.reason);
        lastGarmentAcquirePlan_ = garmentPlan.step;
        lastGarmentAcquireItem_ = garment->what;
    }

    // 3. Heal potions. HealPotions() has sat in the profession catalogue
    // since M5 and no code path ever filled it: warriors declared a need for
    // eight and carried none. "you are crafter you dont have heal skill so
    // buy healing potion 3-4" and "you can buy from same place you buy
    // healer" (project owner, 2026-08-30). The healer sells them -- the same
    // counter the bandage errand above already walks to; the alchemist is
    // the fallback (tm_vend's ALCHEMIST list carries i_potion_heal at {3 18}).
    const prof::ConsumableNeed* potions = nullptr;
    if (needCfg_.profession) {
        for (const prof::ConsumableNeed& c : needCfg_.profession->consumables) {
            if (c.name == "heal potion") { potions = &c; break; }
        }
    }
    life::AcquirePlan potionPlan;   // default Done -- vacuously satisfied
    u16 potionGfx = 0;
    if (potions && !potions->graphics.empty()) {
        // The graphic comes from the need itself rather than a second copy
        // of the constant: one table, one truth.
        potionGfx = potions->graphics.front();
        const i32 held = static_cast<i32>(client.BackpackItemCount(potionGfx));
        if (held < potions->low) {
            life::AcquireRequest req;
            req.graphic = potionGfx;
            req.item = "heal potion";
            req.desiredTotal = potions->restockTo;
            req.mustWear = false;
            req.wearable = true;
            // Unlike bandages this is not the last-coin emergency: a character
            // that spends its final gold on potions cannot buy the ore that
            // earns the next lot.
            req.minimumGoldReserve = medicineOnly ? 0 : 50;
            req.Sell("healer", wm::Service::Healer);
            req.Sell("alchemist", wm::Service::Alchemist);
            potionPlan = life::DecideAcquire(req, held, 0);
        }
    }
    if (potionPlan.step != lastPotionAcquirePlan_) {
        LogPlan(life::AcquireStepName(potionPlan.step), potionPlan.reason);
        lastPotionAcquirePlan_ = potionPlan.step;
    }

    // ALL THREE DONE. Every plan in this check, on purpose -- dropping one is
    // the exact regression named above.
    if ((medicineOnly || obs.weaponEquipped) && bandagePlan.step == life::AcquireStep::Done &&
        garmentPlan.step == life::AcquireStep::Done &&
        potionPlan.step == life::AcquireStep::Done)
        return true;

    // Refuse: unreachable today -- bandages and potions are never `mustWear`,
    // and every garment above is `wearable=true`. Kept so the switch stays
    // exhaustive once armour (MayWear-gated) joins this errand.
    if (bandagePlan.step == life::AcquireStep::Refuse) {
        LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"%s\"", bandagePlan.reason);
        state_.memory.NoteEvent("policy_refused", "i_bandage", "", obs.x, obs.y,
                                obs.nowMs);
        return HandOff(planner_.Current().kind, GoalKind::Bank,
                       kGearCooldownMs, bandagePlan.reason, obs.nowMs);
    }
    if (garment && garmentPlan.step == life::AcquireStep::Refuse) {
        LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"%s\"", garmentPlan.reason);
        state_.memory.NoteEvent("policy_refused", garment->item, "", obs.x,
                                obs.y, obs.nowMs);
        return HandOff(planner_.Current().kind, GoalKind::Bank,
                       kGearCooldownMs, garmentPlan.reason, obs.nowMs);
    }
    if (potionPlan.step == life::AcquireStep::Refuse) {
        LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"%s\"", potionPlan.reason);
        state_.memory.NoteEvent("policy_refused", "i_potion_heal", "", obs.x,
                                obs.y, obs.nowMs);
        return HandOff(planner_.Current().kind, GoalKind::Bank,
                       kGearCooldownMs, potionPlan.reason, obs.nowMs);
    }

    // Buy: bandages. The `bandageBuy_.Running()` half of the guard keeps an
    // in-flight purchase ticking to a terminal status even on a tick where
    // the pack has already crossed back above the decision's own threshold.
    //
    // IN CATALOGUE ORDER, FIRST ACTIONABLE WINS: bandages, then the garment,
    // then heal potions. Each branch runs only when ITS OWN plan says Buy, so
    // a life that wants no bandages falls straight through to the potion
    // branch instead of being stopped by a request it never made.
    if (wantsBandages &&
        (bandagePlan.step == life::AcquireStep::Buy || bandageBuy_.Running())) {
        const econ::VendorRuling ruling = econ::CanBuyFromNPCGraphic(kBandage);
        if (!ruling.allowed) {
            // FINISH the goal rather than retrying every 30 seconds forever.
            // The policy verdict will not change within a session, so a retry
            // is not a retry -- it is a character standing still. The need
            // itself is now reported blocked in AssessNeeds, so this is the
            // belt to that braces.
            LogLine("goal_failed=REPLACE_EQUIPMENT reason=\"the vendor policy "
                    "grades a bandage %s, and no player supplier is known\"",
                    econ::VendorClassName(ruling.klass));
            state_.memory.NoteEvent("policy_refused", "i_bandage",
                                    econ::VendorClassName(ruling.klass),
                                    obs.x, obs.y, obs.nowMs);
            planner_.Finish(false, "no legitimate source of bandages",
                            obs.nowMs);
            return false;
        }
        if (client.TravelBusy()) return false;

        // --- ONE ACTIVITY, EVERY PURCHASE --------------------------------
        //
        // This goal no longer knows how buying works. It states WHAT it wants
        // to end up holding and who might sell it; BuyActivity does the sums
        // (shortfall, reserve, ceiling) and VendorErrand does the handshake
        // (find a shopkeeper who is not a guildmaster, ask who is present,
        // walk into reach, open, clamp to stock, verify the pack moved).
        //
        // Note the request is a TOTAL, not a quantity to buy. That phrasing
        // is what makes "I already have thirty" expressible at all -- the
        // version that said "buy twenty" is the one that bought six heater
        // shields because a slot was still empty.
        if (!bandageBuy_.Running()) {
            life::BuyRequest req;
            req.graphic = kBandage;
            req.item = "clean bandages";
            req.desiredTotal = needCfg_.bandageFull;
            req.minimumGoldReserve = 0;
            req.Sell("healer", wm::Service::Healer);
            // A SECOND SHELF, NOT A SECOND VISIT. The healer's list is
            // i_bandage {5 20} (tm_vend.scp:1106-1114) and the vet's is
            // {6 66} (tm_vend.scp:547), so one healer cannot fill a
            // fighter's pack and the vet is the obvious next counter.
            req.Sell("veterinarian", wm::Service::Veterinarian);
            // Do not walk back to a counter this character has already
            // emptied -- it stays empty for ten minutes.
            for (u32 drained : DrainedShelves(obs.nowMs)) req.Avoid(drained);
            bandageBuy_.Begin(req);
        }
        const life::ActivityTickResult r = bandageBuy_.Tick(client, obs);
        LogErrandReason("bandages", r.reason, obs.nowMs);
        if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
            nextActionMs_ = obs.nowMs + r.delayMs;

        if (!life::IsTerminal(r.status)) {
        // AN ASK IS AN ATTEMPT; A WAIT IS NOT.
        //
        // This counted every non-terminal poll, so the ~60ms tick rate --
        // not the errand -- decided when the goal ran out of tries. Bruin's
        // potion errand issued ONE vendor ask, which needs its full 8s
        // deadline, and the five "an action is already in flight" polls
        // behind it spent the whole budget in 300ms
        // (run_r4/w_Bruin.console.txt:317-323). REPLACE_EQUIPMENT was
        // re-picked 39 times while that single ask was still outstanding.
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
            // Learn the shop. The activity deliberately does not write
            // memory -- remembering where the bandages came from is a life's
            // business, not a purchase's.
            for (const Client::VendorItem& v : client.VendorOffer()) {
                if (v.graphic != kBandage) continue;
                KnownSupplier s;
                s.need = "bandage";
                s.name = v.name;
                s.sourceType = "npc_vendor";
                s.serial = bandageBuy_.Keeper();
                s.x = obs.x; s.y = obs.y; s.z = obs.z;
                s.observedQuantity = v.amount;
                s.observedPricePerUnit = static_cast<i32>(v.price);
                s.lastVerifiedMs = obs.nowMs;
                s.policyAllows = true;
                state_.memory.NoteSupplier(s);
                LogLine("memory_learned=SUPPLIER need=bandage name=\"%s\"",
                        v.name.c_str());
                break;
            }
            planner_.NoteProgress();
            bandageShopFails_ = 0;

            // A PARTIAL BUY IS A DRAINED SHELF, NOT A FINISHED ERRAND.
            //
            // The errand asks for min(shortfall, what the shelf holds), so
            // coming back short means we took everything this counter had.
            // Castor did exactly this on 2026-09-05: twenty bandages from
            // Dale, then REPLACE_EQUIPMENT stood down -- "nothing on the list
            // could be replaced this pass" -- and he fought a skeleton with
            // 27. The request is a TOTAL; the goal keeps going until the
            // total is reached or every counter in town is empty.
            const i32 nowHeld =
                static_cast<i32>(client.BackpackItemCount(kBandage));
            if (nowHeld < needCfg_.bandageFull) {
                NoteDrainedShelf(bandageBuy_.Keeper(), obs.nowMs);
                // THE FLOOR IS THE OWNER'S NUMBER; THE TOTAL IS A WISH. Once
                // the pack holds the floor and a shelf has just run dry, the
                // rest of the total waits for the next session -- Hector
                // (2026-09-05 14:39-14:48) spent 577 s and zero fights
                // chasing 300 through a town whose healers held 16.
                if (nowHeld >= needCfg_.bandageLow) {
                    LogLine("bandages: %d held is past the floor of %d and this "
                            "counter is dry -- enough for today, the rest can wait",
                            nowHeld, needCfg_.bandageLow);
                    bandageTopUp_ = false;
                    return true;
                }
                LogLine("bandages: %d of %d after this counter -- its shelf is "
                        "empty for ten minutes, looking for another",
                        nowHeld, needCfg_.bandageFull);
            }
            return false;
        }

        // Every other terminal status ends the goal. The activity has stopped
        // running, so a caller that recognised only Failed would Begin() a
        // fresh one next tick -- the spin this whole layer exists to end,
        // reintroduced at the seam.
        // A SILENT OR EMPTY COUNTER IS ONE COUNTER. The shelf this errand
        // just failed at is written off for the restock window, and the goal
        // asks the next shopkeeper of the trade rather than ending an errand
        // that is still short of the total.
        NoteDrainedShelf(bandageBuy_.Keeper(), obs.nowMs);
        const i32 stillHeld =
            static_cast<i32>(client.BackpackItemCount(kBandage));
        // NOBODY OF THAT TRADE WAS THERE -- not a drained shelf but an empty
        // room, and the atlas has one healer place per town, so "another
        // counter" is the same room again. Faustus (2026-09-05 12:54-12:57)
        // walked it nine times. An empty room ends the shop errand at once.
        const bool nobodyThere = std::strstr(r.reason, "answered") != nullptr;
        if (!nobodyThere && ++bandageShopFails_ < kMaxBandageShops &&
            stillHeld < needCfg_.bandageLow) {
            LogLine("bandages: %s (%d/%d held) -- trying another counter "
                    "(%d of %d)", r.reason, stillHeld, needCfg_.bandageFull,
                    bandageShopFails_ + 1, kMaxBandageShops);
            nextActionMs_ = obs.nowMs + kShortRestMs;
            return false;
        }
        // EVERY COUNTER TRIED. Stand down cleanly and go and MAKE them: the
        // cloth route (buy loose cloth, cut it) lives in DoMakeBandages.
        LogLine("goal_failed=REPLACE_EQUIPMENT status=%s reason=\"%s\"",
                life::ActivityStatusName(r.status), r.reason);
        // TELL THE NEED MODEL WHAT THE TOWN IS OUT OF. Without this the
        // handoff below is advice nobody can take: NeedMakeBandages is
        // deliberately blocked while there is money to shop with, and money
        // is not a source once every shelf is empty.
        {
            char detail[32];
            std::snprintf(detail, sizeof(detail), "session=%d",
                          needCfg_.sessionIndex);
            state_.memory.NoteEvent("bandage_counters_empty", detail, "",
                                    obs.x, obs.y, obs.nowMs);
        }
        bandageShopFails_ = 0;
        bandageTopUp_ = false;
        const i64 rest = (r.status == life::ActivityStatus::RetryableFailure)
                             ? kShortRestMs : kGearCooldownMs;
        return HandOff(planner_.Current().kind, GoalKind::MakeBandages, rest,
                       "no bandages bought", obs.nowMs);
    }

    // A hunter's first trip must be for survival gear, not a civilian
    // wardrobe.  Hector had no legal armour, yet the generic equipment goal
    // chose missing trousers and sent him from Minoc to Vesper's tailor before
    // he could safely start the graveyard loop.  Free clothing in the pack is
    // still worn above; defer only a PURCHASE until the fighter has at least
    // one legal armour piece.
    const bool hunterNeedsArmor =
        needCfg_.profession && WantsToHunt(*needCfg_.profession) &&
        !HasBasicArmor(client, obs);

    // Buy: the missing garment.
    if (!hunterNeedsArmor && garment &&
        (garmentPlan.step == life::AcquireStep::Buy ||
                    clothingBuy_.Running())) {
        if (client.TravelBusy()) return false;
        if (!clothingBuy_.Running()) {
            LogLine("clothes: no %s on the body or in the pack -- buying one",
                    garment->what);
            life::BuyRequest req;
            req.graphic = garment->graphic;
            req.item = garment->item;
            req.desiredTotal = 1;
            req.minimumGoldReserve = 20;
            // A cobbler for the shoes, a tailor for the cloth -- and the
            // provisioner as the catch-all, because a small town has one of
            // those when it has neither of the others.
            req.Sell(garment->firstSeller, wm::Service::Tailor);
            req.Sell("tailor", wm::Service::Tailor);
            req.Sell("provisioner", wm::Service::Provisioner);
            clothingBuy_.Begin(req);
        }
        const life::ActivityTickResult cr = clothingBuy_.Tick(client, obs);
        LogErrandReason("clothes", cr.reason, obs.nowMs);
        if (cr.wake == life::Wake::AfterDelay && cr.delayMs > 0)
            nextActionMs_ = obs.nowMs + cr.delayMs;
        if (!life::IsTerminal(cr.status)) {
            // An ask is an attempt; a wait is not. See the note above.
            if (cr.acted) planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (cr.status == life::ActivityStatus::Success) {
            planner_.NoteProgress();
            return false;   // worn on the next pass
        }
        LogLine("clothes: no %s bought (%s)", garment->what, cr.reason);
    }

    // Buy: heal potions.
    if (potions && !potions->graphics.empty() &&
        (potionPlan.step == life::AcquireStep::Buy || potionBuy_.Running())) {
        if (client.TravelBusy()) return false;
        if (!potionBuy_.Running()) {
            const i32 held =
                static_cast<i32>(client.BackpackItemCount(potionGfx));
            LogLine("potions: carrying %d heal potion(s), below %d -- "
                    "buying up to %d", held, potions->low,
                    potions->restockTo);
            life::BuyRequest req;
            req.graphic = potionGfx;
            req.item = "heal potion";
            req.desiredTotal = potions->restockTo;
            req.minimumGoldReserve = medicineOnly ? 0 : 50;
            req.Sell("healer", wm::Service::Healer);
            req.Sell("alchemist", wm::Service::Alchemist);
            potionBuy_.Begin(req);
        }
        const life::ActivityTickResult pr = potionBuy_.Tick(client, obs);
        LogErrandReason("potions", pr.reason, obs.nowMs);
        if (pr.wake == life::Wake::AfterDelay && pr.delayMs > 0)
            nextActionMs_ = obs.nowMs + pr.delayMs;

        if (!life::IsTerminal(pr.status)) {
            // An ask is an attempt; a wait is not. See the note above.
            if (pr.acted) planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (pr.status == life::ActivityStatus::Success) {
            planner_.NoteProgress();
            return false;
        }
        // A shop that would not sell is not a reason to keep the goal
        // spinning; the next errand can have the turn.
        LogLine("potions: none bought (%s)", pr.reason);
    }

    // FALLING OFF THE END IS NOT SUCCESS.
    //
    // The ONE genuine completion of this goal is the all-plans-Done early-out
    // above. Everything that reaches here got past it -- so something on the
    // list is still missing and this pass did not fix it: the shop would not
    // sell, the purse was empty, or a plan was in a state no branch acts on.
    // Reporting `goal_completed` for that is the exact defect the anti-spin
    // backstop exists to catch, and it caught it: Aelia completed
    // REPLACE_EQUIPMENT fifteen times with progress=0 on 2026-09-01, every
    // one of them this line, with 0 gold and a healer quoting 30
    // (g_Aelia.console.txt; g_Illyria.console.txt:150-152 shows the same shape
    // -- "0 gold with a floor of 50 cannot buy one heal potion at 30" and then
    // goal_completed).
    //
    // So stand down properly: say why, cool the goal off, and finish FAILED so
    // the planner hands the turn to something that can act. Same rule
    // DoEarnGold already follows for "nothing spare to sell".
    LogLine("equipment: nothing on the list could be replaced this pass -- "
            "standing down so something that CAN act gets a turn");
    planner_.Cooldown(planner_.Current().kind, obs.nowMs + kGearCooldownMs);
    planner_.Finish(false, "nothing on the equipment list could be replaced",
                    obs.nowMs);
    return false;
}

// PRACTISE THE SKILL BY DOING IT.
//
// The half of progression that is not buying tenths from a guildmaster. A
// guildmaster sells up to 30.0 and stops; everything above that is the hours a
// character puts in. Meditation is the honest first case: it is raised purely
// by using it, needs no target, no reagents and no foe, and this life already
// wants it.
//
// DELIBERATELY NARROW. Magery and Evaluating Intelligence are raised by
// casting, which needs a spell choice, reagents and a legal target, and
// getting that wrong would have a mage burning its scribe stock on practice
// casts. Fighting skills already have TRAIN_COMBAT. Everything else --
// Inscription, Blacksmithing, Lumberjacking -- is raised by the work the life
// already does, and must NOT get an errand of its own: a scribe writes scrolls
// to sell, not to practise.
// EAT SOMETHING.
//
// The simplest need in the model, and it had NO GOAL AT ALL until now:
// NeedFood was assessed and printed every tick -- 27 times in one twenty
// minute session -- and appeared in no entry of the goal table, so it fell
// into a void every time. That is why M4's hunger row reads BUILT / NEVER
// FIRED. It was never reachable.
//
// Two halves, in the order a person would do them: eat what you are carrying,
// and if you are carrying none, go and buy some.
// DOES THIS LIFE HAVE A USE FOR THIS GRAPHIC?
//
// The one predicate behind both halves of "everything else is spare": what the
// dead-weight bank pass puts down, and what the loot pass sells. Gold, the
// tools this profession declares, the consumables it stocks, what it makes,
// and what it makes those from -- everything else is spare.
bool Runner::LifeNeedsGraphic(u16 gfx) const {
    if (gfx == kGoldCoin) return true;
    const prof::Profession* me = needCfg_.profession;
    if (!me) return false;
    for (const prof::ToolNeed& t : me->tools)
        for (u16 g : t.graphics) if (g == gfx) return true;
    for (const prof::ConsumableNeed& c : me->consumables)
        for (u16 g : c.graphics) if (g == gfx) return true;
    auto named = [&](const std::string& item) {
        for (u16 g : econ::GraphicsForItem(item.c_str())) if (g == gfx) return true;
        return false;
    };
    for (const std::string& it : me->consumes) if (named(it)) return true;
    for (const std::string& made : me->produces) {
        if (named(made)) return true;
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const prod::Ingredient& in : r->inputs)
            if (in.item && named(in.item)) return true;
    }
    return false;
}

// WHAT THIS GRAPHIC IS FOR, in this character's hands.
//
// LifeNeedsGraphic above answers yes/no, and yes/no is the wrong question for
// anything a life can own more than one of. A smith PRODUCES heater shields,
// so it answered "needed" for Corwyn's sixth shield exactly as loudly as for
// his first, and six of them rode along unsold past a vendor offering 61 gold
// each. The role decides the KEEP-COUNT; see uo/activities/disposal.h.
//
// Order matters. A thing can be both an input and a product -- iron ingots
// are made by a smith and consumed by one -- and being needed for the next
// item on the bench outranks being stock to sell.
ItemRole Runner::RoleOfGraphic(u16 gfx) const {
    if (gfx == kGoldCoin) return ItemRole::Money;

    const prof::Profession* me = needCfg_.profession;
    if (!me) return ItemRole::Unknown;

    auto named = [&](const std::string& item) {
        for (u16 g : econ::GraphicsForItem(item.c_str()))
            if (g == gfx) return true;
        return false;
    };

    for (const prof::ToolNeed& t : me->tools)
        for (u16 g : t.graphics) if (g == gfx) return ItemRole::Tool;

    for (const prof::ConsumableNeed& c : me->consumables)
        for (u16 g : c.graphics) if (g == gfx) return ItemRole::Consumable;

    for (const std::string& it : me->consumes)
        if (named(it)) return ItemRole::CraftInput;

    // An input to something on the recipe list is stock for the next make,
    // and outranks being a product in its own right.
    for (const std::string& made : me->produces) {
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const prod::Ingredient& in : r->inputs)
            if (in.item && named(in.item)) return ItemRole::CraftInput;
    }

    // WHAT IT MAKES IS STOCK, AND STOCK IS FOR SELLING. Nothing here keeps a
    // spare back: the one being worn is equipped, and an equipped item is not
    // in the backpack the vendor's list is built from.
    for (const std::string& made : me->produces)
        if (named(made)) return ItemRole::Produce;

    // Anything else in the pack is loot, a gift, or a mistake. None of those
    // is a reason to keep carrying it.
    return ItemRole::Unknown;
}

// The first Create Food reagent the pack is out of, or nullptr when the cast
// can be paid for. Same table PRACTICE_SKILL reads (spellcast.h), same pack.
static const char* CreateFoodReagentShort(const Observation& obs) {
    const spell::SpellDef* d = spell::DefForSpell(kSpellCreateFood);
    if (!d) return nullptr;
    for (const char* const* r = d->reagents; *r; ++r)
        if (market::QtyOf(obs.pack, *r) <= 0) return *r;
    return nullptr;
}

bool Runner::DoGetFood(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // WHEN IS SUPPER OVER?
    //
    // This goal had no completion at all -- not one `return true` in the whole
    // body -- so it could never finish. Brannoc ate ONE HUNDRED AND SIXTY
    // times in a single session and the goal simply kept running until the
    // planner's 300-second limit killed it, whereupon the same unchanged
    // hunger picked it straight back:
    //
    //   session_goals families=1 picks=6 top=100% | GET_FOOD=6(100%)
    //   session_summary goals=0/6 gold=837->789
    //
    // The gold moving is the proof it was working -- he really did buy bread
    // twelve times and eat it -- and the goal still reported nothing, took the
    // whole session, and let no other family have a turn. Voris did the same.
    //
    // Fed, with something in the pack for later, is done.
    if (!obs.hungry && !obs.starving && obs.food >= needCfg_.foodLow) {
        LogLine("food: fed, and carrying %d for later "
                "-- this errand is done", obs.food);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // AND EAT ONLY WHEN HUNGRY. "if they are full they dont need to eat"
    // (project owner, 2026-08-29). The eat branch fired on carrying food
    // rather than on needing it, so a fed character chewed through its whole
    // pack -- 160 mouthfuls in one session -- and then had to go and buy more.
    // Food costs gold; a full stomach wastes it.
    const u32 food = (obs.hungry || obs.starving)
                         ? FindAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]))
                         : 0;
    if (food) {
        LogLine("food: eating (hungry=%d starving=%d, carrying %d)",
                obs.hungry ? 1 : 0, obs.starving ? 1 : 0, obs.food);
        // A double-click is how a player eats. The server decides whether it
        // helped; the next tick's journal says so.
        client.ActionUseObject(food);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // A FISHER MAKES ITS OWN SUPPER TOO.
    //
    // "Marla shouldnt buy food she can make food" (project owner,
    // 2026-08-29). A life that catches fish has dinner in its pack already:
    // one whole fish cuts into four steaks and a campfire cooks them, and
    // i_fish_cut_cooked is t_food. Walking to a baker to buy bread with the
    // river behind you is not a shopping trip, it is a failure to look in
    // your own backpack.
    //
    // The cut and the cook belong to the fishing and crafting goals, which
    // already know how; what this does is stop the FOOD errand spending gold
    // when the makings are carried.
    if (needCfg_.profession && needCfg_.profession->gathers == "fish") {
        const bool haveMakings =
            FindAny(client, kWholeFish,
                    sizeof(kWholeFish) / sizeof(kWholeFish[0])) != 0 ||
            client.FindBackpackItemByGraphic(kFishRawSteak) != 0;
        if (haveMakings) {
            LogLine("food: carrying the catch -- cutting and cooking it rather "
                    "than buying bread");
            planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
            planner_.Finish(false, "will cook the catch instead", obs.nowMs);
            return false;
        }
    }

    // A MAGE MAKES ITS OWN SUPPER.
    //
    // "food was not a problem at all normally in Revolution UO -- mages make
    // their own food with the spell, you can collect from farms throughout the
    // world, fishers can sell fish steaks to people" (project owner,
    // 2026-08-29). Treating hunger as a shopping errand was the mistake under
    // all of this: it made a solved problem into a goal that could eat a whole
    // session.
    //
    // [SPELL 2] s_create_food is spellflag_playeronly with no target flag at
    // all (spells_magery.scp:36) -- four mana, MAGERY 10.0 to attempt, cast at
    // nobody. Anyone who can cast it should, before walking anywhere: it costs
    // no gold, needs no shop, and works while broke, which is exactly the
    // predicament the stand-down below was written for. It also raises Magery,
    // so the errand pays for itself.
    //
    // Mana is the renewable resource. Spending 4 of it on dinner is free in a
    // way that 30 gold is not.
    // HAVING THE SKILL IS NOT HAVING THE SPELL.
    //
    // First live outing of the line above: Voris, an alchemist carrying Magery
    // 50.0 and no spellbook worth the name, asked for Create Food every six
    // seconds for the whole session and was told every time --
    //
    //   [ACTION] cast_spell id=2 target=0x00000000 mana=32
    //   System: The spell is not in your spellbook.
    //
    // -- with mana sitting at 32 the entire time, which is the tell: a cast
    // that costs nothing never happened. The skill check passed and the
    // capability check did not exist. Ask once, believe the answer, and go
    // shopping like anyone else.
    if (noCreateFoodSpell_) {
        // fall through to buying
    } else if (obs.SkillTenths(rules::kMagery) >= 100) {
        if (createFoodMark_ != 0 &&
            client.JournalSaidSince("not in your spellbook", createFoodMark_)) {
            noCreateFoodSpell_ = true;
            LogLine("food: Create Food is not in this character's spellbook "
                    "(magery %.1f) -- buying food instead for the rest of the "
                    "session", obs.SkillTenths(rules::kMagery) / 10.0);
        } else if (obs.mana >= kCreateFoodMana &&
                   CreateFoodReagentShort(obs) != nullptr) {
            // ReagentsRequired=1 on this shard (sphere.ini:1136): a cast with
            // an empty pouch is answered "You lack Mandrake Root for this
            // spell" and nothing else -- Elara logged it 94 times in one wave
            // (2026-09-02). No reagents means shop for bread like anyone else;
            // the practice loop's restock errand refills the pouch on its own
            // schedule.
            LogLine("food: Create Food is short of %s -- buying food instead "
                    "this time", CreateFoodReagentShort(obs));
            // fall through to buying
        } else if (obs.mana >= kCreateFoodMana) {
            LogLine("food: casting Create Food rather than shopping "
                    "(magery %.1f, mana %d)",
                    obs.SkillTenths(rules::kMagery) / 10.0, obs.mana);
            createFoodMark_ = client.JournalNowMs();
            client.ActionCastSpell(kSpellCreateFood);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 6000;
            return false;
        }
        // Out of mana but able to cast: waiting for mana beats walking to a
        // shop, and beats standing the goal down while broke.
        else if (obs.gold < kFoodMoney) {
            LogLine("food: %d mana is short of the %d Create Food needs -- "
                    "resting for it rather than shopping with %d gold",
                    obs.mana, kCreateFoodMana, obs.gold);
            nextActionMs_ = obs.nowMs + 15000;
            return false;
        }
    }

    // STILL TO DO, and deliberately not faked here: crops on the world's farms
    // are a second free source, and fish steaks bought from a FISHER are the
    // third -- which is the same errand as R4's first player-to-player trade,
    // since the fisher selling them will be another bot. Both belong with the
    // market layer rather than bolted into this goal.

    // NOTHING TO EAT AND NOTHING TO BUY WITH.
    //
    // A goal that cannot possibly succeed must stand down, or it eats the
    // session. This one did exactly that on its first live outing: Kaelen died,
    // lost everything to full loot, and woke with no food and no gold -- so
    // GET_FOOD failed, was re-picked, and took the WHOLE 25 minutes:
    //
    //   session_goals families=1 picks=5 top=100% varied=0 | upkeep=5(100%)
    //   session_summary goals=0/5 gold=0->0
    //
    // Being hungry with an empty purse is a real predicament and the honest
    // response is to go and earn something, not to keep walking to a shop.
    if (obs.gold < kFoodMoney) {
        LogLine("food: hungry with %d gold -- nothing to eat and nothing to buy "
                "with; standing down to go and earn", obs.gold);
        planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
        planner_.Finish(false, "no food and no money", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Buying food is an ordinary provisioner errand, and it is NOT a craft
    // input -- so it does not belong in DoBuySupplies, which is about the
    // things a profession makes other things from.
    if (client.TravelBusy()) return false;

    // CORRECTION, 2026-08-30: A PROVISIONER *DOES* SELL FOOD HERE.
    //
    // The note below is kept because its reasoning is still right, but its
    // FACT is now wrong and acting on it would be a mistake. The four SELL
    // lines it calls commented out are live in the current tm_vend.scp
    // (1350-1353: bread, lamb, chicken, cooked bird at {5 38}), and the
    // project owner confirms provisioners sold food on Revolution.
    //
    // What actually failed in v1_Corwyn was not stock but the HANDSHAKE:
    // "food: the 'provisioner' would not open a shop". The shop never
    // opened, so its list was never read. Different problem, same afternoon.
    //
    // --- the original note, now historical -------------------------------
    // A PROVISIONER ON THIS SHARD CANNOT SELL FOOD.
    //
    // This goal asked one anyway, all session, and never ate. The shop window
    // opened every time -- 24 items, all of them backpacks, lockpicks, bottles
    // and board games -- because TNS's VENDOR_S_PROVISIONER has its four food
    // lines COMMENTED OUT (tm_vend.scp:1276-1279):
    //
    //   //SELL=i_bread_loaf,{5 38}
    //   //SELL=i_lamb_leg,{5 38}
    //   //SELL=i_chicken_leg,{5 38}
    //   //SELL=i_bird_cooked,{5 38}
    //
    // So this was never a protocol failure or a pathing failure. The errand was
    // addressed to a shop that structurally does not stock the goods. Voris and
    // Ysolde spent a whole 25-minute session on it and picked no other goal:
    //   session_goals families=1 picks=4 top=100% | GET_FOOD=4(100%)
    //
    // The BAKER carries it -- SELL=i_bread_loaf,{55 140}, plus pies, muffins
    // and cakes -- and i_bread_loaf is ITEMDEF 0103b, which is already the
    // first entry in kFood above, so the eating side needed no change at all.
    // Ask the baker first and keep the provisioner only as a fallback: it costs
    // nothing when a baker is near, and a town without one still gets a try.
    //
    // The runtime vendor lists are TNS's, kept deliberately. Uncommenting those
    // four lines would have been the smaller diff and the wrong one -- it edits
    // the shard's economy to suit the bot instead of teaching the bot where
    // food is sold.
    // --- THE ERRAND OWNS THE HANDSHAKE FROM HERE -------------------------
    //
    // Baker first, provisioner as the fallback -- the seller list, not two
    // hand-written branches and a trip-parity trick. Everything else this
    // block used to do (walk up before speaking, scan when the keeper is not
    // in the cache, wait past the vendor deadline) is the same sequence the
    // bandage errand needed, and is now written once.
    //
    // graphic = 0 means "the caller chooses from the offer", because food is
    // not one item: kFood lists bread, lamb, chicken and cooked bird, and any
    // of them ends the hunger. The errand opens the shop and hands back the
    // window; picking the row stays here, where the eating rules live.
    if (!foodErrand_.Running()) {
        life::VendorErrandSpec spec;
        // Revolution provisioners sell ordinary food.  Bakers are deliberately
        // not part of autonomous routing: normal lives use provisioners,
        // fishers, farms, or mage-created food, never a cross-city bakery trip.
        spec.Sell("provisioner", wm::Service::Provisioner);
        spec.graphic = 0;
        spec.what = "something to eat";
        spec.maxTrips = kMaxFoodTrips;
        foodErrand_.Begin(spec);
    }
    const life::VendorErrandResult r = foodErrand_.Tick(client, obs);
    LogErrandReason("food", r.why.c_str(), obs.nowMs);
    if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
        nextActionMs_ = obs.nowMs + r.delayMs;

    // Any terminal state ends it -- see the note in DoReplaceEquipment.
    if (life::IsTerminal(r.status) && r.status != life::ActivityStatus::Success) {
        LogLine("goal_failed=GET_FOOD status=%s reason=\"%s\"",
                life::ActivityStatusName(r.status), r.why.c_str());
        const i64 rest = (r.status == life::ActivityStatus::RetryableFailure)
                             ? kShortRestMs : kNoFoodCooldownMs;
        planner_.Cooldown(GoalKind::GetFood, obs.nowMs + rest);
        planner_.Finish(false, "no food seller reachable", obs.nowMs);
        return false;
    }
    if (!r.offerOpen) {
        // An ask is an attempt; a wait is not. See DoReplaceEquipment.
        if (r.acted) planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // The shop is open and it is OURS. Buy the first edible row we can
    // afford -- two of them, because walking back for the second loaf is the
    // errand nobody wants to run twice.
    for (const Client::VendorItem& v : client.VendorOffer()) {
        if (!GraphicIsAny(v.graphic, kFood, sizeof(kFood) / sizeof(kFood[0])))
            continue;
        if (static_cast<i32>(v.price) * 2 > obs.gold) continue;
        LogLine("food: buying %s at %d gold", v.name.c_str(),
                static_cast<i32>(v.price));
        client.ActionVendorBuy(r.keeper, v.serial, 2);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }
    LogLine("goal_failed=GET_FOOD reason=\"this shop has nothing edible this "
            "character can afford\"");
    planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
    planner_.Finish(false, "nothing edible for sale", obs.nowMs);
    foodErrand_.Cancel();
    return false;
}

// ---------------------------------------------------------------------------
// GEAR.
//
// "always try to wear better equipment based on your class", "bots also always
// check for gear", "Kaelen needs to buy some armor" (project owner,
// 2026-08-29).
//
// Two halves. WEAR what is already carried if it beats what is worn -- loot
// arrives in the pack and sat there forever, because nothing ever looked. And
// BUY an armorer upgrade when the purse is clear of the reserve.  A filled
// slot is not automatically done: fighters begin in weak leather and must be
// able to replace it with a stronger legal piece sold by an armorer.
//
// The class rule is not a preference. On this shard a metal set stops a
// caster casting entirely, so ArmorFor refuses metal to anyone with Magery
// rather than scoring it lower.
const ArmorPiece* ArmorFor(u16 graphic) {
    for (const ArmorPiece& a : kArmorPieces)
        if (a.graphic == graphic) return &a;
    return nullptr;
}

// IS ANYTHING FROM kArmorPieces ON THE BODY RIGHT NOW.
//
// A helmet alone is not "geared for a graveyard".  Require three distinct
// protected layers and make one of them a core torso/leg layer.  This still
// lets a 50-STR newcomer use leather/ringmail rather than waiting for plate,
// while ordinary clothing has no entry in kArmorPieces and never counts.
bool Runner::HasBasicArmor(Client& client, const Observation& obs) const {
    bool layers[32] = {};
    int protectedLayers = 0;
    bool hasCore = false;
    for (const ArmorPiece& a : kArmorPieces) {
        if (!MayWear(a, obs)) continue;
        const u8 layer = client.ItemEquipLayer(a.graphic);
        if (!layer) continue;
        const u16 wornGfx = client.EquippedGraphicAt(layer);
        if (!wornGfx || !ArmorFor(wornGfx)) continue;
        if (layer < 32 && !layers[layer]) {
            layers[layer] = true;
            ++protectedLayers;
        }
        // Inner/middle/outer torso and inner/outer legs in the classic UO
        // equipment-layer protocol.
        if (layer == 13 || layer == 17 || layer == 22 ||
            layer == 23 || layer == 24) {
            hasCore = true;
        }
    }
    return hasCore && protectedLayers >= 3;
}

bool Runner::MayWear(const ArmorPiece& a, const Observation& obs) const {
    // THE PROFESSION ANSWERS FIRST, because it knows before login.
    //
    // "mage wears only mage equipment" (project owner). Read off the
    // catalogue's `wears`/`maysShield` (M5, professions.h) rather than
    // re-derived here. This closes a gap the Magery test never covered: a
    // tailor has no Magery and plenty of STR, so nothing stopped it putting on
    // a platemail gorget it had looted -- it was allowed to wear anything it
    // could lift.
    if (const prof::Profession* pr = needCfg_.profession) {
        const bool metal = (a.cls == ArmorClass::Metal);
        const bool leather = (a.cls == ArmorClass::Leather);
        if (a.cls == ArmorClass::Shield) {
            if (!pr->maysShield) return false;
        } else if (metal && pr->wears != prof::Profession::Wear::Metal) {
            return false;
        } else if (leather && pr->wears == prof::Profession::Wear::Cloth) {
            return false;
        }
    }

    // The Magery test below is the answer ONLY for a life with no profession.
    // A profession that says Metal must not then be talked out of it by its
    // own utility Magery -- a swordsman who learned Recall is still a
    // swordsman, and the earlier version of this function would have kept him
    // in cloth forever.
    if (needCfg_.profession) {
        if (obs.str <= 0) return false;      // unknown STR is not "strong enough"
        return obs.str >= static_cast<i32>(a.reqStr);
    }

    // NOT KNOWING IS NOT THE SAME AS ZERO.
    //
    // Thessaly is an Apprentice Mage with Magery 50.0, and she was found
    // WEARING PLATEMAIL GAUNTLETS -- ten pairs in her pack and one on her
    // paperdoll. The rule below is right and it read her Magery as 0, because
    // the skill list had not arrived from the server yet: obs.skills was empty
    // and SkillTenths returns 0 for a skill it cannot find. An empty skill
    // list is silence, not evidence of a warrior.
    //
    // On this shard the mistake is unrecoverable while worn -- a metal set
    // stops a caster casting at all -- so the safe reading of silence is to
    // refuse. A character that genuinely has no Magery loses nothing but a few
    // seconds, until its skills arrive.
    const bool skillsKnown = !obs.skills.empty();
    const bool mayBeCaster =
        !skillsKnown || obs.SkillTenths(rules::kMagery) > 0;
    if (mayBeCaster && a.cls == ArmorClass::Metal)
        return false;                       // metal ends a caster's casting
    if (mayBeCaster && a.cls == ArmorClass::Shield)
        return false;                       // a shield hand is a spell hand
    // Strength is read the same way: unknown is not "strong enough".
    if (obs.str <= 0) return false;
    return obs.str >= static_cast<i32>(a.reqStr);
}

bool Runner::DoUpgradeGear(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    // AND NOT WHILE ALREADY WALKING TO THE SHOP.
    //
    // Without this the goal re-decided every tick during the journey, printed
    // "no piece for an empty slot" again and re-issued the trip: Nessa logged
    // it 849 times in one session and spent it running between a lake and a
    // tailor. The third goal to spin this way by re-entering mid-travel.
    if (client.TravelBusy()) return false;

    // --- WEAR WHAT IS ALREADY CARRIED ------------------------------------
    //
    // Every armour graphic this shard defines, checked against the pack. The
    // layer comes from tiledata, so the comparison is against the piece
    // actually in that slot rather than a guess about what a slot holds.
    for (const ArmorPiece& a : kArmorPieces) {
        if (!MayWear(a, obs)) continue;
        const u32 have = client.FindBackpackItemByGraphic(a.graphic);
        if (!have) continue;
        const u8 layer = client.ItemEquipLayer(a.graphic);
        if (!layer) continue;
        const u16 wornGfx = client.EquippedGraphicAt(layer);
        const ArmorPiece* worn = wornGfx ? ArmorFor(wornGfx) : nullptr;
        const int wornArmor = worn ? worn->armor : 0;
        if (wornGfx && wornArmor >= a.armor) continue;   // no better
        LogLine("gear: wearing 0x%04X (armor %d, needs str %d, have %d) over "
                "0x%04X (armor %d)", a.graphic, a.armor, a.reqStr, obs.str,
                wornGfx, wornArmor);
        client.ActionEquip(have, layer);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }


    // --- WHAT THIS LIFE WILL NOT WEAR ------------------------------------
    //
    // M7's disposal order, applied to the pack. Until now the wear pass simply
    // skipped a piece the class refuses and said nothing, so a mage carried
    // looted platemail around for a whole session with no record of why it was
    // never worn and no decision about where it should go.
    //
    // market::DisposeOfGear owns the order -- wear, then offer to players,
    // then sell to an NPC only where the Gold Faucet Registry establishes that
    // route. For armour it does not (monster_loot_resale is UNKNOWN), so the
    // answer today is the bank box, and the BANK goal already carries pack
    // weight to a bank. What this adds is the RECORD: one line per item naming
    // the step the order reached and the reason it stopped there.
    if (needCfg_.profession && !dispositionLogged_) {
        for (const ArmorPiece& a : kArmorPieces) {
            if (MayWear(a, obs)) continue;              // the wear pass has it
            if (!client.FindBackpackItemByGraphic(a.graphic)) continue;
            const char* name = econ::ItemNameForGraphic(a.graphic);
            const market::DisposalRuling r = market::DisposeOfGear(
                *needCfg_.profession, name ? name : "", false,
                /*playersDeclined=*/false, state_.ledger);
            LogLine("gear: carrying 0x%04X (%s) this life will not wear -- %s: %s",
                    a.graphic, name ? name : "unmapped item",
                    market::DisposalName(r.what), r.reason ? r.reason : "");
        }
        dispositionLogged_ = true;
    }

    // --- BUY THE BEST ARMORER UPGRADE ------------------------------------
    //
    // Only above the reserve: armour is worth having and is never worth being
    // unable to eat for. The best affordable legal piece is chosen, which for
    // a caster means the best LEATHER, and for a fighter the best its
    // strength allows.
    // A CRAFTER DOES NOT GO ARMOUR SHOPPING. "for crafter upgrade gear just
    // wear normal clothing for now" (project owner, 2026-08-29).
    //
    // A life that does not pick fights has little use for armour and every use
    // for its gold: a tailor buying a leather tunic is spending the money that
    // buys its lessons and its cloth. It still WEARS anything better that it
    // loots -- that part ran above and costs nothing -- but it does not shop.
    if (needCfg_.profession && !WantsToHunt(*needCfg_.profession)) {
        LogLine("gear: nothing carried is an upgrade, and this life does not "
                "fight -- ordinary clothes will do, so no armour shopping");
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    const i32 reserve =
        needCfg_.profession ? needCfg_.profession->goldReserve : 0;
    // The first armour set is part of a fighter's starting kit, not a luxury
    // upgrade.  A 10k fighter reserve is meant to finance bandages and the
    // return from a graveyard; treating it as an untouchable floor when the
    // character starts with exactly 10k leaves the fighter naked forever.
    const bool bootstrapArmor = needCfg_.profession &&
                               WantsToHunt(*needCfg_.profession) &&
                               !HasBasicArmor(client, obs);
    const i32 spendFloor = bootstrapArmor ? kArmorMoney : reserve + kArmorMoney;
    if (obs.gold <= spendFloor) {
        LogLine("gear: nothing carried is an upgrade, and %d gold is not clear "
                "of the %d%s -- earning first", obs.gold, spendFloor,
                bootstrapArmor ? " starter-gear floor" : " reserve");
        planner_.Cooldown(GoalKind::UpgradeGear, obs.nowMs + kGearCooldownMs);
        planner_.Finish(false, "no upgrade and no spare money", obs.nowMs);
        return false;
    }

    // BUY ONLY WHAT IS ACTUALLY FOR SALE -- and the list is longer than I first
    // thought.
    //
    // I claimed metal armour was sold by nobody. That was WRONG, and the owner
    // caught it: "armorers has more to sell at vendors". There are FIVE
    // armorer templates -- VENDOR_S_ARMORER_LEATHER, _RING, _CHAIN, _PLATE and
    // _SHIELDS -- carrying 36 distinct pieces between them, ringmail through
    // platemail and helms and shields. My check had matched
    // "i_(plate|chain|ring)_", and the defnames are i_platemail_,
    // i_chainmail_, i_ringmail_, so it found nothing and I concluded from that
    // nothing existed. Absence of a grep hit is not absence of the thing.
    //
    // kSoldArmour is generated from those templates: every SELL row in any
    // ARMORER list, resolved to its graphic. Anything outside it is
    // smith-crafted or looted and must not be shopped for.
    const ArmorPiece* want = nullptr;
    for (const ArmorPiece& a : kArmorPieces) {
        if (!GraphicIsAny(a.graphic, kSoldArmour,
                          sizeof(kSoldArmour) / sizeof(kSoldArmour[0])))
            continue;                                  // nobody stocks it
        if (!MayWear(a, obs)) continue;
        const u8 layer = client.ItemEquipLayer(a.graphic);
        if (!layer) continue;
        const u16 wornGfx = client.EquippedGraphicAt(layer);
        const ArmorPiece* worn = wornGfx ? ArmorFor(wornGfx) : nullptr;
        const int wornArmor = worn ? worn->armor : 0;
        // A worn item only closes this slot when it is at least as protective
        // as the candidate.  The prior `if (worn) continue` made every
        // warrior who owned starter leather permanently ineligible for an
        // armorer upgrade.
        if (wornArmor >= a.armor) continue;
        // ALREADY BOUGHT ONE, AND IT IS STILL IN THE PACK.
        //
        // The wear pass at the top of this goal runs FIRST and wears anything
        // carried that fits. So a piece still sitting in the pack when we get
        // here is one this character could not put on -- and buying a second
        // copy cannot change that. Nothing checked, so the empty slot was
        // read as "buy one" on every single visit:
        //
        //   11,645 "no piece for an empty slot ... buying" lines across the
        //   recorded runs -- 5,140 on Cassia alone, 2,914 Nessa, 2,475
        //   Maribel -- and Corwyn's backpack holding SIX i_shield_heater
        //   (reqStr 90) bought by a character with STR 56, none ever worn.
        //
        // The M7 disposal order is what such a piece is for now: it will be
        // offered to players and otherwise banked, rather than restocked.
        if (client.FindBackpackItemByGraphic(a.graphic)) {
            LogLine("gear: already carrying 0x%04X and it is still not worn "
                    "-- not buying another", a.graphic);
            continue;
        }
        if (!want || a.armor > want->armor) want = &a;
    }
    if (!want) {
        // AND REST, rather than reporting success and being re-picked. There
        // is nothing to buy and nothing has changed, so an immediate second
        // look asks the same question of the same pack -- the "goal that
        // achieved nothing" family again. UPGRADE_GEAR completed with
        // progress=0 and was re-picked 60 ms later in run_m7/v_Corwyn; the
        // cooldown is what makes the answer stick until something moves.
        LogLine("gear: every slot this class may fill is filled");
        planner_.Cooldown(GoalKind::UpgradeGear, obs.nowMs + kGearCooldownMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // Never infer that a town has no armourer from the current screen.  At
    // startup a fighter normally sees a healer or provisioner, not the smithy;
    // using that short-range cache sent Hector on a long tailor tour before
    // looking for the armour this goal was created to buy.  The atlas routes
    // the armorer service across town (and across gates) and the errand itself
    // can report a genuine stock failure if the shop does not carry the piece.
    const u8 wantLayer = client.ItemEquipLayer(want->graphic);
    const u16 currentGfx = wantLayer ? client.EquippedGraphicAt(wantLayer) : 0;
    const ArmorPiece* current = currentGfx ? ArmorFor(currentGfx) : nullptr;
    LogLine("gear: armorer upgrade 0x%04X (armor %d, needs str %d) replaces "
            "0x%04X (armor %d); buying from a %s",
            want->graphic, want->armor, want->reqStr,
            currentGfx, current ? current->armor : 0,
            "armorer");
    BuyScrollFrom(client, obs, "armorer", wm::Service::Blacksmith,
                  want->graphic, false, 1, "a piece of armour",
                  GoalKind::UpgradeGear);
    return false;
}

}  // namespace uo::life
