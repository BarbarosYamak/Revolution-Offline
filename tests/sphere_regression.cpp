// Deterministic regression tests for the Sphere/Source-X compatibility rules.
//
// Every case here corresponds to a bug that actually bit us during M1 (see
// docs/M1_BOT_ALIVE.md) or to a rule the client must not silently lose. These
// exercise the same headers the client compiles against -- uo/sphere_rules.h,
// uo/packet_lengths_sphere.h and net/PacketStream -- so they cannot drift from
// the shipping behaviour.
//
// They are unit tests: they prove framing and decision logic, NOT that a login
// works. Runtime acceptance still requires a live Sphere shard.

#include "net/PacketStream.h"
#include "uo/builders.h"
#include "uo/packet_lengths_sphere.h"
#include "uo/sphere_rules.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------------------
// 1. 0x8C same-socket relay
// ---------------------------------------------------------------------------
void TestRelayDecision() {
    Section("0x8C same-socket relay");
    const u32 kLocal = 0x7F000001u;   // 127.0.0.1

    // Sphere advertises its own ServIP/ServPort: same endpoint -> stay, and
    // crucially do NOT re-send the seed.
    Check(sphere::StayOnLoginSocket(kLocal, 2593, kLocal, 2593, false),
          "same ip+port stays on the socket");

    // Some emulators advertise no address at all; that also means "stay here".
    Check(sphere::StayOnLoginSocket(0, 2593, kLocal, 2593, false),
          "advertised ip 0 stays on the socket");

    // A genuine relay to another endpoint must reconnect (and seed).
    Check(!sphere::StayOnLoginSocket(0x0A000001u, 2593, kLocal, 2593, false),
          "different ip reconnects");
    Check(!sphere::StayOnLoginSocket(kLocal, 3593, kLocal, 2593, false),
          "different port reconnects");

    // An explicit operator override always means "go where I said".
    Check(!sphere::StayOnLoginSocket(kLocal, 2593, kLocal, 2593, true),
          "endpoint override forces reconnect");

    // The M1 bug: the old rule keyed off "advertised port != login port", so a
    // single-server shard took the reconnect branch by accident. Guard the
    // inverted answer explicitly.
    Check(sphere::StayOnLoginSocket(kLocal, 2593, kLocal, 2593, false) != false,
          "single-server shard is not treated as a relay (M1 regression)");
}

// ---------------------------------------------------------------------------
// 2. inbound 0x73 must not create a ping storm
// ---------------------------------------------------------------------------
void TestPingPolicy() {
    Section("0x73 ping policy");
    const i64 kGap = 1000;

    // The reply to our own keepalive is consumed, never echoed. Echoing it is
    // what produced ~24,000 exchanges in 100s and got us quota-kicked.
    Check(sphere::DecidePing(1, 10000, 0, kGap) ==
              sphere::PingAction::ConsumeAsReply,
          "reply to our keepalive is consumed, not echoed");
    Check(sphere::DecidePing(4, 10000, 9999, kGap) ==
              sphere::PingAction::ConsumeAsReply,
          "outstanding keepalives always consume first");

    // A shard that pings first (UO Demo behaviour) still gets an echo...
    Check(sphere::DecidePing(0, 10000, 0, kGap) == sphere::PingAction::Echo,
          "unsolicited ping is echoed");
    Check(sphere::DecidePing(0, 10000, 8000, kGap) == sphere::PingAction::Echo,
          "unsolicited ping echoed again after the gap");

    // ...but never faster than the rate limit, so no peer can drive a storm.
    Check(sphere::DecidePing(0, 10000, 9500, kGap) ==
              sphere::PingAction::Ignore,
          "unsolicited ping inside the rate limit is ignored");

    // Simulate the exact M1 loop: server answers every ping. With one
    // keepalive outstanding per send, we must never emit a reply.
    int outstanding = 0;
    int echoes = 0;
    for (int i = 0; i < 50; ++i) {
        ++outstanding;                       // we send a keepalive
        const auto act = sphere::DecidePing(outstanding, i * 20000, 0, kGap);
        if (act == sphere::PingAction::ConsumeAsReply) --outstanding;
        else if (act == sphere::PingAction::Echo) ++echoes;
    }
    Check(echoes == 0, "50 keepalive round-trips produce zero echoes");
    Check(outstanding == 0, "every keepalive is accounted for");
}

