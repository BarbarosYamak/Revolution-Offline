// Deterministic protocol tests for two false-negative bugs found in
// run_r4/pair_Tarath.console.txt and run_r4/pair_Durnholde.console.txt
// (20:34:17 - 20:53:38):
//
//   1. Move-item verification (Client.cpp ActionOnItemInContainer) called a
//      move a "server_failure ... item landed in a different container" when
//      it had actually succeeded, in two distinct shapes:
//        a. A trade-window drop whose confirming 0x25 named the OTHER side's
//           container instead of ours (pair_Tarath 20:53:35.052-053: item
//           0x40010870 verified as a failure, then "is now in the our
//           window" one log line later).
//        b. A partial-stack move, whose SPLIT ECHO -- the 0x25 Sphere sends
//           for the original serial while it is still in the source container
//           -- was mistaken for a bounce.
//      A REAL refusal -- the whole stack bouncing back to the source
//      container -- must still fail; that path is asserted too.
//
//      The b. reading was itself wrong and is corrected here (wave15, 2026-08-31).
//      Source-X splits during the PICKUP: CChar::ItemPickup
//      (src/game/chars/CCharAct.cpp:3007-3010) calls
//      CItem::UnStackSplit(amount) (src/game/items/CItem.cpp:1251-1284), which
//      sets the ORIGINAL item -- the one about to be dragged -- to the amount
//      being lifted and creates a NEW item for the leftover. SetAmountUpdate
//      then Update()s it (CItem.cpp:2272-2286, 4204-4239), so the client is
//      sent a 0x25 for the ORIGINAL serial, still in the SOURCE container,
//      carrying the LIFTED amount. That packet says nothing about where the
//      item ended up, and a bounce of the same pile is byte-identical to it;
//      only arrival order separates them. Treating it as proof of success
//      scored 7 of wave15's 11 move_item "successes" while nothing ever
//      reached a bank box.
//
//   3. A bank box only answers from the tile it was opened on. Source-X
//      stamps m_itEqBankBox.m_pntOpen at open time
//      (src/game/items/CItemContainer.cpp:1119) and compares it against the
//      character's current top point on every drop
//      (src/game/clients/CClientEvent.cpp:448-467) and every lift
//      (src/game/chars/CCharStatus.cpp:1063-1069) -- exact tile equality, no
//      radius -- bouncing silently when they differ. wave15 Kharain issued
//      1083 deposits while walking; every one bounced.
//
//   2. Unicode speech (0xAE, Client::OnUnicodeMessage) filed an empty
//      speaker name whenever the packet's own name field was empty, even
//      when the speaking mobile's name was already known from an earlier
//      line (pair_Tarath 20:53:30.418, pair_Durnholde 20:53:32.261: "from "
//      with nothing after it). ResolveSpeakerName's fallback through the
//      world cache is what this suite proves.
//
// Every packet below is hand-built to the exact wire layout Client.cpp's own
// handlers document (see the comments at OnSecureTrade, OnAddItemToContainer,
// OnAsciiMessage, OnUnicodeMessage) and fed through
// Client::DispatchPacketForTest -- the real dispatcher, the exact object the
// live bot runs. No server: ConnectAndSendSeed() is pointed at a loopback
// listener this file opens itself, purely so Client::Send() has a live
// socket to write the outbound lift/drop packets into (their bytes are never
// inspected -- only the resulting local state is).

#include "Client.h"
#include "net/Socket.h"
#include "uo/endian.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

using namespace uo;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------------------
// Packet builders. Each mirrors the layout documented at the handler in
// Client.cpp/ClientTrade.cpp; offsets are cited there, not re-derived here.
// ---------------------------------------------------------------------------

// 0x6F SECURE_TRADE_OPEN (ClientTrade.cpp OnSecureTrade):
//   [3]=0 action, [4..7] partner, [8..11] myContainer, [12..15] theirContainer,
//   [16] flag, [17..46] name (30 ASCII).
std::vector<u8> MakeTradeOpen(u32 partner, u32 myContainer, u32 theirContainer,
                              const char* name) {
    std::vector<u8> p(47, 0);
    p[0] = 0x6F;
    p[3] = 0x00;
    StoreBE32(&p[4], partner);
    StoreBE32(&p[8], myContainer);
    StoreBE32(&p[12], theirContainer);
    p[16] = 1;
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[17], name, n < 30 ? n : 30);
    }
    return p;
}

