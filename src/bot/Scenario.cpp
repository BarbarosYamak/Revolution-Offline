#include "bot/Scenario.h"

#include "Client.h"
#include "uo/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace uo::bot {

namespace {

bool ParseDir(const std::string& tok, u8* out) {
    static const struct { const char* name; u8 dir; } kDirs[] = {
        {"n", 0}, {"north", 0}, {"ne", 1}, {"northeast", 1},
        {"e", 2}, {"east",  2}, {"se", 3}, {"southeast", 3},
        {"s", 4}, {"south", 4}, {"sw", 5}, {"southwest", 5},
        {"w", 6}, {"west",  6}, {"nw", 7}, {"northwest", 7},
    };
    for (const auto& d : kDirs) {
        if (tok == d.name) { *out = d.dir; return true; }
    }
    return false;
}

// Indexed by Scenario::Op. Scenario::Load static_asserts the length against
// Op::Count -- Op is private, so the check lives in a member function.
const char* const kOpNames[] = {
    "wait_world", "walk", "wait_walk", "say", "backpack",
    "wait_backpack", "sleep", "hold", "logout", "goto", "wait_goto",
    "use", "open", "move", "drop_ground", "equip", "unequip", "skill",
    "cast", "attack", "war", "bandage", "bank", "vendor_open", "vendor_buy", "target",
    "target_ground", "target_cancel", "wait_action", "expect",
    "wait_target", "resurrect", "remember", "require", "cast_scroll",
    "goto_mobile", "wait_dead", "wait_alive",
    "mark_hp", "expect_hp_gain", "wait_hp_below",
    "mark_item", "expect_item_drop",
    "mark_gold", "expect_gold_gain", "vendor_sell_open", "vendor_sell",
    "scan_mobiles",
    // Index-aligned with Scenario::Op. Append here, never insert.
    "gait",
    "travel_point", "travel_place", "travel_region", "travel_service",
    "travel_resource", "travel_entity", "travel_corpse", "travel_home",
    "set_home", "wait_travel", "expect_travel", "use_moongates",
    "ensure_peace", "expect_peace", "expect_war",
    "expect_region", "expect_place", "expect_service_known",
};

constexpr usize kOpNameCount = sizeof(kOpNames) / sizeof(kOpNames[0]);

const char* OpName(int op) {
    return (op >= 0 && static_cast<usize>(op) < kOpNameCount) ? kOpNames[op]
                                                              : "?";
}

// `gait walk|run|auto`. Anything else is a load-time error, not a silent
// fallback -- a typo'd gait would otherwise change how the whole run looks.
bool ParseGait(const std::string& tok, sphere::Gait* out) {
    if (tok == "walk") { *out = sphere::Gait::Walk; return true; }
    if (tok == "run")  { *out = sphere::Gait::Run;  return true; }
    if (tok == "auto") { *out = sphere::Gait::Auto; return true; }
    return false;
}

bool ParseResultName(const std::string& s, act::Result* out) {
    static const struct { const char* name; act::Result r; } kMap[] = {
        {"success",        act::Result::Success},
        {"timeout",        act::Result::Timeout},
        {"rejected",       act::Result::Rejected},
        {"invalid_state",  act::Result::InvalidState},
        {"unavailable",    act::Result::Unavailable},
        {"server_failure", act::Result::ServerFailure},
    };
    for (const auto& m : kMap) {
        if (s == m.name) { *out = m.r; return true; }
    }
    return false;
}

bool ContainsNoCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return false;
    auto lower = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };
    return lower(hay).find(lower(needle)) != std::string::npos;
}

}  // namespace

void Scenario::FailTravelStart(Client& client, const Step& st,
                               const char* verb) {
    LogError("[scenario] %s could not resolve a destination: %s (line %d); "
             "aborting\n", verb, client.TravelFailureText(), st.line);
    failed_ = true;
}

void Scenario::Bind(const std::string& name, u32 serial) {
    for (auto& b : binds_) {
        if (b.first == name) { b.second = serial; return; }
    }
    binds_.emplace_back(name, serial);
}