// ---------------------------------------------------------------------------
// 3. unknown opcode behaviour + 4. 0xD1 logout framing
// ---------------------------------------------------------------------------
void TestFraming() {
    Section("packet framing");

    // 0xD1 PacketLogoutAck is not in the 2.0.7 client table; the Sphere
    // overlay supplies it. Without this the logout acknowledgement itself
    // would be an unframeable opcode and would end the session.
    Check(kPacketLength[0xD1] == 0, "0xD1 absent from the 2.0.7 table");
    Check(PacketLengthFor(0xD1) == 2, "0xD1 framed as 2 bytes by the overlay");

    // The overlay must not shadow anything the 2.0.7 table already knows.
    for (int i = 0; i < 256; ++i) {
        if (kPacketLength[i] != 0 && kSpherePacketLength[i] != 0) {
            Check(false, "overlay shadows a known 2.0.7 opcode");
            break;
        }
    }

    // A real logout exchange frames cleanly.
    net::PacketStream ps;
    u8 ack[2] = {0xD1, 0x01};
    Check(ps.FeedBytes(ack, sizeof(ack)), "feed 0xD1 ack");
    const u8* pkt = nullptr;
    usize sz = 0;
    const char* err = nullptr;
    Check(ps.TryNext(&pkt, &sz, &err) && sz == 2 && pkt[0] == 0xD1 && !err,
          "0xD1 ack extracted whole");

    // An opcode with no length anywhere is reported, not silently skipped:
    // framing is unrecoverable and the caller must end the session.
    net::PacketStream bad;
    u8 junk[4] = {0xCD, 0x01, 0x02, 0x03};   // 0xCD: unknown in both tables
    Check(PacketLengthFor(0xCD) == 0, "0xCD has no length in either table");
    bad.FeedBytes(junk, sizeof(junk));
    err = nullptr;
    const bool got = bad.TryNext(&pkt, &sz, &err);
    Check(!got && err != nullptr, "unknown opcode reports an error");
    Check(bad.Pending() == 4 && bad.PendingData()[0] == 0xCD,
          "unknown opcode leaves the buffer intact for diagnostics");

    // Fixed-length and variable-length packets both frame, including a split
    // arrival (the server does not respect our packet boundaries).
    net::PacketStream mixed;
    u8 ackMove[3] = {0x22, 0x07, 0x01};                    // fixed, 3 bytes
    u8 speech[]   = {0x1C, 0x00, 0x08, 1, 2, 3, 4, 5};     // variable, len 8
    mixed.FeedBytes(ackMove, 1);                           // deliberate split
    err = nullptr;
    Check(!mixed.TryNext(&pkt, &sz, &err) && !err, "partial packet waits");
    mixed.FeedBytes(ackMove + 1, 2);
    Check(mixed.TryNext(&pkt, &sz, &err) && sz == 3 && pkt[0] == 0x22,
          "0x22 framed after the rest arrives");
    mixed.FeedBytes(speech, sizeof(speech));
    Check(mixed.TryNext(&pkt, &sz, &err) && sz == 8 && pkt[0] == 0x1C,
          "variable-length 0x1C framed by its own length field");
    Check(!mixed.TryNext(&pkt, &sz, &err) && !err, "stream drained cleanly");
}

