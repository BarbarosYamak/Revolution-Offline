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

const char* OpName(int op) {
    static const char* kNames[] = {
        "wait_world", "walk", "wait_walk", "say", "backpack",
        "wait_backpack", "sleep", "hold", "logout", "goto", "wait_goto",
        "use", "open", "move", "drop_ground", "equip", "unequip", "skill",
        "cast", "attack", "war", "bandage", "bank", "vendor_open", "vendor_buy", "target",
        "target_ground", "target_cancel", "wait_action", "expect",
        "wait_target", "resurrect", "remember", "require", "cast_scroll",
        "goto_mobile", "wait_dead", "wait_alive",
    };
    const int n = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
    return (op >= 0 && op < n) ? kNames[op] : "?";
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

}  // namespace

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
    if (tok == "vendor_first") {
        const auto& offer = client.VendorOffer();
        return offer.empty() ? 0u : offer.front().serial;
    }
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
        return static_cast<u32>(std::strtoul(tok.c_str(), nullptr, 16));
    if (tok.find_first_not_of("0123456789") == std::string::npos)
        return static_cast<u32>(std::strtoul(tok.c_str(), nullptr, 10));
    return 0;
}

bool Scenario::Load(const char* path, std::string* err) {
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
        else if (verb == "vendor_buy") {
            if (!need(st.a, "<vendor>") || !need(st.b, "<item>") ||
                !need(st.c, "<qty>")) { steps_.clear(); return false; }
            st.op = Op::VendorBuy;
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
            ls >> st.c;   // optional argument for the expression
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
                case Op::GotoMobile:
                    if (!client.ActionGotoMobile(Resolve(client, st.a)))
                        LogWarn("[scenario] goto_mobile: unknown mobile\n");
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
                    } else if (st.b == "mobile_name") {
                        serial = client.NearestMobileNamed(st.c.c_str());
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
