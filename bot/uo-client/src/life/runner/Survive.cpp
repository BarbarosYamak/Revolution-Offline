#include "RunnerInternal.h"
#include "NoviceEngage.h"
#include <algorithm>

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;

bool Runner::RefillManaWhenSafe(Client& client, const Observation& obs) {
    const auto* region = client.CurrentRegion();
    const bool fighting = currentFoe_ != 0 || obs.underAttack || obs.attackersOnMe > 0;
    const bool safe = !obs.dead && !fighting &&
        obs.HpFraction() >= needCfg_.healHpFraction &&
        (obs.hostilesNear == 0 || (region && region->flags.guarded));
    const bool needsMana = BuildCastsSpells(needCfg_.profession) &&
        obs.manaMax > 0 && obs.mana < obs.manaMax;
    // Between casts, repeatedly try Meditation when there is room to stand.
    // Never replace a cast, movement, healing, or a ready attack with it.
    if (fighting && needsMana && !obs.dead &&
        obs.HpFraction() >= needCfg_.healHpFraction &&
        !client.ActionBusy() && !client.TravelBusy() && !client.Trade().Active() &&
        obs.nowMs < nextActionMs_ && obs.nowMs >= combatMeditationRetryMs_) {
        std::vector<Client::HostileHit> close;
        client.ScanHostiles(2, close);
        if (close.empty()) {
            client.ActionUseSkill(rules::kMeditation);
            combatMeditationRetryMs_ = obs.nowMs + 2000;
            LogLine("mana: combat opening -- retrying Meditation at %d/%d", obs.mana, obs.manaMax);
            return true;
        }
    }
    if (!safe || !needsMana) {
        if (refillingMana_) {
            LogLine("mana: refill %s at %d/%d", safe ? "complete" : "interrupted by danger",
                    obs.mana, obs.manaMax);
            refillingMana_ = false;
            nextActionMs_ = obs.nowMs;
        }
        return false;
    }
    // Finish bounded transfers first; do not strand a customer's trade.
    if (client.Trade().Active()) return false;
    if (client.ActionBusy()) return true;
    if (!refillingMana_) {
        const auto goal = planner_.Current().kind;
        LeaveGoal(client, goal, GoalKind::IdleBriefly, false, "refill mana while safe");
        client.ForgetCraftMenu(); // reopen after mana changes the server's filtered recipes
        if (planner_.Current().active)
            planner_.Finish(false, "refill mana while safe", obs.nowMs);
        if (client.TravelBusy()) client.TravelAbort("refill mana while safe");
        travelInFlight_ = false;
        refillingMana_ = true;
        manaLastSeen_ = obs.mana;
        manaRetryMs_ = obs.nowMs;
        LogLine("mana: safe at %d/%d -- meditating until full", obs.mana, obs.manaMax);
    }
    // Do not restart a successful meditation as each mana point arrives.
    if (obs.mana > manaLastSeen_) manaRetryMs_ = obs.nowMs + 12000;
    manaLastSeen_ = obs.mana;
    if (obs.nowMs >= manaRetryMs_) {
        client.ActionUseSkill(rules::kMeditation);
        manaRetryMs_ = obs.nowMs + 12000;
    }
    return true;
}

// CASTER PRESENCE, WITHOUT A BUFF-ICON PROTOCOL.
//
// Sphere puts Night Sight, Reactive Armor, Protection, Bless and Magic
// Reflection on effect layers, but this client has no packet that reports the
// remaining time on those layers.  Recasting every tick would waste reagents;
// waiting for an unknown expiry would leave the character bare.  Their shard
// durations are at least two minutes (and three for all but Bless), so one
// legal, affordable spell every 75 seconds keeps a rolling set of effects
// while leaving mana to react to an actual fight.
bool Runner::MaintainCasterBuffs(Client& client, const Observation& obs) {
    const prof::Profession* p = needCfg_.profession;
    if (!p || (p->id != "mage" && p->id != "warlock")) return false;
    if (obs.dead || obs.underAttack || obs.attackersOnMe > 0 ||
        obs.hostilesNear > 0 || obs.HpFraction() < needCfg_.healHpFraction ||
        obs.manaMax <= 0 || client.ActionBusy() || client.TravelBusy() ||
        client.Trade().Active() || obs.nowMs < casterBuffRetryMs_ ||
        obs.spellbookSerial == 0)
        return false;

    spell::LoadSpellTable(client.DataDir());
    static constexpr const char* kBuffs[] = {
        "s_night_sight", "s_reactive_armor", "s_protection", "s_bless",
        "s_magic_reflection"
    };
    constexpr usize kBuffCount = sizeof(kBuffs) / sizeof(kBuffs[0]);
    // Leave enough mana for a Heal/Cure response or a retreat spell after a
    // cosmetic or defensive refresh; no buff is worth entering a fight empty.
    const i32 manaReserve = std::max<i32>(8, obs.manaMax / 4);
    for (usize offset = 0; offset < kBuffCount; ++offset) {
        const usize index = (casterBuffCursor_ + offset) % kBuffCount;
        const spell::SpellDef* pick = nullptr;
        for (const spell::SpellDef& d : spell::SpellTable()) {
            if (std::strcmp(d.defname, kBuffs[index]) == 0) {
                pick = &d;
                break;
            }
        }
        if (!pick || pick->unknownFlags ||
            obs.SkillTenths(rules::kMagery) < pick->minSkillTenths ||
            obs.mana < pick->mana || obs.mana - pick->mana < manaReserve ||
            !BookHasSpell(client, obs.spellbookSerial, pick->spell))
            continue;
        bool supplied = true;
        for (const char* reagent : pick->reagents) {
            if (!reagent) break;
            if (market::QtyOf(obs.pack, reagent) < 1) {
                supplied = false;
                break;
            }
        }
        if (!supplied) continue;

        casterBuffCursor_ = (index + 1) % kBuffCount;
        constexpr i64 kCasterBuffRefreshMs = 75 * 1000;
        casterBuffRetryMs_ = obs.nowMs + kCasterBuffRefreshMs;
        LogLine("caster_buff: cast=%s mana=%d/%d", pick->name, obs.mana,
                obs.manaMax);
        client.ActionCastSpell(pick->spell, client.PlayerSerial());
        return true;
    }
    // The book, skill, mana or reagent state can change on the next tick;
    // only throttle a successful attempt, never a temporarily unavailable one.
    return false;
}


// --- survival --------------------------------------------------------------
//
// SurvivalTick already owns potion / bandage / disengage, proven live in
// M3.9.1. This goal adds the two things a tick-level policy cannot decide:
// whether to fight back at all, and where to go when the answer is no.