// Operands are either a literal serial, a previously bound @name, or a
// keyword the client can resolve from its own server-driven state. Nothing
// here invents world knowledge: every keyword reads what the server sent.
u32 Scenario::Resolve(Client& client, const std::string& tok) const {
    if (tok.empty()) return 0;

    if (tok[0] == '@') {
        const std::string name = tok.substr(1);
        for (const auto& b : binds_) {
            if (b.first == name) return b.second;
        }
        return 0;
    }
    if (tok == "self")     return client.PlayerSerial();
    if (tok == "backpack") return client.BackpackSerial();
    if (tok == "bank")     return client.BankContainer();
    if (tok == "vendor")   return client.VendorOfferFrom();
    if (tok == "vendor_sell_first") {
        const auto& offer = client.VendorSellOffer();
        return offer.empty() ? 0u : offer.front().serial;
    }
    // vendor_sell_graphic:<hex> -- the scenario names exactly which item it
    // means, so the choice does not depend on the order Sphere happens to send.
    if (tok.rfind("vendor_sell_graphic:", 0) == 0) {
        const u16 want = static_cast<u16>(
            std::strtoul(tok.c_str() + 20, nullptr, 0));
        for (const auto& v : client.VendorSellOffer())
            if (v.graphic == want) return v.serial;
        return 0;
    }
    if (tok == "vendor_first") {
        const auto& offer = client.VendorOffer();
        return offer.empty() ? 0u : offer.front().serial;
    }
    // vendor_graphic:<hex> -- name the exact item to buy, the same way
    // vendor_sell_graphic: names the exact item to sell. Without this a
    // scenario can only take whatever Sphere happens to list first, which is
    // no way to ask for a recall rune.
    if (tok.rfind("vendor_graphic:", 0) == 0) {
        const u16 want = static_cast<u16>(
            std::strtoul(tok.c_str() + 15, nullptr, 0));
        for (const auto& v : client.VendorOffer())
            if (v.graphic == want) return v.serial;
        return 0;
    }
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
        return static_cast<u32>(std::strtoul(tok.c_str(), nullptr, 16));
    if (tok.find_first_not_of("0123456789") == std::string::npos)
        return static_cast<u32>(std::strtoul(tok.c_str(), nullptr, 10));
    return 0;
}

