// tests/cliloc_decode.cpp -- S6: packet 0xC1 (cliloc message) used to be
// discarded as a no-op at src/Client.cpp:569 (docs/S3_CHARACTERIZATION.md's
// "shard messages" section), so the bot never heard any of the crafting
// system's cliloc-numbered messages (crafting_messages.scp: craft_msg_fail
// 1044043, craft_msg_noresources 1044253, craft_msg_noskill 1044153).
//
// No Cliloc.enu / Cliloc.tur ships with this shard's client data -- checked
// across runtime/, local/revolution-client/, and bot/uo-client/, nothing
// named *cliloc* anywhere but protocol documentation -- so this is the
// ID-ONLY FALLBACK path (src/net/Cliloc.h FormatClilocJournalText), not a
// real cliloc-table lookup. Every case below hand-builds the wire bytes
// exactly as pol_packets.md documents them and checks the text that would
// land in the journal via Client::OnClilocMessage /
// Client::OnClilocMessageAffix.
//
// No server, no MULs, no world data, no Client -- this links src/net/Cliloc.cpp
// alone, the same client-free seam interaction_progress.cpp exercises for
// CraftConfirm.cpp.

#include "net/Cliloc.h"
#include "uo/activities/craft_confirm.h"
#include "uo/endian.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace uo;

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void ExpectEq(const std::string& got, const std::string& want,
              const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("  FAIL: %s -- wanted \"%s\", got \"%s\"\n", what,
                    want.c_str(), got.c_str());
        ++g_failures;
    }
}

void PutBE32(std::vector<u8>& b, u32 v) {
    u8 t[4];
    StoreBE32(t, v);
    b.insert(b.end(), t, t + 4);
}

void PutBE16(std::vector<u8>& b, u16 v) {
    u8 t[2];
    StoreBE16(t, v);
    b.insert(b.end(), t, t + 2);
}

void PutFixedAscii(std::vector<u8>& b, const char* s, usize width) {
    usize n = 0;
    while (s && s[n] && n < width) { b.push_back(static_cast<u8>(s[n])); ++n; }
    while (n < width) { b.push_back(0); ++n; }
}

// Wide (u16) tab-separated, NUL-terminated argument string, ASCII only,
// in the requested byte order -- little-endian for 0xC1, big-endian for
// 0xCC (see src/net/Cliloc.h).
void PutWideArgs(std::vector<u8>& b, const std::vector<std::string>& args,
                  bool bigEndian) {
    bool first = true;
    for (const std::string& a : args) {
        if (!first) {
            if (bigEndian) PutBE16(b, 0x0009); else { b.push_back(0x09); b.push_back(0x00); }
        }
        first = false;
        for (char c : a) {
            const u16 ch = static_cast<u16>(static_cast<unsigned char>(c));
            if (bigEndian) PutBE16(b, ch); else { b.push_back(static_cast<u8>(ch)); b.push_back(0x00); }
        }
    }
    if (bigEndian) PutBE16(b, 0x0000); else { b.push_back(0x00); b.push_back(0x00); }
}

// Builds a complete 0xC1 packet: serial/body/type/hue/font/id/speaker,
// then the wide args (little-endian, per doc).
std::vector<u8> BuildCliloc(u32 serial, u16 body, u8 type, u16 hue, u16 font,
                            u32 clilocId, const char* speaker,
                            const std::vector<std::string>& args) {
    std::vector<u8> b;
    b.push_back(0xC1);
    PutBE16(b, 0);  // length patched below
    PutBE32(b, serial);
    PutBE16(b, body);
    b.push_back(type);
    PutBE16(b, hue);
    PutBE16(b, font);
    PutBE32(b, clilocId);
    PutFixedAscii(b, speaker, 30);
    PutWideArgs(b, args, /*bigEndian=*/false);
    StoreBE16(&b[1], static_cast<u16>(b.size()));
    return b;
}

// Builds a complete 0xCC packet: same header through the id, then flags,
// speaker, a NUL-terminated ASCII affix, then wide args (big-endian).
std::vector<u8> BuildClilocAffix(u32 serial, u16 body, u8 type, u16 hue,
                                 u16 font, u32 clilocId, u8 flags,
                                 const char* speaker, const char* affix,
                                 const std::vector<std::string>& args) {
    std::vector<u8> b;
    b.push_back(0xCC);
    PutBE16(b, 0);  // length patched below
    PutBE32(b, serial);
    PutBE16(b, body);
    b.push_back(type);
    PutBE16(b, hue);
    PutBE16(b, font);
    PutBE32(b, clilocId);
    b.push_back(flags);
    PutFixedAscii(b, speaker, 30);
    for (const char* p = affix; *p; ++p) b.push_back(static_cast<u8>(*p));
    b.push_back(0x00);
    PutWideArgs(b, args, /*bigEndian=*/true);
    StoreBE16(&b[1], static_cast<u16>(b.size()));
    return b;
}