bool Runner::ProcessHuntAftermath(Client& client, const Observation& obs) {
    if (currentFoe_) {
        const u32 corpse = client.CorpseOfMobile(currentFoe_);
        if (corpse) {
            ++session_.kills;
            chatKillMs_ = obs.nowMs;       // small talk: a word after the kill
            // The trip's owner gets the credit, not whichever goal happens
            // to hold the slot during the fight. See Runner::huntKillsPending_.
            ++huntKillsPending_;
            const bool cheap = obs.HpFraction() >= 0.75;
            state_.memory.NoteCreatureOutcome(currentFoeName_.c_str(),
                cheap ? kCreatureEvidenceCheapKill : kCreatureEvidenceCostlyKill, obs.nowMs);
            // AND THE GROUND GETS THE SAME VERDICT. Heat that only ever rose
            // shut a novice out of the one yard it was ready for after a
            // WINNING session (D11, artifacts/validation_wave_2026-09-06.md:
            // Aurelius 1.38 -> 3.20 with one kill and no deaths). A kill is
            // this character's own evidence that the place is workable, worth
            // as much relief as it is worth exoneration for the creature.
            state_.memory.CoolDanger(obs.x, obs.y,
                cheap ? -kCreatureEvidenceCheapKill : -kCreatureEvidenceCostlyKill,
                obs.nowMs);
            state_.memory.NoteEvent("confirmed_kill", currentFoeName_.c_str(), "",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("hunt: confirmed kill target='%s' corpse=0x%08X", currentFoeName_.c_str(), corpse);
            huntLootCorpse_ = corpse;
            huntLootFailures_ = 0;
            currentFoe_ = 0;
        }
    }
    if (!huntLootCorpse_) return false;
    if (obs.dead) { huntLootCorpse_ = 0; return false; }
    // Never open a corpse instead of dealing with an attacker.
    if (obs.underAttack || obs.attackersOnMe > 0) return false;
    // Let the normal healing/food goals work, retaining the known corpse.
    if (obs.HpFraction() < needCfg_.healHpFraction) return false;
    if (obs.nowMs < huntLootNextMs_) return true;
    if (client.ActionBusy()) return true;
    if (huntLootMovePending_) {
        huntLootMovePending_ = false;
        if (client.ActionResult() == act::Result::Success) {
            huntLootFailures_ = 0;
            // ONE CORPSE IS NOT A BANK RUN. Castor killed a single skeleton at
            // Britain graveyard, looted it and left for the bank at once, then
            // walked back for the next one (g_Castor 2026-09-05 01:39) -- a
            // kill/loot/bank/return loop that spends the session on the road.
            // A player stays in the yard and banks when the pack is getting
            // heavy or the coin on hand is worth more than the risk of losing
            // it -- "worth" measured against this character's own wealth, not
            // a global constant.
            const i32 secureCoin = std::max(needCfg_.goldFloor, obs.gold / 10);
            const bool heavy = obs.WeightFraction() >= BankWeightLine(needCfg_);
            const bool richPack = obs.goldOnHand >= secureCoin;
            if (heavy || richPack) {
                state_.huntReturnPending = true;
                LogLine("hunt: loot transfer confirmed; bank return required "
                        "(load=%.0f%% coin=%d/%d)",
                        obs.WeightFraction() * 100, obs.goldOnHand, secureCoin);
            } else {
                LogLine("hunt: loot transfer confirmed; staying in the yard "
                        "(load=%.0f%% coin=%d/%d)",
                        obs.WeightFraction() * 100, obs.goldOnHand, secureCoin);
            }
        } else ++huntLootFailures_;
    }
    i32 x = 0, y = 0;
    const bool visible = client.WorldItemPosition(huntLootCorpse_, &x, &y);
    std::vector<Client::HostileHit> nearby;
    client.ScanHostiles(combat::kCrowdRadius, nearby);
    const bool heavy = obs.WeightFraction() >= BankWeightLine(needCfg_);
    if (heavy) state_.huntReturnPending = true;
    if (!visible || huntLootFailures_ >= 3 || heavy || nearby.size() >= 3) {
        LogLine("hunt: ending loot attempt visible=%d failures=%d hp=%.0f%% load=%.0f%%",
                visible ? 1 : 0, huntLootFailures_, obs.HpFraction()*100, obs.WeightFraction()*100);
        huntLootCorpse_ = 0;
        Checkpoint(client, obs.nowMs, "hunt loot attempt ended");
        return false;
    }
    if (TileDist(obs.x, obs.y, x, y) > 2) {
        if (!client.TravelBusy() && !client.GotoBusy()) {
            ++huntLootFailures_;
            client.TravelToPoint(x, y, 2, "loot confirmed hunt corpse");
        }
        huntLootNextMs_ = obs.nowMs + 2000;
        return true;
    }
    client.EnsurePeaceMode();
    if (!client.ContainerKnown(huntLootCorpse_)) {
        ++huntLootFailures_;
        client.ActionOpenContainer(huntLootCorpse_);
        huntLootNextMs_ = obs.nowMs + 2000;
        return true;
    }
    if (client.ContainerItemCount(huntLootCorpse_) == 0) {
        LogLine("hunt: corpse emptied; %s", state_.huntReturnPending
            ? "returning to bank" : "ready for next target");
        huntLootCorpse_ = 0;
        Checkpoint(client, obs.nowMs, "hunt loot complete");
        return false;
    }
    u32 item = 0; u16 graphic = 0, amount = 0;
    if (client.ContainerItemAt(huntLootCorpse_, 0, &item, &graphic, &amount)) {
        client.ActionMoveItem(item, amount ? amount : 1, client.BackpackSerial());
        huntLootMovePending_ = true;
        huntLootNextMs_ = obs.nowMs + 1000;
    }
    return true;
}

// THE ZONE IS THE WEAPON. Inside a guarded region a player in trouble does
// not out-walk the thing chasing him -- he shouts, and Sphere's guardcall
// keyword answers: `guardcall GUARD,GUARDS`
// (runtime/scripts/core/defs.scp:16), with GuardsInstantKill=1 on this shard.
// Odessa was killed by c_lizardman_mace INSIDE a_townBritain at 00:38:38 on
// 2026-09-06, sixteen seconds after the client logged "You are now under the
// protection of the city guards", having called nobody (D14,
// artifacts/validation_wave_2026-09-06.md).
//
// Returns true when the guards were the answer -- i.e. the caller is standing
// in the protection it needs and should not also run.
bool Runner::CallGuardsIfProtected(Client& client, const Observation& obs) {
    const wm::Region* here = client.CurrentRegion();
    if (!here || !here->flags.guarded) return false;
    // Once every fifteen seconds, not once per tick: a shout is a packet and
    // a repeated one is the same retry-inside-its-own-deadline fault the
    // resurrect, bank and vendor asks all had.
    constexpr i64 kGuardCallIntervalMs = 15000;
    if (lastGuardCallMs_ != 0 &&
        obs.nowMs - lastGuardCallMs_ < kGuardCallIntervalMs) return true;
    lastGuardCallMs_ = obs.nowMs;
    // The keeper only reaches this call for an active attack, or for a badly
    // hurt retreat that still has a hostile in sight.  A stranger who merely
    // enters the scan is not an emergency and must not create town-wide guard
    // spam.
    LogLine("interrupt=GUARDS reason=\"at %.0f%% HP inside %s with %d "
            "hostile(s) in sight (%d on me) -- calling the guards\"",
            obs.HpFraction() * 100.0, here->name.c_str(), obs.hostilesNear,
            obs.attackersOnMe);
    client.ActionSay("Guards");  // bare word: Source-X FindStrWord rejects a trailing "!" (CClientEvent.cpp:1861)
    return true;
}

// THE SHOUT HAS TO OUTLIVE THE DECISION THAT FIRST MADE IT.
//
// CallGuardsIfProtected is reached from exactly two places -- the near-death
// flee arm (below) and DecideHeal's rest arm -- and both of them are decided
// ONCE, on the tile the character is standing on at that moment. A retreat
// that starts OUTSIDE a guard zone therefore asks "can the guards hear me?",
// is told no, and never asks again, including for the whole stretch after it
// crosses the town line. Tordor, 2026-09-06 03:12:17-03:13:09
// (run_gates fleet_ramp_20260906/Tordor.console.txt:1218-1510): interrupt=
// FLEE_TO_GUARDS at 40% HP with three hostiles; forty seconds later the
// client printed "You are now under the protection of the city guards"; twelve
// seconds after that he was dead at 1447,1528, INSIDE a_townBritain, having
// never said the word. Sphere summons guards on the spoken keyword and on
// nothing else (Source-X CClientEvent.cpp:1871), so the zone he reached was
// worth exactly what he asked of it.
//
// So the shout is a per-tick keeper, not a branch: every tick, under whatever
// goal, while the character is actively attacked -- or while a badly hurt
// survival retreat still has a hostile in sight -- if this tile is guarded,
// call. The 15 s throttle inside CallGuardsIfProtected bounds legitimate
// retries; a visible-but-peaceful character never enters this path.
void Runner::KeepCallingGuards(Client& client, const Observation& obs) {
    if (!obs.inWorld || obs.dead) return;
    const bool underActiveAttack = obs.underAttack || obs.attackersOnMe > 0;
    // Retain the town-line rescue case without letting a stale retreat flag
    // repeatedly summon guards.  The character must be substantially hurt
    // and the pursuer must still be observable.
    const bool woundedRetreatWithPursuer =
        survivalRetreat_ && obs.HpFraction() <= 0.50 && obs.hostilesNear > 0;
    if (!underActiveAttack && !woundedRetreatWithPursuer) return;
    CallGuardsIfProtected(client, obs);
}

void Runner::RetreatToSafety(Client& client) {
    client.EnsurePeaceMode();
    if (!survivalRetreat_) {
        client.TravelAbort("survival retreat supersedes the old errand");
        travelInFlight_ = false;
        survivalRetreat_ = true;
    }
    if (!client.TravelBusy()) client.TravelToService(wm::Service::Banker, nullptr);
}

bool Runner::ContinueSurvivalRetreat(Client& client, const Observation& obs) {
    if (!survivalRetreat_) return false;

    // A new hostile observation still gets the full survival decision. That
    // branch may call guards, fight defensively, or deliberately abort the
    // journey to make room for an urgent defensive cast.
    if (obs.underAttack || obs.attackersOnMe > 0 || obs.hostilesNear > 0) {
        DoSurvive(client, obs);
        return true;
    }

    // Attack attribution and hostile scans do not clear in lockstep. Once a
    // flee has started, the first quiet observation is evidence to continue
    // toward safety, not permission for a newly picked errand to cancel it.
    // RunGoal checks its exhaustion bound before this helper, so a route that
    // is genuinely stuck remains bounded by the active goal's normal limit.
    if (client.TravelBusy() || client.GotoBusy()) {
        client.EnsurePeaceMode();
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2000;
        return true;
    }

    // A finished route is safe only when it actually arrived. A failed route
    // gets another normal retreat attempt; progress versus attempt accounting
    // keeps the existing planner bound effective when no route can be made.
    const wm::Region* here = client.CurrentRegion();
    const bool safeHere = (here && here->flags.guarded) ||
        (client.BankContainer() && client.BankOpenTileHeld());
    if (!client.TravelSucceeded() && !safeHere) {
        survivalRetreat_ = false;
        RetreatToSafety(client);
        if (client.TravelBusy()) planner_.NoteProgress();
        else planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return true;
    }

    survivalRetreat_ = false;
    return false;
}

bool Runner::DoSurvive(Client& client, const Observation& obs) {
    if (obs.dead) {
        // ASK ONCE AND WAIT. Resurrection is driven by the world -- a healer
        // walking over, a shrine -- so its deadline is fifteen minutes
        // (kResurrectTimeoutMs). Re-announcing the ghost every three seconds
        // superseded the outstanding request every single time:
        //
        //   resurrect invalid_state took=3056ms superseded
        //   [ACTION] resurrect start
        //
        // for as long as the character stayed dead (run_m5/r1warrior.console
        // .txt, the first death this project has ever recorded). Same
        // retry-inside-its-own-deadline fault as the bank, the vendor and the
        // trainer -- and it survived undetected precisely because nothing had
        // ever died to exercise it. "Never fired" and "broken" look identical.
        // AND GO AND FIND A HEALER, because waiting does not work.
        //
        // ActionResurrectAccept only ANSWERS an offer the server has already
        // made. A ghost standing in a field is never offered anything, so the
        // previous version waited out a fifteen-minute deadline, failed the
        // goal, was re-picked and waited again -- for the whole session.
        // Kaelen died in a graveyard and LOGGED IN STILL DEAD on the next run
        // (run_m5/r2a.console.txt: "needs considered: StayAlive(resurrection
        // 1.00)" and nothing else, forever). That is the ghost trap the
        // project already knew about from the other end: a character that dies
        // somewhere hostile silently fails every later session.
        //
        // WHAT KILLED US, recorded once. DeathRecord carries where and when
        // but not who (PersonalKnowledge.h:44-52), so the only witness is the
        // foe we were last fighting. Death is the strongest evidence a
        // creature type can give about itself, and without this the warrior
        // would walk back to the same lich next session having learned only
        // that the graveyard is dangerous -- which it already knew.
        if (!deathBlamed_) {
            deathBlamed_ = true;
            ghostTrips_ = 0;
            ghostHealerAvoid_.clear();
            ghostHealerTarget_ = 0;
            ghostHealerAsked_ = 0;
            ghostHealerAskMs_ = 0;
            ghostHealerScanAtMs_ = 0;
            // A live corpse serial cannot survive a reconnect, but this
            // location can.  Save it immediately: dying must not make loot
            // recovery depend on keeping the original client process alive.
            state_.memory.NoteEvent("corpse_pending", "recover after resurrection", "",
                                    obs.x, obs.y, obs.nowMs);
            Checkpoint(client, obs.nowMs, "death location recorded");
            // TrackDeathEdge already blamed the last thing that SWUNG at us
            // when it could name one, and that is the better witness: Caelos
            // and Lorys both died on 2026-09-07 while fighting a Zombie, to a
            // lich lord that joined in. Only fall back to "what we were
            // fighting" when nothing swung inside the window.
            if (!deathKillerName_.empty()) {
                LogLine("dead: blaming '%s' -- it is the last thing that swung "
                        "at us", deathKillerName_.c_str());
            } else if (!currentFoeName_.empty()) {
                LogLine("dead: blaming '%s' -- it is what we were fighting",
                        currentFoeName_.c_str());
                state_.memory.NoteCreatureOutcome(currentFoeName_.c_str(),
                                                  kCreatureEvidenceDeath,
                                                  obs.nowMs);
            }
        }

        // A player walks to a healer. So does this.  A resurrection reply is
        // valid only after the healer has offered it; sending one while still
        // travelling creates a busy action and strands the ghost in place.
        if (travelInFlight_ && !client.TravelBusy()) {
            travelInFlight_ = false;
            if (!client.TravelSucceeded() && ghostHealerTarget_) {
                const u32 failedHealer = ghostHealerTarget_;
                ghostHealerTarget_ = 0;
                if (std::find(ghostHealerAvoid_.begin(), ghostHealerAvoid_.end(),
                              failedHealer) == ghostHealerAvoid_.end()) {
                    ghostHealerAvoid_.push_back(failedHealer);
                }
                LogLine("dead: healer 0x%08X could not be reached (%s); trying another",
                        failedHealer, client.TravelFailureText());
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            ghostHealerTarget_ = 0;
            LogLine("dead: arrived at healer destination -- scanning nearby healers");
            client.ActionScanMobiles();
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        if (client.ActionBusy()) return false;

        // The atlas records where a healer normally belongs, but a ghost can
        // meet a wandering healer on the road. Keep requesting paperdolls
        // while travelling, not only after arriving at a fixed landmark. The
        // scan is human-only inside Client, so it cannot mount passing pets.
        constexpr i64 kGhostHealerScanMs = 2500;
        if (obs.nowMs >= ghostHealerScanAtMs_) {
            client.ActionScanMobiles();
            ghostHealerScanAtMs_ = obs.nowMs + kGhostHealerScanMs;
        }

        const u32 healer = client.NearestMobileWithTrade("healer", ghostHealerAvoid_);
        if (healer) {
            i32 hx = 0, hy = 0; i8 hz = 0;
            // Healers commonly stand behind a counter or in a small room.
            // The interaction range is wider than adjacency, so path to an
            // accessible tile in that range instead of attempting to occupy
            // the NPC's sealed tile through the wall.
            // Sphere's NPC_LookAtCharHealer requires distance <= 3.
            constexpr i32 kHealerReach = 2;
            if (client.MobilePosition(healer, &hx, &hy, &hz)) {
                // A visible healer is better evidence than an atlas point.
                // Replace a fixed-service trip (or a trip toward another
                // healer) immediately; the Client journey keeps the chosen
                // healer's stand tile current when it wanders.
                if (client.TravelBusy() && ghostHealerTarget_ != healer) {
                    client.TravelAbort("visible wandering healer supersedes route");
                    travelInFlight_ = false;
                    ghostHealerTarget_ = 0;
                }
            }
            if (client.MobilePosition(healer, &hx, &hy, &hz) &&
                TileDist(obs.x, obs.y, hx, hy) > kHealerReach) {
                // Keep walking.  The old `&& !TravelBusy()` condition fell
                // through to the speech arm while the ghost was still across
                // the room, so Ayuna quite correctly answered "What?" and
                // the incomplete trip looked like a failed resurrection.
                if (!client.TravelBusy()) {
                    LogLine("dead: a healer is here -- getting close enough to be "
                            "raised");
                    travelInFlight_ = client.TravelToEntity(healer, kHealerReach);
                    if (travelInFlight_) ghostHealerTarget_ = healer;
                }
            } else if (client.MobilePosition(healer, &hx, &hy, &hz)) {
                // A healer normally notices a nearby ghost on its own tick,
                // but that is not dependable after reconnecting into a busy
                // room. Ask it as a player would. Sphere's healer verb is the
                // bare "resurrect" command; a paperdoll-addressed sentence
                // reaches the NPC as ordinary speech and receives "What?".
                // The throttle leaves time for the server's reply.
                constexpr i64 kGhostHealerAskMs = 8000;
                if (ghostHealerAsked_ != healer ||
                    obs.nowMs - ghostHealerAskMs_ >= kGhostHealerAskMs) {
                    LogLine("dead: asking healer 0x%08X to resurrect us", healer);
                    client.ActionSay("resurrect");
                    ghostHealerAsked_ = healer;
                    ghostHealerAskMs_ = obs.nowMs;
                }
            }
            nextActionMs_ = obs.nowMs + 4000;
            return false;
        }
        if (!client.TravelBusy() && !travelInFlight_) {
            if (++ghostTrips_ > kMaxGhostTrips) {
                LogLine("dead: %d trips and no healer found; still a ghost",
                        ghostTrips_ - 1);
                ghostTrips_ = 0;
                nextActionMs_ = obs.nowMs + 30000;
                return false;
            }
            LogLine("dead: walking to a healer (trip %d)", ghostTrips_);
            travelInFlight_ =
                client.TravelToService(wm::Service::Healer, nullptr);
        }
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Alive again: the next death is a new death, and a new verdict.
    deathBlamed_ = false;
    deathKillerName_.clear();
    ghostTrips_ = 0;
    ghostHealerAvoid_.clear();
    ghostHealerTarget_ = 0;
    ghostHealerAsked_ = 0;
    ghostHealerAskMs_ = 0;
    ghostHealerScanAtMs_ = 0;

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    if (hostiles.empty()) {
        // The last attacker can leave sight before the attack observation
        // expires. Keep the retreat alive until both sources say it is safe.
        if (obs.underAttack || obs.attackersOnMe > 0) {
            RetreatToSafety(client);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        survivalRetreat_ = false;
        currentFoe_ = 0;
        dangerWatchHp_ = -1;    // the next fight is watched from its own start
        client.EnsurePeaceMode();
        return true;   // the danger passed
    }

    // Remember where this went badly -- ONCE PER FIGHT, not once per tick.
    // Per-tick notes are how one twenty-minute stalemate compounded a single
    // wolf into heat 499.89 and made the whole forest look lethal. The
    // once-a-minute cadence is that anti-stalemate guard and stays.
    //
    // WHAT THE MINUTE WRITES IS HARM, NOT TIME. A flat 0.5 per minute of any
    // fight heated a ground the character was WINNING exactly as fast as one
    // that was killing it: Aurelius went 1.38 -> 3.20 at the Britain
    // graveyard with one kill and zero deaths and was then refused the yard
    // (D11, artifacts/validation_wave_2026-09-06.md). What a player actually
    // remembers about a spot is how much it cost, so the note is the damage
    // taken during that minute as a fraction of this character's own maximum
    // health -- half your life in a minute writes 0.5, an unscratched minute
    // writes nothing at all.
    if (dangerWatchHp_ < 0) dangerWatchHp_ = obs.hp;
    if (obs.nowMs - lastDangerNoteMs_ > 60000) {
        const i32 hurt = std::max(0, dangerWatchHp_ - obs.hp);
        lastDangerNoteMs_ = obs.nowMs;
        dangerWatchHp_ = obs.hp;
        if (hurt > 0 && obs.hpMax > 0)
            state_.memory.NoteDanger(obs.x, obs.y, 14, hostiles.front().name.c_str(),
                                     static_cast<double>(hurt) /
                                         static_cast<double>(obs.hpMax),
                                     obs.nowMs);
    }

    CombatStrategyId strategy = needCfg_.profession
        ? needCfg_.profession->combatStrategy : CombatStrategyId::Melee;
    const int attackSpell = strategy == CombatStrategyId::Mage
        ? PickSurvivalSpell(client, obs, false) : -1;
    // A defensive spell can race the target's movement just like an opening
    // hunt cast.  Treat the server rejection as authoritative and remove that
    // foe from this short fight window, so the next decision selects another
    // attacker or retreats instead of casting through the same wall again.
    if (strategy == CombatStrategyId::Mage) {
        // A later action may have replaced the rejected cast before this
        // survival tick.  Consume the packet-time target first so a hostile
        // behind cover cannot monopolize a defensive fight.
        u32 refused = client.TakeSpellReachRefusalTarget();
        if (refused == 0 && !client.ActionBusy() &&
            client.ActionKind() == act::Kind::CastSpell &&
            client.ActionResult() == act::Result::Rejected)
            refused = client.CurrentAction().destination;
        if (refused != 0 && refused != client.PlayerSerial()) {
            if (casterReachRefusedTarget_ != refused) {
                LogLine("combat: server refused line of sight to 0x%08X -- "
                        "changing target or retreating", refused);
                casterReachRefusedTarget_ = refused;
            }
            MarkUnreachable(refused, obs.nowMs);
            if (huntApproachTarget_ == refused) huntApproachTarget_ = 0;
            if (currentFoe_ == refused) currentFoe_ = 0;
        }
    }
    if (strategy == CombatStrategyId::Mage && attackSpell < 0 && needCfg_.profession) {
        const SchoolWeapon* fallback = SchoolWeaponFor(*needCfg_.profession);
        if (fallback && obs.SkillTenths(fallback->skill) > 0)
            strategy = fallback->skill == rules::kArchery ? CombatStrategyId::Ranged
                                                        : CombatStrategyId::Melee;
    }
    // Pet ownership does not prove a live, controllable combat pet. Until the
    // pet command transport exists, a tamer must retreat instead of melee.
    // A BUILD WITH NO COMBAT SKILL IN ITS PLAN NEVER TRADES BLOWS.
    //
    // combatStrategy is a separate column from the skill plan and the two can
    // disagree: a profession may carry Melee because it once had a weapon
    // target, or AvoidCombat because a table author typed it. The plan cannot
    // disagree with itself -- if nothing in the 700 points buys a way to hurt
    // something, there is no fight here to win. Odessa (merchant_tinker, 50 hp,
    // zero weapon/Magery targets) was killed by a Harpy and three orcs north
    // of Britain on 2026-09-07 (g_Odessa.console.txt:1161-1361).
    const bool noCombatBuild = !BuildFightsAtAll(needCfg_.profession);
    // AN EMPTY MANA BAR IS NOT A PACIFIST. attackSpell is what is castable
    // RIGHT NOW -- book, skill, mana and reagents -- and folding it into the
    // avoid-combat flag made every mage a crafter one cast into his own fight:
    // Aurelius 2026-09-07 23:17:22 cast Harm at a foe he had picked under
    // TRAIN_COMBAT and disengaged one second later at hp=100%, attackers=1,
    // reason "this life avoids combat", with `mana 2/25` the real story
    // (fleet122d30_20260907; seven mages, 62 lines). What decides whether this
    // life avoids combat is the book and the skill -- requireSupplies=false --
    // and the empty pool is handled in the fight arm below as a pause.
    const bool mageKnowsAnAttackSpell =
        strategy != CombatStrategyId::Mage ||
        PickSurvivalSpell(client, obs, false, /*requireSupplies=*/false) >= 0;
    const bool rangedHasAmmo = strategy != CombatStrategyId::Ranged ||
        market::QtyOf(obs.pack, "i_arrow") > 0;
    const bool avoidCombatDisengage = novice::LifeAvoidsCombat(
        !noCombatBuild, strategy, mageKnowsAnAttackSpell, rangedHasAmmo);

    // Same nerve-adjusted threshold Needs.cpp uses for the StayAlive need, so
    // the need model and the flee interrupt agree on when a fight is lost.
    // Before this they disagreed: Needs read riskTolerance, this block read
    // the raw 0.32 -- so a fencer's nerve counted for the planner and not for
    // the retreat.
    double bailAt = needCfg_.fleeHpFraction;
    double nerve = 0.5;
    if (needCfg_.profession) {
        nerve = Nerve(needCfg_);
        bailAt = std::min(0.75, std::max(0.20,
                    needCfg_.fleeHpFraction + (0.5 - nerve) * 0.4));
    }
    const i32 extra = obs.attackersOnMe - 1;
    if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

    // ATTACKERS ARE NOT THE WHOLE BOARD. Three things that can reach us are
    // three things that are about to be attackers, and the moment to leave is
    // before the second one swings, not after the third.
    i32 inReach = 0;
    for (const Client::HostileHit& h : hostiles)
        if (TileDist(h.x, h.y, obs.x, obs.y) <= combat::kCrowdRadius) ++inReach;

    // A PERCENT IS THE WRONG UNIT FOR A RETREAT. What has to be true is that
    // enough health is left to survive the blows that land while breaking
    // contact -- and 22% of a 51-hp bar is 11 hp, under two of them. Hector
    // died twice on 2026-09-06 with the flee interrupt firing on time by its
    // own rule and one second too late by the clock: 02:16 "HP 25%; 2
    // attacker(s); bail at 30%", dead 1 s later; 23:38 last seen at 21/51
    // (41%) with "bail at 22%", dead 3.1 s later
    // (artifacts/novice_engagement_2026-09-06.md).
    //
    // So the line is also expressed in expected-hits-to-live: 8 hp per landed
    // blow, two blows of margin, per hostile that can reach us. This can only
    // move the bail line UP -- max(), never min() -- so no character that
    // survives today is made bolder by it. For a 100-hp veteran with one
    // attacker it is 16% and the nerve line above still wins.
    // Company raises the margin by one blow, not by the whole yard --
    // NoviceEngage.h RetreatBoard explains why max(attackers, inReach) was
    // the in-reach veto wearing a health bar.
    i32 support = 0;
    for (const auto& h : hostiles) {
        support = std::max(support, HuntSupport(client, obs, h.serial));
        if (client.IsAttackingMe(h.serial) && client.KnownPlayer(h.serial) && !h.name.empty()) {
            bool recorded = false;
            for (const auto& event : state_.memory.Events())
                if (event.kind == "attacked_by_player" && event.detail == h.name) recorded = true;
            if (!recorded)
                state_.memory.NoteEvent("attacked_by_player", h.name.c_str(), "combat",
                                       obs.x, obs.y, obs.nowMs);
        }
    }
    const i32 board = static_cast<i32>(
        novice::RetreatBoard(static_cast<int>(obs.attackersOnMe),
                             std::max(obs.attackersOnMe, inReach - support)));
    const double retreatFloor =
        novice::RetreatFloorFraction(static_cast<int>(board), obs.hpMax);
    const bool floorBinds = retreatFloor > bailAt;
    bailAt = std::max(bailAt, retreatFloor);

    // How many adjacent attackers this life will stand in before it breaks
    // contact regardless of health. The old rule was a flat two, and a
    // graveyard hands a warrior three at once: Faustus (macer, 2026-09-03)
    // landed one accepted attack on a skeleton and fled at 98% HP with
    // "3 attacker(s)", every session, zero kills. Nerve decides the crowd a
    // character tolerates -- a fencer at 0.75 stands in four, a smith at
    // 0.35 in two, a fisher at 0.20 in one -- and the HP bail above, which
    // already rises per extra attacker, is what actually protects the life.
    //
    // NERVE STILL SETS THE FLOOR, BUT NOT THE CEILING (owner rule,
    // 2026-09-04): "one target at a time; don't fight where 3+ hostiles are
    // within reach." A fencer at 0.75 nerve tolerated four attackers, which is
    // how Hector met a lich, a skeletal knight, a zombie and a skeleton in the
    // same eighteen seconds and died (run_gates/g_Hector.console.txt:977-1017).
    // A timid character still breaks contact sooner; a bold one no longer
    // stands in a pack.
    // A NOVICE STANDS IN ONE, whatever its nerve says. Both 2026-09-06 deaths
    // were groups a bold fencer's tolerance of two let him stay in: the second
    // attacker is the moment to leave, not 25% health.
    const bool novicePolicy =
        novice::IsNovice(static_cast<int>(BestWeaponSkillTenths(obs)), obs.hpMax);
    const i32 crowdTolerated =
        novicePolicy ? novice::GroupAttackerLimit(support) : std::min(2, 1 + static_cast<i32>(nerve * 4.0));
    // Hostile attribution can lag the visible world by a tick.  A damaged
    // character boxed in by a graveyard pack must leave on that evidence;
    // waiting for all of them to become named attackers was the last two
    // seconds of several fleet deaths.
    const bool woundedCrowd = obs.HpFraction() <= std::max(bailAt, 0.70) &&
        inReach >= 2 && obs.hostilesNear >= 3;
    // A normal bank errand has no claim on a character who is already hurt
    // and under attack.  Convert it to the owned survival retreat before the
    // fight path can cancel the journey to cast or swing.
    const bool bankRunInterrupted = !survivalRetreat_ && client.TravelBusy() &&
        (obs.underAttack || obs.attackersOnMe > 0) &&
        (obs.HpFraction() <= 0.75 || inReach >= 2);
    // A FIGHT ENDS ON ATTACKERS OR ON HEALTH, NOT ON BYSTANDERS (owner ruling,
    // 2026-09-07, "novice rule too strict"). The 3+ ceiling
    // (novice::kNoviceCrowdCeiling) still refuses to OPEN on that board -- it
    // is enforced where the prey is picked, Train.cpp NoviceMayOpen -- but it
    // no longer breaks off a duel already under way. Hector, 2026-09-07:
    // three engagements at 01:11-01:15, every one ended
    // `disengage=yes attackers=1 in_reach=3 ... "3+ hostiles within reach"` at
    // 100% health with the target still alive, and the session killed nothing
    // (g_Hector.console.txt:518,539,946,967). in_reach is still counted, still
    // logged, and still buys a blow of retreat margin through RetreatBoard.
    if (bankRunInterrupted || woundedCrowd ||
        novice::ShouldBreakContact(avoidCombatDisengage, obs.attackersOnMe,
                                   static_cast<int>(crowdTolerated),
                                   obs.HpFraction(), bailAt)) {
        LogLine("interrupt=FLEE reason=\"%sHP %.0f%%; %d attacker(s); bail at %.0f%% "
                "(retreat floor %.0f%% for %d on the board%s)\"",
                bankRunInterrupted ? "bank trip interrupted: " :
                woundedCrowd ? "wounded hostile crowd: " : "",
                obs.HpFraction() * 100.0, obs.attackersOnMe, bailAt * 100.0,
                retreatFloor * 100.0, board, floorBinds ? ", binding" : "");
        LogLine("disengage=yes attackers=%d in_reach=%d tolerate=%d novice=%d "
                "hp=%.0f%% bandages=%d reason=\"%s\"",
                obs.attackersOnMe, inReach, crowdTolerated, novicePolicy ? 1 : 0,
                obs.HpFraction() * 100.0, obs.bandages,
                noCombatBuild             ? "this build plans no combat skill"
                : !mageKnowsAnAttackSpell ? "no attack spell this book and this "
                                            "Magery can cast"
                : !rangedHasAmmo          ? "no arrows left to shoot with"
                : avoidCombatDisengage    ? "this life avoids combat"
                : bankRunInterrupted        ? "bank journey crossed an active attacker"
                : woundedCrowd              ? "hurt among several nearby hostiles"
                : obs.attackersOnMe > crowdTolerated ? "more attackers than "
                                                       "this nerve stands in"
                : floorBinds              ? "not enough health left to walk out "
                                            "of reach"
                                          : "health below the bail line");
        client.EnsurePeaceMode();
        dangerWatchHp_ = -1;
        // Once per fight, not once per tick -- same guard as the note
        // above (S2_WIRING_PLAN.md review finding 4). This is now also
        // where the AvoidCombat arm's danger note lands, since it always
        // falls through into this block.
        if (obs.nowMs - lastDangerNoteMs_ > 60000) {
            lastDangerNoteMs_ = obs.nowMs;
            state_.memory.NoteDanger(obs.x, obs.y, 18, hostiles.front().name.c_str(),
                                     1.5, obs.nowMs);
        }
        // AND WHAT IT WAS, not just where it happened. A place cannot un-scare
        // you, but a creature type can prove itself safe or dangerous, and
        // "learn which graveyard mobs are safe and which are dangerous" is the
        // owner's warrior loop. Fleeing at low health from THIS thing is the
        // strongest evidence short of dying to it.
        state_.memory.NoteCreatureOutcome(hostiles.front().name.c_str(),
                                          kCreatureEvidenceNearDeathFlee,
                                          obs.nowMs);
        // These are the creatures we just decided not to fight.  Without a
        // temporary exclusion, the next SURVIVE tick re-selected one as soon
        // as the attacker count flickered from two to one and cancelled the
        // retreat by attacking again.
        for (const Client::HostileHit& h : hostiles)
            MarkUnreachable(h.serial, obs.nowMs);
        if (!state_.memory.HasEvent("first_near_death")) {
            state_.memory.NoteEvent("first_near_death", hostiles.front().name.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
        }
        CallGuardsIfProtected(client, obs);

        // Two attackers remain a hard novice stop.  A healthy trainee should
        // not, however, cross the whole world to a bank just to resume the
        // same weak hunting ground thirty seconds later.  Move it to the
        // patrol lane furthest from the present attackers, and let the normal
        // survival-retreat keeper own that journey.  If the move cannot be
        // started (or danger persists after it arrives), the existing banker
        // retreat takes over unchanged.
        // SURVIVE preempts TRAIN_COMBAT the instant a hostile answers, so the
        // planner's current slot is usually Survive precisely when this code
        // needs to preserve the hunt. `currentFoe_` is the fight ownership
        // TrainCombat recorded when it opened the target; keep that evidence
        // alongside the direct goal check instead of sending a healthy novice
        // to the bank merely because survival correctly preempted it.
        const bool combatTraining =
            planner_.Current().kind == GoalKind::TrainCombat || currentFoe_ != 0;
        if (combatTraining && huntCrowdMoves_ >= 2) {
            ++huntGroundRotation_;
            huntCrowdMoves_ = 0;
            LogLine("hunt: repeated crowded pulls -- next outing uses another graveyard");
            RetreatToSafety(client);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        if (novice::MayRepositionWithinHunt(novicePolicy, combatTraining,
                                            obs.attackersOnMe, crowdTolerated,
                                            obs.HpFraction())) {
            const auto* atlas = client.WorldAtlas();
            const auto* ground = atlas
                ? atlas->NearestHuntingGroundOfTier(world_atlas::HuntTier::Weak,
                                                     obs.x, obs.y, 64)
                : nullptr;
            if (ground) {
                const auto points = atlas->HuntingPatrol(*ground);
                const wm::Point* safest = nullptr;
                i32 bestFoeDistance = -1;
                i32 bestTravelDistance = -1;
                for (const wm::Point& point : points) {
                    const i32 travelDistance = TileDist(obs.x, obs.y, point.x, point.y);
                    if (travelDistance < 5) continue;
                    i32 nearestFoe = 0;
                    for (const Client::HostileHit& h : hostiles) {
                        const i32 d = TileDist(point.x, point.y, h.x, h.y);
                        if (nearestFoe == 0 || d < nearestFoe) nearestFoe = d;
                    }
                    if (!safest || nearestFoe > bestFoeDistance ||
                        (nearestFoe == bestFoeDistance &&
                         travelDistance > bestTravelDistance)) {
                        safest = &point;
                        bestFoeDistance = nearestFoe;
                        bestTravelDistance = travelDistance;
                    }
                }
                if (safest) {
                    client.TravelAbort("healthy novice changing crowded hunt lane");
                    travelInFlight_ = client.TravelToPoint(
                        safest->x, safest->y, 2, "safe novice hunt lane");
                    if (travelInFlight_) {
                        ++huntCrowdMoves_;
                        survivalRetreat_ = true;
                        LogLine("hunt: novice crowd at %d,%d; changing to safe lane %d,%d "
                                "(nearest attacker %d tiles from destination)",
                                obs.x, obs.y, safest->x, safest->y, bestFoeDistance);
                        nextActionMs_ = obs.nowMs + 2000;
                        planner_.NoteProgress();
                        return false;
                    }
                }
            }
        }
        RetreatToSafety(client);
        nextActionMs_ = obs.nowMs + 2000;
        // A RETREAT THAT IS MOVING IS NOT A FAILED ATTEMPT.
        //
        // This arm ticks every 2 s, so five plain NoteAttempt calls exhausted
        // SURVIVE ten seconds into every flight: Odessa went
        // SURVIVE -> "previous goal abandoned: attempts 5 >= 5" -> BANK ->
        // SURVIVE (emergency preempt) three times in forty seconds
        // (g_Odessa.console.txt:648,659,687,698,727), and each handover
        // aborted and re-planned the escape route she was standing in.
        //
        // Ground covered is the progress; a retreat with no route is still an
        // attempt, so a genuinely stuck flee still exhausts on schedule and
        // the goal's own time limit still bounds both.
        if (client.TravelBusy()) planner_.NoteProgress();
        else planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Once an escape owns the journey, survival never abandons it just to
    // exchange another spell or swing.  This is especially important for
    // mages: the old defensive branch cancelled a banker retreat and stood in
    // the cemetery again whenever one attacker remained visible.
    if (survivalRetreat_) {
        client.EnsurePeaceMode();
        for (const Client::HostileHit& h : hostiles)
            MarkUnreachable(h.serial, obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Fight back at whatever is actually on us. Never pick a NEW fight here:
    // this goal exists because something already started one.
    const Client::HostileHit* target = nullptr;
    // The attacker named by the shard wins over the foe we happened to be
    // hunting.  In a graveyard a stale hunt target can be behind a wall or
    // across the pack; casting at it produced an immediate line-of-sight
    // refusal while the Zombie already hitting us went unanswered.
    for (const Client::HostileHit& h : hostiles) {
        if (IsUnreachable(h.serial, obs.nowMs)) continue;
        if (!client.IsAttackingMe(h.serial)) continue;
        if (!target || TileDist(h.x, h.y, obs.x, obs.y) <
                           TileDist(target->x, target->y, obs.x, obs.y)) {
            target = &h;
        }
    }
    if (!target) {
        for (const Client::HostileHit& h : hostiles) {
            if (IsUnreachable(h.serial, obs.nowMs)) continue;
            if (h.serial == currentFoe_) { target = &h; break; }
            if (!target || TileDist(h.x, h.y, obs.x, obs.y) <
                               TileDist(target->x, target->y, obs.x, obs.y)) {
                target = &h;
            }
        }
    }
    if (!target) {
        RetreatToSafety(client);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    const i32 dist = TileDist(target->x, target->y, obs.x, obs.y);
    HuntSupport(client, obs, target->serial, true);

    if (strategy == CombatStrategyId::Mage) {
        // Defensive casts at a lawful hostile, from actual book/reagents.
        // Never turn a mage into a sword user when its spell is unavailable.
        // Point-blank is where a defensive fight IS: the thing hitting us is
        // adjacent by definition, and a mage that only ever backs away from
        // it never casts once (Aurelius, 2026-09-05: skeleton at 1 tile for
        // two minutes, zero casts). Retreat when hurt or when there is
        // nothing to cast; otherwise cast at it from where we stand.
        if (attackSpell < 0) {
            // NOTHING CASTABLE THIS SECOND. If the pool is the only thing
            // missing -- the reagents for a rung are in the pack -- this is a
            // pause in the fight, not the end of it: mana returns in seconds,
            // the ladder walks back down to whatever rung it will pay for, and
            // the foe stays selected. Walking away here is what turned "cast
            // one spell and go somewhere else" into the whole mage session.
            // Health is not at issue: the bail line above already ran, so we
            // are above it, and it runs again on every tick of this wait.
            Observation full = obs;
            full.mana = full.manaMax > 0 ? full.manaMax : full.mana + 100;
            const bool manaOnly = PickSurvivalSpell(client, full, false) >= 0;
            if (manaOnly) {
                currentFoe_ = target->serial;
                currentFoeName_ = target->name;
                LogLine("hold=wait_for_mana foe=%s mana=%d/%d hp=%.0f%%",
                        target->name.empty() ? "a hostile" : target->name.c_str(),
                        obs.mana, obs.manaMax, obs.HpFraction() * 100.0);
                nextActionMs_ = obs.nowMs + 3000;
                return false;
            }
            // Out of reagents: this fight cannot be paid for at all.
            RetreatToSafety(client);
        } else if (!client.ActionBusy()) {
            currentFoe_ = target->serial;
            currentFoeName_ = target->name;
            if (client.TravelBusy() || client.GotoBusy()) {
                client.TravelAbort("hold position to cast at range");
                travelInFlight_ = false;
                survivalRetreat_ = false;
            }
            const int heal = obs.HpFraction() < needCfg_.healHpFraction
                ? PickSurvivalSpell(client, obs, true) : -1;
            const int poison = heal < 0 && poisonOpenedTarget_ != target->serial
                ? PickPoisonOpener(client, obs) : -1;
            casterReachRefusedTarget_ = 0;
            client.ActionCastSpell(heal >= 0 ? heal : poison >= 0 ? poison : attackSpell,
                                  heal >= 0 ? client.PlayerSerial() : target->serial);
            if (poison >= 0) poisonOpenedTarget_ = target->serial;
        }
        nextActionMs_ = obs.nowMs + 4000;
        return false;
    }
    // Equip only the weapon school selected by the build. A bow or kryss
    // carried alongside a tool must not be replaced by an arbitrary sword.
    if (needCfg_.profession) {
        if (const SchoolWeapon* school = SchoolWeaponFor(*needCfg_.profession)) {
            bool armed = false;
            for (u16 g : school->graphics)
                armed = armed || client.EquippedGraphicAt(kLayerHand1) == g ||
                                 client.EquippedGraphicAt(kLayerHand2) == g;
            if (!armed) {
                if (client.ActionBusy()) return false;
                const u32 weapon = FindAny(client, school->graphics, 2);
                if (weapon) client.ActionEquip(weapon, kLayerServerChooses);
                else {
                    RetreatToSafety(client);
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
    }
    if (strategy == CombatStrategyId::Ranged && dist <= 2) {
        RetreatToSafety(client);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    if (currentFoe_ != target->serial) {
        currentFoe_ = target->serial;
        // The NAME as well as the serial. A serial dies with the corpse; the
        // name is what a per-creature verdict is keyed on, and it is the only
        // thing left to blame once we are a ghost.
        currentFoeName_ = target->name;
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
        fightStartedMs_ = obs.nowMs;
        // ASK FOR ITS HEALTH, or the whole fight is judged blind.
        //
        // The stalemate test below reads target->hpCur, and NOTHING in the
        // life layer ever filled it: SendStatusRequest -- the 0x34 status
        // query that makes a server send a mobile's health -- was called only
        // from the JS scenario bindings. So hpCur stayed at its -1 default,
        // foeHp was always -1.0, `noDent` was UNCONDITIONALLY TRUE, and every
        // autonomous fight disengaged at 21 seconds as a "stalemate" however
        // well it was going:
        //   interrupt=DISENGAGE reason="21s of fighting and Zombie is still at
        //   unknown health; this is a stalemate"
        // It then marked the foe unreachable and noted it as dangerous, so the
        // character taught itself to avoid the very monsters it was beating --
        // and that verdict persisted across sessions. This is why no bot has
        // ever recorded a confirmed kill.
        client.RequestMobileStatus(target->serial);
        foeHpAtStart_ = target->hpCur >= 0 && target->hpMax > 0
                            ? static_cast<double>(target->hpCur) / target->hpMax
                            : -1.0;
        foeHpAskedMs_ = obs.nowMs;
        LogLine("engaging %s (noto %d) at %d,%d",
                target->name.empty() ? "a hostile" : target->name.c_str(),
                target->noto, target->x, target->y);
    }

    // CANNOT DENT IT. A fight neither side can win is the worst outcome
    // available: Session A spent twenty of its thirty-one minutes in one, and
    // the goal-level timeout only restarted it every five minutes because
    // something was still attacking. So the fight itself is bounded on the one
    // signal a client actually has -- the foe's health bar.
    // KEEP ASKING. One query at the start only ever yields the opening value;
    // the stalemate test needs to see the bar MOVE, so re-ask while swinging.
    if (obs.nowMs - foeHpAskedMs_ > 3000) {
        client.RequestMobileStatus(target->serial);
        foeHpAskedMs_ = obs.nowMs;
        // The first reply is also the first honest opening reading -- before
        // it, foeHpAtStart_ could only ever have been -1.
        if (foeHpAtStart_ < 0.0 && target->hpCur >= 0 && target->hpMax > 0) {
            foeHpAtStart_ =
                static_cast<double>(target->hpCur) / target->hpMax;
        }
    }

    if (obs.nowMs - fightStartedMs_ > kFightAssessMs) {
        const double foeHp = target->hpCur >= 0 && target->hpMax > 0
                                 ? static_cast<double>(target->hpCur) / target->hpMax
                                 : -1.0;
        const bool noDent = foeHp < 0.0 || foeHpAtStart_ < 0.0 ||
                            (foeHpAtStart_ - foeHp) < 0.05;
        if (noDent) {
            LogLine("interrupt=DISENGAGE reason=\"%llds of fighting and %s is "
                    "still at %s health; this is a stalemate\"",
                    static_cast<long long>((obs.nowMs - fightStartedMs_) / 1000),
                    target->name.empty() ? "it" : target->name.c_str(),
                    foeHp >= 0.0 ? "the same" : "unknown");
            MarkUnreachable(target->serial, obs.nowMs);
            // The FOE is struck off; the GROUND is not. A stalemate is a
            // fight nobody is losing, and Castor 2026-09-05 02:25 logged two
            // of them in a row against one skeleton -- heat 2.66 -> 5.42 at
            // Britain Graveyard, past kHuntGroundHeatLimit, and with a 45 min
            // half-life the only novice ground was gone for the session. He
            // spent it exploring Trinsic. Danger heat is for being hurt,
            // fleeing and dying (the notes above and in Core.cpp); an
            // undented skeleton is not evidence the yard kills people.
            currentFoe_ = 0;
            client.EnsurePeaceMode();
            // Walk away, or the same foe is simply re-engaged next tick.
            const KnownResourceSource* stand =
                state_.memory.BestResource("logs", obs.x, obs.y, obs.nowMs);
            if (stand && !client.TravelBusy()) {
                client.TravelToPoint(stand->x, stand->y, 4, "leave_stalemate");
            }
            return true;
        }
        // It IS taking damage -- reset the window and keep fighting.
        fightStartedMs_ = obs.nowMs;
        foeHpAtStart_ = foeHp;
    }

    // BOUNDED CHASE. A wounded animal runs, and a lumberjack that follows it
    // across the countryside has stopped being a lumberjack -- the first live
    // run spent four of its five minutes chasing one fleeing mobile and never
    // returned to the trees. Progress means getting CLOSER; when there is none
    // for a while, the foe is written off and work resumes.
    const i32 combatRange = strategy == CombatStrategyId::Ranged ? 6 : 1;
    if (dist <= combatRange) {
        if (strategy == CombatStrategyId::Ranged &&
            (client.TravelBusy() || client.GotoBusy())) {
            // Sphere delays bow swings until the archer stops walking.
            client.TravelAbort("hold firing range");
            travelInFlight_ = false;
            survivalRetreat_ = false;
        }
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (dist < chaseBestDist_) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (obs.nowMs - chaseProgressMs_ > kChaseGiveUpMs) {
        LogLine("interrupt=DISENGAGE reason=\"cannot close on %s in %llds "
                "(best %d tiles); it is not worth the chase\"",
                target->name.empty() ? "it" : target->name.c_str(),
                static_cast<long long>(kChaseGiveUpMs / 1000), chaseBestDist_);
        MarkUnreachable(target->serial, obs.nowMs);
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;
    }

    if (client.ActionBusy()) return false;
    if (obs.HpFraction() < needCfg_.healHpFraction && obs.bandages > 0 &&
        WantsConsumable(needCfg_, "bandage") && obs.SkillTenths(rules::kHealing) > 0 &&
        (strategy != CombatStrategyId::Ranged || dist >= combatRange)) {
        const u32 bandage = client.FindBackpackItemByGraphic(kBandage);
        if (bandage) {
            client.ActionUseBandage(bandage, client.PlayerSerial());
            nextActionMs_ = obs.nowMs + 4000;
            return false;
        }
    }
    if (!client.WarModeOn()) client.EnterWarMode();
    // Sphere ignores a repeated attack on the same target while the weapon
    // skill is active. Reassert occasionally to resume after a healing skill.
    constexpr i64 kAttackReassertMs = 6000;
    if (lastAttackOrderTarget_ != target->serial ||
        obs.nowMs - lastAttackOrderMs_ >= kAttackReassertMs) {
        client.ActionAttack(target->serial);
        lastAttackOrderTarget_ = target->serial;
        lastAttackOrderMs_ = obs.nowMs;
    }
    if (dist > combatRange && !client.GotoBusy())
        client.ActionGotoMobile(target->serial, combatRange);
    nextActionMs_ = obs.nowMs + 1200;
    return false;
}

// POISON THE SPELL IS CAST OUT OF MAGERY, NOT OUT OF POISONING.
//
// This used to require the Poisoning SKILL above zero and a Poisoning target
// in the 700-point plan -- the skill that applies venom to a blade, which has
// nothing to do with s_poison in a spellbook. The gate meant no pure mage ever
// cast it: Aurelius held Poison (id 20) in her book through a whole session
// and cast Harm instead (run_gates/g_Aurelius.console.txt 2026-09-07 00:44).
// The only gates that belong here are the ones every other rung answers to --
// the book holds it, Magery reaches it, mana and reagents pay for it.
int Runner::PickPoisonOpener(Client& client, const Observation& obs) const {
    if (!obs.spellbookSerial) return -1;
    for (const auto& d : spell::SpellTable()) {
        // Defname comes from the shard export; Poison is not a direct-damage spell.
        if (std::strcmp(d.defname, "s_poison") != 0) continue;
        if (obs.SkillTenths(rules::kMagery) < d.minSkillTenths || obs.mana < d.mana ||
            !BookHasSpell(client, obs.spellbookSerial, d.spell)) return -1;
        for (const char* reagent : d.reagents) {
            if (!reagent) break;
            if (market::QtyOf(obs.pack, reagent) < 1) return -1;
        }
        return d.spell;
    }
    return -1;
}

int Runner::PickSurvivalSpell(Client& client, const Observation& obs, bool healing,
                              bool requireSupplies) const {
    if (!obs.spellbookSerial) return -1;
    for (u8 layer = 1; layer <= 24; ++layer) {
        const ArmorPiece* armor = ArmorFor(client.EquippedGraphicAt(layer));
        if (armor && (armor->cls == ArmorClass::Metal || armor->cls == ArmorClass::Shield))
            return -1;
    }
    spell::LoadSpellTable(client.DataDir());
    std::vector<SpellRung> rungs;
    for (const spell::SpellDef& d : spell::SpellTable()) {
        // WHAT COUNTS AS A RUNG lives with the rest of the spell knowledge
        // (uo/spellcast.h): a mobile target -- targ_char OR targ_obj, because
        // this shard flags Fireball, Poison and Lightning as targ_obj and they
        // all land on a creature -- and harm that either damages or ticks.
        if (!BookHasSpell(client, obs.spellbookSerial, d.spell)) continue;
        if (healing ? !spell::IsHealRung(d) : !spell::IsAttackRung(d)) continue;
        SpellRung r;
        r.spell = d.spell;
        r.circle = d.circle;
        r.mana = d.mana;
        r.minSkillTenths = d.minSkillTenths;
        r.supplied = true;
        for (const char* reagent : d.reagents) {
            if (!reagent) break;
            if (market::QtyOf(obs.pack, reagent) < 1) r.supplied = false;
        }
        rungs.push_back(r);
    }
    // The choice itself is pure and lives with the rest of the life policy
    // (life.h): strongest castable rung to attack with, cheapest to heal with,
    // walking back down the ladder when mana or reagents run short.
    return PickSpellRung(rungs, obs.SkillTenths(rules::kMagery), obs.mana,
                         healing, requireSupplies);
}

// EVERY ATTACK SPELL THIS BOOK AND THIS SKILL ALLOW, strongest rung first,
// ignoring mana and the pack. This is the ladder the fight will walk down, and
// therefore the list the reagent errand has to shop for: stocking only the
// cheapest rung is why Aurelius fought a whole session with Magic Arrow while
// holding Harm, Fireball, Poison and Lightning (D8,
// artifacts/validation_wave_2026-09-06.md).
void Runner::AttackLadder(Client& client, const Observation& obs,
                          std::vector<const spell::SpellDef*>& out) const {
    out.clear();
    if (!obs.spellbookSerial) return;
    spell::LoadSpellTable(client.DataDir());
    for (const spell::SpellDef& d : spell::SpellTable()) {
        if (!spell::IsAttackRung(d) ||
            obs.SkillTenths(rules::kMagery) < d.minSkillTenths ||
            !BookHasSpell(client, obs.spellbookSerial, d.spell)) continue;
        out.push_back(&d);
    }
    std::sort(out.begin(), out.end(),
              [](const spell::SpellDef* a, const spell::SpellDef* b) {
                  SpellRung ra, rb;
                  ra.circle = a->circle; ra.mana = a->mana;
                  rb.circle = b->circle; rb.mana = b->mana;
                  return StrongerRung(ra, rb, false);
              });
}

bool Runner::DoHeal(Client& client, const Observation& obs) {
    if (obs.dead) return DoSurvive(client, obs);
    if (ContinueSurvivalRetreat(client, obs)) return false;
    if (obs.underAttack || obs.attackersOnMe > 0) {
        DoSurvive(client, obs);
        return false;
    }
    if (obs.HpFraction() >= needCfg_.healHpFraction) return true;
    client.EnsurePeaceMode();
    // Read a real book before declaring that a caster has no healing spell.
    if (obs.spellbookSerial && !client.ContainerKnown(obs.spellbookSerial) &&
        !client.ActionBusy()) {
        client.ActionOpenContainer(obs.spellbookSerial);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    // See docs/S2_WIRING_PLAN.md S2.1 for the field-source table this mirrors.
    HealSight see;
    see.hp = obs.hp;
    see.hpMax = obs.hpMax;
    see.mana = obs.mana;
    see.bandages = obs.bandages;
    see.healPotions = obs.healPotions;
    // UNKNOWN: this does not prove *Heal* is in the spellbook -- that needs
    // BookHasGraphic with the book open, not just a skill/spellbook check.
    // Left false for this slice; a crafter has no Magery at all (the R4 pair
    // are miner_smith / lumberjack_swordsman), so nothing here loses by it.
    const int healingSpell = PickSurvivalSpell(client, obs, true);
    see.canCastHeal = healingSpell >= 0;
    see.useBandages = WantsConsumable(needCfg_, "bandage") &&
                      obs.SkillTenths(rules::kHealing) > 0;
    const bool canBuyBandages = see.useBandages && obs.SkillTenths(rules::kHealing) >= 300;
    // NOTHING TO HEAL WITH AND NOTHING ON THE WAY. Below the heal line with
    // no bandage, no potion and no heal spell, the only question left is
    // whether a counter will sell something -- and the healer's own list
    // holds both i_bandage {5 20} and i_potion_heal {3 12}
    // (tm_vend.scp:1110-1111), with gold paid from the bank. Odessa was in
    // this exact state at 6/50 HP beside that healer on 2026-09-06 and the
    // errand had nothing it was allowed to ask for (D13,
    // artifacts/validation_wave_2026-09-06.md).
    //
    // AND IT IS AN EMERGENCY, NOT MERELY A SCRATCH. The line is this life's
    // own flee fraction -- the health at which it breaks off a fight -- so a
    // tamer at 66% with Veterinary and no Healing still gets the standing
    // answer ("get a potion, a bandage you cannot use is not the fix"), and
    // only a character that is actually dying overrides its catalogue.
    const bool emergency = obs.bandages == 0 && obs.healPotions == 0 &&
                           !see.canCastHeal &&
                           obs.HpFraction() < needCfg_.fleeHpFraction;
    see.canBuySupplies = canBuyBandages ||
                        WantsConsumable(needCfg_, "heal potion") ||
                        emergency;
    // Use the counter for this build's self-healing supplies.
    const wm::Service healingService = canBuyBandages
        ? wm::Service::Healer : wm::Service::Alchemist;
    if (const wm::Place* shop = client.NearestServicePlace(healingService))
        see.supplyDistance = TileDist(obs.x, obs.y, shop->position.x, shop->position.y);
    // obs.gold is the BANK total on this shard, not the pack (obs.goldOnHand
    // is that) -- "can this be fixed with money" is the bank question, not
    // "can I hand it over right now".
    see.gold = obs.gold;
    // The same four graphics DoMakeBandages walks, in the same order.
    see.hasBandageMaterial =
        FindAny(client, kCuttableClothing,
                sizeof(kCuttableClothing) / sizeof(kCuttableClothing[0])) !=
            0 ||
        client.FindBackpackItemByGraphic(kClothGraphic) != 0 ||
        client.FindBackpackItemByGraphic(kClothBoltGraphic) != 0 ||
        client.FindBackpackItemByGraphic(kWoolGraphic) != 0;
    see.hungry = obs.hungry;
    // Under attack right now, not merely near a hostile -- a cow standing
    // next to the character is not a fight.
    see.inDanger = obs.underAttack;

    // A bandage on an untrained healer only produces the shard's "barely
    // help" one-point result.  A warrior must not consume its emergency kit
    // that way: obtain a healing potion first, then resume healing/training.
    if (obs.SkillTenths(rules::kHealing) < 300 &&
        (see.healPotions > 0 || see.canCastHeal)) see.useBandages = false;

    HealTuning tune;
    tune.healHpFraction = needCfg_.healHpFraction;
    const wm::Region* healingRegion = client.CurrentRegion();
    if (healingRegion && healingRegion->flags.guarded)
        tune.minHpToShop = 0.0;  // a local medical errand inside town is safe
    // WAITING IS ONLY A PLAN IF WAITING FIXES IT. minHpToShop exists so a
    // character regenerates to a safe margin before walking a shopping
    // circuit -- sound when it has SOMETHING, and the Faustus case in
    // heal.h:78-82 shows what it costs when it has nothing: ten minutes
    // beside a counter that was selling the answer. In the emergency the
    // counter IS the plan.
    if (emergency) tune.minHpToShop = 0.0;
    // UNKNOWN until an observation exists; the struct default of 2 stands in
    // for it until then.
    if (const market::PriceObservation* p = state_.prices.Latest(
            "bandage", market::PriceSource::NpcVendorSells)) {
        tune.bandagePrice = p->pricePerUnit;
    }
    // UNKNOWN as a field: needCfg_.goldFloor (100) is the nearest honest
    // number, but the bandage errand deliberately spends the character's
    // last coin on purpose (see the reserve comment near Runner.cpp:3314) --
    // so this is left at the struct default of 0 rather than block the poor
    // branch on a number that contradicts existing behaviour.

    const HealPlan p = DecideHeal(see, tune);
    if (p.step != lastHealPlan_) {
        LogPlan(HealStepName(p.step), p.reason);
        lastHealPlan_ = p.step;
    }

    switch (p.step) {
        case HealStep::None:
            return true;

        case HealStep::Bandage:
            // SurvivalTick owns the actual bandage timing (it knows the ~3s
            // skill delay and will not restart a running heal, which is the
            // bug that made uo-offline's first bandage loop heal nothing at
            // all) -- but only at <=60% HP, out of contact (CombatPolicy's
            // kPotionPercent). DecideHeal fires Bandage anywhere below
            // healHpFraction (80%), so 61-79% was nobody's: SurvivalTick
            // would not act (pct > 60) and this arm only delegated, which
            // meant a HP band where the character silently never healed.
            // Below 60% we still only make sure nothing else is competing
            // for the body and let SurvivalTick do the actual bandaging;
            // above it, apply the bandage ourselves with the same client
            // call SurvivalTick uses.
            if (client.WarModeOn() && obs.hostilesNear == 0)
                client.EnsurePeaceMode();
            if (obs.HpFraction() <= 0.60) {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            if (client.ActionBusy()) {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            {
                const u32 bandage = client.FindBackpackItemByGraphic(kBandage);
                if (bandage) {
                    client.ActionUseBandage(bandage, client.PlayerSerial());
                    nextActionMs_ = obs.nowMs + 4000;
                    planner_.NoteProgress();
                } else {
                    nextActionMs_ = obs.nowMs + 2000;
                    planner_.NoteAttempt(obs.nowMs);
                }
            }
            return false;

        case HealStep::DrinkPotion: {
            // SurvivalTick already drinks autonomously at <=60% HP once out
            // of contact -- below that line it owns the tick, and a second
            // click here would race it. DoHeal only acts above 60%, and only
            // once SurvivalTick's own click (if any) is not still in flight.
            // The comparison is done in the same integer percent
            // combat::HealthPercent uses (not obs.HpFraction()'s double), so
            // the two never disagree about which side of 60% a tick is on.
            const bool aboveSurvivalLine =
                obs.hpMax > 0 && (obs.hp * 100) / obs.hpMax > 60;
            const u32 potion = client.FindBackpackItemByGraphic(kHealPotion);
            if (aboveSurvivalLine && !client.ActionBusy() && potion != 0) {
                if (client.ActionDrinkPotion(potion)) {
                    nextActionMs_ = obs.nowMs + 2500;
                    planner_.NoteProgress();
                } else {
                    nextActionMs_ = obs.nowMs + 1000;
                    planner_.NoteAttempt(obs.nowMs);
                }
            } else {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
            }
            return false;
        }

        case HealStep::CastHeal:
            if (!client.ActionBusy()) {
                client.ActionCastSpell(healingSpell, client.PlayerSerial());
                nextActionMs_ = obs.nowMs + 6000;
                planner_.NoteAttempt(obs.nowMs);
            }
            return false;

        case HealStep::BuySupplies:
            // Reuse the existing purchasing policy and activities, but defer
            // weapon/clothing shopping until the patient has recovered.
            //
            // THE FLAG IS SET FOR THIS CALL AND NO OTHER. DoReplaceEquipment
            // is also a GOAL, picked by the planner for upkeep, and upkeep is
            // where the "a crafter does not buy bandages" rule belongs. What
            // the flag says is narrower and true only here: this patient has
            // nothing to heal with at all, so the medicine the counter DOES
            // stock is the medicine to buy.
            emergencySelfHeal_ = emergency;
            DoReplaceEquipment(client, obs, true);
            emergencySelfHeal_ = false;
            return false;

        case HealStep::MakeBandages:
            return HandOff(planner_.Current().kind, GoalKind::MakeBandages, 60000,
                           "too poor to buy; cutting cloth", obs.nowMs);

        case HealStep::Rest: {
            // RESTING IS A THING YOU DO SOMEWHERE SAFE.
            //
            // DecideHeal's rest arm reads see.inDanger = obs.underAttack,
            // which is the moment of a swing, not the presence of a pursuer.
            // Odessa's plan on 2026-09-06 at 00:37:28 was "rest -- recover
            // health here before a shopping trip" while a named lizardman
            // that had followed her from the road took her from 12 HP to
            // dead (D14). A hostile in sight makes this ground the wrong
            // ground: shout if the guards can hear, otherwise walk to where
            // they can.
            const bool threatened = obs.underAttack || obs.attackersOnMe > 0 ||
                                    obs.hostilesNear > 0;
            if (threatened) {
                if (!CallGuardsIfProtected(client, obs)) {
                    LogLine("interrupt=FLEE_TO_GUARDS reason=\"hurt at %.0f%% "
                            "with %d hostile(s) in sight and no guard "
                            "protection here -- resting is not an option\"",
                            obs.HpFraction() * 100.0, obs.hostilesNear);
                    RetreatToSafety(client);
                }
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            // No NoteProgress -- resting is not progress; five of these trip
            // the anti-spin backstop, which is correct here.
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }

        case HealStep::Stuck:
            // ARM B OF THE NEED/HANDLER CONTRACT
            // (docs/NEED_HANDLER_CONTRACT.md). "Nothing to heal with and
            // nothing on the way" is a sum of the pack, the purse, the book
            // and the nearest counter -- DecideHeal's, not the need model's,
            // which sees only the health bar. So NeedHeal kept scoring 700,
            // winning, and being handed away, and Odessa's HEAL was abandoned
            // on "attempts 5 >= 5" at 6/50 HP (D13). Recorded here in
            // DecideHeal's own words, for the restock window, so the planner
            // spends those ticks on the errand that can actually change the
            // situation instead.
            //
            // Window, never Session: this is the one need that must reopen the
            // moment a bandage, a potion or a coin arrives.
            LogLine("goal_stuck=HEAL reason=\"%s\"", p.reason);
            life::NoteNeedBlocked(state_.memory, life::NeedKind::Heal, p.reason,
                                  life::BlockScope::Window, needCfg_, obs.nowMs);
            return HandOff(planner_.Current().kind, GoalKind::GetFood, 120000, p.reason,
                           obs.nowMs);
    }
    return false;
}

// --- corpse ----------------------------------------------------------------

bool Runner::DoRecoverCorpse(Client& client, const Observation& obs) {
    const travel::DeathRecord& death = client.Knowledge().LastDeath();

    // Corpse serials are intentionally session-local.  Once we are back at
    // the recorded tile, the normal world-item stream makes the corpse visible
    // again; bind that fresh serial before attempting to open it.
    if (death.valid && death.corpseSerial == 0 &&
        TileDist(death.x, death.y, obs.x, obs.y) <= 8) {
        const u32 corpse = client.FindWorldItemByGraphic(0x2006, 8);
        if (corpse) {
            client.Knowledge().NoteCorpse(corpse, death.x, death.y, 0);
            LogLine("corpse run: found own corpse 0x%08X at the death site", corpse);
        }
    }

    RecoverySight see;
    see.dead = obs.dead;
    see.threatened = obs.underAttack || obs.attackersOnMe > 0;
    see.corpseKnown = obs.corpseKnown;
    see.corpseDistance = TileDist(death.x, death.y, obs.x, obs.y);
    // This character's OWN memory of the corpse's place, not of here --
    // somewhere it died three times is dangerous to it specifically.
    see.dangerHeatAtCorpse = state_.memory.DangerHeatAt(death.x, death.y, obs.nowMs);
    see.hpFraction = obs.HpFraction();
    see.attemptsSoFar = obs.corpseRecoveryAttempts;
    // Unknown is not empty: only claim corpseEmpty once the container has
    // actually been opened and counted (mirrors the ContainerKnown gate the
    // handler always kept before opening).
    see.corpseEmpty = death.corpseSerial != 0 &&
                       client.ContainerKnown(death.corpseSerial) &&
                       client.ContainerItemCount(death.corpseSerial) == 0;
    // A DEATH RECORD OUTLIVES A CORPSE (sphere.ini CorpsePlayerDecay=7 min).
    // Count the decisions spent standing on the tile with nothing bound so
    // DecideRecovery can call it gone instead of opening serial 0 forever.
    see.corpseVisible = death.corpseSerial != 0;
    if (see.corpseVisible || see.corpseDistance > 2) corpseProbesAtSite_ = 0;
    else if (!obs.dead) ++corpseProbesAtSite_;
    see.probesAtSite = corpseProbesAtSite_;
    // UNKNOWN (S2_WIRING_PLAN.md S2.3): no cheap "loose gear in the pack"
    // read exists without duplicating MayWear's loop (Runner.cpp:8440-ish).
    // Left false -- DoUpgradeGear/DoReplaceEquipment re-dress on their own
    // goal, which is what happens today; ReEquip below is unreachable.
    see.gearInPack = false;

    RecoveryTuning tune;
    if (needCfg_.profession) tune.riskTolerance = Nerve(needCfg_);
    tune.minHpToReturn = needCfg_.healHpFraction;

    // Starting a trip consumes its attempt. Let that last permitted trip
    // arrive before evaluating whether another attempt would exceed budget.
    if (travelInFlight_ && client.TravelBusy() && !see.threatened &&
        see.hpFraction >= tune.minHpToReturn) return false;

    const RecoveryPlan plan = DecideRecovery(see, tune);
    const bool interruptCorpseTrip = lastRecoveryPlan_ == RecoveryStep::TravelToCorpse &&
                                    plan.step == RecoveryStep::Recover;
    if (plan.step != lastRecoveryPlan_) {
        LogPlan(RecoveryStepName(plan.step), plan.reason);
        lastRecoveryPlan_ = plan.step;
    }

    switch (plan.step) {
        case RecoveryStep::SeekResurrection:
            // Same guard as DoSurvive: one outstanding resurrection request,
            // not one every three seconds against a fifteen-minute deadline.
            // Both goals can be the one running while the character is a
            // ghost, so both had the fault.
            if (client.ActionBusy()) return false;
            return DoSurvive(client, obs);

        case RecoveryStep::Recover:
            // Mandatory: without this cooldown RecoverCorpse (950) outscores
            // Heal (700) forever and the character never heals up to walk
            // back -- the exact death loop this handler exists to prevent.
            // Keep ownership until healthy; a timed handoff can expire while
            // regeneration is still working and restart the corpse trip.
            if (interruptCorpseTrip) {
                client.TravelAbort("heal before corpse recovery");
                travelInFlight_ = false;
            }
            // Subsequent ticks may own a medical-supply journey. Let it arrive.
            return DoHeal(client, obs);

        case RecoveryStep::TravelToCorpse:
            if (client.TravelBusy()) return false;
            if (!travelInFlight_) {
                LogLine("corpse run: heading to %d,%d (attempt %d)", death.x, death.y,
                        death.recoveryAttempts + 1);
                travelInFlight_ = client.TravelToLastCorpse();
                if (!travelInFlight_) {
                    LogLine("corpse run: no route (%s)", client.TravelFailureText());
                    planner_.NoteAttempt(obs.nowMs);
                    nextActionMs_ = obs.nowMs + 5000;
                }
                return false;
            }
            travelInFlight_ = false;
            if (!client.TravelSucceeded()) {
                // TravelToLastCorpse already counted this trip.
                planner_.NoteAttempt(obs.nowMs);
            }
            return false;

        case RecoveryStep::Loot:
            // Standing on it. Open, then take everything the container
            // reports.
            // Never address a request to nobody: serial 0 is answered with
            // "invalid_state / null serial" as fast as it is asked, which is
            // a busy loop, not an attempt.  CorpseGone above ends it.
            if (death.corpseSerial == 0) {
                nextActionMs_ = obs.nowMs + 1000;
                return false;
            }
            if (!client.ContainerKnown(death.corpseSerial)) {
                if (client.ActionBusy()) return false;
                client.ActionOpenContainer(death.corpseSerial);
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
            if (client.ActionBusy()) return false;
            {
                u32 serial = 0;
                u16 graphic = 0, amount = 0;
                if (client.ContainerItemAt(death.corpseSerial, 0, &serial, &graphic,
                                           &amount)) {
                    client.TakeFromContainer(serial, amount ? amount : 1);
                    planner_.NoteProgress();
                    nextActionMs_ = obs.nowMs + 900;
                }
            }
            return false;

        case RecoveryStep::ReEquip:
            // Unreachable while gearInPack is left false above (S2.3 scope).
            return HandOff(GoalKind::RecoverCorpse, GoalKind::ReplaceEquipment,
                           30000, plan.reason, obs.nowMs);

        case RecoveryStep::CorpseGone:
            // A REAL FAILURE with a reason, not a decision: the loot is gone.
            // That is Revolution death -- no shortcuts, no retry. Clear the
            // record so the need dies with it, cool the goal down so a stale
            // 950-point urgency cannot outscore everything else again, and
            // go get dressed.
            {
            const i32 deathX = death.x;
            const i32 deathY = death.y;
            client.Knowledge().ClearDeath();
            corpseProbesAtSite_ = 0;
            state_.memory.NoteEvent("corpse_lost", plan.reason, "", deathX,
                                    deathY, obs.nowMs);
            LogLine("goal_failed=RECOVER_CORPSE reason=\"%s\"", plan.reason);
            return HandOff(GoalKind::RecoverCorpse, GoalKind::ReplaceEquipment,
                           600000, plan.reason, obs.nowMs);
            }

        case RecoveryStep::Abandon:
            // A completed decision, not a failure -- never Finish(false).
            {
            const i32 deathX = death.x;
            const i32 deathY = death.y;
            client.Knowledge().ClearDeath();
            state_.memory.NoteEvent("corpse_abandoned", plan.reason, "", deathX,
                                    deathY, obs.nowMs);
            return true;
            }

        case RecoveryStep::Done:
            {
            const i32 deathX = death.x;
            const i32 deathY = death.y;
            client.Knowledge().ClearDeath();
            state_.memory.NoteEvent("corpse_recovered", "", "", deathX, deathY,
                                    obs.nowMs);
            return true;
            }
    }
    return true;
}

}  // namespace uo::life
