#include "uo/log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace uo {

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() { Close(); }

bool Logger::OpenFile(const char* path) {
    Close();
    file_ = std::fopen(path, "wb");
    return file_ != nullptr;
}

void Logger::Close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

static const char* LevelTag(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Log:   return "LOG  ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

// "HH:MM:SS.mmm" (local wall clock) into out (needs >= 13 bytes).
static void Timestamp(char* out, usize cap) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const long ms = static_cast<long>(
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::snprintf(out, cap, "%02d:%02d:%02d.%03ld",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
}

void Logger::SetTag(const char* tag) {
    if (!tag || !tag[0]) { tag_[0] = 0; return; }
    std::snprintf(tag_, sizeof(tag_), "%s", tag);
}

void Logger::Emit(LogLevel lvl, LogSink sink, const char* body) {
    if (static_cast<u8>(lvl) < static_cast<u8>(minLevel_)) return;

    char ts[16];
    Timestamp(ts, sizeof(ts));
    const int s = static_cast<int>(sink);
    // "[hh:mm:ss.mmm] LEVEL [tag] message" -- the tag identifies the session.
    char tagbuf[32];
    if (tag_[0]) std::snprintf(tagbuf, sizeof(tagbuf), "[%s] ", tag_);
    else         tagbuf[0] = 0;

    if (s & static_cast<int>(LogSink::Console)) {
        std::FILE* dst =
            (static_cast<u8>(lvl) >= static_cast<u8>(LogLevel::Warn)) ? stderr : stdout;
        std::fprintf(dst, "[%s] %s %s%s", ts, LevelTag(lvl), tagbuf, body);
        std::fflush(dst);
    }
    if ((s & static_cast<int>(LogSink::File)) && file_) {
        std::fprintf(file_, "[%s] %s %s%s", ts, LevelTag(lvl), tagbuf, body);
        std::fflush(file_);
    }
}

void Logger::WriteV(LogLevel lvl, LogSink sink, const char* fmt, std::va_list ap) {
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    Emit(lvl, sink, buf);
}

void Logger::Write(LogLevel lvl, LogSink sink, const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    WriteV(lvl, sink, fmt, ap);
    va_end(ap);
}

void Logger::Packet(Direction dir, const u8* data, usize size, const char* note) {
    if (size == 0 || !file_) return;   // packet log lives in the file only

    char ts[16];
    Timestamp(ts, sizeof(ts));
    const char* d = (dir == Direction::In) ? "in " : "out";
    // The tag says which session produced this packet.
    char tagbuf[32];
    if (tag_[0]) std::snprintf(tagbuf, sizeof(tagbuf), "[%s] ", tag_);
    else         tagbuf[0] = 0;
    std::fprintf(file_, "[%s] PKT   %s%s 0x%02X len=%zu  ",
                 ts, tagbuf, d, data[0], size);

    static const char hex[] = "0123456789abcdef";
    for (usize i = 0; i < size; ++i) {
        std::fputc(hex[data[i] >> 4],   file_);
        std::fputc(hex[data[i] & 0x0F], file_);
    }
    if (note && note[0]) std::fprintf(file_, "  ; %s", note);
    std::fputc('\n', file_);
    std::fflush(file_);
}

void Logger::Event(const char* kind, const char* detail, LogSink sink) {
    Write(LogLevel::Log, sink, "event %s: %s\n",
          kind ? kind : "?", detail ? detail : "");
}

// --- free convenience wrappers ---------------------------------------------

#define UO_LOG_WRAPPER(fn, lvl)                                       \
    void fn(const char* fmt, ...) {                                   \
        std::va_list ap;                                             \
        va_start(ap, fmt);                                           \
        Logger::Instance().WriteV(lvl, LogSink::Both, fmt, ap);      \
        va_end(ap);                                                  \
    }

UO_LOG_WRAPPER(LogTrace, LogLevel::Trace)
UO_LOG_WRAPPER(LogDebug, LogLevel::Debug)
UO_LOG_WRAPPER(LogInfo,  LogLevel::Info)
UO_LOG_WRAPPER(LogLog,   LogLevel::Log)
UO_LOG_WRAPPER(LogWarn,  LogLevel::Warn)
UO_LOG_WRAPPER(LogError, LogLevel::Error)

#undef UO_LOG_WRAPPER

void LogMsg(LogLevel lvl, LogSink sink, const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    Logger::Instance().WriteV(lvl, sink, fmt, ap);
    va_end(ap);
}

void LogPacket(Direction dir, const u8* data, usize size, const char* note) {
    Logger::Instance().Packet(dir, data, size, note);
}

void LogEvent(const char* kind, const char* detail) {
    Logger::Instance().Event(kind, detail);
}

}
