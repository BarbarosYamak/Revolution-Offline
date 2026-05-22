#pragma once

#include "uo/types.h"

#include <winsock2.h>

namespace uo::net {

class Socket {
public:
    Socket();
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Resolve and connect (TCP). Returns false on failure.
    bool Connect(const char* host, u16 port);

    // Send all bytes. Returns false on partial / failure.
    bool SendAll(const u8* data, usize len);

    // Non-blocking recv into caller buffer. Returns bytes read; 0 if
    // would-block; -1 on hard error or graceful close (use Closed()).
    int RecvSome(u8* dst, usize cap);

    void Close();
    bool IsOpen() const { return s_ != INVALID_SOCKET; }
    bool Closed() const { return closed_; }

    // Block until socket is readable or timeout (ms). 0 -> poll, -1 -> infinite.
    // Returns: 1 readable, 0 timeout, -1 error.
    int WaitReadable(int timeout_ms);

    static bool WSAStart();
    static void WSACleanupOnce();

private:
    SOCKET s_;
    bool closed_;
};

}
