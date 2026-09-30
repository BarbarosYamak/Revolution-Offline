#include "life/runner/RunnerInternal.h"
#include "uo/family.h"

#include <cctype>

// FAMILIES (uo/family.h, runtime/scripts/revolution/revolution_family.scp).
//
//   founder  saves 75k, buys a family deed, uses it, answers the server's
//            name prompt (0x9A) with a Turkish last name -- walking down its
//            own list if the server says a name is taken -- and is head once
//            its own NAME ends with that last name.
//   head     buys an invitation deed when a trusted friend stands near,
//            uses it on them, and tells new members "aile evi: <town>".
//   invitee  answers the server's "Aile daveti" gump: yes for a trusted
//            friend, no otherwise; a member once its own NAME carries the name.
//   everyone treats a passer-by whose name ends with our last name as family:
//            trusted, an ally in PvP, the first choice for a party.

namespace uo::life {
using namespace runner_detail;

namespace {
constexpr u16 kDeed = 0x14F0;

bool EndsWithSurname(const std::string& name, const std::string& surname) {
    return family::IsFamilyName(name, surname);
}

i32 TrustIn(const Memory& memory, const std::string& name) {
    const auto* r = social::Find(memory.relationships, name);
    return r && !r->foe ? r->trust : 0;
}
}  // namespace

bool Runner::FamilyMember(const std::string& name) const {
    return !state_.family.surname.empty() && EndsWithSurname(name, state_.family.surname);
}

void Runner::TickFamily(Client& client, const Observation& obs) {
    if (obs.nowMs - familyTickMs_ < 2000) return;
    familyTickMs_ = obs.nowMs;
    auto& fam = state_.family;
    const std::string me = client.PlayerName();

    // The server's invitation gump: accept a trusted friend, decline anyone else.
    if (client.GumpActive() && family::IsInviteGump(client.GumpTexts())) {
        const auto& t = client.GumpTexts();
        const std::string inviter = t[1], surname = t[2];
        const bool yes = family::AcceptInvite(!fam.surname.empty(), TrustIn(state_.memory, inviter),
                                              SocialFoe(inviter));
        LogLine("family: %s invites us into the %s family -- %s", inviter.c_str(), surname.c_str(),
                yes ? "accepting" : "declining");
        client.AnswerGump(yes ? 1u : 0u, 0);
        if (yes) { familyPendingSurname_ = surname; familyPendingHead_ = inviter; }
        return;
    }

    // Proof: our own name now carries the last name the server gave us.
    if (fam.surname.empty() && !familyPendingSurname_.empty() && EndsWithSurname(me, familyPendingSurname_)) {
        fam.surname = familyPendingSurname_;
        fam.head = familyPendingHead_.empty();
        fam.headName = fam.head ? me : familyPendingHead_;
        familyPendingSurname_.clear(); familyPendingHead_.clear();
        LogLine("family: we are now of the %s family%s", fam.surname.c_str(), fam.head ? ", as its head" : "");
        Checkpoint(client, obs.nowMs, "family joined");
    }
    if (fam.surname.empty()) return;

    // Family is known by its name: trusted, and remembered as members.
    std::vector<Client::HostileHit> players;
    client.NearbyPlayers(18, players);
    for (const auto& p : players) {
        if (p.name.empty() || !EndsWithSurname(p.name, fam.surname)) continue;
        social::Relationship* r = nullptr;
        for (auto& rel : state_.memory.relationships) if (rel.name == p.name) r = &rel;
        if (!r) { social::Remember(state_.memory.relationships, p.name, social::Encounter::Greeting, obs.nowMs);
                  for (auto& rel : state_.memory.relationships) if (rel.name == p.name) r = &rel; }
        if (r && !r->foe && r->trust < 5) r->trust = 5;
        if (std::find(fam.members.begin(), fam.members.end(), p.name) == fam.members.end()) {
            fam.members.push_back(p.name);
            LogLine("family: %s carries our name", p.name.c_str());
            if (fam.head && !state_.homeCity.empty())
                client.ActionSay(family::HomeCall(state_.homeCity).c_str());
        }
    }

    // A member hears where the family lives, from the head.
    if (!fam.head) {
        const i64 now = client.JournalNowMs();
        if (familyHeardMs_ == 0 || familyHeardMs_ < now - 30000) familyHeardMs_ = now - 30000;
        std::vector<Client::Heard> heard;
        client.JournalHeardSince(familyHeardMs_, heard);
        if (!heard.empty()) familyHeardMs_ = heard.back().timeMs;
        for (const auto& h : heard) {
            std::string city;
            if (h.name == fam.headName && family::ParseHomeCall(h.text, &city) && city != state_.homeCity) {
                LogLine("family: moving home to %s, where the family lives", city.c_str());
                state_.homeCity = city;
            }
        }
    }
}

// GUILDMATES. The guild stone scripts are not in this repo, so a bot cannot
// found or join a guild yet (UNKNOWN menus; docs/M5_14_GUILDS.md). What it can
// do is what any player does: read the "[ABC]" guild tag the server shows in
// name labels, and treat someone with its own tag -- or the server's
// guild-green notoriety -- as a guildmate: trusted, a PvP ally, a first pick
// for a party.
bool Runner::GuildMate(Client& client, u32 serial, u8 noto) const {
    if (noto == 2) return true;
    const std::string mine = client.MobileGuildTag(client.PlayerSerial());
    return !mine.empty() && client.MobileGuildTag(serial) == mine;
}

void Runner::TickGuild(Client& client, const Observation& obs) {
    if (obs.nowMs - guildTickMs_ < 10000) return;
    guildTickMs_ = obs.nowMs;
    if (!guildSelfLooked_) { guildSelfLooked_ = true; client.ActionLookAt(client.PlayerSerial()); return; }
    const std::string mine = client.MobileGuildTag(client.PlayerSerial());
    if (mine != guildTag_) {
        guildTag_ = mine;
        if (!mine.empty()) LogLine("guild: we wear the [%s] tag", mine.c_str());
    }
    std::vector<Client::HostileHit> players;
    client.NearbyPlayers(18, players);
    for (const auto& p : players) {
        if (p.name.empty() || !GuildMate(client, p.serial, p.noto)) continue;
        social::Relationship* r = nullptr;
        for (auto& rel : state_.memory.relationships) if (rel.name == p.name) r = &rel;
        if (!r) {
            social::Remember(state_.memory.relationships, p.name, social::Encounter::Greeting, obs.nowMs);
            for (auto& rel : state_.memory.relationships) if (rel.name == p.name) r = &rel;
        }
        if (r && !r->foe && r->trust < 4) r->trust = 4;
    }
}

void Runner::AddFamilyNeeds(Client& client, const Observation& obs, std::vector<Need>& needs) {
    if (obs.dead || obs.underAttack || obs.attackersOnMe > 0 || !needCfg_.profession) return;
    const auto& fam = state_.family;
    family::FounderSight s;
    s.inFamily = !fam.surname.empty();
    s.totalGold = obs.gold;
    s.sociability = state_.persona.sociability;
    for (const auto& r : state_.memory.relationships) s.closeFriends += !r.foe && r.trust >= family::kMinTrustToInvite;
    Need n;
    n.kind = NeedKind::NeedFamily;
    if (family::WantsToFound(s) && obs.nowMs >= familyRestUntilMs_) {
        n.what = "a family"; n.urgency = 0.30; n.reason = "rich enough, with friends to invite";
        needs.push_back(n);
        return;
    }
    if (family::CanInvite(fam.head, static_cast<int>(fam.members.size()) + 1, obs.gold) &&
        obs.nowMs >= familyRestUntilMs_ && FamilyInviteCandidate(client, obs)) {
        n.what = "invite a friend"; n.urgency = 0.60; n.reason = "a trusted friend stands near";
        needs.push_back(n);
    }
}

u32 Runner::FamilyInviteCandidate(Client& client, const Observation& obs) const {
    std::vector<Client::HostileHit> players;
    client.NearbyPlayers(8, players);
    for (const auto& p : players) {
        if (p.name.empty() || (p.noto != 1 && p.noto != 2) || FamilyMember(p.name)) continue;
        // Anyone already carrying SOME family name is taken: a two-word name
        // is a family name on this shard (UNKNOWN whether single names had
        // spaces; the server refuses a second family anyway).
        if (TrustIn(state_.memory, p.name) >= family::kMinTrustToInvite &&
            TileDist(obs.x, obs.y, p.x, p.y) <= 8)
            return p.serial;
    }
    return 0;
}

bool Runner::DoFamily(Client& client, const Observation& obs) {
    auto& fam = state_.family;
    const bool founding = fam.surname.empty();
    // Answer the server's name prompt with our next last name.
    if (founding && client.PromptActive()) {
        const std::string name = family::ChooseSurname(state_.identity.identityId, fam.surnameAttempt++);
        LogLine("family: naming the family %s", name.c_str());
        client.ActionAnswerPrompt(name);
        familyPendingSurname_ = name;
        familyPendingHead_.clear();
        familyPromptMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    if (founding && !familyPendingSurname_.empty()) {
        if (obs.nowMs - familyPromptMs_ < 8000) { nextActionMs_ = obs.nowMs + 1000; return false; }
        LogLine("family: the name %s was not accepted", familyPendingSurname_.c_str());
        familyPendingSurname_.clear();         // try again with the next name
    }
    if (!founding && !fam.head) return true;

    // Answer an invitation cursor the deed armed.
    if (familyCursorTarget_) {
        if (client.TargetActive()) {
            client.ActionTargetObject(familyCursorTarget_);
            familyCursorTarget_ = 0;
            nextActionMs_ = obs.nowMs + 2000;
            return true;                       // the server shows them the gump; their answer decides
        }
        if (obs.nowMs - familyCursorMs_ < 5000) return false;
        familyCursorTarget_ = 0;
    }

    const char* want = founding ? "family deed" : "family invitation";
    const i32 price = founding ? family::kFoundDeedGold : family::kInviteDeedGold;
    u32 deed = 0;
    for (usize i = 0; i < client.ContainerItemCount(client.BackpackSerial()); ++i) {
        u32 s = 0; u16 g = 0, a = 0;
        if (!client.ContainerItemAt(client.BackpackSerial(), i, &s, &g, &a) || g != kDeed) continue;
        const std::string* label = client.ServerItemName(s);
        if (!label) { client.ActionLookAt(s); nextActionMs_ = obs.nowMs + 1500; return false; }
        std::string l = *label;
        for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (l.find(want) != std::string::npos) deed = s;
    }
    if (deed) {
        if (founding) {
            LogLine("family: using the family deed");
            client.ActionUseObject(deed);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        const u32 friendSerial = FamilyInviteCandidate(client, obs);
        if (!friendSerial) return true;        // nobody to invite right now; keep the deed
        familyCursorTarget_ = friendSerial;
        familyCursorMs_ = obs.nowMs;
        LogLine("family: inviting a friend");
        client.ActionUseObject(deed);
        nextActionMs_ = obs.nowMs + 500;
        return false;
    }

    // Buy the deed. Which NPC sold family deeds is UNKNOWN; a banker and a
    // provisioner are asked, and a shop that lists none rests this for a day.
    if (FetchCoinForPurchase(client, obs, price + 500)) return false;
    if (!familyErrand_.Running()) {
        life::VendorErrandSpec spec;
        spec.Sell("banker", wm::Service::Banker);
        spec.Sell("provisioner", wm::Service::Provisioner);
        spec.graphic = kDeed;
        spec.nameContains = want;
        spec.qty = 1;
        spec.what = want;
        spec.maxPricePerUnit = price * 3 / 2;
        familyErrand_.Begin(spec);
    }
    const life::VendorErrandResult r = familyErrand_.Tick(client, obs);
    LogErrandReason(want, r.why.c_str(), obs.nowMs);
    if (r.wake == life::Wake::AfterDelay && r.delayMs > 0) nextActionMs_ = obs.nowMs + r.delayMs;
    if (life::IsTerminal(r.status)) {
        familyErrand_.Cancel();
        if (r.status != life::ActivityStatus::Success) {
            familyRestUntilMs_ = obs.nowMs + 24LL * 60 * 60000;
            return BlockNeed(GoalKind::Family, NeedKind::NeedFamily, BlockScope::Window,
                             "no NPC here sells family deeds", 24LL * 60 * 60000, obs.nowMs);
        }
    }
    if (r.acted) planner_.NoteProgress();
    return false;
}

}  // namespace uo::life
