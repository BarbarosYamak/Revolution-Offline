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
//        b. A partial-stack move, where Sphere keeps the ORIGINAL serial on
//           the remainder left behind in the SOURCE container and reports
//           that with its own 0x25 (pair_Durnholde 20:34:17.897: 40-of-50
//           i_ingot_iron banked; pair_Tarath 20:53:35.052: 47-of-133 i_log
//           traded).
//      A REAL refusal -- the whole stack bouncing back to the source
//      container unchanged -- must still fail; that path is asserted too.
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
// 1b. Partial stack: the remainder left behind in the source container, at
// the arithmetically-correct reduced amount, verifies as success.
// ---------------------------------------------------------------------------
void TestPartialStackRemainderIsSuccess() {
    Section("partial stack: remainder in the source container verifies");

    auto c = MakeConnectedClient();
    const u32 pack = 0x40001000;
    const u32 bank = 0x40014400;

    // Seed the local container cache: 50 ingots already sitting in the pack.
    auto seed = MakeAddItem(0x400143D7, 0x1BF2, 50, pack);
    c->DispatchPacketForTest(seed.data(), seed.size());

    c->ActionMoveItem(0x400143D7, 40, bank);
    Check(c->ActionBusy(), "move_item action started");

    // The leftover-in-source echo: same serial, back in the SAME container,
    // amount reduced by exactly what moved (50 - 40 = 10).
    auto leftover = MakeAddItem(0x400143D7, 0x1BF2, 10, pack);
    c->DispatchPacketForTest(leftover.data(), leftover.size());
    Check(c->ActionResult() == act::Result::Success,
          "40-of-50 partial move verifies from the source-container echo");
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
    TestPartialStackRemainderIsSuccess();
    TestFullBounceBackIsStillAFailure();
    TestUnicodeSpeechResolvesKnownSerial();
    TestUnicodeSpeechUnknownSerialStaysRaw();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");

    net::Socket::WSACleanupOnce();
    return g_failures == 0 ? 0 : 1;
}