// ---------------------------------------------------------------------------
// 5. movement sequence wrap
// ---------------------------------------------------------------------------
void TestSequence() {
    Section("movement sequence");

    Check(sphere::NextMoveSequence(0) == 1, "0 -> 1");
    Check(sphere::NextMoveSequence(200) == 201, "200 -> 201");

    // Sphere itself does `if (seq == 255) seq = 0; m_sequence = ++seq;`
    // (src/network/receive.cpp:276-279), so its next expected value after 255
    // is 1. The client must wrap the same way -- NOT to 0, which is reserved
    // for a post-reject resync.
    Check(sphere::NextMoveSequence(255) == 1, "255 wraps to 1, matching Sphere");
    Check(sphere::NextMoveSequence(255) != 0, "255 does not wrap to 0");

    // A full lap never emits 0 after the start: 0 only comes from a reset.
    u8 seq = 0;
    int zeros = 0;
    for (int i = 0; i < 600; ++i) {
        seq = sphere::NextMoveSequence(seq);
        if (seq == 0) ++zeros;
    }
    Check(zeros == 0, "0 is never produced by wrapping");
}

// ---------------------------------------------------------------------------
// 5b. movement gait -- running is the default, and the run bit is bit 7
// ---------------------------------------------------------------------------
void TestGait() {
    Section("movement gait");

    // Source-X splits the 0x02 direction byte into `rawdir & 0xF` (facing) and
    // `rawdir & DIR_MASK_RUNNING` (gait), DIR_MASK_RUNNING = 0x80
    // (src/game/uo_files/uofiles_enums.h:435; CClient::Event_Walk,
    // src/game/clients/CClientEvent.cpp:862,904). Setting the bit must never
    // disturb the low nibble -- a leaked bit 3 would read as DIR_QTY and be
    // rejected outright (CClientEvent.cpp:863-867).
    for (u8 d = 0; d < 8; ++d) {
        const u8 walk = sphere::MoveDirectionByte(d, false);
        const u8 run  = sphere::MoveDirectionByte(d, true);
        Check(walk == d, "walk byte is the bare direction");
        Check((run & 0x0F) == d, "run byte keeps the direction in the low nibble");
        Check(run == (d | 0x80u), "run byte sets exactly bit 7");
    }

    // M3.7.1 -- a mount halves the server's minimum time per step.
    // Event_CheckWalkBuffer, src/game/clients/CClientEvent.cpp:759-762:
    //   Mount 100 / 200, foot 200 / 400.
    Check(sphere::MountedStepMs(200, true) == 100,
          "mounted running is 100ms, half the on-foot floor");
    Check(sphere::MountedStepMs(400, true) == 200,
          "mounted walking is 200ms");
    Check(sphere::MountedStepMs(200, false) == 200,
          "on foot the cadence is untouched");
    Check(sphere::MountedStepMs(400, false) == 400,
          "on foot the walk cadence is untouched");

    // The mount lives on equipment layer 25 -- the same layer a chair uses to
    // seat a player, which is why the client reads the LAYER and not a body id.
    Check(sphere::kLayerMount == 25, "mount layer is 25");

    // The divisor is exactly two. If Source-X's table ever stops being a clean
    // halving this check is what should fail first.
    Check(sphere::kMountedStepDivisor == 2, "mounted cadence is exactly 2x");

    // The whole point of the model: Auto means run.
    Check(sphere::GaitWantsRun(sphere::Gait::Auto, -1, -1, -1, -1),
          "Auto runs before the server has sent any stats");
    Check(sphere::GaitWantsRun(sphere::Gait::Auto, 50, 50, 0, 400),
          "Auto runs when rested and unencumbered");
    Check(sphere::GaitWantsRun(sphere::Gait::Run, 0, 100, 500, 400),
          "explicit Run always runs");
    Check(!sphere::GaitWantsRun(sphere::Gait::Walk, 100, 100, 0, 400),
          "explicit Walk never runs");

    // Fatigue reserve: CChar::CanMove refuses EVERY step at STAT_DEX <= 0
    // (src/game/chars/CCharAct.cpp:4611-4617), so Auto stops spending before
    // it gets there. The threshold itself is ours, not the server's.
    Check(!sphere::GaitWantsRun(sphere::Gait::Auto, 5, 100, 0, 400),
          "Auto walks below the stamina reserve");
    Check(sphere::GaitWantsRun(sphere::Gait::Auto, 20, 100, 0, 400),
          "Auto runs above the stamina reserve");

    // Encumbrance: the run flag adds RunningPenalty (50) to the weight-load
    // percent fed into the per-step stamina-loss roll (CanMoveWalkTo,
    // src/game/chars/CCharAct.cpp:4818-4838; runtime/sphere.ini:316,319), so
    // running is what pushes a loaded character into the loss band.
    Check(sphere::GaitWantsRun(sphere::Gait::Auto, 100, 100, 200, 400),
          "Auto runs at half load -- running is free while light");
    Check(!sphere::GaitWantsRun(sphere::Gait::Auto, 100, 100, 400, 400),
          "Auto walks at full load");
    Check(sphere::GaitWantsRun(sphere::Gait::Auto, 100, 100, 400, -1),
          "unknown max weight cannot veto running");
}

