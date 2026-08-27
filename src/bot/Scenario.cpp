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
    "skill_report", "mark_skill", "expect_skill_gain", "wait_skill",
    "request_skills", "loop", "endloop", "wait_mana", "expect_item_gain",
    "trade_start", "trade_offer", "trade_accept", "trade_retract",
    "trade_cancel", "wait_trade_open", "wait_trade_partner",
    "wait_trade_closed", "expect_trade", "wait_trade_offer", "wait_trade_mine",
    "npc_train", "give",
    "wait_gump", "gump_button", "gump_report",
    "target_static", "menu_choose", "expect_item_same",
    "mount", "wait_mounted", "expect_mounted", "dismount", "menu_pick",
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

// Several verbs name an item by graphic, and sometimes one name covers a set:
// Sphere's fishing yields any of i_fish_big_1..4, so "a fish" is four graphics
// and a scenario that named one would pass or fail on a coin toss.
usize ParseGraphicList(const std::string& text, u16* out, usize cap) {
    usize n = 0;
    const char* p = text.c_str();
    while (*p && n < cap) {
        out[n++] = static_cast<u16>(std::strtoul(p, nullptr, 0));
        const char* comma = std::strchr(p, ',');
        if (!comma) break;
        p = comma + 1;
    }
    return n;
}

