#pragma once

#include "uo/types.h"

#include <cstdarg>
#include <cstdio>

namespace uo {

enum class Direction : u8 { In, Out };

// Severity, low to high. The minimum-level filter drops anything below it.
// (`Log` is the plain catch-all level between Info and Warn.)
enum class LogLevel : u8 { Trace, Debug, Info, Log, Warn, Error };

// Per-message routing. Console writes to stdout (stderr for Warn/Error);
// File writes to the open text log. Both = Console | File.
enum class LogSink : u8 { Console = 1, File = 2, Both = 3 };

// Process-wide text logger. One timestamped line per message:
//   [HH:MM:SS.mmm] LEVEL message
// Packet hex dumps go to the FILE ONLY so the console stays readable; to chase
// a packet, take a console message's timestamp and grep the log file for it.
class Logger {
public:
    // Process-wide logger: console output and anything with no session
    // context (startup, CLI errors). Sessions own their own Logger instead.
    static Logger& Instance();

    Logger() = default;
    ~Logger();

    bool OpenFile(const char* path);   // truncates; false on open error
    void Close();
    bool IsOpen() const { return file_ != nullptr; }

    // Drop any message strictly below this level. Default: Trace (keep all).
    void     SetMinLevel(LogLevel lvl) { minLevel_ = lvl; }
    LogLevel MinLevel() const { return minLevel_; }

    // Short session tag ("bot01"). When set it prefixes every line this
    // logger writes, so output from concurrent sessions stays attributable.
    void        SetTag(const char* tag);
    const char* Tag() const { return tag_; }

    // printf-style; the caller supplies the trailing '\n'.
    void Write(LogLevel lvl, LogSink sink, const char* fmt, ...);
    void WriteV(LogLevel lvl, LogSink sink, const char* fmt, std::va_list ap);

    // One packet record -> file only (hex + optional note).
    void Packet(Direction dir, const u8* data, usize size, const char* note = nullptr);

    // Named milestone (connect, disconnect, ...). Both sinks by default.
    void Event(const char* kind, const char* detail, LogSink sink = LogSink::Both);

private:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Emit(LogLevel lvl, LogSink sink, const char* body);

    std::FILE* file_     = nullptr;
    LogLevel   minLevel_ = LogLevel::Trace;
    char       tag_[24]  = {0};
};

// Free convenience wrappers over Logger::Instance(). Default sink = Both, so
// the file keeps a full session record while the console shows the message.
// printf-style; include your own trailing '\n'.
void LogTrace(const char* fmt, ...);
void LogDebug(const char* fmt, ...);
void LogInfo (const char* fmt, ...);
void LogLog  (const char* fmt, ...);
void LogWarn (const char* fmt, ...);
void LogError(const char* fmt, ...);

// Explicit-sink variant, e.g. LogMsg(LogLevel::Debug, LogSink::File, "...").
void LogMsg(LogLevel lvl, LogSink sink, const char* fmt, ...);

// Shorthands mirroring the surface the client already used.
void LogPacket(Direction dir, const u8* data, usize size, const char* note = nullptr);
void LogEvent(const char* kind, const char* detail);

}