// 0x25 ADD_ITEM_TO_CONTAINER (Client.cpp OnAddItemToContainer), 20 bytes:
// serial(4) graphic(2) gfxOffset(1) amount(2) x(2) y(2) container(4) hue(2).
std::vector<u8> MakeAddItem(u32 serial, u16 graphic, u16 amount, u32 container) {
    std::vector<u8> p(20, 0);
    p[0] = 0x25;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[5], graphic);
    p[7] = 0;
    StoreBE16(&p[8], amount);
    StoreBE16(&p[10], 0);
    StoreBE16(&p[12], 0);
    StoreBE32(&p[14], container);
    StoreBE16(&p[18], 0);
    return p;
}

// 0x1B LOGIN_CONFIRM (Client.cpp OnLoginConfirm, >=18 bytes):
// serial(4) .. body@9(2) x@11(2) y@13(2) z@15(2) dir@17(1).
std::vector<u8> MakeLoginConfirm(u32 serial, u16 x, u16 y) {
    std::vector<u8> p(37, 0);
    p[0] = 0x1B;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[9], 0x0190);
    StoreBE16(&p[11], x);
    StoreBE16(&p[13], y);
    StoreBE16(&p[15], 0);
    p[17] = 0;
    return p;
}

// 0x24 DRAW_CONTAINER (Client.cpp OnDrawContainer): serial(4) gumpId(2).
std::vector<u8> MakeDrawContainer(u32 serial, u16 gumpId) {
    std::vector<u8> p(7, 0);
    p[0] = 0x24;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[5], gumpId);
    return p;
}

// 0x2E EQUIP_ITEM (Client.cpp OnEquipItem, 15 bytes):
// serial(4) graphic(2) pad(1) layer(1) mobile(4) hue(2).
std::vector<u8> MakeEquip(u32 item, u16 graphic, u8 layer, u32 mobile) {
    std::vector<u8> p(15, 0);
    p[0] = 0x2E;
    StoreBE32(&p[1], item);
    StoreBE16(&p[5], graphic);
    p[7] = 0;
    p[8] = layer;
    StoreBE32(&p[9], mobile);
    StoreBE16(&p[13], 0);
    return p;
}

// 0x1C ASCII_MESSAGE (Client.cpp OnAsciiMessage): serial(4) body(2) type(1)
// hue(2) font(2) name[30] text (NUL-terminated ASCII). Header is 44 bytes.
std::vector<u8> MakeAsciiMessage(u32 serial, const char* name, const char* text) {
    const usize textLen = std::strlen(text) + 1;
    std::vector<u8> p(44 + textLen, 0);
    p[0] = 0x1C;
    StoreBE32(&p[3], serial);
    p[9] = 0;
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[14], name, n < 30 ? n : 30);
    }
    std::memcpy(&p[44], text, textLen);
    return p;
}

// 0xAE UNICODE_MESSAGE (Client.cpp OnUnicodeMessage): same 44-byte header as
// 0x1C, then a 4-byte language code, then UTF-16BE text, NUL-terminated.
std::vector<u8> MakeUnicodeMessage(u32 serial, const char* name, const char* text) {
    const usize chars = std::strlen(text);
    std::vector<u8> p(48 + (chars + 1) * 2, 0);
    p[0] = 0xAE;
    StoreBE32(&p[3], serial);
    p[9] = 0;
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[14], name, n < 30 ? n : 30);
    }
    std::memcpy(&p[44], "ENU", 3);
    for (usize i = 0; i < chars; ++i)
        StoreBE16(&p[48 + i * 2],
                  static_cast<u16>(static_cast<unsigned char>(text[i])));
    StoreBE16(&p[48 + chars * 2], 0);
    return p;
}

// ---------------------------------------------------------------------------
// A Client whose Send() has somewhere real to write: a loopback listener
// this process also owns. Nothing sent is inspected -- ActionMoveItem/
// ActionTradeOffer only need Send() to return true so the action stays
// Pending until the scripted 0x25 answers it, exactly as it would waiting on
// a real shard.
// ---------------------------------------------------------------------------
Client::Config MakeConfig() {
    Client::Config cfg{};
    cfg.loginHost = "127.0.0.1";
    cfg.username = "trade_verify";
    cfg.password = "trade_verify";
    cfg.version = "2.0.7";
    cfg.sendSeed = false;
    cfg.sessionTag = "trade_verify";
    return cfg;
}

