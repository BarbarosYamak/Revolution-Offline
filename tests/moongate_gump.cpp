// A held moongate dialog must survive the packets that arrive behind it.
//
// Live defect (wave 2026-09-02, run_gates/g_Xerxes.console.txt:119-171 and
// g_Vorar.console.txt): the Magincia gate 0x4000289E opened its destination
// dialog at 09:10:47.002 while the bot was still two legs away from the gate
// tile. 2.4s later the bot stood on the gate and logged
//   [travel] using moongate 0x4000289E for 'Ocllo (Newplayers)' (gump active=0)
// -- the dialog had been thrown away, and Sphere will not open a second one
// while the first is unanswered, so all 158 subsequent double-clicks were met
// with silence.
//
// Cause, in Client.cpp's packet dispatch: 0x23/0x53/0x54/0x5B/0x65/0x6D/0x70/
// 0x8B/0x97 were listed as "log + ignore" cases but had no `break;`, so they
// FELL THROUGH into `case 0xB0: OnGenericGump(...)`. OnGenericGump's first
// statement cleared gump_ and it then returned on its size check. The gate's
// own sound (0x54) and effect (0x70) are exactly the packets that follow a
// gate dialog, so the dialog was wiped microseconds after it arrived.
//
// This suite feeds the real dispatcher (Client::DispatchPacketForTest) a real
// moongate gump followed by those packets and asserts the dialog is still
// held. Packet layouts are the ones documented at Client::OnGenericGump in
// src/travel/ClientTravel.cpp. No server, no MULs; the loopback listener only
// exists so Client::Send() has a live socket, exactly as in trade_verify.cpp.

#include "Client.h"
#include "net/Socket.h"
#include "uo/endian.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
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

// 0xB0 GENERIC_GUMP: cmd(1) len(2) serial(4) context(4) x(4) y(4)
// ctrlLen(2) controls[ctrlLen] textCount(2) { textLen(2) utf16be[textLen] }...
std::vector<u8> MakeGump(u32 serial, u32 context, const std::string& controls,
                         const std::vector<std::string>& texts) {
    std::vector<u8> p;
    p.resize(21, 0);
    p[0] = 0xB0;
    StoreBE32(&p[3], serial);
    StoreBE32(&p[7], context);
    StoreBE16(&p[19], static_cast<u16>(controls.size()));
    p.insert(p.end(), controls.begin(), controls.end());

    const usize tc = p.size();
    p.resize(tc + 2, 0);
    StoreBE16(&p[tc], static_cast<u16>(texts.size()));
    for (const std::string& t : texts) {
        const usize at = p.size();
        p.resize(at + 2, 0);
        StoreBE16(&p[at], static_cast<u16>(t.size()));
        for (char ch : t) {
            p.push_back(0);
            p.push_back(static_cast<u8>(ch));
        }
    }
    StoreBE16(&p[1], static_cast<u16>(p.size()));
    return p;
}

// The Magincia gate's dialog, cut down to one destination. Control grammar is
// what OnGenericGump tokenises: a radio/checkbox takes tok[6] as its id, a
// button tok[7], and each takes the label of the text control that follows.
std::vector<u8> MakeMoongateGump(u32 serial, u32 context) {
    return MakeGump(serial, context,
                    "{button 0 0 0 0 1 0 1000}{text 0 0 0 1}"
                    "{radio 0 0 0 0 0 10}{text 0 0 0 2}",
                    {"Pick your destination:", "OKAY", "Ocllo (Newplayers)"});
}

// 0x54 PLAY_SOUND, 12 bytes -- the gate's own hum, and the packet that used to
// destroy the dialog it arrives behind.
std::vector<u8> MakeSound(u16 soundId) {
    std::vector<u8> p(12, 0);
    p[0] = 0x54;
    p[1] = 0x01;
    StoreBE16(&p[2], soundId);
    return p;
}

// 0x70 GRAPHICAL_EFFECT, 28 bytes -- the gate's sparkle.
std::vector<u8> MakeEffect(u32 srcSerial) {
    std::vector<u8> p(28, 0);
    p[0] = 0x70;
    p[1] = 0x00;
    StoreBE32(&p[2], srcSerial);
    return p;
}

