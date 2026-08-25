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
    };
    return (op >= 0 && op < 11) ? kNames[op] : "?";
}

}  // namespace

bool Scenario::Load(const char* path, std::string* err) {
    steps_.clear();
    pc_ = 0;
    entered_ = false;
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
        // Strip comments and surrounding whitespace.
        const auto hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        const auto first = raw.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = raw.find_last_not_of(" \t\r\n");
        std::string line = raw.substr(first, last - first + 1);

        std::istringstream ls(line);
        std::string verb;
        ls >> verb;

        Step st;
        st.line = lineNo;
        if (verb == "wait_world")         st.op = Op::WaitWorld;
        else if (verb == "wait_walk")     st.op = Op::WaitWalk;
        else if (verb == "backpack")      st.op = Op::Backpack;
        else if (verb == "wait_backpack") st.op = Op::WaitBackpack;
        else if (verb == "logout")        st.op = Op::Logout;
        else if (verb == "wait_goto")     st.op = Op::WaitGoto;
        else if (verb == "goto") {
            int gx = 0, gy = 0;
            ls >> gx >> gy;
            if (gx <= 0 || gy <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": goto needs <x> <y>";
                steps_.clear();
                return false;
            }
            st.op = Op::Goto;
            st.x = gx;
            st.y = gy;
        }
        else if (verb == "walk") {
            std::string dir;
            int count = 0;
            ls >> dir >> count;
            if (!ParseDir(dir, &st.dir) || count <= 0) {
                if (err) *err = "line " + std::to_string(lineNo) +
                                ": walk needs <dir> <count>";
                steps_.clear();
                return false;
            }
            st.op = Op::Walk;
            st.count = count;
        } else if (verb == "say") {
            std::string rest;
            std::getline(ls, rest);
            const auto s0 = rest.find_first_not_of(" \t");
            st.op = Op::Say;
            st.text = (s0 == std::string::npos) ? "" : rest.substr(s0);
        } else if (verb == "sleep" || verb == "hold") {
            long long ms = 0;
            ls >> ms;
            if (ms < 0) ms = 0;
            st.op = (verb == "sleep") ? Op::Sleep : Op::Hold;
            st.durationMs = static_cast<i64>(ms);
        } else {
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
    // One step may complete per tick; the loop lets zero-cost steps chain.
    for (int guard = 0; guard < 16 && pc_ < steps_.size(); ++guard) {
        Step& st = steps_[pc_];

        if (!entered_) {
            entered_ = true;
            LogInfo("[scenario] line %d: %s\n", st.line,
                    OpName(static_cast<int>(st.op)));
            switch (st.op) {
                case Op::Walk:
                    client.ActionWalk(st.dir, st.count);
                    break;
                case Op::Say:
                    client.ActionSay(st.text.c_str());
                    break;
                case Op::Backpack:
                    client.ActionOpenBackpack();
                    break;
                case Op::Sleep:
                case Op::Hold:
                    deadlineMs_ = nowMs + st.durationMs;
                    break;
                case Op::Goto:
                    client.ActionGoto(st.x, st.y);
                    break;
                case Op::Logout:
                    client.ActionLogout();
                    break;
                default:
                    break;
            }
        }

        bool done = false;
        switch (st.op) {
            case Op::WaitWorld:    done = client.IsInWorld(); break;
            case Op::Walk:         done = true; break;   // completion is wait_walk
            case Op::WaitWalk:     done = !client.WalkQueueBusy(); break;
            case Op::Say:          done = true; break;
            case Op::Backpack:     done = true; break;
            case Op::WaitBackpack: done = client.BackpackContentsKnown(); break;
            case Op::Goto:         done = true; break;   // completion is wait_goto
            case Op::WaitGoto:     done = !client.GotoBusy(); break;
            case Op::Sleep:
            case Op::Hold:         done = nowMs >= deadlineMs_; break;
            case Op::Logout:       done = true; break;
        }
        if (!done) return;

        ++pc_;
        entered_ = false;
        if (pc_ >= steps_.size()) {
            LogInfo("[scenario] finished (%zu steps)\n", steps_.size());
            return;
        }
    }
}

}
