#include "uo/activities/craft_confirm.h"

namespace uo::life {

// ---------------------------------------------------------------------------
// THE TABLE. Every row is quoted verbatim from a shard script AND was heard
// in a recorded run; the counts are how many times the string appears across
// run_m5/*.console.txt and run_m7/*.console.txt on 2026-08-30.
// ---------------------------------------------------------------------------
static const CraftFailure kCraftFailures[] = {
    // The inscriber's ruined scroll. The shard SAYS this one -- it is a
    // SRC.SYSMESSAGE with the text written out, not a cliloc -- which is why
    // it is the only craft failure the bot has ever actually heard about its
    // own work. The input is consumed and nothing is produced, so counting
    // attempts would count wrong and the pack alone would look like silence.
    {"you fail to inscribe the scroll, and the scroll is ruined.",
     "the scroll was ruined and the blank is spent",
     /*blocking=*/false,
     "runtime/scripts/skills/skill23_inscription.scp:25; heard 17x in "
     "run_m5/run_m7 consoles"},

    // No fire in reach. Blocking: the campfire either burns or it does not,
    // and swinging again from the same tile cannot change that. DoCraft
    // already lights kindling before it gets here, so hearing this means the
    // fire went out or was never lit -- stand down and let the goal be
    // re-picked once, rather than cooking at nothing.
    {"you must be near a fire source to cook.",
     "there is no fire in reach to cook on",
     /*blocking=*/true,
     "runtime/scripts/core/messages.scp:115 (cooking_fire_source), cliloc "
     "1044487 in crafting/interface/def_cooking.scp:20; heard 27x live"},

    // The opener double-click was refused. Blocking, and the most expensive
    // of the three to get wrong: the menu is never going to open, so every
    // further click is a session spent. This is the string that answered
    // alchemy's reagent before OpenerFor() existed.
    {"you can't think of a way to use that item.",
     "the shard refused the thing used to open the menu",
     /*blocking=*/true,
     "runtime/scripts/core/messages.scp:268 (itemuse_cantthink) and :351 "
     "(itemuse_unable); heard 9x live"},

    // S6: 0xC1 (cliloc message) used to be discarded at src/Client.cpp:569 --
    // see docs/S3_CHARACTERIZATION.md's "shard messages" section, which is
    // where these three rows come from. crafting/crafting_messages.scp is
    // almost entirely cliloc numbers, and these three are the ones that gate
    // a craft. Client.cpp now decodes 0xC1 into the journal (src/net/Cliloc.h,
    // Client::OnClilocMessage), but NO Cliloc.enu/.tur ships with this
    // shard's client data (checked 2026-08-30 across runtime/,
    // local/revolution-client/, bot/uo-client/ -- nothing named *cliloc*
    // anywhere but protocol docs), so an id that DID arrive as a real 0xC1
    // packet could not be resolved to real text and would fall back to the
    // id-only form "[cliloc <id>]" (src/net/Cliloc.cpp
    // FormatClilocJournalText). The "[cliloc N]" rows below are kept for
    // exactly that case.
    //
    // S6b correction: these three messages do NOT actually travel as a
    // SYSMESSAGE/0xC1 packet at all. crafting_events.scp:77,85 and
    // crafting_functions.scp:46,50 write the numeric cliloc into
    // `ctag.craft.message`, a CLIENT TAG consumed only by
    // crafting/crafting_dialog.scp:93-97 -- the crafting gump's "notices"
    // panel: `if <isnum <ctag.craft.message>> then XMFHTMLGUMPCOLOR (id
    // resolved client-side from Cliloc.enu -- absent here, renders BLANK)
    // else DHTMLGUMP (literal text baked into the gump)`. That control lives
    // inside the persistent crafting gump packet (0xB0/0xDD), not a 0xC1/0xCC
    // packet, so Client::OnClilocMessage never fires for these three and
    // JournalSaidSince can't see them via the journal either form. Confirmed
    // by grepping server/Source-X/src for the literal ids/text: no hit.
    //
    // The fix applied in this slice: crafting/crafting_messages.scp now
    // holds the plain English text (see crafting_messages.scp.bak_20260830_cliloc
    // for the pre-edit numeric version) so `isnum` is false and the human
    // client's gump renders the readable DHTMLGUMP text instead of a blank
    // XMFHTMLGUMPCOLOR box. Whether the bot can ever see this text is
    // UNKNOWN and OUT OF SCOPE here: it would need gump-content parsing (no
    // such code exists in Client.cpp/Runner.cpp today), not journal
    // decoding. The plain-text rows below are added on the chance a future
    // path surfaces this text into the journal (e.g. if the gump content is
    // ever mirrored there); until then neither the plain-text nor the
    // "[cliloc N]" row for these three defs is expected to actually match
    // anything JournalSaidSince observes. Both are harmless to keep.
    //
    // The roll failed and consumed the input -- same category as the ruined
    // scroll above (Sphere's @skillfail/@skillabort handlers set this
    // message; crafting_events.scp:77,85), so blocking=false.
    {"You failed to create the item, and some of your materials are lost.",
     "the roll failed and materials were consumed (craft_msg_fail)",
     /*blocking=*/false,
     "runtime/scripts/crafting/crafting_messages.scp:10 (post-S6b): "
     "scp.craft_msg_fail now the literal text, was \"1044043\" // You "
     "failed to create the item, and some of your materials are lost. -- "
     "gated by @skillfail/@skillabort in crafting_events.scp:77,85, before "
     "consume; reaches only the crafting gump's notices panel via "
     "ctag.craft.message (crafting_dialog.scp:93-97), not the journal; "
     "never observed live as journal text."},
    {"[cliloc 1044043]",
     "the roll failed and materials were consumed (craft_msg_fail)",
     /*blocking=*/false,
     "fallback kept in case a real 0xC1 for this id is ever observed; "
     "runtime/scripts/crafting/crafting_messages.scp.bak_20260830_cliloc:10 "
     "(pre-edit): scp.craft_msg_fail \"1044043\" // You failed to create "
     "the item, and some of your materials are lost."},

    // Gate check in f_craft, BEFORE makeitem/consume -- nothing spent,
    // nothing to gain by swinging again without training. blocking=true.
    {"You don't have the required skills to attempt this item.",
     "the shard says the skill required is not there yet (craft_msg_noskill)",
     /*blocking=*/true,
     "runtime/scripts/crafting/crafting_messages.scp:11 (post-S6b): "
     "scp.craft_msg_noskill now the literal text, was \"1044153\" // You "
     "don't have the required skills to attempt this item. -- gated in "
     "crafting_functions.scp:46 (!<canmakeskill>), before makeitem; "
     "reaches only the crafting gump's notices panel via ctag.craft.message, "
     "not the journal; never observed live as journal text."},
    {"[cliloc 1044153]",
     "the shard says the skill required is not there yet (craft_msg_noskill)",
     /*blocking=*/true,
     "fallback kept in case a real 0xC1 for this id is ever observed; "
     "runtime/scripts/crafting/crafting_messages.scp.bak_20260830_cliloc:11 "
     "(pre-edit): scp.craft_msg_noskill \"1044153\" // You don't have the "
     "required skills to attempt this item."},

    // Same gate function, the sibling check right after it -- also before
    // makeitem/consume. blocking=true.
    {"You don't have the components needed to make that.",
     "the shard says the components needed are not there (craft_msg_noresources)",
     /*blocking=*/true,
     "runtime/scripts/crafting/crafting_messages.scp:21 (post-S6b): "
     "scp.craft_msg_noresources now the literal text, was \"1044253\" // "
     "You don't have the components needed to make that. -- gated in "
     "crafting_functions.scp:50 (!<canmake>), before makeitem; reaches "
     "only the crafting gump's notices panel via ctag.craft.message, not "
     "the journal; never observed live as journal text."},
    {"[cliloc 1044253]",
     "the shard says the components needed are not there (craft_msg_noresources)",
     /*blocking=*/true,
     "fallback kept in case a real 0xC1 for this id is ever observed; "
     "runtime/scripts/crafting/crafting_messages.scp.bak_20260830_cliloc:21 "
     "(pre-edit): scp.craft_msg_noresources \"1044253\" // You don't have "
     "the components needed to make that."},
};

const CraftFailure* CraftFailures(usize* count) {
    if (count) *count = sizeof(kCraftFailures) / sizeof(kCraftFailures[0]);
    return kCraftFailures;
}

const char* CraftVerdictName(CraftVerdict v) {
    switch (v) {
        case CraftVerdict::Waiting:      return "waiting";
        case CraftVerdict::Made:         return "made";
        case CraftVerdict::Spoiled:      return "spoiled";
        case CraftVerdict::ShardRefused: return "shard_refused";
        case CraftVerdict::NoProgress:   return "no_progress";
    }
    return "?";
}

CraftConfirmResult ConfirmCraft(const CraftConfirmInput& in) {
    CraftConfirmResult out;

    // THE PACK FIRST, ALWAYS. A craft that landed is a craft that landed
    // however loudly the shard also complained: a batch can ruin one scroll
    // and finish the next inside the same window, and reading the complaint
    // first would throw away the item that arrived.
    //
    // Routed through interaction/progress.h rather than a `>` written here,
    // so the craft path accounts for success exactly the way the buy path
    // does -- one definition of "the world confirms it", not two.
    Expectation want;
    want.itemBefore = in.packBefore;
    want.itemGain = 1;

    Observed seen;
    seen.itemNow = in.packNow;

    out.check = Verify(want, seen);
    if (out.check.verdict == Verdict::Confirmed) {
        out.verdict = CraftVerdict::Made;
        out.made = out.check.itemDelta;
        out.reason = "the pack count rose";
        return out;
    }

    // THEN THE SHARD'S OWN WORDS. A no is worth more than a not-yet: it ends
    // the swing honestly instead of leaving it to time out, which is how a
    // bot spends a session at one forge.
    if (in.heard && in.heard->text) {
        out.verdict = in.heard->blocking ? CraftVerdict::ShardRefused
                                         : CraftVerdict::Spoiled;
        out.reason = in.heard->why;
        return out;
    }

    // THEN THE DEADLINE. Section 19: the timer may only ever say "give up",
    // never "it worked" and never "swing again inside your own deadline".
    if (in.attemptsExhausted) {
        out.verdict = CraftVerdict::NoProgress;
        out.reason = "the attempts are spent and the pack never moved";
        return out;
    }

    out.verdict = CraftVerdict::Waiting;
    out.reason = in.deadlineExpired
                     ? "the swing is closed out; another may go"
                     : out.check.reason;
    return out;
}

}  // namespace uo::life
