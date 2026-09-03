#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// --- survival --------------------------------------------------------------
//
// SurvivalTick already owns potion / bandage / disengage, proven live in
// M3.9.1. This goal adds the two things a tick-level policy cannot decide:
// whether to fight back at all, and where to go when the answer is no.

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
            // A live corpse serial cannot survive a reconnect, but this
            // location can.  Save it immediately: dying must not make loot
            // recovery depend on keeping the original client process alive.
            state_.memory.NoteEvent("corpse_pending", "recover after resurrection", "",
                                    obs.x, obs.y, obs.nowMs);
            Checkpoint(client, obs.nowMs, "death location recorded");
            if (!currentFoeName_.empty()) {
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
            LogLine("dead: arrived at healer destination -- scanning nearby healers");
            client.ActionScanMobiles();
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        if (client.ActionBusy()) return false;

        const u32 healer = client.NearestMobileWithTrade("healer");
        if (healer) {
            i32 hx = 0, hy = 0; i8 hz = 0;
            // Healers commonly stand behind a counter or in a small room.
            // The interaction range is wider than adjacency, so path to an
            // accessible tile in that range instead of attempting to occupy
            // the NPC's sealed tile through the wall.
            constexpr i32 kHealerReach = 4;
            if (client.MobilePosition(healer, &hx, &hy, &hz) &&
                TileDist(obs.x, obs.y, hx, hy) > kHealerReach && !client.TravelBusy()) {
                LogLine("dead: a healer is here -- getting close enough to be "
                        "raised");
                travelInFlight_ = client.TravelToEntity(healer, kHealerReach);
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
                client.TravelToService(wm::Service::Healer, HomeOrNearest(state_.homeCity));
        }
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Alive again: the next death is a new death, and a new verdict.
    deathBlamed_ = false;

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    if (hostiles.empty()) {
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;   // the danger passed
    }

    // Remember where this went badly -- ONCE PER FIGHT, not once per tick.
    // Per-tick notes are how one twenty-minute stalemate compounded a single
    // wolf into heat 499.89 and made the whole forest look lethal.
    if (obs.nowMs - lastDangerNoteMs_ > 60000) {
        lastDangerNoteMs_ = obs.nowMs;
        state_.memory.NoteDanger(obs.x, obs.y, 14, hostiles.front().name.c_str(),
                                 0.5, obs.nowMs);
    }

    // AvoidCombat only (S2_WIRING_PLAN.md S2.6). A null profession is a
    // pre-catalogue life -- combatStrategy defaults to AvoidCombat
    // (professions.h:235), so gating on the enum alone would silently turn
    // every legacy character pacifist. Every other strategy falls through to
    // the body below byte-for-byte; Melee/Ranged/Mage/Tamer tuning is R2's.
    bool avoidCombatDisengage = false;
    if (needCfg_.profession &&
        needCfg_.profession->combatStrategy == life::CombatStrategyId::AvoidCombat) {
        life::CombatSight see;
        see.hp = obs.hp;
        see.hpMax = obs.hpMax;
        see.mana = obs.mana;
        // UNKNOWN: no Observation field and no status flag for this on the
        // shard (the same gap Runner.cpp records for weight). Left 0.
        see.manaMax = 0;
        // Neither the hostile nor the target-selection loop has run yet at
        // this point in the function -- write what we have, the nearest
        // hostile, same source NoteDanger above just used.
        see.foeDistance =
            TileDist(hostiles.front().x, hostiles.front().y, obs.x, obs.y);
        see.foeHpFraction = hostiles.front().hpCur >= 0 && hostiles.front().hpMax > 0
                                ? static_cast<double>(hostiles.front().hpCur) /
                                      hostiles.front().hpMax
                                : -1.0;
        see.attackersOnMe = obs.attackersOnMe;
        see.bandages = obs.bandages;
        // UNKNOWN: obs.hasPet answers ownership, not health. Left at
        // defaults (petAlive=false, petHpFraction=-1.0).
        {
            bool armedNow = false;
            for (usize i = 0; i < sizeof(kMeleeWeaponGfx) / sizeof(u16); ++i) {
                const u16 g = kMeleeWeaponGfx[i];
                if (client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    armedNow = true;
                    break;
                }
            }
            see.armed = armedNow;
        }

        life::CombatTuning tune;
        tune.fleeHpFraction = needCfg_.fleeHpFraction;
        tune.healHpFraction = needCfg_.healHpFraction;
        // UNKNOWN: neither field exists on NeedConfig or a personality
        // record. Left at the struct defaults (preferredRange=6,
        // riskTolerance=0.5).

        const life::CombatDecision d = life::DecideCombat(
            needCfg_.profession->combatStrategy, see, tune);
        if (d.move != lastCombatMove_) {
            LogPlan(life::CombatMoveName(d.move), d.reason);
            lastCombatMove_ = d.move;
        }

        // AvoidCombat always decides Disengage (CombatStrategy.cpp:71-73),
        // before ShouldBreakOff is even consulted -- so this is the only
        // reachable arm in S2. Every other CombatMove is unreachable here.
        // Only peace mode happens right here: returning true on the spot
        // (the earlier version) skipped the bailAt block below entirely,
        // which meant an AvoidCombat life fled with none of the FLEE
        // path's creature-outcome evidence, first_near_death event, or
        // rate-limited danger note -- a second, thinner retreat instead of
        // the proven one (S2_WIRING_PLAN.md review finding 5).
        // avoidCombatDisengage forces that block to run below regardless
        // of HP, since this life never reaches the fight-back code after
        // it either way.
        if (d.move == life::CombatMove::Disengage) {
            client.EnsurePeaceMode();
            avoidCombatDisengage = true;
        }
    }

    // Same nerve-adjusted threshold Needs.cpp uses for the StayAlive need, so
    // the need model and the flee interrupt agree on when a fight is lost.
    // Before this they disagreed: Needs read riskTolerance, this block read
    // the raw 0.32 -- so a fencer's nerve counted for the planner and not for
    // the retreat.
    double bailAt = needCfg_.fleeHpFraction;
    double nerve = 0.5;
    if (needCfg_.profession) {
        nerve = needCfg_.profession->riskTolerance;
        bailAt = std::min(0.75, std::max(0.20,
                    needCfg_.fleeHpFraction + (0.5 - nerve) * 0.4));
    }
    const i32 extra = obs.attackersOnMe - 1;
    if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

    // How many adjacent attackers this life will stand in before it breaks
    // contact regardless of health. The old rule was a flat two, and a
    // graveyard hands a warrior three at once: Faustus (macer, 2026-09-03)
    // landed one accepted attack on a skeleton and fled at 98% HP with
    // "3 attacker(s)", every session, zero kills. Nerve decides the crowd a
    // character tolerates -- a fencer at 0.75 stands in four, a smith at
    // 0.35 in two, a fisher at 0.20 in one -- and the HP bail above, which
    // already rises per extra attacker, is what actually protects the life.
    const i32 crowdTolerated = 1 + static_cast<i32>(nerve * 4.0);

    if (avoidCombatDisengage || obs.attackersOnMe > crowdTolerated ||
        obs.HpFraction() < bailAt) {
        LogLine("interrupt=FLEE reason=\"HP %.0f%%; %d attacker(s); bail at %.0f%%\"",
                obs.HpFraction() * 100.0, obs.attackersOnMe, bailAt * 100.0);
        client.EnsurePeaceMode();
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
        // Retreat toward somewhere known-safe rather than a random direction.
        const KnownPlace* bank = state_.memory.NearestPlace("bank", obs.x, obs.y);
        if (bank && !client.TravelBusy()) {
            client.TravelToPoint(bank->x, bank->y, 3, "flee_to_bank");
        } else if (!client.TravelBusy()) {
            // A new character may not have personally recorded a bank yet;
            // the world atlas still knows where guarded bankers are.
            client.TravelToService(wm::Service::Banker,
                                   HomeOrNearest(state_.homeCity));
        }
        nextActionMs_ = obs.nowMs + 2000;
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Fight back at whatever is actually on us. Never pick a NEW fight here:
    // this goal exists because something already started one.
    const Client::HostileHit* target = nullptr;
    for (const Client::HostileHit& h : hostiles) {
        if (IsUnreachable(h.serial, obs.nowMs)) continue;
        if (!target || TileDist(h.x, h.y, obs.x, obs.y) <
                           TileDist(target->x, target->y, obs.x, obs.y)) {
            target = &h;
        }
    }
    if (!target) {
        client.EnsurePeaceMode();
        return true;
    }

    const i32 dist = TileDist(target->x, target->y, obs.x, obs.y);

    // DID THE LAST ONE DIE?
    //
    // Nothing in this file ever recorded WINNING a fight:
    // kCreatureEvidenceCheapKill and kCreatureEvidenceCostlyKill were declared
    // and never used, so a creature's danger could only ever go UP -- fleeing
    // added 1.0, dying added 2.0, and killing something added nothing at all.
    // A character that beat a zombie ten times learned exactly as much about
    // zombies as one that had never seen one.
    //
    // The foe vanishing from the mobile list while we were fighting it is the
    // kill: Sphere removes the mobile and drops a corpse. Cheap or costly is
    // decided by the health we finished on, which is the thing that actually
    // matters when choosing the next fight.
    if (currentFoe_ != 0 && currentFoe_ != target->serial &&
        !currentFoeName_.empty()) {
        bool stillThere = false;
        for (const Client::HostileHit& h : hostiles)
            stillThere = stillThere || (h.serial == currentFoe_);
        if (!stillThere) {
            const bool cheap = obs.HpFraction() >= 0.75;
            LogLine("hunt: killed '%s' -- finished at %.0f%% health, recording "
                    "it as a %s win", currentFoeName_.c_str(),
                    obs.HpFraction() * 100.0, cheap ? "cheap" : "costly");
            state_.memory.NoteCreatureOutcome(
                currentFoeName_.c_str(),
                cheap ? kCreatureEvidenceCheapKill : kCreatureEvidenceCostlyKill,
                obs.nowMs);
            if (!state_.memory.HasEvent("first_kill")) {
                state_.memory.NoteEvent("first_kill", currentFoeName_.c_str(),
                                        "", obs.x, obs.y, obs.nowMs);
            }
            session_.kills++;
        }
    }

    // TAKE THE WEAPON OUT OF THE BAG FIRST.
    //
    // Nothing in the life layer ever wielded one. The fighting professions
    // define no tools at all (fencer, macer and archer all have empty
    // p.tools), so no goal ever asked for a weapon in hand -- and the shard
    // hands every fighter one at creation, which then sits in the backpack for
    // the character's whole life. The result reads as a combat problem and is
    // not one: "20s of fighting and Spectre is still at the same health; this
    // is a stalemate", swung with bare fists.
    if (!client.ActionBusy()) {
        bool armed = false;
        for (usize i = 0; i < sizeof(kMeleeWeaponGfx) / sizeof(u16); ++i) {
            const u16 g = kMeleeWeaponGfx[i];
            if (client.EquippedGraphicAt(kLayerHand1) == g ||
                client.EquippedGraphicAt(kLayerHand2) == g) {
                armed = true;
                break;
            }
        }
        if (!armed) {
            const u32 inPack = FindAny(client, kMeleeWeaponGfx,
                                       sizeof(kMeleeWeaponGfx) / sizeof(u16));
            if (inPack) {
                LogLine("combat: drawing a weapon before swinging -- it was in "
                        "the pack");
                client.ActionEquip(inPack, kLayerHand1);
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
        }
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
            state_.memory.NoteDanger(obs.x, obs.y, 16,
                                     target->name.empty() ? "a stalemate foe"
                                                          : target->name.c_str(),
                                     1.0, obs.nowMs);
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
    if (dist <= 1) {
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

    if (!client.WarModeOn()) client.EnterWarMode();
    // Attack is a target-selection command, not a weapon swing.  Once the
    // server has accepted it, it owns the normal melee cadence.  Reissuing it
    // each 1.2-second life tick was resetting that cadence before the first
    // hit could resolve, leaving a healthy Zombie and a dead newcomer.
    constexpr i64 kAttackReassertMs = 6000;
    if (lastAttackOrderTarget_ != target->serial ||
        obs.nowMs - lastAttackOrderMs_ >= kAttackReassertMs) {
        client.ActionAttack(target->serial);
        lastAttackOrderTarget_ = target->serial;
        lastAttackOrderMs_ = obs.nowMs;
    }
    if (dist > 1 && !client.GotoBusy()) client.ActionGotoMobile(target->serial, 1);
    nextActionMs_ = obs.nowMs + 1200;
    return false;
}

bool Runner::DoHeal(Client& client, const Observation& obs) {
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
    see.canCastHeal = false;
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
    constexpr i32 kUsableHealingTenths = 300;
    if (obs.SkillTenths(rules::kHealing) < kUsableHealingTenths &&
        see.healPotions == 0 && obs.gold >= 200) {
        return HandOff(GoalKind::Heal, GoalKind::ReplaceEquipment, 60000,
                       "Healing is untrained; buying a potion instead of wasting bandages",
                       obs.nowMs);
    }

    HealTuning tune;
    tune.healHpFraction = needCfg_.healHpFraction;
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
                client.ActionUseObject(potion);
                nextActionMs_ = obs.nowMs + 2500;
                planner_.NoteProgress();
            } else {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
            }
            return false;
        }

        case HealStep::CastHeal:
            // Unreachable while canCastHeal is hardwired false above. Casting
            // a spell id is a new mechanic this slice does not add.
            nextActionMs_ = obs.nowMs + 3000;
            return false;

        case HealStep::BuySupplies:
            // NOT GoalKind::BuySupplies -- DoBuySupplies shops for craft
            // inputs only. The bandage/potion buyer is ReplaceEquipment.
            return HandOff(GoalKind::Heal, GoalKind::ReplaceEquipment, 60000,
                           "nothing to heal with; going shopping", obs.nowMs);

        case HealStep::MakeBandages:
            return HandOff(GoalKind::Heal, GoalKind::MakeBandages, 60000,
                           "too poor to buy; cutting cloth", obs.nowMs);

        case HealStep::Rest:
            // No NoteProgress -- resting is not progress; five of these trip
            // the anti-spin backstop, which is correct here.
            nextActionMs_ = obs.nowMs + 5000;
            return false;

        case HealStep::Stuck:
            LogLine("goal_stuck=HEAL reason=\"%s\"", p.reason);
            return HandOff(GoalKind::Heal, GoalKind::GetFood, 120000, p.reason,
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

    // RecoveryTuning: riskTolerance, minHpToReturn and maxAttempts all left
    // at their struct defaults. riskTolerance is UNKNOWN -- no per-character
    // personality field exists yet (spec S3 defers it).
    RecoveryTuning tune;

    const RecoveryPlan plan = DecideRecovery(see, tune);
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
            client.ActionResurrectAccept();
            nextActionMs_ = obs.nowMs + 10000;
            return false;

        case RecoveryStep::Recover:
            // Mandatory: without this cooldown RecoverCorpse (950) outscores
            // Heal (700) forever and the character never heals up to walk
            // back -- the exact death loop this handler exists to prevent.
            return HandOff(GoalKind::RecoverCorpse, GoalKind::Heal, 60000,
                           plan.reason, obs.nowMs);

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
                client.Knowledge().NoteCorpseRecoveryAttempt();
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
