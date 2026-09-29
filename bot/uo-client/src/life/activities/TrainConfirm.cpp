#include "uo/activities/train_confirm.h"

namespace uo::life {

// ---------------------------------------------------------------------------
// THE REFUSALS. Lifted verbatim out of DoTrainAtNpc, where they had been a
// file-static table inside a 497-line function that nothing could test.
//
// Each is matched case-insensitively against the journal since the ask, and
// each ENDS the errand: a refusal is an answer. Before this was read, one
// mage refused Ysolde thirty-odd times in a single session, patiently, every
// two seconds.
// ---------------------------------------------------------------------------
static const TrainerRefusal kRefusals[] = {
    {"i know nothing about",        "this NPC does not teach it"},
    {"you know more about",         "the character already exceeds the trainer"},
    {"you already know as much",    "the trainer has nothing left to give"},
    {"i would never train",         "the trainer refuses this character"},
    {"there is nothing that i can", "the trainer has nothing to teach"},
};

const TrainerRefusal* TrainerRefusals(usize* count) {
    if (count) *count = sizeof(kRefusals) / sizeof(kRefusals[0]);
    return kRefusals;
}

const char* TrainVerdictName(TrainVerdict v) {
    switch (v) {
        case TrainVerdict::Waiting:          return "waiting";
        case TrainVerdict::Learned:          return "learned";
        case TrainVerdict::AskForSkills:     return "ask_for_skills";
        case TrainVerdict::FeeTakenNoLesson: return "fee_taken_no_lesson";
        case TrainVerdict::NoAnswer:         return "no_answer";
    }
    return "?";
}

TrainConfirmResult ConfirmTraining(const TrainConfirmInput& in) {
    TrainConfirmResult out;

    // THE PROOF IS THE SERVER'S SKILL NUMBER, not our own bookkeeping and not
    // the fact that a give packet went out.
    Expectation want;
    // Verify() reads skillId only as "this field is being checked"; the
    // identity of the skill is the caller's business and never enters the
    // arithmetic. Zero is as good a marker as any and keeps the input struct
    // from carrying a field nobody reads.
    want.skillId = 0;
    want.skillBefore = in.skillBefore;
    want.skillGainMin = 1;

    // THE PURSE IS CHECKED TOO, and that is the whole point of this file:
    // "the fee was taken and nothing was taught" and "the trainer kept
    // nothing and simply has not answered" used to log the identical
    // `training_unverified` line, and they are completely different problems.
    // The first is a trainer to write off; the second is a report in flight.
    want.goldBefore = in.goldBefore;
    want.goldSpendMin = 1;
    want.goldSpendMax = in.quoted;

    Observed seen;
    seen.skillNow = in.skillNow;
    seen.goldNow = in.goldNow;

    out.check = Verify(want, seen);
    out.reason = out.check.reason;

    // A LESSON IS THE NUMBER MOVING. Nothing else counts, and in particular a
    // fee leaving the purse does not: that is what `training_unverified` was.
    if (in.skillNow >= 0 && in.skillBefore >= 0 &&
        in.skillNow > in.skillBefore) {
        out.verdict = TrainVerdict::Learned;
        out.reason = "the server's own skill value moved";
        return out;
    }

    // ASK FOR THE NUMBER. Sphere does not push it after a lesson, so a
    // player's client requests the skill list -- promptly, and once.
    if (!in.skillsAsked && in.msSincePaid > in.askSkillsAfterMs) {
        out.verdict = TrainVerdict::AskForSkills;
        out.reason = "no skill report has arrived; ask for one";
        return out;
    }

    if (in.msSincePaid > in.giveUpAfterMs) {
        // NAME WHICH FAILURE THIS IS. The pair of numbers answers it: the fee
        // either left the purse or it did not.
        out.verdict = out.check.goldDelta < 0 ? TrainVerdict::FeeTakenNoLesson
                                              : TrainVerdict::NoAnswer;
        return out;
    }

    out.verdict = TrainVerdict::Waiting;
    return out;
}

}  // namespace uo::life
