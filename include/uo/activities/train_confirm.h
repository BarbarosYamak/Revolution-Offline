#pragma once

// ---------------------------------------------------------------------------
// TRAINER CONFIRMATION -- did the gold that went to the trainer buy anything?
// (docs/BOT_ARCHITECTURE.md sections 18 and 20.)
//
// Section 18 states it in one line:
//
//     Train   skill changed, or the trainer definitively refused
//
// and `training_unverified` is the name this project gave to breaking it:
// GOLD_DESTROYED_TRAINER in the ledger, a dozen times in one fleet run, with
// no confirmed skill gain to show for any of it, because nobody read the 0x3A
// that follows (run_m7/fleet2.console.txt:5470).
//
// This file owns exactly two things, and both are the ones the verification
// work touches:
//
//   THE REFUSALS    what an NPC trainer SAYS when it will not teach. A
//                   refusal is an answer, and it must end the errand rather
//                   than leave it to time out -- Ysolde asked one refusing
//                   mage 30+ times in a session before this was a table.
//   THE VERDICT     after the fee is handed over: has the SERVER'S OWN skill
//                   number moved, has the fee gone, and which of the four
//                   things that means is it.
//
// What stays in the runner is the conversation itself: finding the trade,
// walking into earshot, reading the quote, paying the speaker who quoted.
// Those are Client gestures. This is the judgement.
//
// DELIBERATELY CLIENT-FREE, routing through interaction/progress.h, so ctest
// exercises the very rule the live bot applies rather than a copy of it.
//
// UNKNOWN, and left that way: WHY one NPC of a trade answers and another of
// the same trade does not. Alenne quoted nothing across seven asks while
// Alek, also "the mage", quoted 184 gold on the first ask. Silence is
// therefore NOT in the refusal table below -- a verdict is what an NPC said,
// and this one said nothing. Recording silence as a refusal would teach the
// character something the world never told it.
//
// -- THE FOUR WAYS THIS EXCHANGE HAS GONE WRONG, all live ------------------
//
//   PAID THE WRONG NPC. The quote is read out of the journal by TEXT, so the
//   fee went to whoever had been ADDRESSED rather than whoever SPOKE:
//     [TRAIN] ask 0x00009096 say='Rhyssa train Tinkering'
//     Pembroke: For 101 gold I will train you in all I know of Tinkering
//     training: paying the quoted 101 gold ... (purse 9801)
//     training: paid 101 for Tinkering but the server still reports 19.9
//   Two tinkers stand together in Minoc and Sphere answered with the nearer.
//   The gold went to Rhyssa, who had offered nothing; Pembroke, who had, was
//   never paid. The runner now pays the SPEAKER of the quote.
//
//   PAID A RETIRED SERIAL. Sphere splits a gold stack to make change, which
//   retires the old serial, and a give addressed to a retired serial is a
//   SILENT no-op: no gold moves, the NPC says nothing, nothing reports an
//   error. The second purchase of the first successful live run did exactly
//   this -- the first 108gp give did nothing and only the retry landed. The
//   runner asks for the pack's contents before paying, for that reason.
//
//   DECLARED FAILURE BEFORE ASKING. The skill list was only requested after a
//   ten-second timeout had already written the errand off, so a purchase that
//   really worked (11.8 -> 21.1 for 93 gold) was filed as "has not moved".
//   Hence TrainVerdict::AskForSkills, promptly and once.
//
//   ASKED A REFUSING NPC FOREVER. The refusal was logged as an event nothing
//   read, and the trip counter reset, so the character re-selected the goal
//   and asked the same NPC again every two seconds -- 30+ times in the first
//   live run, the NPC patiently refusing each time. A refusal is now a
//   DURABLE TrainerVerdict, and the ceiling it records is that NPC's own.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/interaction/progress.h"

namespace uo::life {

// One definitive thing a trainer says when it will not teach.
//
// THE CEILING IS THIS NPC'S, NOT THE TRADE'S. Every row below is a fact about
// the character standing there, never about the skill -- writing off a trade
// on one refusal is what cost Ysolde Meditation entirely.
struct TrainerRefusal {
    const char* text;   // matched case-insensitively as a substring
    const char* why;    // the reason recorded in the durable TrainerVerdict
};

const TrainerRefusal* TrainerRefusals(usize* count);

enum class TrainVerdict : u8 {
    // The fee has gone out and nothing has come back yet, inside the window.
    Waiting = 0,
    // THE ONLY PROGRESS ANSWER: the server's own skill number moved.
    Learned,
    // Sphere does not push the new number after a lesson, so a player's
    // client asks for it. Do that ONCE, promptly -- the first version only
    // asked after the timeout had already declared failure, so a purchase
    // that really worked (11.8 -> 21.1 for 93 gold, live) was filed as
    // "has not moved".
    AskForSkills,
    // The fee left the purse and the skill did not move. A fact about THIS
    // trainer: write him off, not the skill.
    FeeTakenNoLesson,
    // The window closed and neither number moved. The give most likely
    // addressed a serial Sphere had already retired when it split the gold
    // stack to make change -- a silent no-op, no error anywhere.
    NoAnswer,
};

const char* TrainVerdictName(TrainVerdict v);

struct TrainConfirmInput {
    // The server's own numbers, in tenths, as 0x3A reports them.
    i32 skillBefore = -1;
    i32 skillNow = -1;
    // The purse, before the lesson and now. obs.gold counts the bank box on
    // this shard, which is fine here: both readings are taken the same way,
    // so the DELTA is still honest.
    i32 goldBefore = -1;
    i32 goldNow = -1;
    // What the NPC quoted, which is also the ceiling this errand authorised.
    i32 quoted = 0;

    // How long since the give went out, on the TICK clock. Mixing this with
    // the journal clock made a 10-second window expire in 8.7 seconds.
    i64 msSincePaid = 0;
    // Has the skill list already been asked for once?
    bool skillsAsked = false;

    // Both deadlines are the caller's, not this file's -- no clock lives here.
    i64 askSkillsAfterMs = 1500;
    i64 giveUpAfterMs = 15000;
};

struct TrainConfirmResult {
    TrainVerdict  verdict = TrainVerdict::Waiting;
    // The numbers and the phrasing, straight from interaction/progress.h.
    ProgressCheck check;
    const char*   reason = "";
};

TrainConfirmResult ConfirmTraining(const TrainConfirmInput& in);

}  // namespace uo::life
