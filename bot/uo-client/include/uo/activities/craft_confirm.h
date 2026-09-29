#pragma once

// ---------------------------------------------------------------------------
// CRAFT CONFIRMATION -- the one question DoCraft kept answering by timer:
// DID AN ITEM APPEAR? (docs/BOT_ARCHITECTURE.md sections 18 and 19.)
//
// Section 18 states it for craft in one line:
//
//     Craft   crafted item count increased, or a definitive craft failure
//             received
//
// and section 19 states the other half: "Never start another craft merely
// because 2 seconds passed."
//
// This file owns exactly that judgement and nothing else. The menu walking,
// the forge, the hammer and the .makelast batching all stay in the runner,
// because they are craft-specific gestures rather than verification. What
// moves here is the part that was wrong: deciding, from the pack and from the
// shard's own words, whether the last swing produced anything.
//
// DELIBERATELY CLIENT-FREE, like interaction/progress.h, which it routes
// through. The caller reads the pack count and the journal; this header stays
// ignorant of both, which is what lets ctest exercise the very rule the live
// bot applies.
//
// -- THE SHARD'S OWN WORDS ---------------------------------------------------
//
// Every string in `CraftFailures()` is quoted verbatim from this shard's
// scripts. The first three rows were also actually observed in a run
// console; the three CLILOC rows below were not -- they match the id-only
// fallback text the client's own decoder produces, since there is no
// Cliloc.enu/.tur in this shard's data to render them as English. Nothing
// here is generic-UO folklore either way.
//
// CLILOC ROWS (S6). runtime/scripts/crafting/crafting_messages.scp is almost
// entirely CLILOC NUMBERS -- craft_msg_fail "1044043", craft_msg_noresources
// "1044253", craft_msg_noskill "1044153". A cliloc used to reach the client
// as packet 0xC1 and be discarded as a no-op at src/Client.cpp:569; it is now
// decoded into the journal (src/net/Cliloc.h, Client::OnClilocMessage). But
// NO Cliloc.enu/.tur ships with this shard's client data -- there is nothing
// to resolve an id to real text with -- so the three cliloc rows below match
// the literal id-only fallback text ("[cliloc 1044043]", see
// src/net/Cliloc.cpp) rather than the English the script comments quote.
// Whether this shard ALSO sends a plain-text equivalent for them, and what
// the real client-rendered text is, remain UNKNOWN and must be measured
// live, not assumed (docs/S3_CHARACTERIZATION.md UNKNOWN #1).
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/interaction/progress.h"

namespace uo::life {

// One definitive thing a Revolution craft can say. `blocking` separates the
// two kinds, and the distinction is the whole value of the table:
//
//   blocking = false  the ATTEMPT failed and the material is gone. Normal at
//                     low skill -- an inscriber ruined 17 scrolls across the
//                     recorded runs -- so the goal takes another swing.
//   blocking = true   the craft CANNOT happen from here. Swinging again just
//                     spends the session, which is what a stand-down and a
//                     cooldown are for.
struct CraftFailure {
    const char* text;      // verbatim, matched case-insensitively as a substring
    const char* why;       // for the log line a human reads at 3am
    bool        blocking;
    const char* evidence;  // where this string is written down
};

// The table, and its size. Zero invention: see `evidence` on each row.
const CraftFailure* CraftFailures(usize* count);

enum class CraftVerdict : u8 {
    // The pack has not moved, the shard has said nothing, the deadline has
    // not passed. The overwhelmingly common answer and the one that used to
    // be mistaken for "swing again".
    Waiting = 0,
    // THE ONLY PROGRESS ANSWER: the pack count rose.
    Made,
    // The attempt failed and consumed the input. A real answer, so the wait
    // is over -- but not a reason to give up on the trade.
    Spoiled,
    // The shard said, in words, that this craft cannot happen from here.
    ShardRefused,
    // The attempts are spent and the pack never moved. Section 14: this is
    // NoProgress, and it must not be reported as success.
    NoProgress,
};

const char* CraftVerdictName(CraftVerdict v);

struct CraftConfirmInput {
    // How many of the OUTPUT were held when the swing went out, and how many
    // are held now. -1 for "not measured", exactly as Expectation reads it.
    i32 packBefore = -1;
    i32 packNow = -1;
    // The definitive line heard since the swing, or nullptr/"" for none. The
    // caller matches `CraftFailures()` against its own journal; this half only
    // has to be told WHICH row hit.
    const CraftFailure* heard = nullptr;
    // The handshake's own answer -- passed in rather than owned, because one
    // activity owns one handshake and this is not it.
    bool deadlineExpired = false;
    bool attemptsExhausted = false;
};

struct CraftConfirmResult {
    CraftVerdict verdict = CraftVerdict::Waiting;
    // How many appeared. Zero for every verdict but Made.
    i32          made = 0;
    // The numbers and the phrasing, straight from interaction/progress.h, so
    // the craft path and the buy path account for success the same way.
    ProgressCheck check;
    const char*  reason = "";
};

CraftConfirmResult ConfirmCraft(const CraftConfirmInput& in);

}  // namespace uo::life
