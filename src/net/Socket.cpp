#include "net/Socket.h"

#include "uo/log.h"

#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace uo::net {

static bool g_wsa_started = false;

bool Socket::WSAStart() {
    if (g_wsa_started) return true;
    WSADATA wd{};
    int rc = WSAStartup(MAKEWORD(2, 2), &wd);
    if (rc != 0) {
        LogError( "WSAStartup failed: %d\n", rc);
        return false;
    }
    g_wsa_started = true;
    return true;
}

void Socket::WSACleanupOnce() {
    if (g_wsa_started) {
        WSACleanup();
        g_wsa_started = false;
    }
}

Socket::Socket() : s_(INVALID_SOCKET), closed_(false) {}

Socket::~Socket() { Close(); }

bool Socket::Connect(const char* host, u16 port) {
    Close();
    closed_ = false;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portstr[16];
    std::snprintf(portstr, sizeof(portstr), "%u", static_cast<unsigned>(port));

    addrinfo* res = nullptr;
    int rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0 || !res) {
        LogError( "getaddrinfo(%s:%u) failed: %d\n", host, port, rc);
        return false;
    }

    bool ok = false;
    int last_err = 0;
    for (addrinfo* p = res; p; p = p->ai_next) {
        SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) { last_err = WSAGetLastError(); continue; }
        if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            s_ = s;
            ok = true;
            break;
        }
        last_err = WSAGetLastError();
        closesocket(s);
    }
    freeaddrinfo(res);

    if (!ok) {
        LogError( "connect(%s:%u) failed: WSA=%d\n",
                     host, port, last_err);
        return false;
    }

    // Switch to non-blocking for the receive loop. Sends use the
    // blocking-style SendAll wrapper with a partial-send loop.
    u_long nonblock = 1;
    ioctlsocket(s_, FIONBIO, &nonblock);
    return true;
}

bool Socket::SendAll(const u8* data, usize len) {
    usize sent = 0;
    while (sent < len) {
        int n = send(s_, reinterpret_cast<const char*>(data + sent),
                       static_cast<int>(len - sent), 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // Wait briefly for writability.
                fd_set wfd;
                FD_ZERO(&wfd);
                FD_SET(s_, &wfd);
                timeval tv{1, 0};
                if (select(0, nullptr, &wfd, nullptr, &tv) <= 0) {
                    LogError( "send timeout\n");
                    return false;
                }
                continue;
            }
            LogError( "send failed: WSA=%d\n", err);
            return false;
        }
        sent += static_cast<usize>(n);
    }
    return true;
}

int Socket::RecvSome(u8* dst, usize cap) {
    int n = ::recv(s_, reinterpret_cast<char*>(dst), static_cast<int>(cap), 0);
    if (n == 0) {
        closed_ = true;
        return -1;
    }
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
        LogError( "recv failed: WSA=%d\n", err);
        closed_ = true;
        return -1;
    }
    return n;
}

int Socket::WaitReadable(int timeout_ms) {
    if (s_ == INVALID_SOCKET) return -1;
    fd_set rfd;
    FD_ZERO(&rfd);
    FD_SET(s_, &rfd);
    timeval tv;
    timeval* ptv = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }
    int rc = select(0, &rfd, nullptr, nullptr, ptv);
    if (rc == SOCKET_ERROR) return -1;
    return rc;
}

void Socket::Close() {
    if (s_ != INVALID_SOCKET) {
        ::closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

}
