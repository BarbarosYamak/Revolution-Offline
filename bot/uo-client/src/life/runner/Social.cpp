#include "life/runner/RunnerInternal.h"
#include "world/Atlas.h"
#include "uo/rules.h"
#include "uo/sparring.h"

namespace uo::life {
using namespace runner_detail;

// Sparring is combat practice.  A stocked crafter may carry bandages and an
// iron piece for ordinary work, but that does not turn production time into a
// combat-training party.  Hunters and hunting spellcasters keep the social
// sparring loop; crafters can still chat, trade, and join ordinary work groups.
static bool MaySocialSpar(const prof::Profession* profession) {
    return profession && (WantsToHunt(*profession) || WantsSpellCombat(*profession));
}

bool Runner::SocialFoe(const std::string& name) const {
    if (const auto* person = social::Find(state_.memory.relationships, name))
        if (person->foe) return true;
    for (const auto& event : state_.memory.Events())
        if (event.kind == "attacked_by_player" && event.detail == name) return true;
    return false;
}

bool Runner::SocialSay(Client& client, i64 nowMs, const std::string& text) {
    if (socialChatMs_ && nowMs - socialChatMs_ < 15000) return false;
    socialChatMs_ = nowMs;
    client.ActionSay(text.c_str());
    LogLine("social: said '%s'", text.c_str());
    return true;
}

void Runner::EndSocialGroup(Client& client, const char* reason) {
    if (socialActivity_ == social::Activity::Poison) client.AbandonGoalOwnedAction(reason);
    client.StopSparring(reason);
    sparActive_ = sparRoundStarted_ = sparPeerReady_ = false;
    sparReadyMs_ = sparPollMs_ = sparRoundEndMs_ = sparMeetingStartMs_ = 0;
    sparRounds_ = 0;
    sparKit_ = 0;
    poisonStudent_ = poisonSeen_ = false;
    poisonRound_ = 1; poisonReadyMs_ = 0;
    if (socialOwnParty_ || socialLeavePending_) {
        socialLeavePending_ = client.PartySize() == 0;
        client.ActionPartyLeave();
    }
    if (socialPeer_) LogLine("social: group ended with %s (%s)", socialPeerName_.c_str(), reason);
    socialPeer_ = socialPreferred_ = 0;
    socialPeerName_.clear();
    socialActivity_ = social::Activity::None;
    socialConsented_ = socialOwnParty_ = socialMeetingPicked_ = false;
    socialStartedMs_ = socialAgreedMs_ = socialInviteMs_ = socialGroupUntilMs_ = 0;
    socialPracticeMs_ = 0;
    socialTrainingGains_ = 0;
    socialTrainingSkills_.clear();
}

void Runner::ObserveSocial(Client& client, const Observation& obs) {
    if (socialLeavePending_ && client.PartySize() > 0) {
        client.ActionPartyLeave();
        socialLeavePending_ = false;
    }
    const bool safe = social::SafeToSocialize(obs.dead, obs.underAttack || obs.attackersOnMe > 0,
                                             obs.hostilesNear, obs.HpFraction(), obs.WeightFraction());
    if (safe && !client.ActionBusy() && !client.TravelBusy() &&
        obs.nowMs - socialScanMs_ >= 30000 + (client.PlayerSerial() % 10) * 1000) {
        socialScanMs_ = obs.nowMs;
        client.ActionIdentifyNearbyPerson();
    }
    // Aggression is observed independently of the survival goal being selected.
    std::vector<Client::HostileHit> players;
    client.NearbyPlayers(18, players);
    for (const auto& p : players) {
        if (client.IsAttackingMe(p.serial))
            social::Remember(state_.memory.relationships, p.name, social::Encounter::Attack, obs.nowMs);
    }
    if (socialGroupUntilMs_ && socialPeer_) {
        const auto companion = std::find_if(players.begin(), players.end(), [&](const auto& p) { return p.serial == socialPeer_; });
        if (companion != players.end()) socialLastSeenMs_ = obs.nowMs;
        if (!client.PartyContains(socialPeer_) || obs.nowMs - socialLastSeenMs_ > 30000) {
            EndSocialGroup(client, "party ended or companion out of sight");
            socialRestUntilMs_ = obs.nowMs + 180000;
        } else if (socialActivity_ == social::Activity::Train) {
            const i32 gain = social::TrainingGains(socialTrainingSkills_, obs.skills);
            socialTrainingSkills_ = obs.skills;
            if (gain > 0) {
                LogLine("social: work-group training gained %d tenths with %s", gain, socialPeerName_.c_str());
                social::Remember(state_.memory.relationships, socialPeerName_, social::Encounter::Help, obs.nowMs);
            }
        }
    }
    if (socialPeer_ && (obs.dead || SocialFoe(socialPeerName_) ||
        (socialGroupUntilMs_ && obs.nowMs >= socialGroupUntilMs_))) {
        EndSocialGroup(client, "unsafe companion or outing finished");
        socialRestUntilMs_ = obs.nowMs + 300000;
    }
    const i64 journalNow = client.JournalNowMs();
    if (socialHeardMs_ == 0 || socialHeardMs_ < journalNow - 30000) socialHeardMs_ = journalNow - 30000;
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(socialHeardMs_, heard);
    if (!heard.empty()) socialHeardMs_ = heard.back().timeMs;
    for (const auto& h : heard) {
        if (!h.hasPosition || !client.KnownPlayer(h.speaker) || SocialFoe(h.name) ||
            TileDist(obs.x, obs.y, h.x, h.y) > 12) continue;
        const auto visible = std::find_if(players.begin(), players.end(), [&](const auto& p) { return p.serial == h.speaker; });
        if (visible == players.end() || (visible->noto != 1 && visible->noto != 2)) continue;
        if (socialPeer_ == h.speaker && h.text == state_.identity.characterName + ": Let's stop and regroup.") {
            EndSocialGroup(client, "companion requested a stop");
            socialRestUntilMs_ = obs.nowMs + 120000;
            continue;
        }
        if (!safe) continue;
        // Existing market matching decides actual demand, surplus and prices.
        // This listener only wakes that verified trading loop during other work.
        if (needCfg_.profession && tradePartner_ == 0) {
            market::TradeIntent wanted, fill;
            bool opportunity = market::ClassifyBuyLine(h.text, &wanted) == market::BuyLineKind::Announce &&
                market::AnswerBuyWant(*needCfg_.profession, obs.pack, state_.prices, tradePolicy_, wanted, &fill);
            if (wanted.Valid()) {
                for (const auto* producer : market::WhoProduces(wanted.item.c_str())) {
                    if (producer->id != needCfg_.profession->id) continue;
                    socialDemandItem_ = wanted.item;
                    socialDemandUntilMs_ = obs.nowMs + 180000;
                    state_.memory.NoteEvent("local_demand", wanted.item.c_str(), h.name.c_str(), obs.x, obs.y, obs.nowMs);
                    break;
                }
            }
            market::TradeIntent offer;
            if (market::ParseSellOffer(h.text, &offer)) {
                const std::vector<market::WornItem> worn = {{1, client.EquippedGraphicAt(1)}, {2, client.EquippedGraphicAt(2)}};
                opportunity = opportunity || market::ConsiderOffer(*needCfg_.profession, obs.pack,
                    obs.goldOnHand, tradePolicy_, offer, worn).accept;
            }
            if (opportunity) {
                socialMarketUntilMs_ = obs.nowMs + 20000;
                planner_.ClearCooldown(GoalKind::TradeWithPlayer);
                marketQuietUntilMs_ = 0;
                LogLine("social: local supply opportunity from %s", h.name.c_str());
            }
        }
        // A spar consent between two OTHER players: a bystander who still
        // wants Healing may go and bandage them (TickSparHealer).
        if (!socialPeer_ && h.speaker != client.PlayerSerial()) {
            const std::string consent = ": I consent to sparring. Stop when hurt.";
            if (h.text.size() > consent.size() &&
                h.text.compare(h.text.size() - consent.size(), consent.size(), consent) == 0) {
                const std::string leader = h.text.substr(0, h.text.size() - consent.size());
                u32 leaderSerial = 0;
                for (const auto& p : players) if (p.name == leader) leaderSerial = p.serial;
                if (leader != state_.identity.characterName && leaderSerial) {
                    sparWatchA_ = h.speaker; sparWatchB_ = leaderSerial;
                    sparWatchUntilMs_ = obs.nowMs + sparring::kBystanderWindowMs;
                    LogLine("social: %s and %s are sparring nearby", h.name.c_str(), leader.c_str());
                }
            }
        }
        social::Activity invitation = social::Activity::None;
        std::string invitationText = h.text;
        const std::string address = state_.identity.characterName + ": ";
        if (invitationText.compare(0, address.size(), address) == 0) invitationText.erase(0, address.size());
        if (social::IsInvitation(invitationText, &invitation) && socialPeer_ == 0 &&
            obs.nowMs >= socialRestUntilMs_ && !socialLeavePending_ && client.PartySize() == 0 &&
            (invitation != social::Activity::Spar ||
             (MaySocialSpar(needCfg_.profession) && obs.bandages >= sparring::kMinBandages &&
              static_cast<int>(client.SparringKitOf(client.PlayerSerial())) == social::SparInvitationKit(invitationText))) &&
            (invitation != social::Activity::Poison || sparring::PoisonReceiver(
                obs.SkillTenths(rules::kHealing), obs.SkillTenths(rules::kAnatomy), obs.bandages)) &&
            (invitation != social::Activity::Hunt || (needCfg_.profession && WantsToHunt(*needCfg_.profession)))) {
            // Lower serial leads if two players invite each other simultaneously.
            if (socialStartedMs_ && client.PlayerSerial() < h.speaker) continue;
            // A consent reply is latched to one peer below. Let it follow our
            // own invitation immediately: two simultaneous shouts must not
            // repeatedly suppress each other's replies through the chat timer.
            const std::string reply = social::JoinReply(h.name, invitation);
            client.ActionSay(reply.c_str());
            socialChatMs_ = obs.nowMs;
            LogLine("social: consent reply '%s'", reply.c_str());
            socialPeer_ = h.speaker; socialPeerName_ = h.name;
            socialActivity_ = invitation;
            if (invitation == social::Activity::Spar) sparKit_ = social::SparInvitationKit(invitationText);
            poisonStudent_ = false;
            socialConsented_ = true;
            socialAgreedMs_ = socialStartedMs_ = obs.nowMs;
            socialTrainingStart_ = obs.SkillSumTenths();
            socialTrainingSkills_ = obs.skills;
            socialMeetingX_ = h.x; socialMeetingY_ = h.y; socialMeetingPicked_ = true;
            planner_.ClearCooldown(GoalKind::Socialize);
        } else if (socialStartedMs_ && !socialConsented_ && socialPeer_ == 0 &&
                   (!socialPreferred_ || socialPreferred_ == h.speaker) &&
                   h.text == social::JoinReply(state_.identity.characterName, socialActivity_)) {
            socialPeer_ = h.speaker; socialPeerName_ = h.name;
            socialConsented_ = true; socialAgreedMs_ = obs.nowMs;
        } else if ((h.text == "Hello." || h.text == "Hello!" || h.text == "Hi." ||
                    h.text == "hello" || h.text == "hi") &&
                   SocialSay(client, obs.nowMs, h.name + ": Good to see you. I'm working nearby.")) {
            social::Remember(state_.memory.relationships, h.name, social::Encounter::Greeting, obs.nowMs);
        }
    }
    if (safe && !socialPeer_ && obs.nowMs - socialChatMs_ >= 60000) {
        for (const auto& person : players) {
            if (person.name.empty() || SocialFoe(person.name) ||
                (person.noto != 1 && person.noto != 2) ||
                TileDist(obs.x, obs.y, person.x, person.y) > 8 ||
                social::Find(state_.memory.relationships, person.name)) continue;
            // Speech does not own the action slot: a brief greeting can
            // accompany a productive bulk run without cancelling its work.
            if (SocialSay(client, obs.nowMs, person.name + ": Hello. I'm working nearby; let me know if you need supplies."))
                social::Remember(state_.memory.relationships, person.name, social::Encounter::Greeting, obs.nowMs);
            break;
        }
    }
    if (!safe && socialPeer_ && obs.HpFraction() < 0.6)
        SocialSay(client, obs.nowMs, socialPeerName_ + ": Let's stop and regroup.");
}

void Runner::AddSocialNeeds(Client& client, const Observation& obs, std::vector<Need>& needs) {
    if (!social::SafeToSocialize(obs.dead, obs.underAttack || obs.attackersOnMe > 0,
                               obs.hostilesNear, obs.HpFraction(), obs.WeightFraction())) return;
    if (socialMarketUntilMs_ > obs.nowMs) {
        Need n; n.kind = NeedKind::NeedTrade; n.urgency = 1;
        n.what = "nearby supplier or buyer"; n.reason = "heard a matching offer during ordinary work";
        auto existing = std::find_if(needs.begin(), needs.end(), [](const auto& old) { return old.kind == NeedKind::NeedTrade; });
        if (existing == needs.end()) needs.push_back(n);
        else *existing = n;
    }
    if (socialDemandUntilMs_ > obs.nowMs) {
        // Demand raises only already-feasible production needs. It cannot
        // conjure tools/inputs, override reserves or promise an unpaid order.
        for (auto& need : needs) if (!need.blocked &&
            (need.kind == NeedKind::NeedCraft || need.kind == NeedKind::NeedOre ||
             need.kind == NeedKind::NeedLogs || need.kind == NeedKind::NeedCloth ||
             need.kind == NeedKind::NeedCatch || need.kind == NeedKind::NeedSmelt)) {
            need.urgency = std::min(1.0, need.urgency + 0.15);
            need.reason += "; nearby demand for " + socialDemandItem_;
        }
    }
    if (socialGroupUntilMs_) return;
    if (obs.nowMs < socialRestUntilMs_ || tradePartner_ || socialLeavePending_) return;
    if (!socialConsented_ && client.PartySize() != 0) return;
    if (client.ActionBusy() && planner_.Current().kind != GoalKind::Socialize) return;
    // Passers-by socialize locally. Do not divert a productive long-distance trip.
    if (!socialConsented_ && client.TravelBusy() && planner_.Current().kind != GoalKind::Socialize) return;
    if (!socialConsented_ && client.PlayersNearby(12) == 0 && planner_.Current().kind != GoalKind::Socialize) return;
    Need n; n.kind = NeedKind::NeedSocial;
    n.urgency = socialConsented_ ? 1.0 : 0.65;
    n.what = socialConsented_ ? "meet an agreed companion" : "local company";
    n.reason = "train, hunt or meet nearby players between work loops";
    needs.push_back(n);
}

bool Runner::DoSocialize(Client& client, const Observation& obs) {
    if (!socialStartedMs_) {
        socialStartedMs_ = obs.nowMs;
        socialTrainingStart_ = obs.SkillSumTenths();
        socialTrainingSkills_ = obs.skills;
        socialActivity_ = needCfg_.profession && WantsToHunt(*needCfg_.profession) &&
            (client.PlayerSerial() + state_.identity.sessions) % 2 == 0
            ? social::Activity::Hunt : social::Activity::Train;
        // Every fighter type may spar (owner ruling 2026-09-04): fists need no
        // armour, a training weapon needs full iron.
        if (MaySocialSpar(needCfg_.profession) && obs.bandages >= sparring::kMinBandages &&
            client.SparringKit(client.PlayerSerial())) {
            socialActivity_ = social::Activity::Spar;
            sparKit_ = static_cast<int>(client.SparringKitOf(client.PlayerSerial()));
        }
        if (WantsPoisonPractice(obs) && PickPoisonOpener(client, obs) >= 0) {
            socialActivity_ = social::Activity::Poison;
            poisonStudent_ = true;
        }
    }
    // Meeting place + consent + party formation budget. A spar gets longer:
    // its own 5-minute clock only starts once the party exists.
    if (!sparActive_ && obs.nowMs - socialStartedMs_ > (socialActivity_ == social::Activity::Spar ? 180000 : 120000)) {
        const bool gained = socialTrainingGains_ > 0 || social::TrainingGains(socialTrainingSkills_, obs.skills) > 0;
        EndSocialGroup(client, gained ? "training gains confirmed" : "meeting window finished");
        socialRestUntilMs_ = obs.nowMs + 300000;
        planner_.Cooldown(GoalKind::Socialize, socialRestUntilMs_);
        if (gained) { planner_.NoteProgress(); return true; }
        planner_.Finish(false, "no completed group work in this meeting", obs.nowMs);
        return false;
    }
    if (client.ActionBusy()) return false;
    if (!socialConsented_) {
        if (!socialMeetingPicked_) {
            // Choose among nearby civic places, spreading meetings across town.
            // Keep travel local so socializing cannot become another cross-map errand.
            // Sparring is deliberately not a bank-counter activity.  The
            // counter is where a character settles a transaction, not where
            // two armed players can exchange ready messages, form a party and
            // train without a crowd stealing target cursors.  Pick a guarded
            // town centre or inn for a spar, while ordinary social work can
            // still use the bank as a genuine meeting point.
            std::vector<const wm::Place*> spots;
            if (const auto* atlas = client.WorldAtlas()) for (const auto& p : atlas->Places()) {
                if (p.position.map != 0 || !client.PlaceGuarded(p) ||
                    TileDist(obs.x, obs.y, p.position.x, p.position.y) > 64) continue;
                const bool civic = p.category == wm::PlaceCategory::Bank ||
                    p.category == wm::PlaceCategory::Inn ||
                    p.category == wm::PlaceCategory::TownCenter;
                if (!civic) continue;
                if (socialActivity_ == social::Activity::Spar &&
                    p.category == wm::PlaceCategory::Bank) continue;
                spots.push_back(&p);
            }
            std::sort(spots.begin(), spots.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
            socialMeetingX_ = obs.x; socialMeetingY_ = obs.y;
            if (!spots.empty()) {
                const auto* p = spots[(client.PlayerSerial() / 2 + socialMeetingRotation_++) % spots.size()];
                socialMeetingX_ = p->position.x; socialMeetingY_ = p->position.y;
                LogLine("social: meeting place=%s", p->name.c_str());
            }
            socialMeetingPicked_ = true;
        }
        if (client.TravelBusy()) return false;
        if (TileDist(obs.x, obs.y, socialMeetingX_, socialMeetingY_) > 5) {
            client.TravelToPoint(socialMeetingX_, socialMeetingY_, 4, "town social meeting");
            return false;
        }
        if (obs.nowMs - socialScanMs_ > 30000) {
            socialScanMs_ = obs.nowMs; client.ActionScanMobiles(); return false;
        }
        if (client.PlayersNearby(12) > 0 && obs.nowMs - socialChatMs_ >= 60000) {
            std::string preferred;
            socialPreferred_ = 0;
            i32 bestTrust = 9;
            std::vector<Client::HostileHit> company;
            client.NearbyPlayers(12, company);
            if (obs.nowMs - socialStartedMs_ < 60000) for (const auto& p : company) {
                const auto* relation = social::Find(state_.memory.relationships, p.name);
                if (!relation || relation->foe || relation->trust <= bestTrust ||
                    (p.noto != 1 && p.noto != 2)) continue;
                bestTrust = relation->trust; socialPreferred_ = p.serial; preferred = p.name + ": ";
            }
            SocialSay(client, obs.nowMs, preferred + (socialActivity_ == social::Activity::Hunt
                ? "Anyone for a graveyard hunt? Meet here."
                : socialActivity_ == social::Activity::Poison ? "Anyone for Poison spell practice? Healing and Anatomy above 60 required. Cure between casts."
                : socialActivity_ == social::Activity::Spar ? (sparKit_ == 1 ? social::kFistSparInvitation : social::kIronSparInvitation)
                : "Anyone for training and healing practice? Meet here."));
        }
        return false;
    }

    i32 px = 0, py = 0;
    if (!client.MobilePosition(socialPeer_, &px, &py) || SocialFoe(socialPeerName_)) {
        EndSocialGroup(client, "companion left sight");
        socialRestUntilMs_ = obs.nowMs + 120000;
        planner_.Finish(false, "companion unavailable", obs.nowMs); return false;
    }
    if (client.TravelBusy()) return false;
    if (TileDist(obs.x, obs.y, px, py) > 3) {
        client.ActionGotoMobile(socialPeer_, 2); return false;
    }
    if (!client.PartyContains(socialPeer_)) {
        // A party target cursor competes with the very chat and crowd that
        // formed the agreement.  Thirty seconds was enough for a quiet test
        // pair but not a live city gathering; keep the agreement valid for a
        // minute, still bounded so a departed companion cannot hold the goal.
        if (obs.nowMs - socialAgreedMs_ > 60000 || client.PartySize() > 0) {
            EndSocialGroup(client, "party membership not confirmed");
            socialRestUntilMs_ = obs.nowMs + 120000;
            planner_.Finish(false, "party confirmation timed out", obs.nowMs); return false;
        }
        if (client.PartyInviter() == socialPeer_) {
            client.ActionPartyAccept(socialPeer_); socialOwnParty_ = true;
        } else if (client.PlayerSerial() < socialPeer_) {
            if (!socialInviteMs_) {
                socialTargetGeneration_ = client.TargetGeneration();
                socialInviteMs_ = obs.nowMs;
                client.ActionPartyInvite();
            } else if (client.TargetActive() && client.TargetGeneration() != socialTargetGeneration_) {
                client.ActionTargetObject(socialPeer_); socialOwnParty_ = true;
            }
        }
        return false;
    }
    social::Remember(state_.memory.relationships, socialPeerName_, social::Encounter::Greeting, obs.nowMs);
    socialLastSeenMs_ = obs.nowMs;
    if (socialActivity_ == social::Activity::Hunt) {
        socialGroupUntilMs_ = obs.nowMs + 10 * 60000;
        SocialSay(client, obs.nowMs, socialPeerName_ + ": Ready. Stay together and regroup if hurt.");
        LogLine("social: confirmed hunting party with %s leader=0x%08X", socialPeerName_.c_str(), client.PartyLeader());
        planner_.Cooldown(GoalKind::Socialize, socialGroupUntilMs_);
        return HandOff(GoalKind::Socialize, GoalKind::TrainCombat, 1000, "party ready to hunt", obs.nowMs);
    }
    if (socialActivity_ == social::Activity::Poison) {
        socialGroupUntilMs_ = obs.nowMs + 120000;
        socialHeardMs_ = client.JournalNowMs();
        return false;
    }
    if (socialActivity_ == social::Activity::Spar) {
        // The meeting clock starts HERE, when the party exists -- not at the
        // invitation. v1 spent its whole budget walking and forming the party.
        sparActive_ = true;
        sparMeetingStartMs_ = obs.nowMs;
        socialHeardMs_ = client.JournalNowMs();
        LogLine("sparring: party formed with %s (%s)", socialPeerName_.c_str(),
                sparring::KitName(static_cast<sparring::Kit>(sparKit_)));
        return false;
    }
    // Real skill use and real healing, never unconsented attacks on a player.
    // Party membership alone does not authorize sparring or lethal weapons.
    const i32 gain = social::TrainingGains(socialTrainingSkills_, obs.skills);
    socialTrainingSkills_ = obs.skills;
    if (gain > 0) {
        LogLine("social: group training gained %d tenths with %s", gain, socialPeerName_.c_str());
        socialTrainingGains_ += gain;
        planner_.NoteProgress();
    }
    if (socialPracticeMs_ && obs.nowMs - socialPracticeMs_ < 10000) {
        if (client.TargetActive() && client.TargetGeneration() != socialTargetGeneration_)
            client.ActionTargetObject(socialPeer_);
        return false;
    }
    std::vector<Client::HostileHit> peers;
    client.NearbyPlayers(3, peers);
    for (const auto& peer : peers) if (peer.serial == socialPeer_ && peer.hpMax > 0 &&
        peer.hpCur > 0 && peer.hpCur < peer.hpMax && obs.bandages > 0) {
        const u32 bandage = client.FindBackpackItemByGraphic(0x0E21);
        if (bandage) {
            client.ActionUseBandage(bandage, peer.serial);
            socialPracticeMs_ = obs.nowMs;
            SocialSay(client, obs.nowMs, socialPeerName_ + ": Hold still, I'll bandage you.");
            return false;
        }
    }
    client.RequestMobileStatus(socialPeer_);
    if (needCfg_.profession) {
        const CraftIntent craft = ChooseCraft(*needCfg_.profession, obs, 1, &craftFocus_, state_.productionBatch.item.c_str());
        if (craft.item) {
            socialGroupUntilMs_ = obs.nowMs + 180000;
            socialTrainingStart_ = obs.SkillSumTenths();
            SocialSay(client, obs.nowMs, socialPeerName_ + ": I'll work on my crafting. Let me know what supplies you need.");
            return HandOff(GoalKind::Socialize, GoalKind::Craft, 180000,
                           "practice the profession with a nearby companion", obs.nowMs);
        }
    }
    for (const auto& skill : state_.plan.skills) {
        if (obs.SkillTenths(skill.skillId) >= skill.tenths) continue;
        if (skill.skillId != rules::kAnatomy && skill.skillId != rules::kEvaluatingIntel &&
            !(skill.skillId == rules::kMeditation && obs.mana < obs.manaMax)) continue;
        socialTargetGeneration_ = client.TargetGeneration();
        socialPracticeMs_ = obs.nowMs;
        client.ActionUseSkill(skill.skillId, skill.skillId == rules::kMeditation ? 0 : socialPeer_);
        return false;
    }
    socialPracticeMs_ = obs.nowMs;
    SocialSay(client, obs.nowMs, socialPeerName_ + ": I'm ready to help with healing while you practice.");
    return false;
}

bool Runner::FollowHuntingParty(Client& client, const Observation& obs) {
    if (!socialGroupUntilMs_ || socialActivity_ != social::Activity::Hunt || !socialPeer_) return false;
    if (!client.PartyContains(socialPeer_) || obs.nowMs >= socialGroupUntilMs_ || SocialFoe(socialPeerName_)) {
        EndSocialGroup(client, "party ended"); return false;
    }
    i32 x = 0, y = 0;
    if (!client.MobilePosition(socialPeer_, &x, &y)) {
        if (obs.nowMs - socialLastSeenMs_ > 20000) EndSocialGroup(client, "separated for twenty seconds");
        return socialPeer_ != 0;
    }
    socialLastSeenMs_ = obs.nowMs;
    const i32 distance = TileDist(obs.x, obs.y, x, y);
    if (client.PartyLeader() != client.PlayerSerial()) {
        if (distance > 3 && !client.ActionBusy() && !client.GotoBusy() && !client.TravelBusy())
            client.ActionGotoMobile(socialPeer_, 2);
        // Let nearby combat targeting run; suppress independent long trips below.
        return distance > 3;
    }
    if (distance > 8 && obs.hostilesNear == 0) {
        if (client.TravelBusy()) client.TravelAbort("wait for hunting companion");
        SocialSay(client, obs.nowMs, socialPeerName_ + ": I'm waiting here. Catch up when safe.");
        return true;
    }
    return false;
}


bool Runner::WantsPoisonPractice(const Observation& obs) const {
    if (!needCfg_.profession || !WantsSpellCombat(*needCfg_.profession)) return false;
    for (const auto& target : state_.plan.skills)
        if (target.skillId == rules::kPoisoning && obs.SkillTenths(target.skillId) < target.tenths) return true;
    return false;
}

bool Runner::TickPoisonPractice(Client& client, const Observation& obs) {
    if (socialActivity_ != social::Activity::Poison || !socialGroupUntilMs_) return false;
    auto end = [&](const char* reason) {
        client.ActionSay((socialPeerName_ + ": Let's stop and regroup.").c_str());
        const bool gained = socialTrainingGains_ > 0;
        EndSocialGroup(client, reason);
        socialRestUntilMs_ = obs.nowMs + 300000;
        planner_.Finish(gained, reason, obs.nowMs);
    };
    if (obs.dead || client.SparringExternalThreat(socialPeer_)) {
        end("Poison practice interrupted by danger"); return false;
    }
    if (obs.nowMs - sparPollMs_ >= 1000) {
        sparPollMs_ = obs.nowMs;
        client.RequestMobileStatus(client.PlayerSerial()); client.RequestMobileStatus(socialPeer_);
    }
    const i32 gains = social::TrainingGains(socialTrainingSkills_, obs.skills);
    socialTrainingSkills_ = obs.skills;
    if (gains > 0) {
        socialTrainingGains_ += gains; planner_.NoteProgress();
        LogLine("poison_practice: server confirmed %d skill tenths", gains);
    }
    // Finish curing even if the meeting expires or the partner leaves.
    if (!poisonStudent_ && client.MobilePoisoned(client.PlayerSerial())) {
        poisonSeen_ = true;
        if (!client.ActionBusy() && obs.nowMs - socialPracticeMs_ >= 10000) {
            const u32 bandage = client.FindBackpackItemByGraphic(0x0E21);
            if (!bandage) { end("no bandages left to cure"); return false; }
            client.ActionUseBandage(bandage, client.PlayerSerial());
            socialPracticeMs_ = obs.nowMs;
            LogLine("poison_practice: curing after round %d", poisonRound_);
        }
        return true;
    }
    if (!socialConsented_ || !client.PartyContains(socialPeer_) || client.PartySize() != 2 ||
        SocialFoe(socialPeerName_) || obs.nowMs >= socialGroupUntilMs_ ||
        (cfg_.sessionLimitMs > 0 && obs.nowMs-sessionStartMs_ >= cfg_.sessionLimitMs)) {
        end("Poison practice meeting ended"); return false;
    }
    if (!poisonStudent_ && poisonSeen_) {
        poisonSeen_ = false; ++poisonRound_;
        LogLine("poison_practice: cure confirmed before next round");
    }
    const std::string ready = ": Ready to receive Poison round " + std::to_string(poisonRound_) + ".";
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(socialHeardMs_, heard);
    for (const auto& h : heard) {
        socialHeardMs_ = std::max(socialHeardMs_, h.timeMs);
        if (h.speaker != socialPeer_) continue;
        if (h.text == state_.identity.characterName + ": Let's stop and regroup.") {
            end("partner withdrew Poison practice consent"); return false;
        }
        if (h.text == state_.identity.characterName + ready) poisonReadyMs_ = obs.nowMs;
    }
    if (client.ActionBusy()) return true;
    if (!poisonStudent_) {
        if (poisonRound_ > 3 || !sparring::PoisonReceiver(obs.SkillTenths(rules::kHealing),
            obs.SkillTenths(rules::kAnatomy), obs.bandages)) {
            end("healer practice complete or supplies below reserve"); return false;
        }
        if (obs.HpFraction() < 0.90) {
            if (obs.nowMs-socialPracticeMs_ >= 10000) {
                client.ActionUseBandage(client.FindBackpackItemByGraphic(0x0E21), client.PlayerSerial());
                socialPracticeMs_ = obs.nowMs;
            }
            return true;
        }
        if (client.PoisonPracticeReady(socialPeer_) && obs.nowMs-poisonReadyMs_ >= 4000) {
            client.ActionSay((socialPeerName_ + ready).c_str()); poisonReadyMs_ = obs.nowMs;
        }
        return true;
    }
    if (!WantsPoisonPractice(obs)) { end("Poisoning target reached"); return false; }
    if (socialGroupUntilMs_ - obs.nowMs < 30000 ||
        (cfg_.sessionLimitMs > 0 && cfg_.sessionLimitMs - (obs.nowMs-sessionStartMs_) < 30000))
        return true; // leave a cure window before wind-down; never start a last-second poison
    // One cast per numbered consent. A resisted cast cannot fabricate a cure
    // or advance the receiver's round; the meeting times out without credit.
    if (poisonRound_ <= 3 && poisonReadyMs_ > 0 && obs.nowMs-poisonReadyMs_ <= 4000 &&
        client.PoisonPracticeReady(socialPeer_)) {
        const int poison = PickPoisonOpener(client, obs);
        if (poison >= 0) {
            client.ActionCastSpell(poison, socialPeer_);
            LogLine("poison_practice: cast round %d on consenting healer %s", poisonRound_, socialPeerName_.c_str());
            ++poisonRound_; poisonReadyMs_ = 0;
        }
    }
    return true;
}

bool Runner::TickSparring(Client& client, const Observation& obs) {
    if (!sparActive_) return false;
    auto end = [&](const char* reason) {
        client.ActionSay((socialPeerName_ + ": Let's stop and regroup.").c_str());
        const bool gained = socialTrainingGains_ > 0;
        LogLine("sparring: ended with %s after %d round(s): %s", socialPeerName_.c_str(), sparRounds_, reason);
        EndSocialGroup(client, reason);
        socialRestUntilMs_ = obs.nowMs + 300000;
        planner_.Finish(gained, reason, obs.nowMs);
    };
    if (obs.dead || (cfg_.sessionLimitMs > 0 && obs.nowMs-sessionStartMs_ >= cfg_.sessionLimitMs)) {
        end("session ending"); return false;
    }

    const i32 gain = social::TrainingGains(socialTrainingSkills_, obs.skills);
    socialTrainingSkills_ = obs.skills;
    if (gain > 0) { socialTrainingGains_ += gain; planner_.NoteProgress();
        LogLine("sparring: confirmed %d skill tenths with %s", gain, socialPeerName_.c_str()); }

    std::vector<Client::Heard> heard;
    client.JournalHeardSince(socialHeardMs_, heard);
    for (const auto& h : heard) {
        socialHeardMs_ = std::max(socialHeardMs_, h.timeMs);
        if (h.speaker != socialPeer_) continue;
        if (h.text == state_.identity.characterName + ": Let's stop and regroup.") { end("companion withdrew consent"); return false; }
        if (h.text == state_.identity.characterName + ": " + social::kSparReady) sparPeerReady_ = true;
    }
    if (obs.nowMs-sparPollMs_ >= 1000) {
        sparPollMs_ = obs.nowMs;
        client.RequestMobileStatus(client.PlayerSerial()); client.RequestMobileStatus(socialPeer_);
    }
    // A round the client ended (safety stop or lease) is counted once.
    if (sparRoundStarted_ && !client.SparringPeer()) {
        sparRoundStarted_ = false;
        sparRoundEndMs_ = obs.nowMs;
        ++sparRounds_;
        LogLine("sparring: round %d stopped", sparRounds_);
    }

    std::vector<Client::HostileHit> peers;
    client.NearbyPlayers(3, peers);
    i32 peerPct = -1;
    for (const auto& p : peers) if (p.serial == socialPeer_ && p.hpMax > 0) peerPct = p.hpCur * 100 / p.hpMax;

    sparring::RoundSight sight;
    sight.nowMs = obs.nowMs;
    sight.meetingStartMs = sparMeetingStartMs_;
    sight.consented = socialConsented_;
    sight.inParty = client.PartyContains(socialPeer_);
    sight.peerReady = sparPeerReady_;
    sight.readyAskedMs = sparReadyMs_;
    sight.kitsCompatible = sparring::Compatible(client.SparringKitOf(client.PlayerSerial()),
                                                client.SparringKitOf(socialPeer_));
    sight.externalThreat = client.SparringExternalThreat(socialPeer_);
    sight.healthFresh = client.SparringHealthFresh(socialPeer_);
    sight.selfPct = obs.hpMax > 0 ? obs.hp * 100 / obs.hpMax : -1;
    sight.peerPct = peerPct;
    sight.bandages = obs.bandages;
    sight.roundActive = sparRoundStarted_;
    sight.lastRoundEndMs = sparRoundEndMs_;
    sight.roundsDone = sparRounds_;
    sight.busy = client.ActionBusy();

    const sparring::RoundPlan plan = sparring::DecideRound(sight);
    switch (plan.step) {
        case sparring::Step::End: end(plan.reason); return false;
        case sparring::Step::SayReady:
            client.ActionSay((socialPeerName_ + ": " + social::kSparReady).c_str());
            sparReadyMs_ = obs.nowMs;
            return true;
        case sparring::Step::Bandage:
            if (obs.nowMs-socialPracticeMs_ >= 10000) {
                const u32 bandage = client.FindBackpackItemByGraphic(0x0E21);
                if (bandage) { client.ActionUseBandage(bandage, client.PlayerSerial()); socialPracticeMs_ = obs.nowMs; }
            }
            return true;
        case sparring::Step::StartRound:
            // SparringReady re-checks range, fresh health and threats at the
            // very moment of the swing; a refusal just waits a tick.
            if (client.BeginSparringRound(socialPeer_)) {
                sparRoundStarted_ = true;
                LogLine("sparring: round %d started with %s", sparRounds_ + 1, socialPeerName_.c_str());
            }
            return true;
        case sparring::Step::Continue:
        case sparring::Step::Wait:
            return true;
    }
    return true;
}

// A bystander who heard two players agree to spar, and still wants Healing,
// walks over and bandages whichever of them is hurt -- the Revolution habit of
// farming Healing on sparring partners. Only from idle or wandering time, never
// during its own work, fights or trades.
bool Runner::TickSparHealer(Client& client, const Observation& obs) {
    if (!sparWatchUntilMs_) return false;
    if (obs.nowMs >= sparWatchUntilMs_ || socialPeer_ || sparActive_) { sparWatchUntilMs_ = 0; return false; }
    const GoalKind g = planner_.Current().kind;
    if (g != GoalKind::IdleBriefly && g != GoalKind::Explore && g != GoalKind::Socialize && g != GoalKind::ReturnHome)
        return false;
    if (obs.dead || obs.underAttack || obs.attackersOnMe > 0 || obs.HpFraction() < 0.8 ||
        obs.bandages < sparring::kMinBandages || client.ActionBusy()) return false;
    bool wantsHealing = false;
    for (const auto& skill : state_.plan.skills)
        if (skill.skillId == rules::kHealing && obs.SkillTenths(rules::kHealing) < skill.tenths) wantsHealing = true;
    if (!wantsHealing) { sparWatchUntilMs_ = 0; return false; }

    std::vector<Client::HostileHit> around;
    client.NearbyPlayers(12, around);
    const Client::HostileHit* patient = nullptr;
    i32 lowest = sparring::kBystanderHealBelow;
    for (const auto& p : around) {
        if ((p.serial != sparWatchA_ && p.serial != sparWatchB_) || p.hpMax <= 0 || p.hpCur <= 0) continue;
        const i32 pct = p.hpCur * 100 / p.hpMax;
        if (pct < lowest) { lowest = pct; patient = &p; }
    }
    if (!patient) return false;
    if (TileDist(obs.x, obs.y, patient->x, patient->y) > 1) { client.ActionGotoMobile(patient->serial, 1); return true; }
    if (obs.nowMs - sparHealMs_ < sparring::kBystanderBandageGapMs) return true;
    const u32 bandage = client.FindBackpackItemByGraphic(0x0E21);
    if (!bandage) return false;
    client.ActionUseBandage(bandage, patient->serial);
    sparHealMs_ = obs.nowMs;
    LogLine("sparring: bystander bandage on %s (%d%%)", patient->name.c_str(), lowest);
    social::Remember(state_.memory.relationships, patient->name, social::Encounter::Help, obs.nowMs);
    return true;
}

} // namespace uo::life
