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
#include "uo/market.h"
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

// 0x6F SECURE_TRADE_CLOSE (ClientTrade.cpp OnSecureTrade), 17 bytes:
//   [3]=1 action, [4..7] the container being closed, [8..15] zero, [16] 0.
// Source-X writes the deleted container's own UID there
// (PacketTradeAction::prepareClose, send.cpp:1902-1911) and sends it to that
// container's owner (CItemContainer::Trade_Delete, CItemContainer.cpp:298-306).
std::vector<u8> MakeTradeClose(u32 container) {
    std::vector<u8> p(17, 0);
    p[0] = 0x6F;
    p[3] = 0x01;
    StoreBE32(&p[4], container);
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

// 0x1D DELETE_OBJECT (Client.cpp OnDeleteObject, 5 bytes): cmd + serial(4 BE).
std::vector<u8> MakeDeleteObject(u32 serial) {
    std::vector<u8> p(5, 0);
    p[0] = 0x1D;
    StoreBE32(&p[1], serial);
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

// 0xAE UNICODE_MESSAGE (Client.cpp OnUnicodeMessage):
//   serial@3 body@7 mode@9 hue@10 font@12 language(4 ASCII)@14
//   name(30 ASCII)@18, UTF-16BE NUL-terminated text @48.
// THE LANGUAGE COMES BEFORE THE NAME. This builder used to put the name at
// 14 and the language at 44, mirroring a handler that read the same wrong
// offsets, so the pair agreed with each other and with nothing on the wire.
// The real order is Source-X's PacketMessageUNICODE (network/send.cpp:
// writeStringFixedASCII(language, 4) then writeStringFixedASCII(name, 30))
// and pol_packets.md "Packet: 0xAE". `lang` is a parameter because a
// NUL-filled language field is exactly what made the old reader return an
// empty name for every speaker -- see TestUnicodeSpeechNameFromPacket.
std::vector<u8> MakeUnicodeMessage(u32 serial, const char* name,
                                   const char* text, const char* lang = "") {
    const usize chars = std::strlen(text);
    std::vector<u8> p(48 + (chars + 1) * 2, 0);
    p[0] = 0xAE;
    StoreBE16(&p[1], static_cast<u16>(p.size()));
    StoreBE32(&p[3], serial);
    p[9] = 0;
    if (lang) {
        const usize n = std::strlen(lang);
        std::memcpy(&p[14], lang, n < 4 ? n : 4);
    }
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[18], name, n < 30 ? n : 30);
    }
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
// 1d. A CLOSE ONLY CLOSES ITS OWN WINDOW.
//
// wave30 (artifacts/wave30_bandage20_20260907): Kharos was mid-trade with
// Aelia when Wren dropped goods on him too. The second window was correctly
// declined on the wire, but the CLOSE the server sent back for THAT container
// was applied to the one live trade object: Kharos.console.txt:432-441 shows
// the decline at 18:06:51.134 and "window closed (partner_cancelled)" 15ms
// later, while Aelia.console.txt:805-812 shows her side never closed and
// timing itself out 25s afterwards. Twenty bots at one bank make competing
// windows routine, so the close has to name the window it belongs to.
//
// No socket here: SetOfflineForTest captures Client::Send instead of writing
// it, which is also how the outbound decline is inspected below.
// ---------------------------------------------------------------------------
std::unique_ptr<Client> MakeOfflineClient() {
    auto c = std::make_unique<Client>(MakeConfig());
    c->SetOfflineForTest(true);
    return c;
}

void TestCloseForAnotherWindowLeavesTheLiveTradeOpen() {
    Section("trade: a close for a foreign window does not end the live trade");

    auto c = MakeOfflineClient();
    const u32 partner = 0x0000FD27;      // Aelia
    const u32 mineA   = 0x4001A496;      // our side of the live window
    const u32 theirsA = 0x4001A497;
    auto open = MakeTradeOpen(partner, mineA, theirsA, "Aelia");
    c->DispatchPacketForTest(open.data(), open.size());
    Check(c->Trade().Active(), "trade A opened");

    // (1) A close addressed to a container that is neither side of A.
    const u32 mineB = 0x4001B111;
    auto strayClose = MakeTradeClose(mineB);
    c->DispatchPacketForTest(strayClose.data(), strayClose.size());
    Check(c->Trade().Active(), "trade A survives a close for another window");
    Check(c->Trade().PartnerSerial() == partner, "still trading with Aelia");
    Check(c->Trade().MyContainer() == mineA, "A's container is untouched");

    // (2) The close that DOES name our window still ends it, exactly as before.
    auto ourClose = MakeTradeClose(mineA);
    c->DispatchPacketForTest(ourClose.data(), ourClose.size());
    Check(!c->Trade().Active(), "the close for our own window closes A");
    Check(c->Trade().CurrentPhase() == trade::Phase::Cancelled,
          "and with nothing accepted it reads as a cancellation");

    // A repeat of the same close -- Sphere sends one per container deletion --
    // no longer re-fires the closed-window handling.
    c->DispatchPacketForTest(ourClose.data(), ourClose.size());
    Check(c->Trade().Reason() == trade::CloseReason::PartnerCancelled,
          "a second close after the window is gone changes nothing");
}

// The partner's side is a legitimate close too: Trade_Delete deletes both
// containers, and which serial reaches us is the server's business.
void TestCloseNamingThePartnerContainerStillCloses() {
    Section("trade: a close naming the partner's container still closes");

    auto c = MakeOfflineClient();
    auto open = MakeTradeOpen(0x0000FD27, 0x4001A496, 0x4001A497, "Aelia");
    c->DispatchPacketForTest(open.data(), open.size());
    auto close = MakeTradeClose(0x4001A497);
    c->DispatchPacketForTest(close.data(), close.size());
    Check(!c->Trade().Active(), "the partner-side close ends the trade");
}

// (3) The decline of a second window is a CLOSE for THAT window only, and the
// live trade must be untouched by both the decline and the close it provokes.
void TestSecondWindowDeclineClosesOnlyTheSecondWindow() {
    Section("trade: declining a second window leaves the first one running");

    auto c = MakeOfflineClient();
    const u32 aelia = 0x0000FD27;
    const u32 mineA = 0x4001A496, theirsA = 0x4001A497;
    auto openA = MakeTradeOpen(aelia, mineA, theirsA, "Aelia");
    c->DispatchPacketForTest(openA.data(), openA.size());
    Check(c->Trade().Active(), "trade A opened");
    c->ClearSentForTest();

    // Wren's competing window, the wave30 shape.
    const u32 wren = 0x0000E96C;
    const u32 mineB = 0x4001B222, theirsB = 0x4001B223;
    auto openB = MakeTradeOpen(wren, mineB, theirsB, "Wren");
    c->DispatchPacketForTest(openB.data(), openB.size());

    Check(c->Trade().Active(), "A is still the active trade");
    Check(c->Trade().PartnerSerial() == aelia, "A's partner is unchanged");
    Check(c->Trade().MyContainer() == mineA, "A's container is unchanged");

    // Exactly one 0x6F went out, and it closed B.
    int closes = 0;
    u32 closedContainer = 0;
    for (const auto& sent : c->SentForTest()) {
        if (sent.opcode != 0x6F || sent.size < 12) continue;
        if (sent.bytes[3] != 0x01) continue;
        ++closes;
        closedContainer = LoadBE32(&sent.bytes[4]);
    }
    Check(closes == 1, "one close was sent for the second window");
    Check(closedContainer == mineB,
          "the decline closes B's container, never A's");

    // And the server's answer to that decline -- a close for B -- is ignored.
    auto closeB = MakeTradeClose(mineB);
    c->DispatchPacketForTest(closeB.data(), closeB.size());
    Check(c->Trade().Active(),
          "the close provoked by our own decline does not kill trade A");

    u32 declinedSerial = 0;
    std::string declinedName;
    Check(c->TakeDeclinedTrade(&declinedSerial, &declinedName) &&
              declinedSerial == wren && declinedName == "Wren",
          "the declined partner is still reported to the life layer");
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
// the fix must not fabricate an attribution it cannot support -- but the
// SERIAL is on the journal entry either way, because that is the identity the
// market handshake joins on.
// ---------------------------------------------------------------------------
void TestUnicodeSpeechUnknownSerialStaysRaw() {
    Section("unicode speech: empty name with an unknown serial stays raw");

    auto c = MakeConnectedClient();
    const u32 speaker = 0x00099999;

    auto uni = MakeUnicodeMessage(speaker, "", "hello?");
    c->DispatchPacketForTest(uni.data(), uni.size());
    Check(c->LastJournalSpeakerForTest().empty(),
          "no fabricated name for a serial this session has never seen");

    std::vector<Client::Heard> heard;
    c->JournalHeardSince(0, heard);
    bool carriedSerial = false;
    for (const auto& h : heard)
        if (h.text == "hello?" && h.speaker == speaker) carriedSerial = true;
    Check(carriedSerial,
          "the nameless line still carries the speaker serial");
}

// ---------------------------------------------------------------------------
// 2c. THE NAME IS AT OFFSET 18. Regression for the off-by-four that made every
// unicode speaker anonymous: offset 14 is the four-byte language code, which
// this shard NUL-fills, so a reader at 14 stopped at the first NUL and
// returned "" for everyone (artifacts/wave30_bandage20_20260907/
// Kharos.console.txt -- 252 `trade: heard '...' from ` lines with a blank
// from). Both language forms are exercised: NUL-filled (what Sphere sends
// here, and what hid the bug) and "ENU" (what a reader at 14 would have
// mistaken for the name).
// ---------------------------------------------------------------------------
void TestUnicodeSpeechNameFromPacket() {
    Section("unicode speech: the packet's own name field is read at offset 18");

    auto c = MakeConnectedClient();

    const u32 blankLang = 0x0001AAAA;
    auto uni = MakeUnicodeMessage(blankLang, "Aelia", "WTS 10 i_bandage 3gp", "");
    c->DispatchPacketForTest(uni.data(), uni.size());
    Check(c->LastJournalSpeakerForTest() == "Aelia",
          "a speaker never seen before is named by the packet alone");

    const u32 enuLang = 0x0001BBBB;
    auto uni2 = MakeUnicodeMessage(enuLang, "Kharos", "WTB 20 i_cloth 12gp", "ENU");
    c->DispatchPacketForTest(uni2.data(), uni2.size());
    Check(c->LastJournalSpeakerForTest() == "Kharos",
          "a populated language code is not mistaken for the name");

    // And the name went into the cache, so the NEXT line from that serial is
    // named even if it arrives without one.
    auto uni3 = MakeUnicodeMessage(blankLang, "", "still here");
    c->DispatchPacketForTest(uni3.data(), uni3.size());
    Check(c->LastJournalSpeakerForTest() == "Aelia",
          "the packet name is remembered for the serial");
}

// ---------------------------------------------------------------------------
// 2d. THE HANDSHAKE JOINS ON THE SERIAL, NOT THE NAME.
//
// This is the protocol contract the market's heard path keys on
// (Economy.cpp DriveOpenTrade: `if (h.speaker != tradePartner_) continue;`
// against a tradePartner_ backfilled from TradeState::PartnerSerial()). Prove
// it at the packet level, in the WORST case: a WTS heard from a speaker
// nothing can name, and a 0x6F open from the same serial with a blank name
// field. The two must still be the same person.
//
// smoke_Kharos_Aelia_Wren_Kaelor_Tarath_Faustus_20260907_1902 is the run where
// that failed: Aelia logged "opening a window with (empty name)" and Kharos
// logged "opened a window ... but never said a WTS we heard".
// ---------------------------------------------------------------------------
void TestShoutAndTradeWindowJoinOnSerial() {
    Section("market handshake: a nameless WTS and a nameless window join on "
            "the serial");

    auto c = MakeConnectedClient();
    const u32 seller = 0x0001C0DE;

    auto uni = MakeUnicodeMessage(seller, "", "WTS 10 i_bandage 3gp");
    c->DispatchPacketForTest(uni.data(), uni.size());

    auto open = MakeTradeOpen(seller, 0x40000001, 0x40000002, "");
    c->DispatchPacketForTest(open.data(), open.size());

    Check(c->Trade().Active(), "the window opened");
    Check(c->Trade().PartnerSerial() == seller,
          "0x6F names the partner by serial even with a blank name field");
    Check(c->Trade().PartnerName().empty(),
          "and the name really is unavailable (worst case, as intended)");

    std::vector<Client::Heard> heard;
    c->JournalHeardSince(0, heard);
    bool matched = false;
    for (const auto& h : heard) {
        if (h.speaker != c->Trade().PartnerSerial()) continue;
        market::TradeIntent offer;
        if (!market::ParseSellOffer(h.text, &offer)) continue;
        matched = offer.item == "i_bandage" && offer.qty == 10 &&
                  offer.pricePerUnit == 3;
    }
    Check(matched,
          "the WTS heard from that serial is matched to the window it opened");
}

// ---------------------------------------------------------------------------
// 2e. The same handshake with names present is unchanged: the serial still
// joins the two, and the name is now also there for the log.
// ---------------------------------------------------------------------------
void TestShoutAndTradeWindowJoinWithNames() {
    Section("market handshake: names present changes nothing about the join");

    auto c = MakeConnectedClient();
    const u32 seller = 0x0001BEEF;

    auto uni = MakeUnicodeMessage(seller, "Aelia", "WTS 10 i_bandage 3gp");
    c->DispatchPacketForTest(uni.data(), uni.size());
    Check(c->LastJournalSpeakerForTest() == "Aelia",
          "the WTS is filed under the speaker's name");

    auto open = MakeTradeOpen(seller, 0x40000001, 0x40000002, "Aelia");
    c->DispatchPacketForTest(open.data(), open.size());
    Check(c->Trade().PartnerSerial() == seller, "same serial from 0x6F");
    Check(c->Trade().PartnerName() == "Aelia", "and the same name");

    std::vector<Client::Heard> heard;
    c->JournalHeardSince(0, heard);
    bool matched = false;
    for (const auto& h : heard) {
        if (h.speaker != c->Trade().PartnerSerial()) continue;
        market::TradeIntent offer;
        if (market::ParseSellOffer(h.text, &offer) && offer.item == "i_bandage")
            matched = h.name == "Aelia";
    }
    Check(matched, "the join still holds, and now carries the name too");
}

}  // namespace

// ---------------------------------------------------------------------------
// 3. A SHOPKEEPER BEHIND A COUNTER IS STILL A SHOPKEEPER.
//
// Elara knew "Bret, the alchemist" (0x27D1) from 18:08:57 and spent the rest
// of her buy errand standing three tiles from him. The lookup that finds a
// shop used the sight-line as an identity test, so the two counter tiles
// between them erased Bret entirely: BUY_SUPPLIES logged
// goal_failed "no 'alchemist' found after 4 trips" while travel was steering
// toward that same mobile (run_gates/g_Elara.console.txt:105,547,584,604,639).
//
// This test reproduces exactly that geometry. The harness Client has no world
// data loaded, so MobileInLineOfSight can only answer true for a mobile within
// one tile -- which is precisely the "occluded" case, and makes the old
// behaviour (return 0) and the new one (return the known shopkeeper)
// distinguishable without needing MULs.
// ---------------------------------------------------------------------------

// 0x78 MOBILE_INCOMING (Client.cpp OnMobileIncoming, >=19 bytes):
// cmd(1) len(2) serial(4)@3 body(2)@7 x(2)@9 y(2)@11 z(1)@13 dir(1)@14
// hue(2)@15 flags(1)@17 noto(1)@18, then a zero serial to end the equip list.
std::vector<u8> MakeMobileIncoming(u32 serial, u16 x, u16 y) {
    std::vector<u8> p(23, 0);
    p[0] = 0x78;
    StoreBE16(&p[1], 23);
    StoreBE32(&p[3], serial);
    StoreBE16(&p[7], 0x0190);          // human male -- the body a shop wears
    StoreBE16(&p[9], x);
    StoreBE16(&p[11], y);
    p[13] = 0;                          // z
    p[14] = 0;                          // dir
    StoreBE16(&p[15], 0);               // hue
    p[17] = 0;                          // flags
    p[18] = 1;                          // notoriety
    StoreBE32(&p[19], 0);               // equip list terminator
    return p;
}

// 0x88 OPEN_PAPERDOLL (Client.cpp OnOpenPaperdoll, 66 bytes):
// cmd(1) serial(4)@1 title[60]@5 flags(1)@65.
std::vector<u8> MakePaperdoll(u32 serial, const char* title) {
    std::vector<u8> p(66, 0);
    p[0] = 0x88;
    StoreBE32(&p[1], serial);
    const usize n = std::strlen(title);
    std::memcpy(&p[5], title, n < 60 ? n : 60);
    return p;
}

void TestOccludedShopkeeperIsStillFound() {
    Section("shop lookup: a known shopkeeper behind a counter is not erased");

    auto c = MakeConnectedClient();
    // Elara's own coordinates and Bret's, from the run. Chebyshev 3 apart.
    const u32 kPlayer = 0x00001111;
    const u32 kBret   = 0x000027D1;
    const std::vector<u8> login = MakeLoginConfirm(kPlayer, 605, 2181);
    c->DispatchPacketForTest(login.data(), login.size());
    const std::vector<u8> bret = MakeMobileIncoming(kBret, 606, 2184);
    c->DispatchPacketForTest(bret.data(), bret.size());
    const std::vector<u8> doll = MakePaperdoll(kBret, "Bret, the alchemist");
    c->DispatchPacketForTest(doll.data(), doll.size());

    Check(!c->MobileInLineOfSight(kBret),
          "and with no world data he is treated as out of sight -- the "
          "occluded case this regression is about");

    const u32 found = c->NearestShopkeeperWithTrade("alchemist");
    Check(found == kBret,
          "the errand finds the alchemist it already knows about, instead of "
          "reporting none and burning a trip");

    // The wrong trade must still find nobody. The fallback widens WHERE a
    // shopkeeper may stand, never WHO counts as one.
    Check(c->NearestShopkeeperWithTrade("blacksmith") == 0,
          "an alchemist is not offered up as a blacksmith");

    // With two candidates the nearer one is chosen. NOTE THE LIMIT OF THIS
    // HARNESS: MobileInLineOfSight bails out early when no world data is
    // loaded, so BOTH of these are "occluded" here and this proves the
    // distance ordering within the fallback, not the preference for a
    // visible shopkeeper over an occluded one. That preference is in the
    // code (a visible match always overwrites the blind one) but it needs
    // MULs to exercise, so it is not claimed as proven here.
    const u32 kKelvin = 0x00004944;
    // NOT named `near`: <windows.h> defines that as a macro.
    const std::vector<u8> adjacent = MakeMobileIncoming(kKelvin, 605, 2182);
    c->DispatchPacketForTest(adjacent.data(), adjacent.size());
    const std::vector<u8> doll2 = MakePaperdoll(kKelvin, "Kelvin, the alchemist");
    c->DispatchPacketForTest(doll2.data(), doll2.size());
    Check(c->NearestShopkeeperWithTrade("alchemist") == kKelvin,
          "the nearer of two known alchemists is the one chosen");

    // An exhausted seller is skipped, which is what lets a spellbook errand
    // move on to a different shelf rather than re-reading the same four
    // random scrolls.
    const std::vector<u32> skip{kKelvin};
    Check(c->NearestShopkeeperWithTrade("alchemist", wm::Service::None, &skip)
              == kBret,
          "skipping the tried seller falls through to the next one");
}

// ---------------------------------------------------------------------------
// Spellbook: the 0x24 whose gump id is 0xFFFF is an OPEN, not a close.
//
// Sphere's CClient::addSpellbookOpen answers a spellbook double-click with
// addOpenGump(book, GUMP_OPEN_SPELLBOOK) -- gump id 0xFFFF -- and only then,
// and only when the book holds at least one spell, a 0x3C with the rows. The
// client used to read 0xFFFF as "close this container" and return, so
// open_container never saw an answer and timed out for its full deadline,
// five times, on every caster (run_gates/g_Aurelius.console.txt:58-125).
// ---------------------------------------------------------------------------
std::vector<u8> MakeSpellbookContents(u32 book, const std::vector<u16>& spells) {
    const usize n = spells.size();
    std::vector<u8> p(5 + n * 19, 0);
    p[0] = 0x3C;
    StoreBE16(&p[1], static_cast<u16>(p.size()));
    StoreBE16(&p[3], static_cast<u16>(n));
    usize o = 5;
    for (u16 spell : spells) {
        StoreBE32(&p[o], 0x40000000u + spell);  o += 4;
        StoreBE16(&p[o], 0x1F2E);               o += 2;   // every row's graphic
        p[o] = 0;                               o += 1;
        StoreBE16(&p[o], spell);                o += 2;   // amount IS the spell
        StoreBE16(&p[o], 0);                    o += 2;
        StoreBE16(&p[o], 0);                    o += 2;
        StoreBE32(&p[o], book);                 o += 4;
        StoreBE16(&p[o], 0);                    o += 2;
    }
    return p;
}

void TestSpellbookGumpIsAnOpenNotAClose() {
    Section("spellbook: gump 0xFFFF opens the book and answers the action");

    // 1. An EMPTY book. Sphere sends the 0x24 and nothing else, so this one
    //    packet has to both finish the action and make the book "known" --
    //    otherwise the caller re-opens it forever.
    {
        auto c = MakeConnectedClient();
        const u32 book = 0x40013046;   // Aurelius' pack spellbook, no spells
        c->ActionOpenContainer(book);
        Check(c->ActionBusy(), "open_container started");

        auto open = MakeDrawContainer(book, 0xFFFF);
        c->DispatchPacketForTest(open.data(), open.size());

        Check(c->ActionResult() == act::Result::Success,
              "the spellbook gump finishes open_container");
        Check(c->ContainerKnown(book),
              "an empty book that has been opened counts as read");
        Check(c->ContainerItemCount(book) == 0, "and it reads as empty");
    }

    // 2. A book that HOLDS spells: the 0x3C that follows must still land, and
    //    the pre-seeded empty listing must not swallow it.
    {
        auto c = MakeConnectedClient();
        const u32 book = 0x4000EDD5;   // Aurelius' banked book, 23 spells
        c->ActionOpenContainer(book);
        auto open = MakeDrawContainer(book, 0xFFFF);
        c->DispatchPacketForTest(open.data(), open.size());
        auto rows = MakeSpellbookContents(book, {1, 5, 27});
        c->DispatchPacketForTest(rows.data(), rows.size());

        Check(c->ContainerItemCount(book) == 3,
              "the contents packet still fills the book");
        u32 serial = 0; u16 gfx = 0, amount = 0;
        Check(c->ContainerItemAt(book, 2, &serial, &gfx, &amount) && amount == 27,
              "and a row's amount is still the spell number");
    }

    // 3. A spellbook is not a bank box. ActionOnContainerOpened adopts an
    //    unsolicited container as the bank while open_bank is outstanding;
    //    the spellbook gump must be excluded from that.
    {
        auto c = MakeConnectedClient();
        c->ActionOpenBank(0x00000EE1);
        Check(c->ActionBusy(), "open_bank started");
        auto open = MakeDrawContainer(0x40013046, 0xFFFF);
        c->DispatchPacketForTest(open.data(), open.size());
        Check(c->BankContainer() == 0,
              "a spellbook is not adopted as the bank box");
        Check(c->ActionBusy(), "and open_bank is still waiting for a real one");
    }
}

// ---------------------------------------------------------------------------
// Dismount/mount: layer-25 equip change IS the reply, and use_object must not
// wait out its whole 4s deadline for it.
//
// Kharain, run_gates/g_Kharain.console.txt ~00:46:52 (2026-09-07): "[ACTION]
// dismount" -> 16ms later "[move] dismounted ... event mount_state:
// dismounted" -> 4s later "event action_result: use_object timeout". Six such
// false timeouts in one 10-minute session, three per mining sitting
// (Runner::DismountToWork / RemountAfterWork in src/life/runner/Gather.cpp).
// ---------------------------------------------------------------------------
void TestDismountFinishesOnMountItemRemoval() {
    Section("dismount: the mount item leaving layer 25 finishes use_object");

    auto c = MakeConnectedClient();
    const u32 me    = 0x00012345;
    const u32 horse = 0x40020001;

    auto login = MakeLoginConfirm(me, 1426, 1687);
    c->DispatchPacketForTest(login.data(), login.size());
    auto ride = MakeEquip(horse, 0x3E9F, 25, me);   // layer 25 = kLayerMount
    c->DispatchPacketForTest(ride.data(), ride.size());
    Check(c->PlayerIsMounted(), "riding after the equip");

    c->ActionDismount();
    Check(c->ActionBusy(), "dismount started (double-click self)");
    Check(c->ActionKind() == act::Kind::UseObject, "dismount is a use_object");

    // Source-X reverses a mount by deleting the layer-25 item and putting the
    // animal back in the world -- no gump, no text, no confirmation packet of
    // its own.
    auto gone = MakeDeleteObject(horse);
    c->DispatchPacketForTest(gone.data(), gone.size());

    Check(!c->PlayerIsMounted(), "dismounted");
    Check(!c->ActionBusy(), "the action is not left pending for its timeout");
    Check(c->ActionResult() == act::Result::Success,
          "the layer-25 removal is read as the dismount's own reply");
}

// THE ITEM SERIAL ON LAYER 25 IS NOT THE DOUBLE-CLICKED SERIAL. A first
// version of this fix matched action_.subject == itemSerial, on the
// assumption that Source-X re-serials the double-clicked animal as the
// equipped item. Live evidence contradicted it: Kharain's own remounts
// (run_gates/g_Kharain.console.txt 01:26:18.421-436 and 01:31:00.731-746)
// double-click horse 0x0000B222 and get mount_state:mounted 15ms later, but
// the 0x2E's item serial never equals 0x0000B222 -- the subject check never
// matched and use_object still timed out 4s later both times, twice in the
// same smoke run the dismount half of this fix was proven against. The item
// serial here is deliberately a THIRD serial, matching neither `me` nor the
// double-clicked horse, to keep that mistake from coming back.
void TestMountFinishesOnLayer25EquipRegardlessOfItemSerial() {
    Section("mount: the horse landing on layer 25 finishes use_object even "
            "when the equipped item's serial differs from the click");

    auto c = MakeConnectedClient();
    const u32 me         = 0x00012346;
    const u32 horse      = 0x40020002;   // what RemountAfterWork double-clicks
    const u32 mountItem  = 0x400200FF;   // what the 0x2E actually names

    auto login = MakeLoginConfirm(me, 1426, 1687);
    c->DispatchPacketForTest(login.data(), login.size());

    c->ActionUseObject(horse);   // Runner::RemountAfterWork's own gesture
    Check(c->ActionBusy(), "mount started (double-click the horse)");

    auto mounted = MakeEquip(mountItem, 0x3E9F, 25, me);
    c->DispatchPacketForTest(mounted.data(), mounted.size());

    Check(c->PlayerIsMounted(), "mounted");
    Check(!c->ActionBusy(), "the action is not left pending for its timeout");
    Check(c->ActionResult() == act::Result::Success,
          "the layer-25 equip is read as the mount's own reply");
}

// The single-action-slot model (act::Action -- "a player does one deliberate
// thing at a time") is what makes the loosened match above safe: WHATEVER
// use_object is pending when we land on layer 25 is finished by it, on the
// same precedent the target-cursor completion a few hundred lines up already
// uses (kind == UseObject, no subject check). This is a known, accepted
// trade-off, not an oversight -- it is asserted here so a future tightening
// of one without the other is a deliberate choice, not an accident.
void TestAnyPendingUseObjectFinishesOnOurOwnMount() {
    Section("mount: any pending use_object completes on our own mount "
            "(single-action-slot trade-off, asserted on purpose)");

    auto c = MakeConnectedClient();
    const u32 me        = 0x00012347;
    const u32 mountItem = 0x40030005;
    const u32 otherItem = 0x40030004;

    auto login = MakeLoginConfirm(me, 1426, 1687);
    c->DispatchPacketForTest(login.data(), login.size());

    c->ActionUseObject(otherItem);   // some other, unrelated double-click
    Check(c->ActionBusy(), "the unrelated action started");

    auto mounted = MakeEquip(mountItem, 0x3E9F, 25, me);
    c->DispatchPacketForTest(mounted.data(), mounted.size());

    Check(c->PlayerIsMounted(), "mounted, as the packet says");
    Check(!c->ActionBusy() && c->ActionResult() == act::Result::Success,
          "the otherItem action is finished too -- no other pending "
          "use_object could have put us on layer 25");
}

int main() {
    net::Socket::WSAStart();
    std::printf("trade verification + speech resolution tests\n\n");

    TestTradeWindowAltContainerIsSuccess();
    TestCloseForAnotherWindowLeavesTheLiveTradeOpen();
    TestCloseNamingThePartnerContainerStillCloses();
    TestSecondWindowDeclineClosesOnlyTheSecondWindow();
    TestSplitEchoWaitsForTheDestination();
    TestSplitEchoThenBounceIsAFailure();
    TestFullBounceBackIsStillAFailure();
    TestBankMoveFromAnotherTileIsRefused();
    TestWithdrawalStuckInTheBoxIsAFailure();
    TestUnicodeSpeechResolvesKnownSerial();
    TestUnicodeSpeechUnknownSerialStaysRaw();
    TestUnicodeSpeechNameFromPacket();
    TestShoutAndTradeWindowJoinOnSerial();
    TestShoutAndTradeWindowJoinWithNames();
    TestOccludedShopkeeperIsStillFound();
    TestSpellbookGumpIsAnOpenNotAClose();
    TestDismountFinishesOnMountItemRemoval();
    TestMountFinishesOnLayer25EquipRegardlessOfItemSerial();
    TestAnyPendingUseObjectFinishesOnOurOwnMount();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");

    net::Socket::WSACleanupOnce();
    return g_failures == 0 ? 0 : 1;
}
