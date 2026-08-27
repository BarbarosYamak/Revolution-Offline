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
//   gait walk|run|auto    session gait for every later step (default auto,
//                         which runs; individual steps may still walk)
//   goto <x> <y>          plan a route with A* and walk it
//   goto_mobile <who>     walk to a mobile the server has shown us
//   scan_mobiles          ask the server for nearby mobile names (0x98)
//   wait_goto             wait until the route finishes (arrived or gave up)
//   wait_walk             wait until every queued step has been answered
//   say <text>            speak (ASCII)
//   backpack              open the worn backpack
//   wait_backpack         wait until the backpack's contents have arrived
//   sleep <ms>            pause
//   hold <ms>             stay connected and idle for this long
//   logout                request logout and close the connection
//
// M2 player actions. Each starts an asynchronous action; `wait_action` blocks
// the script until the server confirms, rejects or the deadline passes, and
// `expect <result>` asserts what happened (success by default).
//
//   use <serial|@name>            double-click an object
//   open <serial|@name>           double-click, expecting container contents
//   move <item> <amount> <dest>   move an item into a container
//   drop_ground <item> <amount>   drop an item at the character's feet
//   equip <item> <layer>          wear an item on a layer
//   unequip <item>                take it off, back into the backpack
//   skill <id> [target]           use a skill (auto-answers its cursor)
//   cast <id> [target]            cast a spell (auto-answers its cursor)
//   cast_scroll <item> [target]   cast the spell a scroll carries
//   attack <serial|@name>         initiate combat
//   war <on|off>                  toggle war mode
//   bandage <item> [target]       use a bandage on a character
//   bank <banker> [phrase]        open the bank the way a player does
//   vendor_open <vendor> [phrase]  ask a vendor to show its wares (speech)
//   vendor_buy <vendor> <item> <qty>
//   vendor_sell <vendor> <item> <qty>
//   target <serial|@name>         answer an armed cursor with an object
//   target_ground <x> <y> <z>     answer an armed cursor with a tile
//   target_cancel                 cancel an armed cursor
//   wait_action                   wait for the current action to finish
//   expect <result>               assert the last action result
//   wait_target                   wait until a target cursor is armed
//   resurrect                     acknowledge the ghost state and wait
//   wait_dead                     wait until the server reports us dead
//   wait_alive                    wait until the server reports us alive
//   vendor_sell_open <who> [word] ask a vendor what it will buy
//
// Verification verbs. These read server-driven state only, and abort the
// scenario when the server did not actually do what was expected:
//   mark_hp                       remember current hit points
//   expect_hp_gain                fail unless hit points rose since mark_hp
//   wait_hp_below <n>             wait until hit points drop below n
//   mark_item <graphic>           remember how many of that item we carry
//   expect_item_drop <graphic>    fail unless that count fell
//   mark_gold                     remember current gold
//   expect_gold_gain              fail unless gold rose since mark_gold
//   remember <name> <expr>        bind a serial for later use (see below)
//   require <name>                fail the scenario unless <name> is bound
//   mobile_pos <serial|@name>     log a cached mobile's position and distance
//                                 from us (read-only; does not move anything).
//                                 Pet-command confirmations are silent (see
//                                 uo/pet.h) so this is how a scenario observes
//                                 an owned animal's behaviour actually change
//                                 instead of trusting a sysmessage that never
//                                 comes.
//
// `remember` binds a name to a serial so later steps can refer to it as
// @name. Supported expressions:
//   self                          our own character
//   backpack                      the worn backpack
//   bank                          the container the server opened as our bank
//   pack_graphic <hex>            first backpack item with that graphic
//   carried_graphic <hex,...>     first item with any of those graphics, in
//                                 the backpack or worn
//   mobile_nearest                nearest cached mobile that is not us
//   mobile_name <text>            nearest cached mobile whose name contains text
//   mobile_trade <text>           nearest mobile whose paperdoll trade is
//                                 exactly <text> ("the mage", not "the mage
//                                 guildmaster")
//   mobile_body <hex>             nearest cached mobile with that body graphic
//   vendor_first                  first item in the current vendor offer
//   vendor_graphic:<hex>          the offered item with that graphic id
//   vendor_sell_first             first item the vendor offered to buy
//   vendor_sell_graphic:<hex>     the offered item with that graphic id
//   container_graphic <container> <hex,...>
//                                 first item with any of those graphics inside
//                                 an ARBITRARY open container (e.g. a carved
//                                 corpse) -- <container> is itself resolved as
//                                 an operand (@name, self, backpack, ... or a
//                                 literal serial), then Client::
//                                 FindContainerItemByGraphic searches that
//                                 container's own cached contents. Unlike
//                                 pack_graphic/carried_graphic, which are
//                                 hard-wired to the player's own worn
//                                 backpack, this can look inside anything the
//                                 client has an open 0x3C for.
//   0x1234ABCD                    a literal serial
//
// Container verbs, added alongside container_graphic above so a corpse's
// contents (or any other open container's) can be asserted and moved, not
// just found:
//   expect_container_count <container> <n>
//                                 fail unless that container's cached item
//                                 count is exactly n (e.g. 0 right after a
//                                 kill, before carving)
//   container_report <container> log every item currently cached for that
//                                 container (serial, graphic, amount)
//   take <item> <amount>         lift an item out of whatever open container
//                                 it is in and drop it into our own backpack
//                                 (0x07 lift + 0x08 drop, Client::
//                                 TakeFromContainer) -- <item> is typically a
//                                 name bound by `remember ... container_graphic`
// ---------------------------------------------------------------------------
class Scenario {
public:
    // Loads `path`. Returns false (and leaves the scenario empty) if the file
    // cannot be read; `err` receives a human-readable reason.
    bool Load(const char* path, std::string* err);

