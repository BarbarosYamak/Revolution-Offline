#pragma once

#include "uo/types.h"

#include <cstdio>

namespace uo {

// Line-delimited JSON packet logger. Writes one record per packet to a
// file plus a short hex dump to stdout. The format is deliberately
// minimal so that downstream tools (jq, etc.) can ingest it directly.

enum class Direction : u8 { In, Out };

class PacketLogger {
public:
    PacketLogger();
    ~PacketLogger();

    bool Open(const char* path);
    void Close();

    // Emit one packet record (always to stdout; to the JSON file once
    // verbose-mode has been enabled by EnableVerbose()).
    void Log(Direction dir, const u8* data, usize size, const char* note = nullptr);

    // Until this is called, packets received during login are echoed
    // to stdout but the JSON file is suppressed. Plan: caller flips
    // verbose ON after 0x55 Login Complete arrives.
    void EnableVerbose() { verbose_ = true; }
    bool Verbose() const { return verbose_; }

    // Free-form event for non-packet milestones (disconnect, timeout,
    // watchdog fire, etc.) so the JSONL is a complete session record.
    void Event(const char* kind, const char* detail);

private:
    std::FILE* file_;
    bool verbose_;
    u64 seq_;
};

}
