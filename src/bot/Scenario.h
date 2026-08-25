#pragma once

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo {

class Client;

namespace bot {

// ---------------------------------------------------------------------------
// Scenario — a scripted list of player actions (M1).
//
// This is deliberately NOT a bot brain: no decisions, no world model, no
// goals. It exists to drive a fixed acceptance sequence (walk / say / open
// backpack / stay connected / log out) and to prove the adapter boundary:
// Scenario talks to Client's public action API only. It has no access to
// sockets, packets, sequence numbers or protocol state, and it cannot reach
// around the server — every step is a request the server may refuse.
//
// Script syntax — one command per line, '#' starts a comment:
//
//   wait_world            wait until the character is in the world
//   walk <dir> <count>    queue steps; dir = n|ne|e|se|s|sw|w|nw
//   goto <x> <y>          plan a route with A* and walk it
//   wait_goto             wait until the route finishes (arrived or gave up)
//   wait_walk             wait until every queued step has been answered
//   say <text>            speak (ASCII)
//   backpack              open the worn backpack
//   wait_backpack         wait until the backpack's contents have arrived
//   sleep <ms>            pause
//   hold <ms>             stay connected and idle for this long
//   logout                request logout and close the connection
// ---------------------------------------------------------------------------
class Scenario {
public:
    // Loads `path`. Returns false (and leaves the scenario empty) if the file
    // cannot be read; `err` receives a human-readable reason.
    bool Load(const char* path, std::string* err);

    bool Empty() const { return steps_.empty(); }
    bool Finished() const { return pc_ >= steps_.size(); }

    // Advance the script. Call once per client tick with the current
    // monotonic millisecond clock.
    void Tick(Client& client, i64 nowMs);

private:
    enum class Op : u8 {
        WaitWorld, Walk, WaitWalk, Say, Backpack, WaitBackpack,
        Sleep, Hold, Logout, Goto, WaitGoto,
    };
    struct Step {
        Op          op = Op::WaitWorld;
        u8          dir = 0;
        int         count = 0;
        i32         x = 0;
        i32         y = 0;
        i64         durationMs = 0;
        std::string text;
        int         line = 0;
    };

    std::vector<Step> steps_;
    usize pc_ = 0;
    bool  entered_ = false;    // current step has run its one-shot side effect
    i64   deadlineMs_ = 0;     // for Sleep/Hold
};

}
}