    bool Empty() const { return steps_.empty(); }
    bool Finished() const { return pc_ >= steps_.size() || failed_; }
    bool Failed() const { return failed_; }

    // Advance the script. Call once per client tick with the current
    // monotonic millisecond clock.
    void Tick(Client& client, i64 nowMs);

private:
    enum class Op : u8 {
        WaitWorld, Walk, WaitWalk, Say, Backpack, WaitBackpack,
        Sleep, Hold, Logout, Goto, WaitGoto,
        // M2
        Use, Open, Move, DropGround, Equip, Unequip, Skill, Cast, Attack,
        War, Bandage, Bank, VendorOpen, VendorBuy, Target, TargetGround, TargetCancel,
        WaitAction, Expect, WaitTarget, Resurrect, Remember, Require, CastScroll,
        GotoMobile, WaitDead, WaitAlive,
        MarkHp, ExpectHpGain, WaitHpBelow,
        MarkItem, ExpectItemDrop,
        MarkGold, ExpectGoldGain,
        VendorSellOpen, VendorSell, ScanMobiles,
        // New ops go at the END: OpName()'s table is indexed by this enum, and
        // inserting mid-enum while appending the name silently misnames every
        // op after the insertion point (it has happened).
        SetGait,
        // M2.5 semantic travel. None of these carries a route -- only a
        // destination -- which is the whole point of the milestone.
        TravelPoint, TravelPlace, TravelRegion, TravelService, TravelResource,
        TravelEntity, TravelCorpse, TravelHome, SetHome,
        WaitTravel, ExpectTravel, UseMoongates,
        EnsurePeace, ExpectPeace, ExpectWar,
        ExpectRegion, ExpectPlace, ExpectServiceKnown,
        // M3 progression. Skill values are read from the server's own 0x3A;
        // nothing here can change one.
        SkillReport, MarkSkill, ExpectSkillGain, WaitSkill, RequestSkills,
        // Training is repetition, so the scenario language needs a loop. It is
        // deliberately the simplest thing that works: a bounded count, one
        // level of nesting checked at load time, and no conditionals.
        Loop, EndLoop, WaitMana, ExpectItemGain,
        // M3 secure player trade.
        TradeStart, TradeOffer, TradeAccept, TradeRetract, TradeCancel,
        WaitTradeOpen, WaitTradePartner, WaitTradeClosed, ExpectTrade,
        WaitTradeOffer, WaitTradeMine,
        NpcTrain, Give,
        WaitGump, GumpButton, GumpReport,
        // Answer a cursor with a STATIC tile AND its graphic. Trees are
        // statics and the server identifies them through
        // CanTouchStatic(&pt, id, ...), so a plain ground reply -- which
        // carries graphic 0 -- gets "It appears immune to your blow".
        TargetStatic,
        // Answer a 0x7C MENU -- the old-style craft menu Sphere opens for
        // Carpentry, Blacksmithy, Tailoring and the rest -- by 1-based option
        // index. Distinct from gump_button, which answers a 0xB0 generic gump.
        MenuChoose,
        // Proves a count did NOT move. Needed for refusals: a vendor the policy
        // blocks leaves the pack exactly as it was, and "exactly as it was" is
        // not expressible with ExpectItemDrop, which demands a decrease.
        ExpectItemSame,
        // M3.7.1 mounts. Mounting is an ordinary double-click on the animal;
        // what needs its own ops is the WAIT and the ASSERT, because the
        // server confirms a mount by equipping layer 25, not by any of the
        // things UseObject watches for (container, target cursor, sysmessage).
        Mount,
        WaitMounted,
        ExpectMounted,
        Dismount,
        MenuPick,
        ExpectMenuHas,
        MenuReport,
        Survival,
        // Read-only position probe (see mobile_pos above). Appended last, like
        // every op before it: kOpNames is indexed by this enum.
        MobilePos,
        // M3.9.2 loot pull. See the container verbs above the class comment
        // block. Appended last, like every op before it.
        ExpectContainerCount, ContainerReport, Take,
        Count,   // keep last: OpName() static_asserts its table against this
    };
    struct Step {
        Op          op = Op::WaitWorld;
        u8          dir = 0;
        int         count = 0;
        i32         x = 0;
        i32         y = 0;
        i32         z = 0;
        int         id = 0;          // skill / spell id
        std::string a, b, c;         // operands, resolved at run time
        i64         durationMs = 0;
        std::string text;
        int         line = 0;
    };

    // Resolve an operand (literal serial, @name or a keyword) to a serial.
    u32 Resolve(Client& client, const std::string& tok) const;
    // A travel verb whose destination did not resolve. That is a scenario
    // bug (a place that does not exist, a mobile not in view), not a travel
    // outcome, so it aborts rather than being reported as a failed trip.
    void FailTravelStart(Client& client, const Step& st, const char* verb);
    void Bind(const std::string& name, u32 serial);

    std::vector<Step> steps_;
    std::vector<std::pair<std::string, u32>> binds_;
    i32 markHp_ = -1;
    i32 markGold_ = -1;
    u16 markSkillIndex_ = 0xFFFF;
    i32 markSkillTenths_ = -1;
    struct LoopFrame { usize start; int remaining; };
    std::vector<LoopFrame> loops_;
    u32 markItemCount_ = 0;
    u16 markItemGraphic_ = 0;
    std::string markItemList_;   // the graphics mark_item was given, verbatim
    bool failed_ = false;
    bool aborted_ = false;   // failure already reported and logout issued
    usize pc_ = 0;
    bool  entered_ = false;    // current step has run its one-shot side effect
    i64   deadlineMs_ = 0;     // for Sleep/Hold
};

}
}