std::unique_ptr<Client> MakeConnectedClient() {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    int len = sizeof(addr);
    getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    const u16 port = ntohs(addr.sin_port);
    listen(listener, 1);

    auto client = std::make_unique<Client>(MakeConfig());
    client->ConnectForTest("127.0.0.1", port);

    // Complete the handshake so the accepted socket survives independent of
    // the listener (a socket sitting only in the not-yet-accepted backlog
    // can be torn down when the listener closes). The accepted end is
    // deliberately leaked -- this is a short-lived test process and the OS
    // reclaims it on exit.
    sockaddr_in peer{};
    int plen = sizeof(peer);
    accept(listener, reinterpret_cast<sockaddr*>(&peer), &plen);
    closesocket(listener);
    return client;
}

// ---------------------------------------------------------------------------
// 1a. Trade window: the far/other-side container still verifies.
// ---------------------------------------------------------------------------
void TestTradeWindowAltContainerIsSuccess() {
    Section("trade window: landing in the partner's container still verifies");

    auto c = MakeConnectedClient();
    const u32 partner = 0x00014401;
    const u32 mine = 0x400107D1;
    const u32 theirs = 0x4001078B;
    auto open = MakeTradeOpen(partner, mine, theirs, "Durnholde");
    c->DispatchPacketForTest(open.data(), open.size());
    Check(c->Trade().Active(), "trade window opened");
    Check(c->Trade().MyContainer() == mine, "my container recorded");

    c->ActionTradeOffer(0x40010870, 47);
    Check(c->ActionBusy(), "move_item action started");

    // The wrong-first-packet shape from the evidence: the confirming 0x25
    // names the OTHER side's container, not ours.
    auto add = MakeAddItem(0x40010870, 0x1BFD, 47, theirs);
    c->DispatchPacketForTest(add.data(), add.size());
    Check(c->ActionResult() == act::Result::Success,
          "landing in the trade partner's own container still verifies");
}

// ---------------------------------------------------------------------------
// 1b. Partial stack: the SPLIT ECHO is not an answer. It must leave the action
// pending, and only the destination's own 0x25 may finish it.
// ---------------------------------------------------------------------------
void TestSplitEchoWaitsForTheDestination() {
    Section("partial stack: the split echo waits, the destination verifies");

    auto c = MakeConnectedClient();
    const u32 pack = 0x40001000;
    const u32 bank = 0x40014400;

    // Seed the local container cache: 50 ingots already sitting in the pack.
    auto seed = MakeAddItem(0x400143D7, 0x1BF2, 50, pack);
    c->DispatchPacketForTest(seed.data(), seed.size());

    c->ActionMoveItem(0x400143D7, 40, bank);
    Check(c->ActionBusy(), "move_item action started");

    // The split echo Sphere sends during the LIFT: same serial, still in the
    // SOURCE container, carrying the amount being lifted (CItem.cpp:1251-1284
    // sets THIS item to `amount`, then SetAmountUpdate -> Update -> addItem).
    auto echo = MakeAddItem(0x400143D7, 0x1BF2, 40, pack);
    c->DispatchPacketForTest(echo.data(), echo.size());
    Check(c->ActionBusy(),
          "the split echo does not decide the move either way");
    Check(c->ActionResult() == act::Result::Pending,
          "the move is still pending after the split echo");

    // Then the real answer: the dragged pile lands in the destination.
    auto landed = MakeAddItem(0x400143D7, 0x1BF2, 40, bank);
    c->DispatchPacketForTest(landed.data(), landed.size());
    Check(c->ActionResult() == act::Result::Success,
          "the destination's own 0x25 is what verifies the move");
}

// ---------------------------------------------------------------------------
// 1b-negative. The exact wave15 shape: a partial lift whose drop is refused.
// The split echo comes first, the bounce of the same pile second -- identical
// packets -- and the move must FAIL. Under the old arithmetic (10 of 20 iron
// ingots, exactly half the stack) this was scored a success 1083 times.
// ---------------------------------------------------------------------------
void TestSplitEchoThenBounceIsAFailure() {
    Section("partial stack: echo then bounce is still a failure");

    auto c = MakeConnectedClient();
    const u32 pack = 0x40016AE5;
    const u32 bank = 0x40016AE7;

    auto seed = MakeAddItem(0x400128E9, 0x1BF2, 20, pack);
    c->DispatchPacketForTest(seed.data(), seed.size());

    c->ActionMoveItem(0x400128E9, 10, bank);
    Check(c->ActionBusy(), "move_item action started");

    auto echo = MakeAddItem(0x400128E9, 0x1BF2, 10, pack);
    c->DispatchPacketForTest(echo.data(), echo.size());
    Check(c->ActionBusy(), "still pending after the split echo");

    // Event_Item_Drop_Fail puts the dragged pile straight back
    // (CClientEvent.cpp:248-271): the same serial, the same container, the
    // same amount as the echo.
    auto bounce = MakeAddItem(0x400128E9, 0x1BF2, 10, pack);
    c->DispatchPacketForTest(bounce.data(), bounce.size());
    Check(c->ActionResult() == act::Result::ServerFailure,
          "the bounce after the split echo fails the move");
}