Client::Config MakeConfig() {
    Client::Config cfg{};
    cfg.loginHost = "127.0.0.1";
    cfg.username = "moongate_gump";
    cfg.password = "moongate_gump";
    cfg.version = "2.0.7";
    cfg.sendSeed = false;
    cfg.sessionTag = "moongate_gump";
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
    sockaddr_in peer{};
    int plen = sizeof(peer);
    accept(listener, reinterpret_cast<sockaddr*>(&peer), &plen);
    closesocket(listener);
    return client;
}

// ---------------------------------------------------------------------------
// The dialog parses, and the destination is readable by label.
// ---------------------------------------------------------------------------
void TestGumpParses() {
    Section("a moongate dialog parses into an OKAY button and a destination");

    auto c = MakeConnectedClient();
    auto g = MakeMoongateGump(0x4000289E, 0x80A0041C);
    c->DispatchPacketForTest(g.data(), g.size());

    Check(c->GumpActive(), "the gump is held after it arrives");
    Check(c->GumpContext() == 0x80A0041C, "the context is the server's");

    bool okay = false, ocllo = false;
    for (const Client::GumpOption& o : c->GumpOptions()) {
        if (o.button && o.id == 1000 && o.label == "OKAY") okay = true;
        if (!o.button && o.id == 10 && o.label == "Ocllo (Newplayers)")
            ocllo = true;
    }
    Check(okay, "the OKAY button is option 1000");
    Check(ocllo, "the destination radio is option 10, labelled");
}

// ---------------------------------------------------------------------------
// The regression itself: the packets that follow a gate dialog must not eat it.
// ---------------------------------------------------------------------------
void TestFollowingPacketsDoNotClearIt() {
    Section("sound/effect/other in-world packets do not clear a held gump");

    auto c = MakeConnectedClient();
    auto g = MakeMoongateGump(0x4000289E, 0x80A0041C);
    c->DispatchPacketForTest(g.data(), g.size());
    Check(c->GumpActive(), "held before the noise");

    const u8 ignored[] = { 0x23, 0x53, 0x54, 0x5B, 0x65, 0x6D, 0x70, 0x8B,
                           0x97 };
    for (u8 cmd : ignored) {
        std::vector<u8> p(32, 0);
        p[0] = cmd;
        StoreBE16(&p[1], 32);
        c->DispatchPacketForTest(p.data(), p.size());
    }
    Check(c->GumpActive(), "still held after every log-and-ignore packet");

    auto snd = MakeSound(0x000F);
    c->DispatchPacketForTest(snd.data(), snd.size());
    Check(c->GumpActive(), "still held after the gate's 0x54 sound");

    auto fx = MakeEffect(0x4000289E);
    c->DispatchPacketForTest(fx.data(), fx.size());
    Check(c->GumpActive(), "still held after the gate's 0x70 effect");

    Check(c->GumpContext() == 0x80A0041C,
          "the context survives intact, so the dialog can still be answered");
}

// ---------------------------------------------------------------------------
// A truncated 0xB0 is not a reason to throw away a dialog the server still
// holds open. This is the defence-in-depth half of the fix.
// ---------------------------------------------------------------------------
void TestTruncatedGumpDoesNotClearIt() {
    Section("a truncated 0xB0 leaves the open dialog alone");

    auto c = MakeConnectedClient();
    auto g = MakeMoongateGump(0x4000289E, 0x80A0041C);
    c->DispatchPacketForTest(g.data(), g.size());
    Check(c->GumpActive(), "held before the truncated packet");

    std::vector<u8> shortPkt(12, 0);
    shortPkt[0] = 0xB0;
    StoreBE16(&shortPkt[1], 12);
    c->DispatchPacketForTest(shortPkt.data(), shortPkt.size());
    Check(c->GumpActive(), "a too-short 0xB0 did not clear it");

    // Long enough to pass the 23-byte check but with a control length that
    // runs off the end: the second early return.
    std::vector<u8> bad(30, 0);
    bad[0] = 0xB0;
    StoreBE16(&bad[1], 30);
    StoreBE32(&bad[3], 0x11111111);
    StoreBE16(&bad[19], 4000);
    c->DispatchPacketForTest(bad.data(), bad.size());
    Check(c->GumpActive(), "an over-long ctrlLen did not clear it");
    Check(c->GumpContext() == 0x80A0041C, "and it is still the gate's dialog");
}

} // namespace

int main() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    TestGumpParses();
    TestFollowingPacketsDoNotClearIt();
    TestTruncatedGumpDoesNotClearIt();

    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