// ---------------------------------------------------------------------------
// 6. credential logging never exposes a password
// ---------------------------------------------------------------------------
void TestRedaction() {
    Section("credential redaction");

    const char* user = "revolutionbot01";
    const char* pass = "hunter2hunter2XY";

    u8 login[256] = {0};
    const usize ln = build::LoginRequest(login, user, pass);
    Check(ln == 62, "0x80 is 62 bytes");
    const usize off80 = sphere::CredentialPasswordOffset(login, ln);
    Check(off80 == 31, "0x80 password field at offset 31");
    Check(std::memcmp(login + off80, pass, std::strlen(pass)) == 0,
          "password really is at that offset before redaction");

    u8 game[256] = {0};
    const usize gn = build::GameLogin(game, 0x12345678u, user, pass);
    Check(gn == 65, "0x91 is 65 bytes");
    const usize off91 = sphere::CredentialPasswordOffset(game, gn);
    Check(off91 == 35, "0x91 password field at offset 35");
    Check(std::memcmp(game + off91, pass, std::strlen(pass)) == 0,
          "password really is at that offset before redaction");

    // Packets that carry no password are left alone.
    u8 move[16] = {0};
    const usize mn = build::MoveRequest(move, 2, 0, 0, false);
    Check(sphere::CredentialPasswordOffset(move, mn) == 0,
          "0x02 has no password field");

    // Redaction as the logger performs it: the password must be gone while
    // the length and every other field survive.
    auto redact = [](u8* p, usize n) {
        const usize off = sphere::CredentialPasswordOffset(p, n);
        if (!off) return;
        for (usize i = off; i < off + sphere::kCredentialFieldLen && i < n; ++i)
            p[i] = sphere::kRedactionFill;
    };

    for (u8* pkt : {login, game}) {
        const usize n = (pkt == login) ? ln : gn;
        redact(pkt, n);
        bool leaked = false;
        for (usize i = 0; i + std::strlen(pass) <= n; ++i)
            if (std::memcmp(pkt + i, pass, std::strlen(pass)) == 0) leaked = true;
        Check(!leaked, "password absent from the redacted packet");
        // The account name is not a secret and stays readable for diagnosis.
        bool userPresent = false;
        for (usize i = 0; i + std::strlen(user) <= n; ++i)
            if (std::memcmp(pkt + i, user, std::strlen(user)) == 0) userPresent = true;
        Check(userPresent, "account name still present after redaction");
    }
    Check(ln == 62 && gn == 65, "redaction did not change packet length");
}