// ---------------------------------------------------------------------------
// 3a. A bank move issued from a tile other than the one the box was opened on
// is refused HERE, without sending a lift/drop the server can only bounce.
// ---------------------------------------------------------------------------
void TestBankMoveFromAnotherTileIsRefused() {
    Section("bank: a move from off the open tile never leaves the client");

    auto c = MakeConnectedClient();
    const u32 me   = 0x00012345;
    const u32 pack = 0x40001000;
    const u32 bank = 0x40014400;

    auto login = MakeLoginConfirm(me, 1426, 1687);
    c->DispatchPacketForTest(login.data(), login.size());

    // The bank box arrives as a container we did not double-click while an
    // open_bank action is outstanding -- the live recognition path.
    c->ActionOpenBank(0, "bank");
    auto gump = MakeDrawContainer(bank, 0x004A);
    c->DispatchPacketForTest(gump.data(), gump.size());
    Check(c->BankContainer() == bank, "the bank box was recognised");
    Check(c->BankOpenTileHeld(), "we are still on the tile it opened from");

    auto seed = MakeAddItem(0x400143D7, 0x1BF2, 20, pack);
    c->DispatchPacketForTest(seed.data(), seed.size());

    // One step. That is all Source-X needs to refuse everything.
    auto moved = MakeLoginConfirm(me, 1427, 1687);
    c->DispatchPacketForTest(moved.data(), moved.size());
    Check(!c->BankOpenTileHeld(), "one step off the tile is detected");

    c->ActionMoveItem(0x400143D7, 10, bank);
    Check(!c->ActionBusy(), "the doomed move is not left pending");
    Check(c->ActionResult() == act::Result::InvalidState,
          "a bank move from the wrong tile is refused before it is sent");

    // Back on the tile, the same move goes out and waits for the server.
    auto back = MakeLoginConfirm(me, 1426, 1687);
    c->DispatchPacketForTest(back.data(), back.size());
    c->ActionMoveItem(0x400143D7, 10, bank);
    Check(c->ActionBusy(), "from the open tile the move is sent as normal");
}

// ---------------------------------------------------------------------------
// 3b. A WITHDRAWAL that leaves the item sitting in the bank box is a failure,
// not an alternate spelling of the destination. (wave15 Kharain 18:08:20.467
// asked for the pack, was told "is in the bank box", and scored a success.)
// ---------------------------------------------------------------------------
void TestWithdrawalStuckInTheBoxIsAFailure() {
    Section("bank: an item still in the box is a failed withdrawal");

    auto c = MakeConnectedClient();
    const u32 me   = 0x00012345;
    const u32 pack = 0x40016AE5;
    const u32 bank = 0x40016AE7;

    auto login = MakeLoginConfirm(me, 1426, 1687);
    c->DispatchPacketForTest(login.data(), login.size());
    auto worn = MakeEquip(pack, 0x0E75, 0x15, me);   // layer 21 = backpack
    c->DispatchPacketForTest(worn.data(), worn.size());
    Check(c->BackpackSerial() == pack, "the backpack serial is known");

    c->ActionOpenBank(0, "bank");
    auto gump = MakeDrawContainer(bank, 0x004A);
    c->DispatchPacketForTest(gump.data(), gump.size());
    Check(c->BankContainer() == bank, "the bank box was recognised");

    // 20 coins in the BOX; ask for all of them to come to the pack.
    auto seed = MakeAddItem(0x400128E9, 0x0EED, 20, bank);
    c->DispatchPacketForTest(seed.data(), seed.size());
    c->ActionMoveItem(0x400128E9, 20, pack);
    Check(c->ActionBusy(), "move_item action started");

    auto stuck = MakeAddItem(0x400128E9, 0x0EED, 20, bank);
    c->DispatchPacketForTest(stuck.data(), stuck.size());
    Check(c->ActionResult() == act::Result::ServerFailure,
          "still in the box means the withdrawal failed");
}

