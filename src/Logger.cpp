#include "uo/log.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace uo {

PacketLogger::PacketLogger() : file_(nullptr), verbose_(false), seq_(0) {}

PacketLogger::~PacketLogger() { Close(); }

bool PacketLogger::Open(const char* path) {
    Close();
    file_ = std::fopen(path, "wb");
    return file_ != nullptr;
}

void PacketLogger::Close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

static void WriteHex(std::FILE* dst, const u8* data, usize size) {
    static const char hex[] = "0123456789abcdef";
    for (usize i = 0; i < size; ++i) {
        std::fputc(hex[data[i] >> 4],   dst);
        std::fputc(hex[data[i] & 0x0F], dst);
    }
}

void PacketLogger::Log(Direction dir, const u8* data, usize size, const char* note) {
    if (size == 0) return;

    const char* dir_str = (dir == Direction::In) ? "in" : "out";
    const u8 cmd = data[0];

    // stdout: tag + first 32 bytes of hex.
    std::fprintf(stdout, "[pkt %s 0x%02X len=%zu] ", dir_str, cmd, size);
    const usize dump_n = (size < 32) ? size : 32;
    for (usize i = 0; i < dump_n; ++i) std::fprintf(stdout, "%02x ", data[i]);
    if (size > dump_n) std::fputs("...", stdout);
    if (note) std::fprintf(stdout, "  ; %s", note);
    std::fputc('\n', stdout);
    std::fflush(stdout);

    if (!verbose_ || !file_) return;

    using namespace std::chrono;
    const auto now = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();

    std::fprintf(file_,
                 "{\"seq\":%llu,\"ts_ms\":%lld,\"dir\":\"%s\","
                 "\"id\":\"0x%02X\",\"len\":%zu,\"hex\":\"",
                 static_cast<unsigned long long>(seq_++),
                 static_cast<long long>(now),
                 dir_str, cmd, size);
    WriteHex(file_, data, size);
    if (note && note[0]) {
        std::fputs("\",\"note\":\"", file_);
        // Minimal escaping: assume note contains no control chars / quotes.
        for (const char* p = note; *p; ++p) {
            if (*p == '"' || *p == '\\') std::fputc('\\', file_);
            std::fputc(*p, file_);
        }
        std::fputs("\"}\n", file_);
    } else {
        std::fputs("\"}\n", file_);
    }
    std::fflush(file_);
}

void PacketLogger::Event(const char* kind, const char* detail) {
    if (!kind) kind = "?";
    if (!detail) detail = "";

    std::fprintf(stdout, "[event %s] %s\n", kind, detail);
    std::fflush(stdout);
    if (!verbose_ || !file_) return;

    using namespace std::chrono;
    const auto now = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();

    std::fprintf(file_,
                 "{\"seq\":%llu,\"ts_ms\":%lld,\"event\":\"%s\",\"detail\":\"",
                 static_cast<unsigned long long>(seq_++),
                 static_cast<long long>(now), kind);
    for (const char* p = detail; *p; ++p) {
        if (*p == '"' || *p == '\\') std::fputc('\\', file_);
        if (static_cast<unsigned char>(*p) < 0x20) std::fputc('?', file_);
        else std::fputc(*p, file_);
    }
    std::fputs("\"}\n", file_);
    std::fflush(file_);
}

}