// ---------------------------------------------------------------------------
// 7. the selected character slot is respected
// ---------------------------------------------------------------------------
void TestCharacterSelection() {
    Section("character selection");

    const char* slots[5] = {"RevolutionBot01", "", "Alice", "", ""};

    // By slot index.
    Check(sphere::SelectCharacterSlot(slots, 5, nullptr, 0) == 0, "slot 0");
    Check(sphere::SelectCharacterSlot(slots, 5, nullptr, 2) == 2, "slot 2");
    Check(sphere::SelectCharacterSlot(slots, 5, nullptr, 1) == -1,
          "empty slot is refused");
    Check(sphere::SelectCharacterSlot(slots, 5, nullptr, 9) == -1,
          "out-of-range slot is refused");

    // By name -- and the name must win over a mismatched slot, which is the
    // property that keeps two bot sessions on their own characters.
    Check(sphere::SelectCharacterSlot(slots, 5, "Alice", 0) == 2,
          "name overrides the configured slot");
    Check(sphere::SelectCharacterSlot(slots, 5, "alice", 0) == 2,
          "name match is case-insensitive");
    Check(sphere::SelectCharacterSlot(slots, 5, "Nobody", 0) == -1,
          "unknown name is refused rather than falling back to a slot");

    // Two sessions, two names, one shared slot list -> different characters.
    const char* pair[5] = {"RevolutionBot01", "RevolutionBot02", "", "", ""};
    const int a = sphere::SelectCharacterSlot(pair, 5, "RevolutionBot01", 0);
    const int b = sphere::SelectCharacterSlot(pair, 5, "RevolutionBot02", 0);
    Check(a == 0 && b == 1 && a != b, "two sessions resolve to distinct slots");

    Check(sphere::SelectCharacterSlot(nullptr, 0, "x", 0) == -1, "empty list");
}

// CChar::CanTouch, mirrored. The server refuses on `iDist > 2`
// (Source-X src/game/chars/CCharStatus.cpp:1423) and the vendor buy packet
// checks it first of all (src/network/receive.cpp:752-756, "You can't reach
// the Vendor"). The client used to allow itself 3, which is precisely the
// distance at which the answer is always no: Aurelius opened a shop from
// (1591,1657) with the shopkeeper at (1588,1655) -- Chebyshev 3 -- and both
// his purchases were refused in 1ms (run_gates/g_Aurelius.console.txt:411,
// 468-470,482).
void TestTouchReach() {
    std::printf("[reach: CanTouch is two tiles, Chebyshev]\n");
    Check(sphere::kTouchDist == 2, "arm's length is two tiles, not three");
    Check(sphere::CanTouchAtDist(0), "same tile is reachable");
    Check(sphere::CanTouchAtDist(2), "two tiles is reachable");
    Check(!sphere::CanTouchAtDist(3),
          "THREE IS NOT -- this is the Aurelius case");
    Check(!sphere::CanTouchAtDist(4), "and nor is anything beyond it");

    // The metric matters as much as the number. Aurelius was dx=3, dy=2:
    // Chebyshev 3 (refused), Manhattan 5, Euclidean ~3.6. A client measuring
    // in anything but the server's metric would disagree about the boundary.
    auto chebyshev = [](i32 ax, i32 ay, i32 bx, i32 by) {
        const i32 dx = ax > bx ? ax - bx : bx - ax;
        const i32 dy = ay > by ? ay - by : by - ay;
        return dx > dy ? dx : dy;
    };
    Check(chebyshev(1591, 1657, 1588, 1655) == 3,
          "Aurelius stood three tiles from Kenton by the server's metric");
    Check(!sphere::CanTouchAtDist(chebyshev(1591, 1657, 1588, 1655)),
          "so the client must predict the refusal it actually got");
    // A pure diagonal at two tiles is REACHABLE under Chebyshev even though
    // its Manhattan sum is four -- the case a Manhattan client gets wrong in
    // the other direction, refusing to buy from a vendor it could reach.
    Check(sphere::CanTouchAtDist(chebyshev(100, 100, 102, 102)),
          "a diagonal neighbour two tiles out is reachable");
}

}  // namespace

int main() {
    std::printf("sphere regression tests\n\n");
    TestTouchReach();
    TestRelayDecision();
    TestPingPolicy();
    TestFraming();
    TestSequence();
    TestGait();
    TestRedaction();
    TestCharacterSelection();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