bool Scenario::Load(const char* path, std::string* err) {
    // kOpNames is indexed by Op, so a name appended out of order (or an op
    // inserted mid-enum) silently renames every step after it in the logs.
    // That bug has happened once; this is where it stops happening.
    static_assert(kOpNameCount == static_cast<usize>(Op::Count),
                  "kOpNames is out of sync with Scenario::Op");
    steps_.clear();
    binds_.clear();
    pc_ = 0;
    entered_ = false;
    failed_ = false;
    if (!path || !path[0]) {
        if (err) *err = "no scenario path";
        return false;
    }
    std::ifstream in(path);
    if (!in) {
        if (err) *err = std::string("cannot open '") + path + "'";
        return false;
    }

    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        const auto hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        const auto first = raw.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = raw.find_last_not_of(" \t\r\n");
        const std::string line = raw.substr(first, last - first + 1);

        std::istringstream ls(line);
        std::string verb;
        ls >> verb;

        Step st;
        st.line = lineNo;
        auto need = [&](std::string& dst, const char* what) -> bool {
            if (!(ls >> dst)) {
                if (err) *err = "line " + std::to_string(lineNo) + ": " +
                                verb + " needs " + what;
                return false;
            }
            return true;
        };

        if (verb == "wait_world")           st.op = Op::WaitWorld;
        else if (verb == "wait_walk")       st.op = Op::WaitWalk;
        else if (verb == "backpack")        st.op = Op::Backpack;
        else if (verb == "wait_backpack")   st.op = Op::WaitBackpack;
        else if (verb == "logout")          st.op = Op::Logout;
        else if (verb == "wait_goto")       st.op = Op::WaitGoto;
        else if (verb == "wait_action")     st.op = Op::WaitAction;
        else if (verb == "wait_target")     st.op = Op::WaitTarget;
        else if (verb == "target_cancel")   st.op = Op::TargetCancel;
        else if (verb == "resurrect")       st.op = Op::Resurrect;
        else if (verb == "wait_dead")       st.op = Op::WaitDead;
        else if (verb == "wait_alive")      st.op = Op::WaitAlive;
        else if (verb == "scan_mobiles")    st.op = Op::ScanMobiles;
        else if (verb == "mark_hp")         st.op = Op::MarkHp;
        else if (verb == "expect_hp_gain")  st.op = Op::ExpectHpGain;
        else if (verb == "mark_gold")       st.op = Op::MarkGold;
        else if (verb == "expect_gold_gain") st.op = Op::ExpectGoldGain;
        else if (verb == "wait_hp_below") {
            ls >> st.count;
            st.op = Op::WaitHpBelow;
        }
        else if (verb == "gait") {
            // Overrides the session gait for the rest of the scenario. The
            // per-step exceptions in BotStepGait still apply on top of it.
            if (!need(st.a, "<walk|run|auto>")) { steps_.clear(); return false; }
            sphere::Gait g = sphere::Gait::Auto;
            if (!ParseGait(st.a, &g)) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": gait needs walk|run|auto";
                steps_.clear();
                return false;
            }
            st.count = static_cast<int>(g);
            st.op = Op::SetGait;
        }
        else if (verb == "wait_travel")     st.op = Op::WaitTravel;
        else if (verb == "travel_corpse")   st.op = Op::TravelCorpse;
        else if (verb == "travel_home")     st.op = Op::TravelHome;
        else if (verb == "set_home")        st.op = Op::SetHome;
        else if (verb == "ensure_peace")    st.op = Op::EnsurePeace;
        else if (verb == "expect_peace")    st.op = Op::ExpectPeace;
        else if (verb == "expect_war")      st.op = Op::ExpectWar;
        else if (verb == "use_moongates") {
            if (!need(st.a, "<on|off>")) { steps_.clear(); return false; }
            st.op = Op::UseMoongates;
        }
        else if (verb == "expect_travel") {
            if (!need(st.a, "<ok|fail>")) { steps_.clear(); return false; }
            st.op = Op::ExpectTravel;
        }
        else if (verb == "travel_point") {
            int gx = 0, gy = 0, r = 1;
            ls >> gx >> gy;
            if (!(ls >> r)) r = 1;
            if (gx <= 0 || gy <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": travel_point needs <x> <y> [radius]";
                steps_.clear();
                return false;
            }
            st.op = Op::TravelPoint; st.x = gx; st.y = gy; st.count = r;
        }
        // The destination verbs take the REST of the line, because a place or
        // a region is named the way the shard names it ("Yew Bank", "Britain
        // Territory") and quoting every one of them would be noise.
        else if (verb == "travel_place" || verb == "travel_region" ||
                 verb == "travel_resource" || verb == "expect_region" ||
                 verb == "expect_place" || verb == "expect_service_known") {
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" \t");
            if (s0 == std::string::npos) {
                if (err) *err = "line " + std::to_string(lineNo) + ": " + verb +
                                " needs a name";
                steps_.clear();
                return false;
            }
            st.text = rest.substr(s0);
            st.op = (verb == "travel_place")    ? Op::TravelPlace
                  : (verb == "travel_region")   ? Op::TravelRegion
                  : (verb == "travel_resource") ? Op::TravelResource
                  : (verb == "expect_region")   ? Op::ExpectRegion
                  : (verb == "expect_place")    ? Op::ExpectPlace
                                                : Op::ExpectServiceKnown;
        }
        // `travel_service <service> [region...]` -- the optional tail narrows
        // it to one region, so "a banker" and "the banker in Yew" are both
        // sayable without naming a bank.
        else if (verb == "travel_service") {
            if (!need(st.a, "<service>")) { steps_.clear(); return false; }
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" \t");
            if (s0 != std::string::npos) st.text = rest.substr(s0);
            st.op = Op::TravelService;
        }
        else if (verb == "travel_entity") {
            if (!need(st.a, "<serial|@name>")) { steps_.clear(); return false; }
            if (!(ls >> st.count)) st.count = 2;
            st.op = Op::TravelEntity;
        }
        else if (verb == "mark_item" || verb == "expect_item_drop") {
            if (!need(st.a, "<graphic>")) { steps_.clear(); return false; }
            st.op = (verb == "mark_item") ? Op::MarkItem : Op::ExpectItemDrop;
        }
        else if (verb == "vendor_sell_open") {
            if (!need(st.a, "<vendor>")) { steps_.clear(); return false; }
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" \t");
            st.text = (s0 == std::string::npos) ? "sell" : rest.substr(s0);
            st.op = Op::VendorSellOpen;
        }
        else if (verb == "goto") {
            int gx = 0, gy = 0;
            ls >> gx >> gy;
            if (gx <= 0 || gy <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": goto needs <x> <y>";
                steps_.clear();
                return false;
            }
            st.op = Op::Goto; st.x = gx; st.y = gy;
        }
        else if (verb == "walk") {
            std::string dir; int count = 0;
            ls >> dir >> count;
            if (!ParseDir(dir, &st.dir) || count <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": walk needs <dir> <count>";
                steps_.clear();
                return false;
            }
            st.op = Op::Walk; st.count = count;
        }
        else if (verb == "say") {
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" \t");
            st.op = Op::Say;
            st.text = (s0 == std::string::npos) ? "" : rest.substr(s0);
        }
        else if (verb == "sleep" || verb == "hold") {
            long long ms = 0;
            ls >> ms;
            if (ms < 0) ms = 0;
            st.op = (verb == "sleep") ? Op::Sleep : Op::Hold;
            st.durationMs = static_cast<i64>(ms);
        }
        else if (verb == "use" || verb == "open" || verb == "attack" ||
                 verb == "target" || verb == "unequip") {
            if (!need(st.a, "<serial|@name>")) { steps_.clear(); return false; }
            st.op = (verb == "use")    ? Op::Use
                  : (verb == "open")   ? Op::Open
                  : (verb == "attack") ? Op::Attack
                  : (verb == "target") ? Op::Target
                                       : Op::Unequip;
        }
        else if (verb == "move") {
            if (!need(st.a, "<item>") || !need(st.b, "<amount>") ||
                !need(st.c, "<destination>")) { steps_.clear(); return false; }
            st.op = Op::Move;
        }
        else if (verb == "drop_ground") {
            if (!need(st.a, "<item>") || !need(st.b, "<amount>")) {
                steps_.clear(); return false;
            }
            st.op = Op::DropGround;
        }
        else if (verb == "equip") {
            if (!need(st.a, "<item>") || !need(st.b, "<layer>")) {
                steps_.clear(); return false;
            }
            st.op = Op::Equip;
        }
        else if (verb == "skill" || verb == "cast") {
            ls >> st.id;
            ls >> st.a;   // optional target operand
            st.op = (verb == "skill") ? Op::Skill : Op::Cast;
        }
        else if (verb == "war") {
            if (!need(st.a, "<on|off>")) { steps_.clear(); return false; }
            st.op = Op::War;
        }
        else if (verb == "goto_mobile") {
            if (!need(st.a, "<who>")) { steps_.clear(); return false; }
            st.op = Op::GotoMobile;
        }
        else if (verb == "cast_scroll") {
            if (!need(st.a, "<scroll>")) { steps_.clear(); return false; }
            ls >> st.b;   // optional target
            st.op = Op::CastScroll;
        }
        else if (verb == "bandage") {
            if (!need(st.a, "<item>")) { steps_.clear(); return false; }
            ls >> st.b;   // optional target
            st.op = Op::Bandage;
        }
        else if (verb == "bank") {
            if (!need(st.a, "<banker>")) { steps_.clear(); return false; }
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" \t");
            st.text = (s0 == std::string::npos) ? "bank" : rest.substr(s0);
            st.op = Op::Bank;
        }
        else if (verb == "vendor_open") {
            if (!need(st.a, "<vendor>")) { steps_.clear(); return false; }
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" 	");
            st.text = (s0 == std::string::npos) ? "buy" : rest.substr(s0);
            st.op = Op::VendorOpen;
        }
        else if (verb == "vendor_buy" || verb == "vendor_sell") {
            if (!need(st.a, "<vendor>") || !need(st.b, "<item>") ||
                !need(st.c, "<qty>")) { steps_.clear(); return false; }
            st.op = (verb == "vendor_buy") ? Op::VendorBuy : Op::VendorSell;
        }
        else if (verb == "target_ground") {
            ls >> st.x >> st.y >> st.z;
            st.op = Op::TargetGround;
        }
        else if (verb == "expect") {
            if (!need(st.a, "<result>")) { steps_.clear(); return false; }
            act::Result r;
            if (!ParseResultName(st.a, &r)) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": unknown result '" + st.a + "'";
                steps_.clear();
                return false;
            }
            st.op = Op::Expect;
        }
        else if (verb == "remember") {
            if (!need(st.a, "<name>") || !need(st.b, "<expression>")) {
                steps_.clear(); return false;
            }
            // The rest of the line, so an expression argument can contain
            // spaces -- an NPC's trade lives in its paperdoll title ("Alenne,
            // the mage"), and a single token cannot name one.
            {
                std::string rest;
                std::getline(ls, rest);
                const auto s0 = rest.find_first_not_of(" \t");
                if (s0 != std::string::npos) {
                    st.c = rest.substr(s0);
                    while (!st.c.empty() &&
                           (st.c.back() == ' ' || st.c.back() == '\t' ||
                            st.c.back() == '\r'))
                        st.c.pop_back();
                }
            }
            st.op = Op::Remember;
        }
        else if (verb == "require") {
            if (!need(st.a, "<name>")) { steps_.clear(); return false; }
            st.op = Op::Require;
        }
        else {
            if (err) *err = "line " + std::to_string(lineNo) +
                            ": unknown command '" + verb + "'";
            steps_.clear();
            return false;
        }
        steps_.push_back(std::move(st));
    }
    return true;
}

