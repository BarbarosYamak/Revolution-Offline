#pragma once

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::net {

// Decoded contents of a 0xC1 (Cliloc Message) or 0xCC (Cliloc Message Affix)
// packet. Protocol parsing only -- no journal, no Client -- kept client-free
// on purpose so tests/cliloc_decode.cpp links this one file, the same seam
// activities/craft_confirm.h and interaction/progress.h already use.
//
// Layout (bot-client.md / pol_packets.md "Cliloc Message" / "Cliloc Message
// Affix", cross-checked against the shape of 0x1C/0xAE OnAsciiMessage /
// OnUnicodeMessage already in Client.cpp):
//
//   0xC1  [0] cmd  [1-2] length (BE)  [3-6] source serial (BE)
//         [7-8] source body (BE)  [9] type  [10-11] hue (BE)
//         [12-13] font (BE)  [14-17] cliloc id (BE)
//         [18-47] speaker name (30, NUL-padded ASCII)
//         [48+] args: u16 LITTLE-endian code units, tab (0x0009) separated,
//               NUL (0x0000) terminated -- doc says little-endian here.
//
//   0xCC  same fields through the cliloc id, then:
//         [18] flags (bit 0x01: prepend the affix instead of appending)
//         [19-48] speaker name (30, NUL-padded ASCII)
//         [49+] affix: NUL-terminated ASCII
//         [after affix NUL] args: u16 BIG-endian code units, tab-separated,
//               NUL-terminated -- doc says big-endian here, the opposite of
//               0xC1's args. Kept as documented rather than "fixed" to
//               match, since nothing in this repo has observed a live 0xCC
//               to check it against.
struct ClilocMessage {
    u32 sourceSerial = 0;
    u16 sourceBody = 0;
    u8  type = 0;
    u16 hue = 0;
    u16 font = 0;
    u32 clilocId = 0;
    std::string speaker;
    std::vector<std::string> args;  // decoded (best-effort ASCII), in order

    // 0xCC only.
    bool hasAffix = false;
    std::string affix;
    bool prependAffix = false;  // flags & 0x01
};

// Parses a 0xC1 packet (`data[0]` must be 0xC1). Returns false, leaving
// `out` untouched, if the buffer is shorter than the fixed 48-byte header.
bool ParseClilocMessage(const u8* data, usize size, ClilocMessage& out);

// Parses a 0xCC packet (`data[0]` must be 0xCC). Returns false, leaving
// `out` untouched, if the buffer is shorter than the fixed 49-byte header
// (through the end of the speaker field).
bool ParseClilocMessageAffix(const u8* data, usize size, ClilocMessage& out);

// Renders the journal text for a decoded cliloc message.
//
// NO Cliloc.enu / Cliloc.tur ships with this shard's client data -- checked
// 2026-08-30 across runtime/, local/revolution-client/, and bot/uo-client/,
// nothing named *cliloc* anywhere except protocol documentation. So this is
// an ID-ONLY FALLBACK, not real localized text: "[cliloc 1044043]", or
// "[cliloc 1044043 arg1|arg2]" when the packet carried arguments (joined
// with '|' since the shard's own args are tab-separated and a tab is
// awkward to grep for). If a lazy id->text reader is ever added for a real
// cliloc file (hue-table style: id first, and mark anything unresolved
// UNKNOWN rather than guessing), it plugs in here and this function starts
// returning real text for known ids instead of the bracket form -- nothing
// else in this file needs to change.
std::string FormatClilocJournalText(const ClilocMessage& msg);

}  // namespace uo::net
