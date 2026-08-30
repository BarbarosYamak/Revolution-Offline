#include "net/Cliloc.h"

#include "uo/endian.h"

namespace uo::net {

namespace {

std::string ReadFixedField(const u8* p, usize len) {
    usize n = 0;
    while (n < len && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

// Decodes a tab-separated, NUL(0x0000)-terminated wide-character argument
// string starting at data+offset, running to at most `size`. `bigEndian`
// selects the u16 code-unit byte order (see Cliloc.h: 0xC1 documents
// little-endian args, 0xCC documents big-endian). Non-ASCII code points
// degrade to '?', matching Client::OnUnicodeMessage's existing best-effort
// UTF-16 handling.
//
// An empty argument section (immediate 0x0000, or the buffer simply ends)
// decodes to an EMPTY vector, not a vector holding one empty string -- that
// distinction is what keeps FormatClilocJournalText's no-args case exactly
// "[cliloc <id>]" rather than "[cliloc <id> ]".
std::vector<std::string> ReadArgs(const u8* data, usize offset, usize size,
                                   bool bigEndian) {
    std::vector<std::string> args;
    std::string cur;
    bool sawAny = false;
    for (usize i = offset; i + 1 < size; i += 2) {
        const u16 ch = bigEndian ? LoadBE16(data + i) : LoadLE16(data + i);
        if (ch == 0) break;
        sawAny = true;
        if (ch == 0x0009u) {  // '\t'
            args.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back((ch < 0x80) ? static_cast<char>(ch) : '?');
    }
    if (sawAny) args.push_back(cur);
    return args;
}

}  // namespace

bool ParseClilocMessage(const u8* data, usize size, ClilocMessage& out) {
    static constexpr usize kHeader = 48;  // through the end of speaker(30)
    if (size < kHeader) return false;
    out = ClilocMessage{};
    out.sourceSerial = LoadBE32(data + 3);
    out.sourceBody   = LoadBE16(data + 7);
    out.type         = data[9];
    out.hue          = LoadBE16(data + 10);
    out.font         = LoadBE16(data + 12);
    out.clilocId     = LoadBE32(data + 14);
    out.speaker      = ReadFixedField(data + 18, 30);
    out.args         = ReadArgs(data, kHeader, size, /*bigEndian=*/false);
    return true;
}

bool ParseClilocMessageAffix(const u8* data, usize size, ClilocMessage& out) {
    static constexpr usize kFixed = 49;  // through the end of speaker(30)
    if (size < kFixed) return false;
    out = ClilocMessage{};
    out.sourceSerial = LoadBE32(data + 3);
    out.sourceBody   = LoadBE16(data + 7);
    out.type         = data[9];
    out.hue          = LoadBE16(data + 10);
    out.font         = LoadBE16(data + 12);
    out.clilocId     = LoadBE32(data + 14);
    const u8 flags   = data[18];
    out.prependAffix = (flags & 0x01u) != 0;
    out.speaker      = ReadFixedField(data + 19, 30);

    usize p = kFixed;
    usize affixLen = 0;
    while (p + affixLen < size && data[p + affixLen] != 0) ++affixLen;
    out.hasAffix = true;
    out.affix = std::string(reinterpret_cast<const char*>(data + p), affixLen);
    p += affixLen;
    if (p < size && data[p] == 0) ++p;  // consume the affix's own NUL
    out.args = ReadArgs(data, p, size, /*bigEndian=*/true);
    return true;
}

std::string FormatClilocJournalText(const ClilocMessage& msg) {
    // ID-ONLY FALLBACK -- see the comment on this function in Cliloc.h.
    std::string base = "[cliloc " + std::to_string(msg.clilocId);
    if (!msg.args.empty()) {
        base += " ";
        for (usize i = 0; i < msg.args.size(); ++i) {
            if (i) base += "|";
            base += msg.args[i];
        }
    }
    base += "]";
    if (msg.hasAffix && !msg.affix.empty()) {
        return msg.prependAffix ? (msg.affix + base) : (base + msg.affix);
    }
    return base;
}

}  // namespace uo::net