void Scenario::Tick(Client& client, i64 nowMs) {
    for (int guard = 0; guard < 16 && !failed_ && pc_ < steps_.size(); ++guard) {
        Step& st = steps_[pc_];

        if (!entered_) {
            entered_ = true;
            LogInfo("[scenario] line %d: %s\n", st.line,
                    OpName(static_cast<int>(st.op)));
            switch (st.op) {
                case Op::Walk:  client.ActionWalk(st.dir, st.count); break;
                case Op::Say:   client.ActionSay(st.text.c_str()); break;
                case Op::Backpack: client.ActionOpenBackpack(); break;
                case Op::Sleep:
                case Op::Hold:  deadlineMs_ = nowMs + st.durationMs; break;
                case Op::Goto:  client.ActionGoto(st.x, st.y); break;
                case Op::Logout: client.ActionLogout(); break;

                case Op::Use:   client.ActionUseObject(Resolve(client, st.a)); break;
                case Op::Open:  client.ActionOpenContainer(Resolve(client, st.a)); break;
                case Op::Move:
                    client.ActionMoveItem(Resolve(client, st.a),
                                          static_cast<u16>(std::atoi(st.b.c_str())),
                                          Resolve(client, st.c));
                    break;
                case Op::DropGround:
                    client.ActionDropGround(Resolve(client, st.a),
                                            static_cast<u16>(std::atoi(st.b.c_str())),
                                            client.PlayerX(), client.PlayerY(),
                                            client.PlayerZ());
                    break;
                case Op::Equip:
                    client.ActionEquip(Resolve(client, st.a),
                                       static_cast<u8>(std::strtoul(st.b.c_str(), nullptr, 0)));
                    break;
                case Op::Unequip:
                    client.ActionUnequip(Resolve(client, st.a));
                    break;
                case Op::Skill:
                    client.ActionUseSkill(st.id, Resolve(client, st.a));
                    break;
                case Op::Cast:
                    client.ActionCastSpell(st.id, Resolve(client, st.a));
                    break;
                case Op::Attack:
                    client.ActionAttack(Resolve(client, st.a));
                    break;
                case Op::War:
                    client.ActionWarMode(st.a == "on" || st.a == "1");
                    break;
                case Op::MarkHp:
                    markHp_ = client.PlayerHp();
                    LogInfo("[scenario] mark_hp = %d\n", markHp_);
                    break;
                case Op::MarkGold:
                    markGold_ = client.PlayerGold();
                    LogInfo("[scenario] mark_gold = %d\n", markGold_);
                    break;
                case Op::MarkItem:
                    markItemGraphic_ = static_cast<u16>(
                        std::strtoul(st.a.c_str(), nullptr, 0));
                    markItemCount_ = client.BackpackItemCount(markItemGraphic_);
                    LogInfo("[scenario] mark_item 0x%04X = %u\n",
                            markItemGraphic_, markItemCount_);
                    break;
                case Op::VendorSellOpen:
                    client.ActionVendorSellOpen(Resolve(client, st.a),
                                                st.text.c_str());
                    break;
                case Op::ScanMobiles:
                    client.ActionScanMobiles();
                    break;
                case Op::SetGait:
                    // Setting state, not requesting an action: no wait, no
                    // server round trip. The next step submitted picks it up.
                    client.SetMovementGait(
                        static_cast<sphere::Gait>(st.count));
                    break;
                case Op::GotoMobile:
                    if (!client.ActionGotoMobile(Resolve(client, st.a)))
                        LogWarn("[scenario] goto_mobile: unknown mobile\n");
                    break;

                // --- M2.5 semantic travel ------------------------------
                // Each of these fails the scenario if the DESTINATION cannot
                // be resolved. Whether the trip succeeds is a separate
                // question, asked later by wait_travel + expect_travel.
                case Op::TravelPoint:
                    if (!client.TravelToPoint(st.x, st.y,
                                              st.count > 0 ? st.count : 1,
                                              "scenario point"))
                        FailTravelStart(client, st, "travel_point");
                    break;
                case Op::TravelPlace:
                    if (!client.TravelToPlace(st.text.c_str()))
                        FailTravelStart(client, st, "travel_place");
                    break;
                case Op::TravelRegion:
                    if (!client.TravelToRegion(st.text.c_str()))
                        FailTravelStart(client, st, "travel_region");
                    break;
                case Op::TravelService: {
                    const wm::Service svc = wm::ServiceFromName(st.a.c_str());
                    if (svc == wm::Service::None) {
                        LogError("[scenario] travel_service: '%s' is not a "
                                 "service (line %d)\n", st.a.c_str(), st.line);
                        failed_ = true;
                    } else if (!client.TravelToService(
                                   svc, st.text.empty() ? nullptr
                                                        : st.text.c_str())) {
                        FailTravelStart(client, st, "travel_service");
                    }
                    break;
                }
                case Op::TravelResource: {
                    const wm::ResourceKind r =
                        wm::ResourceFromName(st.text.c_str());
                    if (r == wm::ResourceKind::None) {
                        LogError("[scenario] travel_resource: '%s' is not a "
                                 "resource (line %d)\n", st.text.c_str(),
                                 st.line);
                        failed_ = true;
                    } else if (!client.TravelToResource(r)) {
                        FailTravelStart(client, st, "travel_resource");
                    }
                    break;
                }
                case Op::TravelEntity:
                    if (!client.TravelToEntity(Resolve(client, st.a),
                                               st.count > 0 ? st.count : 2))
                        FailTravelStart(client, st, "travel_entity");
                    break;
                case Op::TravelCorpse:
                    if (!client.TravelToLastCorpse())
                        FailTravelStart(client, st, "travel_corpse");
                    break;
                case Op::TravelHome:
                    if (!client.ReturnHome())
                        FailTravelStart(client, st, "travel_home");
                    break;
                case Op::SetHome:
                    client.Knowledge().SetHome(client.PlayerX(),
                                               client.PlayerY(),
                                               client.PlayerZ(), "");
                    LogInfo("[scenario] home = (%d,%d,%d)\n", client.PlayerX(),
                            client.PlayerY(),
                            static_cast<int>(client.PlayerZ()));
                    break;
                case Op::UseMoongates:
                    client.SetUseMoongates(st.a == "on" || st.a == "1");
                    break;
                case Op::EnsurePeace:
                    client.EnsurePeaceMode();
                    break;
                case Op::CastScroll:
                    client.ActionCastScroll(Resolve(client, st.a),
                                            st.b.empty() ? client.PlayerSerial()
                                                         : Resolve(client, st.b));
                    break;
                case Op::Bandage:
                    client.ActionUseBandage(Resolve(client, st.a),
                                            st.b.empty() ? client.PlayerSerial()
                                                         : Resolve(client, st.b));
                    break;
                case Op::Bank:
                    client.ActionOpenBank(Resolve(client, st.a), st.text.c_str());
                    break;
                case Op::VendorOpen:
                    client.ActionVendorOpen(Resolve(client, st.a), st.text.c_str());
                    break;
                case Op::VendorBuy:
                    client.ActionVendorBuy(Resolve(client, st.a),
                                           Resolve(client, st.b),
                                           static_cast<u16>(std::atoi(st.c.c_str())));
                    break;
                case Op::VendorSell:
                    client.ActionVendorSell(Resolve(client, st.a),
                                            Resolve(client, st.b),
                                            static_cast<u16>(std::atoi(st.c.c_str())));
                    break;
                case Op::Target:
                    if (!client.ActionTargetObject(Resolve(client, st.a)))
                        LogWarn("[scenario] target reply refused (no cursor)\n");
                    break;
                case Op::TargetGround:
                    if (!client.ActionTargetGround(st.x, st.y, static_cast<i8>(st.z)))
                        LogWarn("[scenario] ground target refused (no cursor)\n");
                    break;
                case Op::TargetCancel:
                    client.ActionCancelTarget();
                    break;
                case Op::Resurrect:
                    client.ActionResurrectAccept();
                    break;
                case Op::Remember: {
                    u32 serial = 0;
                    if (st.b == "pack_graphic") {
                        serial = client.FindBackpackItemByGraphic(
                            static_cast<u16>(std::strtoul(st.c.c_str(), nullptr, 0)));
                    } else if (st.b == "mobile_nearest") {
                        serial = client.NearestMobile(0);
                    } else if (st.b == "mobile_body") {
                        // Comma-separated list: the nearest match of ANY of
                        // them wins, so a scenario can name every creature it
                        // considers a valid target for the test.
                        const char* p = st.c.c_str();
                        while (*p && !serial) {
                            const u16 body = static_cast<u16>(
                                std::strtoul(p, nullptr, 0));
                            serial = client.NearestMobileWithBody(body, 0);
                            const char* comma = std::strchr(p, ',');
                            if (!comma) break;
                            p = comma + 1;
                        }
                    } else if (st.b == "mobile_name") {
                        serial = client.NearestMobileNamed(st.c.c_str());
                    } else if (st.b == "mobile_trade") {
                        serial = client.NearestMobileWithTrade(st.c.c_str());
                    } else {
                        serial = Resolve(client, st.b);
                    }
                    Bind(st.a, serial);
                    LogInfo("[scenario] remember %s = 0x%08X\n", st.a.c_str(), serial);
                    break;
                }
                case Op::Require: {
                    const u32 serial = Resolve(client, "@" + st.a);
                    if (!serial) {
                        LogError("[scenario] REQUIRED '%s' is not bound; "
                                 "aborting scenario\n", st.a.c_str());
                        failed_ = true;
                    }
                    break;
                }
                default: break;
            }
        }

        bool done = false;
        switch (st.op) {
            case Op::WaitWorld:    done = client.IsInWorld(); break;
            case Op::WaitWalk:     done = !client.WalkQueueBusy(); break;
            case Op::WaitBackpack: done = client.BackpackContentsKnown(); break;
            case Op::WaitGoto:     done = !client.GotoBusy(); break;
            case Op::WaitDead:     done = client.IsDead(); break;
            case Op::WaitAlive:    done = !client.IsDead(); break;
            case Op::WaitHpBelow:  done = client.PlayerHp() > 0 &&
                                          client.PlayerHp() < st.count; break;
            case Op::ExpectHpGain: {
                const i32 now = client.PlayerHp();
                if (markHp_ < 0 || now <= markHp_) {
                    LogError("[scenario] EXPECT hp gain: %d -> %d (line %d); "
                             "aborting\n", markHp_, now, st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] hp gain confirmed: %d -> %d\n",
                            markHp_, now);
                }
                done = true;
                break;
            }
            case Op::ExpectGoldGain: {
                const i32 now = client.PlayerGold();
                if (markGold_ < 0 || now <= markGold_) {
                    LogError("[scenario] EXPECT gold gain: %d -> %d (line %d); "
                             "aborting\n", markGold_, now, st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] gold gain confirmed: %d -> %d\n",
                            markGold_, now);
                }
                done = true;
                break;
            }
            case Op::ExpectItemDrop: {
                const u16 g = static_cast<u16>(
                    std::strtoul(st.a.c_str(), nullptr, 0));
                const u32 now = client.BackpackItemCount(g);
                if (g != markItemGraphic_ || now >= markItemCount_) {
                    LogError("[scenario] EXPECT item 0x%04X to drop: %u -> %u "
                             "(line %d); aborting\n", g, markItemCount_, now,
                             st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] item 0x%04X drop confirmed: %u -> %u\n",
                            g, markItemCount_, now);
                }
                done = true;
                break;
            }
            case Op::WaitTravel:   done = !client.TravelBusy(); break;
            case Op::ExpectTravel: {
                const bool want = (st.a == "ok" || st.a == "success" ||
                                   st.a == "1");
                const bool got = client.TravelSucceeded();
                if (got != want) {
                    LogError("[scenario] EXPECT travel %s but it %s (%s) "
                             "(line %d); aborting\n", st.a.c_str(),
                             got ? "arrived" : "failed",
                             client.TravelFailureText(), st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] expect travel %s: ok\n", st.a.c_str());
                }
                done = true;
                break;
            }
            case Op::ExpectPeace:
            case Op::ExpectWar: {
                const bool wantWar = (st.op == Op::ExpectWar);
                if (client.WarModeOn() != wantWar) {
                    LogError("[scenario] EXPECT %s but war mode is %s "
                             "(line %d); aborting\n",
                             wantWar ? "war" : "peace",
                             client.WarModeOn() ? "on" : "off", st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] war mode is %s, as expected\n",
                            client.WarModeOn() ? "on" : "off");
                }
                done = true;
                break;
            }
            case Op::ExpectRegion: {
                // Reads the shared atlas against the server's own position --
                // it asserts where the character IS, not where it was sent.
                // The test is containment in the named region's rectangles,
                // not a name match on whatever the smallest region happens to
                // be, so standing in the Empath Abbey still counts as Yew.
                const wm::Region* r = client.CurrentRegion();
                const bool ok = client.WithinRegion(st.text.c_str());
                if (!ok) {
                    LogError("[scenario] EXPECT region '%s' but we are in "
                             "'%s' at (%d,%d) (line %d); aborting\n",
                             st.text.c_str(), r ? r->name.c_str() : "<none>",
                             client.PlayerX(), client.PlayerY(), st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] region is '%s' (%s), as expected\n",
                            r->name.c_str(), r->id.c_str());
                }
                done = true;
                break;
            }
            case Op::ExpectPlace: {
                const bool ok = client.WithinPlace(st.text.c_str());
                if (!ok) {
                    LogError("[scenario] EXPECT to be at '%s' but we are at "
                             "(%d,%d) (line %d); aborting\n", st.text.c_str(),
                             client.PlayerX(), client.PlayerY(), st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] standing at '%s', as expected\n",
                            st.text.c_str());
                }
                done = true;
                break;
            }
            case Op::ExpectServiceKnown: {
                const wm::Service svc = wm::ServiceFromName(st.text.c_str());
                const bool ok = svc != wm::Service::None &&
                                client.Knowledge().RecentService(
                                    svc, nowMs, 0) != nullptr;
                if (!ok) {
                    LogError("[scenario] EXPECT to have seen a %s but this "
                             "character has not (line %d); aborting\n",
                             st.text.c_str(), st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] this character has seen a %s\n",
                            st.text.c_str());
                }
                done = true;
                break;
            }
            case Op::WaitAction:   done = !client.ActionBusy(); break;
            case Op::WaitTarget:   done = client.TargetActive(); break;
            case Op::Sleep:
            case Op::Hold:         done = nowMs >= deadlineMs_; break;
            case Op::Expect: {
                act::Result want = act::Result::Success;
                ParseResultName(st.a, &want);
                const act::Result got = client.ActionResult();
                if (got != want) {
                    LogError("[scenario] EXPECT %s but the action was %s "
                             "(line %d); aborting scenario\n",
                             st.a.c_str(), act::ResultName(got), st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] expect %s: ok\n", st.a.c_str());
                }
                done = true;
                break;
            }
            default:               done = true; break;
        }
        if (!done) return;

        ++pc_;
        entered_ = false;
        if (pc_ >= steps_.size()) {
            LogInfo("[scenario] finished (%zu steps)\n", steps_.size());
            return;
        }
    }
    if (failed_ && !aborted_) {
        aborted_ = true;
        LogError("[scenario] ABORTED at line %d; logging out\n",
                 pc_ < steps_.size() ? steps_[pc_].line : -1);
        // Fail loudly AND end the session: a stalled scenario must never leave
        // the process sitting on the server forever.
        client.ActionLogout();
    }
}

}