u32 CountGraphics(Client& client, const u16* list, usize n) {
    u32 total = 0;
    for (usize i = 0; i < n; ++i) total += client.BackpackItemCount(list[i]);
    return total;
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
        u16 want[8];
        const usize n = ParseGraphicList(tok.substr(20), want, 8);
        for (usize i = 0; i < n; ++i)
            for (const auto& v : client.VendorSellOffer())
                if (v.graphic == want[i]) return v.serial;
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
        else if (verb == "gump_report")     st.op = Op::GumpReport;
        else if (verb == "wait_gump")       st.op = Op::WaitGump;
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
        else if (verb == "skill_report")    st.op = Op::SkillReport;
        else if (verb == "request_skills")  st.op = Op::RequestSkills;
        else if (verb == "trade_accept")    st.op = Op::TradeAccept;
        else if (verb == "trade_retract")   st.op = Op::TradeRetract;
        else if (verb == "trade_cancel")    st.op = Op::TradeCancel;
        else if (verb == "wait_trade_open") st.op = Op::WaitTradeOpen;
        else if (verb == "wait_trade_partner") st.op = Op::WaitTradePartner;
        else if (verb == "wait_trade_closed")  st.op = Op::WaitTradeClosed;
        // Waiting for goods to appear on the table is what lets two sessions
        // agree without a sleep between them.
        else if (verb == "wait_trade_offer") {
            ls >> st.count;
            if (st.count <= 0) st.count = 1;
            st.op = Op::WaitTradeOffer;
        }
        else if (verb == "wait_trade_mine") {
            ls >> st.count;
            if (st.count <= 0) st.count = 1;
            st.op = Op::WaitTradeMine;
        }
        else if (verb == "expect_trade") {
            if (!need(st.a, "<completed|cancelled>")) { steps_.clear(); return false; }
            st.op = Op::ExpectTrade;
        }
        else if (verb == "expect_item_gain") {
            if (!need(st.a, "<graphic>")) { steps_.clear(); return false; }
            st.op = Op::ExpectItemGain;
        }
        else if (verb == "trade_start") {
            if (!need(st.a, "<partner>") || !need(st.b, "<item>")) {
                steps_.clear(); return false;
            }
            st.op = Op::TradeStart;
        }
        else if (verb == "trade_offer") {
            if (!need(st.a, "<item>")) { steps_.clear(); return false; }
            ls >> st.count;
            if (st.count <= 0) st.count = 1;
            st.op = Op::TradeOffer;
        }
        else if (verb == "endloop")         st.op = Op::EndLoop;
        else if (verb == "loop") {
            ls >> st.count;
            if (st.count <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": loop needs a positive count";
                steps_.clear();
                return false;
            }
            st.op = Op::Loop;
        }
        else if (verb == "wait_mana") {
            ls >> st.count;
            if (st.count <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": wait_mana needs a positive amount";
                steps_.clear();
                return false;
            }
            st.op = Op::WaitMana;
        }
        else if (verb == "mark_skill") {
            if (!need(st.a, "<skill index>")) { steps_.clear(); return false; }
            st.op = Op::MarkSkill;
        }
        else if (verb == "expect_skill_gain") {
            if (!need(st.a, "<skill index>")) { steps_.clear(); return false; }
            st.op = Op::ExpectSkillGain;
        }
        else if (verb == "wait_skill") {
            // wait_skill <index> <tenths> -- block until the SERVER reports the
            // trained value at or above this. There is no timeout here on
            // purpose: the run's own timeout is the backstop, and a training
            // scenario that silently gave up would be worse than one that ran long.
            if (!need(st.a, "<skill index>")) { steps_.clear(); return false; }
            ls >> st.count;
            st.op = Op::WaitSkill;
        }
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
        else if (verb == "mount") {
            if (!need(st.a, "<serial|@name>")) { steps_.clear(); return false; }
            st.op = Op::Mount;
        }
        else if (verb == "dismount") {
            st.op = Op::Dismount;
        }
        else if (verb == "menu_pick") {
            // M3.8 Phase 10: pick a craft-menu entry BY NAME from the list the
            // server actually sent. `menu_choose 3` names a position in the
            // .scp; this names the thing. Sphere filters menus by skill and
            // inventory, so the two are routinely different -- and when they
            // differ, the index is wrong and the name is right.
            std::getline(ls, st.text);
            const auto s0 = st.text.find_first_not_of(" 	");
            st.text = (s0 == std::string::npos) ? std::string() : st.text.substr(s0);
            if (!need(st.a, "<name>")) { steps_.clear(); return false; }
            st.op = Op::MenuPick;
        }
        else if (verb == "wait_mounted") {
            st.op = Op::WaitMounted;
        }
        else if (verb == "expect_mounted") {
            // `expect_mounted` alone means "yes"; `expect_mounted 0` asserts the
            // character is on its own feet.
            //
            // THE OPERAND MUST BE READ HERE. st.a is not filled in by any shared
            // preamble -- only `need()` and explicit `ls >> st.a` ever set it --
            // so an earlier version that tested `st.a.empty()` without reading
            // the stream saw an always-empty string and silently asserted
            // "mounted" every time. It failed on the on-foot baseline with
            // "EXPECT mounted but the character is on foot", which is a scenario
            // that was in fact behaving correctly.
            if (!(ls >> st.a)) st.a.clear();
            st.count = st.a.empty() ? 1 : std::atoi(st.a.c_str());
            st.op = Op::ExpectMounted;
        }
        else if (verb == "mark_item" || verb == "expect_item_drop" ||
                 verb == "expect_item_same") {
            if (!need(st.a, "<graphic>")) { steps_.clear(); return false; }
            st.op = (verb == "mark_item")       ? Op::MarkItem
                  : (verb == "expect_item_drop") ? Op::ExpectItemDrop
                                                 : Op::ExpectItemSame;
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
        else if (verb == "gump_button") {
            // gump_button <id> -- the gump's own button id, as the server
            // numbered it. The runebook uses 11..18 to travel and 21..28 to
            // insert a rune.
            if (!need(st.a, "<button>")) { steps_.clear(); return false; }
            st.op = Op::GumpButton;
        }
        else if (verb == "npc_train") {
            // npc_train <npc> <skillkey>  -- the skill key is Sphere's own
            // (e.g. "blacksmithy"), because the server matches on it.
            if (!need(st.a, "<npc>") || !need(st.b, "<skill>")) { steps_.clear(); return false; }
            st.op = Op::NpcTrain;
        }
        else if (verb == "give") {
            // give <mobile> <item> <amount>  -- hand over a counted stack.
            if (!need(st.a, "<mobile>") || !need(st.b, "<item>") ||
                !need(st.c, "<amount>")) { steps_.clear(); return false; }
            st.op = Op::Give;
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
        else if (verb == "menu_choose") {
            // menu_choose <1-based index>   (0 cancels)
            ls >> st.count;
            st.op = Op::MenuChoose;
        }
        else if (verb == "target_static") {
            // target_static <x> <y> <z> <graphic>
            //
            // The graphic is read as a STRING and converted with base 0, not
            // streamed into an int: `ls >> st.id` on "0x0CDA" parses a decimal
            // 0, stops at the 'x', and hands the server model 0 -- which looks
            // exactly like a plain ground reply and earns "It appears immune to
            // your blow" from a tree that is really there.
            std::string gfx;
            ls >> st.x >> st.y >> st.z >> gfx;
            st.id = static_cast<int>(std::strtoul(gfx.c_str(), nullptr, 0));
            st.op = Op::TargetStatic;
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
                case Op::MarkItem: {
                    u16 list[8];
                    const usize n = ParseGraphicList(st.a, list, 8);
                    markItemGraphic_ = n ? list[0] : 0;
                    markItemList_ = st.a;
                    markItemCount_ = CountGraphics(client, list, n);
                    LogInfo("[scenario] mark_item %s = %u\n", st.a.c_str(),
                            markItemCount_);
                    break;
                }
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

                // --- M3 progression ------------------------------------
                case Op::RequestSkills:
                    client.ActionRequestSkills();
                    break;
                case Op::Dismount:
                    client.ActionDismount();
                    break;
                case Op::MenuPick: {
                    // The whole label, so "iron ingot" works as well as "nails".
                    const std::string want =
                        st.text.empty() ? st.a : (st.a + " " + st.text);
                    if (!client.ChooseDialogByName(want.c_str())) {
                        LogError("[scenario] menu_pick '%s' not offered by the "
                                 "server (line %d); aborting\n",
                                 want.c_str(), st.line);
                        failed_ = true;
                    }
                    break;
                }
                case Op::Mount:
                    // A player mounts by double-clicking the animal. There is
                    // no mount packet; the server answers by deleting the
                    // mobile and equipping a mount item on layer 25, which is
                    // what `wait_mounted` watches for.
                    client.ActionUseObject(Resolve(client, st.a));
                    break;
                case Op::TradeStart:
                    client.ActionTradeStart(Resolve(client, st.a),
                                            Resolve(client, st.b));
                    break;
                case Op::TradeOffer:
                    client.ActionTradeOffer(Resolve(client, st.a),
                                            static_cast<u16>(st.count));
                    break;
                case Op::TradeAccept:
                    if (!client.ActionTradeAccept(true))
                        LogWarn("[scenario] trade_accept: no trade open\n");
                    break;
                case Op::TradeRetract:
                    if (!client.ActionTradeAccept(false))
                        LogWarn("[scenario] trade_retract: no trade open\n");
                    break;
                case Op::TradeCancel:
                    client.ActionTradeCancel();
                    break;
                case Op::Loop:
                    // Push the iteration counter. The body runs count times;
                    // EndLoop decrements and jumps back.
                    loops_.push_back(LoopFrame{pc_, st.count});
                    LogInfo("[scenario] loop x%d\n", st.count);
                    break;
                case Op::EndLoop: {
                    if (loops_.empty()) {
                        LogError("[scenario] endloop without loop (line %d); "
                                 "aborting\n", st.line);
                        failed_ = true;
                        break;
                    }
                    LoopFrame& f = loops_.back();
                    if (--f.remaining > 0) {
                        pc_ = f.start;      // ++pc_ below lands on the body
                        entered_ = false;
                    } else {
                        loops_.pop_back();
                    }
                    break;
                }
                case Op::SkillReport: {
                    // Dump exactly what the shard says this character has.
                    // This is the evidence a skill matrix is built from, so it
                    // prints the raw tenths rather than a rounded display.
                    std::vector<Client::SkillReport> all;
                    client.PlayerSkillsAll(all);
                    LogInfo("[skills] %zu reported, trained sum %u.%u, "
                            "STR %d DEX %d INT %d (sum %d, cap %d)\n",
                            all.size(), client.PlayerSkillSum() / 10,
                            client.PlayerSkillSum() % 10, client.PlayerStr(),
                            client.PlayerDex(), client.PlayerInt(),
                            client.PlayerStatSum(), client.PlayerStatCap());
                    for (const Client::SkillReport& s : all) {
                        if (!s.baseTenths && !s.valueTenths) continue;
                        LogInfo("[skills]   %3u base=%u.%u value=%u.%u "
                                "cap=%u.%u lock=%u\n", s.index,
                                s.baseTenths / 10, s.baseTenths % 10,
                                s.valueTenths / 10, s.valueTenths % 10,
                                s.capTenths / 10, s.capTenths % 10, s.lock);
                    }
                    char ev[192];
                    std::snprintf(ev, sizeof(ev),
                                  "skills=%zu sum=%u str=%d dex=%d int=%d",
                                  all.size(), client.PlayerSkillSum(),
                                  client.PlayerStr(), client.PlayerDex(),
                                  client.PlayerInt());
                    LogEvent("skill_report", ev);
                    break;
                }
                case Op::MarkSkill: {
                    markSkillIndex_ = static_cast<u16>(
                        std::strtoul(st.a.c_str(), nullptr, 0));
                    markSkillTenths_ = client.PlayerSkillBase(markSkillIndex_);
                    LogInfo("[scenario] mark_skill %u = %d tenths\n",
                            markSkillIndex_, markSkillTenths_);
                    break;
                }
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
                case Op::GumpButton: {
                    const u32 b = static_cast<u32>(std::strtoul(st.a.c_str(), nullptr, 0));
                    const bool sent = client.AnswerGump(b, 0);
                    std::printf("[scenario] gump_button %u -> %s\n", b,
                                sent ? "sent" : "no gump open");
                    break;
                }
                case Op::GumpReport:
                    std::printf("[scenario] gump active=%d context=0x%08X options=%zu\n",
                                client.GumpActive() ? 1 : 0, client.GumpContext(),
                                static_cast<unsigned>(client.GumpOptions().size()));
                    for (const Client::GumpOption& o : client.GumpOptions())
                        std::printf("[scenario]   gump option id=%u button=%d '%s'\n",
                                    o.id, o.button ? 1 : 0, o.label.c_str());
                    break;
                case Op::NpcTrain:
                    client.ActionNpcTrain(Resolve(client, st.a), st.b.c_str());
                    break;
                case Op::Give:
                    client.ActionNpcGive(Resolve(client, st.a),
                                         Resolve(client, st.b),
                                         static_cast<u16>(std::atoi(st.c.c_str())));
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
                case Op::MenuChoose:
                    // Sphere's craft menus are 0x7C MENUS, not 0xB0 gumps: the
                    // Carpentry menu arrives as `[0x7C] menu=690 "Carpentry"`
                    // with its options listed, and is answered with 0x7D by
                    // 1-based index. gump_button cannot touch it.
                    if (!client.ActionMenuChoose(static_cast<u16>(st.count)))
                        LogWarn("[scenario] menu_choose: no active menu\n");
                    break;
                case Op::TargetStatic:
                    // The reply carries the static's GRAPHIC. Source-X
                    // identifies a targeted static through
                    // CanTouchStatic(&pt, id, ...), so a plain ground reply --
                    // which sends model 0 -- cannot be recognised as a tree and
                    // the shard answers "It appears immune to your blow".
                    if (!client.ActionTargetStatic(st.x, st.y,
                                                   static_cast<i8>(st.z),
                                                   static_cast<u16>(st.id)))
                        LogWarn("[scenario] static target refused (no cursor)\n");
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
                        // A COMMA LIST, like carried_graphic and mobile_body.
                        // It used to take one graphic, and that is a real trap:
                        // a stacked resource CHANGES GRAPHIC as the pile grows
                        // (i_ore_iron carries DUPELIST=019b8,019b9,019ba), so a
                        // single-id lookup silently misses the character's own
                        // gathered stack. A live run mined seven piles, passed
                        // expect_item_gain -- which already took a list -- and
                        // then failed `require ore` on the very next line.
                        u16 list[8];
                        const usize n = ParseGraphicList(st.c, list, 8);
                        for (usize i = 0; i < n && !serial; ++i)
                            serial = client.FindBackpackItemByGraphic(list[i]);
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
                    } else if (st.b == "world_graphic") {
                        // A craft STATION -- a spinning wheel, a loom, a forge.
                        // These are dynamic WORLD items and must be targeted by
                        // serial, never by ground coordinate: Source-X resolves
                        // a use-target with uid.ObjFind(), so a ground target
                        // arrives as pItemTarg == nullptr and the IT_WOOL /
                        // IT_YARN / IT_THREAD cases in OnTarg_Use_Item fall
                        // straight through. The live symptom is
                        // "You can't think of a way to use that item."
                        u16 list[8];
                        const usize n = ParseGraphicList(st.c, list, 8);
                        for (usize i = 0; i < n && !serial; ++i)
                            serial = client.FindWorldItemByGraphic(list[i]);
                    } else if (st.b == "mobile_name") {
                        serial = client.NearestMobileNamed(st.c.c_str());
                    } else if (st.b == "carried_graphic") {
                        // Backpack first, then worn gear -- see
                        // Client::FindItemByGraphic for why worn matters.
                        u16 list[8];
                        const usize n = ParseGraphicList(st.c, list, 8);
                        for (usize i = 0; i < n && !serial; ++i)
                            serial = client.FindItemByGraphic(list[i], true);
                    } else if (st.b == "mobile_trade") {
                        // A comma list, because Sphere's titles are gendered:
                        // the Britain docks have both "the fisherman" and
                        // "the fisherwoman", and a scenario that named one
                        // would work or not depending on which NPC spawned.
                        usize start = 0;
                        while (!serial && start <= st.c.size()) {
                            const usize comma = st.c.find(',', start);
                            const usize end = comma == std::string::npos
                                                  ? st.c.size() : comma;
                            const std::string one =
                                st.c.substr(start, end - start);
                            if (!one.empty())
                                serial = client.NearestMobileWithTrade(one.c_str());
                            if (comma == std::string::npos) break;
                            start = comma + 1;
                        }
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
            case Op::WaitGump:     done = client.GumpActive(); break;
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
                u16 list[8];
                const usize n = ParseGraphicList(st.a, list, 8);
                const u16 g = n ? list[0] : 0;
                const u32 now = CountGraphics(client, list, n);
                if (st.a != markItemList_ || now >= markItemCount_) {
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
            case Op::WaitMounted:
                done = client.PlayerIsMounted();
                break;
            case Op::ExpectMounted: {
                const bool want = (st.count != 0);
                const bool got = client.PlayerIsMounted();
                if (got != want) {
                    LogError("[scenario] EXPECT %s but the character is %s "
                             "(line %d); aborting\n",
                             want ? "mounted" : "on foot",
                             got ? "mounted" : "on foot", st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] expect %s: ok\n",
                            want ? "mounted" : "on foot");
                }
                done = true;
                break;
            }
            case Op::ExpectItemSame: {
                u16 list[8];
                const usize n = ParseGraphicList(st.a, list, 8);
                const u16 g = n ? list[0] : 0;
                const u32 now = CountGraphics(client, list, n);
                if (st.a != markItemList_ || now != markItemCount_) {
                    LogError("[scenario] EXPECT item 0x%04X unchanged: "
                             "%u -> %u (line %d); aborting\n", g,
                             markItemCount_, now, st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] item 0x%04X unchanged at %u, "
                            "as expected\n", g, now);
                }
                done = true;
                break;
            }
            case Op::WaitTravel:   done = !client.TravelBusy(); break;
            case Op::WaitTradeOpen:
                done = client.Trade().Active();
                break;
            case Op::WaitTradePartner:
                // The partner ticked their box. Waiting for this before we
                // tick ours is what makes the two-session test deterministic
                // without a sleep.
                done = client.Trade().TheirCheck() || !client.Trade().Active();
                break;
            case Op::WaitTradeClosed:
                done = !client.Trade().Active();
                break;
            case Op::WaitTradeOffer:
                done = static_cast<int>(client.Trade().TheirOffer().size()) >=
                           st.count ||
                       !client.Trade().Active();
                break;
            case Op::WaitTradeMine:
                done = static_cast<int>(client.Trade().MyOffer().size()) >=
                           st.count ||
                       !client.Trade().Active();
                break;
            case Op::ExpectTrade: {
                const bool wantDone = (st.a == "completed" || st.a == "ok");
                const bool got =
                    client.Trade().CurrentPhase() == trade::Phase::Completed;
                if (got != wantDone) {
                    LogError("[scenario] EXPECT trade %s but it was %s (%s) "
                             "(line %d); aborting\n", st.a.c_str(),
                             trade::PhaseName(client.Trade().CurrentPhase()),
                             trade::CloseReasonName(client.Trade().Reason()),
                             st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] trade %s, as expected (%s)\n",
                            trade::PhaseName(client.Trade().CurrentPhase()),
                            trade::CloseReasonName(client.Trade().Reason()));
                }
                done = true;
                break;
            }
            case Op::ExpectItemGain: {
                u16 list[8];
                const usize n = ParseGraphicList(st.a, list, 8);
                const u16 g = n ? list[0] : 0;
                const u32 now = CountGraphics(client, list, n);
                if (st.a != markItemList_ || now <= markItemCount_) {
                    LogError("[scenario] EXPECT item 0x%04X to increase: "
                             "%u -> %u (line %d); aborting\n", g,
                             markItemCount_, now, st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] item 0x%04X gain confirmed: %u -> %u\n",
                            g, markItemCount_, now);
                }
                done = true;
                break;
            }
            case Op::WaitMana:
                // Real regeneration, at whatever rate the shard runs it. A
                // training loop that cast anyway would just collect "you lack
                // sufficient mana" and no skill.
                done = client.PlayerMana() >= st.count;
                break;
            case Op::WaitSkill: {
                const i32 now = client.PlayerSkillBase(
                    static_cast<u16>(std::strtoul(st.a.c_str(), nullptr, 0)));
                done = now >= st.count;
                break;
            }
            case Op::ExpectSkillGain: {
                const u16 idx = static_cast<u16>(
                    std::strtoul(st.a.c_str(), nullptr, 0));
                const i32 now = client.PlayerSkillBase(idx);
                if (idx != markSkillIndex_ || markSkillTenths_ < 0 ||
                    now <= markSkillTenths_) {
                    LogError("[scenario] EXPECT skill %u to gain: %d -> %d "
                             "tenths (line %d); aborting\n", idx,
                             markSkillTenths_, now, st.line);
                    failed_ = true;
                } else {
                    LogInfo("[scenario] skill %u gain confirmed: %d.%d -> "
                            "%d.%d\n", idx, markSkillTenths_ / 10,
                            markSkillTenths_ % 10, now / 10, now % 10);
                }
                done = true;
                break;
            }
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