// ---------------------------------------------------------------------------
// The exact case the task spec asks for: craft_msg_fail, 1044043, no args,
// system message (serial 0xFFFFFFFF / body 0xFF, as pol_packets.md documents
// "0xffff for system message" -- the client-observed convention is the
// serial's low 16 bits read 0xFFFF and the whole field reads 0xFFFFFFFF).
// The id-only fallback is the only possible answer: there is no cliloc file
// in this repo to resolve 1044043 to its English text.
// ---------------------------------------------------------------------------
void TestCraftMsgFailNoArgs() {
    std::vector<u8> pkt = BuildCliloc(
        0xFFFFFFFFu, 0xFFFFu, /*type=*/6, /*hue=*/0, /*font=*/3,
        /*clilocId=*/1044043u, "", {});

    net::ClilocMessage msg;
    Expect(net::ParseClilocMessage(pkt.data(), pkt.size(), msg),
           "parses a well-formed 0xC1");
    Expect(msg.clilocId == 1044043u, "cliloc id round-trips");
    Expect(msg.args.empty(), "no args decodes to an empty vector, not [\"\"]");
    Expect(msg.speaker.empty(), "empty speaker field decodes to empty string");

    const std::string text = net::FormatClilocJournalText(msg);
    ExpectEq(text, "[cliloc 1044043]",
             "id-only fallback text for craft_msg_fail (no Cliloc.enu in this repo)");

    // The journal entry this produces is exactly what CraftConfirm.cpp's
    // failure table now matches -- tie the two together so a change to
    // either format breaks this test, not a live run.
    usize n = 0;
    const life::CraftFailure* fails = life::CraftFailures(&n);
    bool found = false;
    for (usize i = 0; i < n; ++i) {
        if (text == fails[i].text) { found = true; break; }
    }
    Expect(found, "the decoded fallback text matches a row in CraftFailures()");
}

void TestArgsAreJoinedWithPipe() {
    std::vector<u8> pkt = BuildCliloc(
        0x12345678u, 0x0190u, /*type=*/6, /*hue=*/0x0021, /*font=*/3,
        /*clilocId=*/1044269u, "a smith", {"abc", "5"});

    net::ClilocMessage msg;
    Expect(net::ParseClilocMessage(pkt.data(), pkt.size(), msg),
           "parses a 0xC1 with two args");
    Expect(msg.clilocId == 1044269u, "cliloc id round-trips (craft_smelt_noskill)");
    Expect(msg.speaker == "a smith", "speaker name round-trips");
    Expect(msg.args.size() == 2 && msg.args[0] == "abc" && msg.args[1] == "5",
           "both args decode in order");

    ExpectEq(net::FormatClilocJournalText(msg), "[cliloc 1044269 abc|5]",
             "args are pipe-joined in the fallback text");
}

void TestAffixAppendsByDefault() {
    // flags = 0 -> (flags & 0x1) == 0 -> affix appended, per pol_packets.md.
    std::vector<u8> pkt = BuildClilocAffix(
        0xFFFFFFFFu, 0xFFFFu, /*type=*/6, /*hue=*/0, /*font=*/3,
        /*clilocId=*/1044253u, /*flags=*/0x00, "", " (extra)", {});

    net::ClilocMessage msg;
    Expect(net::ParseClilocMessageAffix(pkt.data(), pkt.size(), msg),
           "parses a well-formed 0xCC");
    Expect(!msg.prependAffix, "flags & 0x1 == 0 means append, not prepend");
    ExpectEq(net::FormatClilocJournalText(msg), "[cliloc 1044253] (extra)",
             "affix is appended when flags bit 0 is clear");
}

void TestAffixPrependsWhenFlagged() {
    std::vector<u8> pkt = BuildClilocAffix(
        0xFFFFFFFFu, 0xFFFFu, /*type=*/6, /*hue=*/0, /*font=*/3,
        /*clilocId=*/1044253u, /*flags=*/0x01, "", "*** ", {});

    net::ClilocMessage msg;
    Expect(net::ParseClilocMessageAffix(pkt.data(), pkt.size(), msg),
           "parses a 0xCC with the prepend bit set");
    Expect(msg.prependAffix, "flags & 0x1 == 1 means prepend");
    ExpectEq(net::FormatClilocJournalText(msg), "*** [cliloc 1044253]",
             "affix is prepended when flags bit 0 is set");
}

void TestAffixArgsAreBigEndian() {
    std::vector<u8> pkt = BuildClilocAffix(
        0x1u, 0x190u, /*type=*/6, /*hue=*/0, /*font=*/3,
        /*clilocId=*/1042762u, /*flags=*/0x00, "", "", {"100 thousand", "25 hundred"});

    net::ClilocMessage msg;
    Expect(net::ParseClilocMessageAffix(pkt.data(), pkt.size(), msg),
           "parses a 0xCC with big-endian args");
    Expect(msg.args.size() == 2 && msg.args[0] == "100 thousand" &&
           msg.args[1] == "25 hundred",
           "0xCC args decode correctly under the documented big-endian rule");
}

void TestTruncatedBuffersAreRejected() {
    std::vector<u8> full = BuildCliloc(1, 0, 6, 0, 3, 1044043u, "x", {});
    std::vector<u8> short_c1(full.begin(), full.begin() + 40);  // < 48-byte header
    net::ClilocMessage msg;
    Expect(!net::ParseClilocMessage(short_c1.data(), short_c1.size(), msg),
           "a 0xC1 shorter than the fixed header is rejected, not read out of bounds");

    std::vector<u8> fullAffix =
        BuildClilocAffix(1, 0, 6, 0, 3, 1044253u, 0, "x", "y", {});
    std::vector<u8> short_cc(fullAffix.begin(), fullAffix.begin() + 30);
    Expect(!net::ParseClilocMessageAffix(short_cc.data(), short_cc.size(), msg),
           "a 0xCC shorter than the fixed header is rejected, not read out of bounds");
}

}  // namespace

int main() {
    std::printf("cliloc_decode\n");
    TestCraftMsgFailNoArgs();
    TestArgsAreJoinedWithPipe();
    TestAffixAppendsByDefault();
    TestAffixPrependsWhenFlagged();
    TestAffixArgsAreBigEndian();
    TestTruncatedBuffersAreRejected();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