// ---------------------------------------------------------------------------
// 1c. A real refusal must still fail: the WHOLE stack bounces back to the
// source container UNCHANGED, which does not satisfy the partial-stack
// arithmetic above.
// ---------------------------------------------------------------------------
void TestFullBounceBackIsStillAFailure() {
    Section("refusal: the whole stack bouncing back unchanged still fails");

    auto c = MakeConnectedClient();
    const u32 pack = 0x40001000;
    const u32 dest = 0x40099999;

    auto seed = MakeAddItem(0x40020000, 0x0EED, 12, pack);
    c->DispatchPacketForTest(seed.data(), seed.size());

    c->ActionMoveItem(0x40020000, 12, dest);
    Check(c->ActionBusy(), "move_item action started");

    // Refused: the item reappears in the SAME source container with the
    // SAME (unreduced) amount -- not a partial-stack split, and dest is
    // neither an open trade container nor the resolved bank box.
    auto bounce = MakeAddItem(0x40020000, 0x0EED, 12, pack);
    c->DispatchPacketForTest(bounce.data(), bounce.size());
    Check(c->ActionResult() == act::Result::ServerFailure,
          "an unreduced bounce back to the source container is still a failure");
}

// ---------------------------------------------------------------------------
// 2a. Unicode speech: an empty name field, with a serial the client already
// knows (an earlier ascii line from the same mobile), resolves in the FILED
// journal entry rather than staying empty.
// ---------------------------------------------------------------------------
void TestUnicodeSpeechResolvesKnownSerial() {
    Section("unicode speech: empty name resolves for a previously-seen serial");

    auto c = MakeConnectedClient();
    const u32 speaker = 0x000143D5;

    // The name is learned from an earlier line -- mirrors the ascii line
    // that carried "Hyman" while the SAME serial's unicode lines did not.
    auto ascii = MakeAsciiMessage(speaker, "Tarath", "hello");
    c->DispatchPacketForTest(ascii.data(), ascii.size());
    Check(c->LastJournalSpeakerForTest() == "Tarath",
          "the ascii line filed with its own name (sanity check)");

    auto uni = MakeUnicodeMessage(speaker, "", "WTS 47 i_log 2gp");
    c->DispatchPacketForTest(uni.data(), uni.size());
    Check(c->LastJournalSpeakerForTest() == "Tarath",
          "the empty-name unicode line resolved through the world cache");

    // And the consumer path the life layer actually reads (Runner.cpp's
    // "trade: heard ... from" line) sees the same name.
    std::vector<Client::Heard> heard;
    c->JournalHeardSince(0, heard);
    bool found = false;
    for (const auto& h : heard) {
        if (h.speaker == speaker && h.text == "WTS 47 i_log 2gp") {
            found = true;
            Check(h.name == "Tarath", "JournalHeardSince also names the speaker");
        }
    }
    Check(found, "the unicode line reached the journal");
}

// ---------------------------------------------------------------------------
// 2b. Negative: a serial never seen before keeps its raw (empty) name --
// the fix must not fabricate an attribution it cannot support.
// ---------------------------------------------------------------------------
void TestUnicodeSpeechUnknownSerialStaysRaw() {
    Section("unicode speech: empty name with an unknown serial stays raw");

    auto c = MakeConnectedClient();
    const u32 speaker = 0x00099999;

    auto uni = MakeUnicodeMessage(speaker, "", "hello?");
    c->DispatchPacketForTest(uni.data(), uni.size());
    Check(c->LastJournalSpeakerForTest().empty(),
          "no fabricated name for a serial this session has never seen");
}

}  // namespace

int main() {
    net::Socket::WSAStart();
    std::printf("trade verification + speech resolution tests\n\n");

    TestTradeWindowAltContainerIsSuccess();
    TestSplitEchoWaitsForTheDestination();
    TestSplitEchoThenBounceIsAFailure();
    TestFullBounceBackIsStillAFailure();
    TestBankMoveFromAnotherTileIsRefused();
    TestWithdrawalStuckInTheBoxIsAFailure();
    TestUnicodeSpeechResolvesKnownSerial();
    TestUnicodeSpeechUnknownSerialStaysRaw();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");

    net::Socket::WSACleanupOnce();
    return g_failures == 0 ? 0 : 1;
}
