#include "Client.h"

#include "uo/builders.h"
#include "uo/endian.h"
#include "uo/map.h"
#include "uo/packet_ids.h"
#include "uo/packet_lengths.h"
#include "uo/tiledata.h"
#include "uo/world.h"
#include "uo/art.h"
#include "uo/texmap.h"
#include "uo/anim.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "render/Minimap.h"
#include "render/RadarColors.h"
#include "win32/MiniFB.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <iostream>
#include <limits>
#include <utility>

#include <winsock2.h>

namespace uo {

namespace {

// Recent mobile cache and delayed name query handling.
constexpr usize kMobileCacheMax = 64;
constexpr i64   kMobilesNamesTimeoutMs = 500;
// UO Demo / Sphere-style shards kick the connection after ~60s of
// client-side silence. Stay well inside the window: 20s gap.
constexpr i64   kKeepaliveIntervalMs = 20000;
// Stringify u32 IPv4 in host order to dotted notation.
void IpToString(u32 host_ip, char* out, usize cap) {
    std::snprintf(out, cap, "%u.%u.%u.%u",
                  (host_ip >> 24) & 0xFF,
                  (host_ip >> 16) & 0xFF,
                  (host_ip >>  8) & 0xFF,
                   host_ip        & 0xFF);
}

const char* StateName(int s) {
    switch (s) {
        case 0:  return "Disconnected";
        case 1:  return "LoginHandshake";
        case 2:  return "AwaitingServerList";
        case 3:  return "AwaitingGameServer";
        case 4:  return "ConnectingToGameServer";
        case 5:  return "GameHandshake";
        case 6:  return "AwaitingCharacterList";
        case 7:  return "AwaitingLoginConfirm";
        case 8:  return "InWorld";
        case 9:  return "Failed";
        default: return "?";
    }
}

std::string PacketString(const u8* p, usize len) {
    usize n = 0;
    while (n < len && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

}

Client::Client(const Config& cfg)
    : cfg_(cfg),
      state_(State::Disconnected),
      decompress_(false),
      serverCount_(0),
      selectedServer_(-1),
      charCount_(0),
      selectedChar_(-1),
      gameSeed_(0),
      gameServerIp_(0),
      gameServerPort_(0),
      player_{},
      playerSerial_(0),
      playerX_(0), playerY_(0), playerZ_(0),
      playerFacing_(0), playerRunning_(false),
      worldLoaded_(false),
      renderInit_(false), renderWindowOpen_(false),
      minimapVisible_(true), minimapKeyDown_(false), spaceKeyDown_(false),
      playerBody_(0x0190), lastManualMoveMs_(0),
      nav_{},
      mobilesListPending_(false), mobilesListDeadlineMs_(0),
      lastStatusProbeMs_(0),
      verboseConsole_(false),
      stop_stdin_(false),
      lastActivityMs_(0) {
    nav_.rng.seed(static_cast<u32>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    navigation::PathPlannerConfig pathConfig;
    pathConfig.tiledataPath = cfg_.tiledataPath ? cfg_.tiledataPath : "";
    pathConfig.mapPath = cfg_.mapPath ? cfg_.mapPath : "";
    pathConfig.staidxPath = cfg_.staidxPath ? cfg_.staidxPath : "";
    pathConfig.staticsPath = cfg_.staticsPath ? cfg_.staticsPath : "";
    pathConfig.verdataPath = cfg_.verdataPath ? cfg_.verdataPath : "";
    pathConfig.acceptDoors = cfg_.acceptDoors;
    pathPlanner_ = std::make_unique<navigation::PathPlanner>(std::move(pathConfig));
    std::memset(servers_, 0, sizeof(servers_));
    std::memset(charSlots_, 0, sizeof(charSlots_));
}

Client::~Client() {
    if (renderWindowOpen_) { mfb_close(); renderWindowOpen_ = false; }
    StopStdinThread();
    sock_.Close();
    Logger::Instance().Close();
    net::Socket::WSACleanupOnce();
}

int Client::Run() {
    if (!net::Socket::WSAStart()) return 1;

    if (cfg_.logFile && cfg_.logFile[0]) {
        if (!Logger::Instance().OpenFile(cfg_.logFile)) {
            LogWarn( "warning: cannot open log file '%s'\n", cfg_.logFile);
        } else {
            LogEvent("session_start", "text log opened");
        }
    }

    if (!ConnectAndSendSeed(cfg_.loginHost, cfg_.loginPort)) return 2;

    // Send 0x80 immediately after the seed; the original client does the
    // same — there is no server-side ack between seed and login.
    u8 buf[256];
    usize n = build::LoginRequest(buf, cfg_.username, cfg_.password);
    if (!Send(buf, n, "0x80 LoginRequest")) return 3;
    state_ = State::AwaitingServerList;

    if (!PumpUntilDisconnected()) return 4;
    return 0;
}

bool Client::ConnectAndSendSeed(const char* host, u16 port) {
    char ev[160];
    std::snprintf(ev, sizeof(ev), "connect %s:%u", host, port);
    LogEvent("net_connect_begin", ev);
    LogInfo("[net] connecting to %s:%u ...\n", host, port);
    if (!sock_.Connect(host, port)) {
        LogEvent("net_connect_fail", ev);
        state_ = State::Failed;
        return false;
    }
    LogEvent("net_connected", ev);
    LogInfo("[net] connected.\n");

    if (cfg_.sendSeed) {
        u8 seedbuf[4];
        build::Seed(seedbuf, cfg_.plaintextSeed);
        LogInfo("[seed] sent 0x%08X (plaintext)\n", cfg_.plaintextSeed);
        // Seed is 4 raw bytes (not a UO packet) — log as event so the
        // JSONL captures the actual wire bytes.
        char hexbuf[20];
        std::snprintf(hexbuf, sizeof(hexbuf), "%02x%02x%02x%02x",
                      seedbuf[0], seedbuf[1], seedbuf[2], seedbuf[3]);
        char detail[64];
        std::snprintf(detail, sizeof(detail),
                      "0x%08X hex=%s", cfg_.plaintextSeed, hexbuf);
        LogEvent("seed_out", detail);
        if (!sock_.SendAll(seedbuf, sizeof(seedbuf))) {
            LogEvent("seed_out_failed", detail);
            state_ = State::Failed;
            return false;
        }
    } else {
        LogEvent("seed_skipped", "cfg_.sendSeed=false");
        LogInfo("[seed] skipped (cfg_.sendSeed=false)\n");
    }
    state_ = State::LoginHandshake;
    return true;
}

bool Client::Send(const u8* data, usize size, const char* note) {
    if (!sock_.SendAll(data, size)) {
        char detail[160];
        std::snprintf(detail, sizeof(detail),
            "SendAll failed; size=%zu first=0x%02X note=%s",
            size, size ? data[0] : 0, note ? note : "");
        LogEvent("send_failed", detail);
        return false;
    }
    LogPacket(Direction::Out, data, size, note);
    return true;
}

bool Client::PumpUntilDisconnected() {
    u8 rxbuf[8192];
    auto last_activity = std::chrono::steady_clock::now();
    bool stalled_logged = false;

    while (state_ != State::Failed && !sock_.Closed()) {
        // Liveness heartbeat — surface server silence after 5s.
        auto now = std::chrono::steady_clock::now();
        auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                        now - last_activity).count();
        if (!stalled_logged && idle >= 5 &&
            state_ != State::InWorld &&
            state_ != State::AwaitingServerList &&
            state_ != State::AwaitingCharacterList) {
            LogWarn(
                "[stall] no packets for %llds, state=%s, "
                "tcp still open=%d\n",
                static_cast<long long>(idle),
                StateName(static_cast<int>(state_)),
                sock_.IsOpen() && !sock_.Closed() ? 1 : 0);
            stalled_logged = true;
        }
        // Wait briefly for socket data; periodically pump stdin speech.
        int rd = sock_.WaitReadable(50);
        if (rd < 0) {
            LogWarn( "[net] select error; bailing\n");
            return false;
        }
        if (rd > 0) {
            int n = sock_.RecvSome(rxbuf, sizeof(rxbuf));
            if (n < 0) {
                LogWarn( "[net] socket closed by peer.\n");
                LogEvent("disconnect", "recv returned -1 (RST or FIN)");
                break;
            }
            if (n > 0) {
                last_activity = std::chrono::steady_clock::now();
                stalled_logged = false;
                // Note: do NOT reset lastActivityMs_ on recv. The
                // original client keeps a SEND-only timer
                // (g_LastNetworkActivity) and emits the 60-second
                // keepalive regardless of inbound traffic.
                const u8* feed = rxbuf;
                usize feed_n = static_cast<usize>(n);
                if (decompress_) {
                    // Game stream is Huffman-compressed; decode before framing.
                    rxScratch_.clear();
                    if (!huff_.Decompress(rxbuf, feed_n, rxScratch_)) {
                        LogWarn( "[huffman] malformed compressed stream\n");
                        LogEvent("huffman_error", "malformed code in game stream");
                        return false;
                    }
                    feed = rxScratch_.data();
                    feed_n = rxScratch_.size();
                    if (feed_n == 0) continue;  // partial block; need more bytes
                }
                if (!stream_.FeedBytes(feed, feed_n)) {
                    LogWarn( "[net] stream buffer overflow\n");
                    return false;
                }
                for (;;) {
                    const u8* pkt = nullptr;
                    usize pkt_size = 0;
                    const char* err = nullptr;
                    if (!stream_.TryNext(&pkt, &pkt_size, &err)) {
                        if (err) {
                            LogWarn( "[stream] %s (pending=%zu)\n",
                                         err, stream_.Pending());
                            char detail[128];
                            std::snprintf(detail, sizeof(detail),
                                "%s (pending=%zu)", err, stream_.Pending());
                            LogEvent("stream_error", detail);
                            return false;
                        }
                        break;
                    }
                    Dispatch(pkt, pkt_size);
                }
            }
        }

        if (state_ == State::InWorld) {
            PumpStdinCommand();
            BotTick();
            RenderTick();

            // Keepalive — mirrors Packet_BuildKeepalive @ 0x4279B0 +
            // GameLoop_Update @ 0x4BF720: original client emits 0x73
            // (cmd + 0x00 sequence) whenever 60s pass without TCP
            // activity. Without it, UO Demo / Sphere shards RST the
            // connection on next client send.
            const auto now_ms2 =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            if (cfg_.enableKeepalive &&
                lastActivityMs_ != 0 &&
                playerSerial_ != 0 &&
                now_ms2 - lastActivityMs_ > kKeepaliveIntervalMs) {
                // Use 0x09 SingleClick on our own serial as a real,
                // unambiguous "I'm here" packet. The shard answers with
                // a 0x1C name message — confirms the connection is
                // live both ways. 0x73 0x00 turned out to be treated as
                // junk on UO Demo emulators and accumulated toward a
                // silent kick.
                u8 buf[8];
                usize n = build::SingleClick(buf, playerSerial_);
                if (Send(buf, n, "0x09 SelfClick (keepalive)")) {
                    lastActivityMs_ = now_ms2;
                }
            }
        }
    }
    return state_ != State::Failed;
}

// ---------------------------------------------------------------------------
// Dispatcher — mirrors Packet_Dispatch @ 0x42DD80 as a 1:1 switch. Every
// opcode the original handles by name appears here; anything else routes
// to OnUnknown so the new client logs but doesn't crash.
// ---------------------------------------------------------------------------
void Client::Dispatch(const u8* data, usize size) {
    LogPacket(Direction::In, data, size);

    const u8 cmd = data[0];
    switch (cmd) {
        case 0x11: OnStats(data, size); break;
        case 0x1B: OnLoginConfirm(data, size); break;
        case 0x1C: OnAsciiMessage(data, size); break;
        case 0x55: OnLoginComplete(data, size); break;
        case 0x20: OnDrawGamePlayer(data, size); break;
        case 0x21: OnMoveReject(data, size); break;
        case 0x22: OnMoveAck(data, size); break;
        case 0x73: OnPing(data, size); break;
        case 0x81: OnLegacyCharList(data, size); break;
        case 0x82: OnLoginDenied(data, size); break;
        case 0x8C: OnConnectToGameServer(data, size); break;
        case 0xA8: OnServerList(data, size); break;
        case 0xA9: OnCharacterList(data, size); break;
        case 0xAE: OnUnicodeMessage(data, size); break;
        case 0xB9: OnFeatures(data, size); break;
        case 0xBD: OnClientVersionQuery(data, size); break;
        case 0xC8: OnViewRange(data, size); break;
        case 0xA1: OnMobileHp(data, size); break;
        case 0xA2: OnMobileMana(data, size); break;
        case 0xA3: OnMobileStamina(data, size); break;
        case 0x1A: OnObjectInfo(data, size); break;
        case 0x1D: OnDeleteObject(data, size); break;
        case 0x77: OnMobileMove(data, size); break;
        case 0x78: OnMobileIncoming(data, size); break;
        case 0x98: OnMobName(data, size); break;
        case 0x2D: OnMobileAttributes(data, size); break;
        case 0x3A: OnSkills(data, size); break;

        // Common in-world packets we just log + ignore for M1.
        case 0x23: case 0x25: case 0x2E: case 0x2F:
        case 0x3C: case 0x4E: case 0x4F: case 0x53:
        case 0x54: case 0x5B: case 0x65: case 0x6D: case 0x6E:
        case 0x70: case 0x72: case 0x88:
        case 0x8B: case 0x97:
        case 0xB0: case 0xBA: case 0xBC: case 0xBF: case 0xC0:
        case 0xC1: case 0xCB: case 0xCC:
            // Logged above; behavior is no-op until later milestones.
            break;

        default:
            OnUnknown(data, size);
            break;
    }
}

// ---------------------------------------------------------------------------
// 0xA8 Server List (variable). Layout from Packet_HandleServerListRecv
// @ 0x4220B0:
//   [0]   cmd 0xA8
//   [1-2] length (BE)
//   [3]   systemInfoFlag
//   [4-5] serverCount (BE)
//   per server (38 bytes), starting at [6]:
//     [+0..1]   serverIndex (BE)
//     [+2..33]  serverName (32 ASCII, may not be NUL-terminated)
//     [+34]     percentFull
//     [+35]     timezone
//     [+36..39] serverIP (BE, big-endian dotted form on the wire)
// ---------------------------------------------------------------------------
void Client::OnServerList(const u8* data, usize size) {
    if (size < 6) return;
    const u8  systemInfoFlag = data[3];
    const u16 count = LoadBE16(data + 4);
    (void)systemInfoFlag;

    serverCount_ = 0;
    const usize entry_size = 40;
    for (u16 i = 0; i < count && serverCount_ < 32; ++i) {
        const usize off = 6 + static_cast<usize>(i) * entry_size;
        if (off + entry_size > size) break;

        ServerEntry& e = servers_[serverCount_];
        e.index = LoadBE16(data + off);
        std::memcpy(e.name, data + off + 2, 32);
        e.name[32] = '\0';
        e.percentFull = data[off + 34];
        e.timezone    = data[off + 35];
        e.ip          = LoadBE32(data + off + 36);
        e.port        = 0; // not in this packet; reused from login flow
        ++serverCount_;
    }

    LogInfo("[0xA8] %d server(s):\n", serverCount_);
    for (int i = 0; i < serverCount_; ++i) {
        char ip[32];
        IpToString(servers_[i].ip, ip, sizeof(ip));
        LogInfo("  [%d] %-24s  %s  full=%u%%  tz=%u\n",
                    i, servers_[i].name, ip,
                    servers_[i].percentFull, servers_[i].timezone);
    }

    state_ = State::AwaitingServerList;
    selectedServer_ = 0; // PromptServerSelection();
    if (selectedServer_ < 0 || selectedServer_ >= serverCount_) {
        LogWarn( "[ui] no server selected; aborting\n");
        state_ = State::Failed;
        return;
    }

    u8 buf[16];
    usize n = build::SelectServer(buf, servers_[selectedServer_].index);
    if (!Send(buf, n, "0xA0 SelectServer")) {
        state_ = State::Failed;
        return;
    }
    state_ = State::AwaitingGameServer;
}

// ---------------------------------------------------------------------------
// 0x81 Legacy Character List / Change Characters (variable). Used by
// pre-T2A protocols (UO Demo / very old shards) where login → char-list
// is direct (no 0xA8/0x8C/0xA9 flow). Layout from
// Packet_HandleServerList @ 0x421320:
//   [0]        cmd 0x81
//   [1-2]      length (BE)
//   [3]        flag (1 == char-list-mode)
//   [4]        protocolType (e.g. 0xCD)
//   [5..304]   5 character slots, 60 bytes each:
//                [+0..29]  character name (30 ASCII, NUL-padded)
//                [+30..59] password placeholder (zeros)
//   [305]      cityCount
//   ...        cities (we ignore for M1)
// ---------------------------------------------------------------------------
void Client::OnLegacyCharList(const u8* data, usize size) {
    if (size < 5 + 60) return;

    charCount_ = 5;
    int populated = 0;
    for (int i = 0; i < 5; ++i) {
        const usize off = 5 + static_cast<usize>(i) * 60;
        if (off + 30 > size) break;
        std::memcpy(charSlots_[i].name, data + off, 30);
        charSlots_[i].name[30] = '\0';
        if (charSlots_[i].name[0]) ++populated;
    }

    LogInfo("[0x81] legacy char list (flag=0x%02X proto=0x%02X) — %d slot(s) (populated %d):\n",
                data[3], data[4], charCount_, populated);
    for (int i = 0; i < charCount_; ++i) {
        const char* nm = charSlots_[i].name[0] ? charSlots_[i].name : "<empty>";
        LogInfo("  [%d] %s\n", i, nm);
    }

    if (populated == 0) {
        LogWarn( "[ui] no characters on this shard; aborting\n");
        state_ = State::Failed;
        return;
    }

    selectedChar_ = PromptCharacterSelection();
    if (selectedChar_ < 0 || selectedChar_ >= charCount_ ||
        !charSlots_[selectedChar_].name[0]) {
        LogWarn( "[ui] invalid character slot\n");
        state_ = State::Failed;
        return;
    }

    // UO Demo speaks 0x5D the same way as later clients (73 bytes).
    // Server will respond with 0x1B Login Confirm directly.
    u8 buf[128];
    usize n = build::PlayCharacter(buf,
                                   charSlots_[selectedChar_].name,
                                   static_cast<u32>(selectedChar_),
                                   0u);
    if (!Send(buf, n, "0x5D PlayCharacter (legacy)")) {
        state_ = State::Failed;
        return;
    }
    state_ = State::AwaitingLoginConfirm;
}

// ---------------------------------------------------------------------------
// 0x8C Connect To Game Server (11 bytes). Layout from
// Packet_HandleConnectToGameServer @ 0x423AB0:
//   [0]   cmd 0x8C
//   [1-4] gameServerIP (BE)
//   [5-6] gameServerPort (BE)
//   [7-10] authKey (BE, becomes the seed for the new connection)
// We close the login-server socket and reconnect to the game server
// addr, then send seed + 0x91 GameServerLogin.
// ---------------------------------------------------------------------------
void Client::OnConnectToGameServer(const u8* data, usize size) {
    if (size < 11) return;
    gameServerIp_   = LoadBE32(data + 1);
    gameServerPort_ = LoadBE16(data + 5);
    gameSeed_       = LoadBE32(data + 7);

    char ip[32];
    IpToString(gameServerIp_, ip, sizeof(ip));
    LogInfo("[0x8C] game server = %s:%u  seed=0x%08X\n",
                ip, gameServerPort_, gameSeed_);
    char ev[160];
    std::snprintf(ev, sizeof(ev), "ip=%s port=%u seed=0x%08X",
                  ip, gameServerPort_, gameSeed_);
    LogEvent("game_server_assigned", ev);

    // Two reconnect modes, mirroring Packet_HandleConnectToGameServer
    // @ 0x423AB0:
    //
    //   * RECONNECT (handshakeState != 0 branch + serverIp != localIP):
    //     close current socket, open new TCP to (ip,port), and the
    //     connect helper sub_42CB90 sends 4 raw seed bytes BEFORE
    //     anything else. Then 0x91 goes on the new stream.
    //
    //   * STAY-ON-SOCKET (LABEL_47, serverIp == localIP):
    //     no new TCP, no new seed; just send 0x91 over the existing
    //     login-server socket.
    //
    // UO Demo-style emulators that use a single TCP socket for both
    // login and game advertise serverIp == 0 to mean "stay here". The
    // canonical client would error ("%s is full"), but we follow
    // LABEL_47 in that case so we keep talking.
    // Stay on socket when the game server is the same host as login server.
    // The server may advertise a different port (e.g. 1593) but it might be
    // the same process.  We detect this by seeing if the game port differs
    // from our login port — if so, stay on the existing socket just like
    // official patched clients do when serverIp == localIP (LABEL_47).
    const bool stay_on_socket =
        (gameServerPort_ != cfg_.loginPort) &&
        !(cfg_.gameHostOverride && cfg_.gameHostOverride[0]) &&
        (cfg_.gamePortOverride == 0);

    const char* connect_host = ip;
    if (cfg_.gameHostOverride && cfg_.gameHostOverride[0]) {
        connect_host = cfg_.gameHostOverride;
    } else if (gameServerIp_ == 0) {
        connect_host = cfg_.loginHost;
    }

    u16 connect_port = gameServerPort_;
    if (cfg_.gamePortOverride != 0) {
        connect_port = cfg_.gamePortOverride;
    }

    // Stay on socket - official clients reconnect to same server (same IP/port)
    if (stay_on_socket) {
        // Same socket, send seed + 0x91
        u8 seedbuf[4];
        build::Seed(seedbuf, gameSeed_);
        if (!sock_.SendAll(seedbuf, sizeof(seedbuf))) {
            state_ = State::Failed;
            return;
        }
        u8 buf[128];
        usize n = build::GameLogin(buf, gameSeed_, cfg_.username, cfg_.password);
        if (!Send(buf, n, "0x91 GameLogin (single-socket)")) {
            state_ = State::Failed;
            return;
        }
        // The server enables Huffman compression on the game stream the
        // moment it handles our 0x91 (HandlePacket_POSTLOGIN). Every byte
        // it sends from here on (0xB9, 0xA9, ...) is compressed.
        huff_.Reset();
        decompress_ = true;
        state_ = State::GameHandshake;
        return;
    }


    // Fallback: reconnect to game server if not staying on socket

    state_ = State::ConnectingToGameServer;
/*
    stream_.Reset();
    sock_.Close();
    if (!sock_.Connect(cfg_.loginHost, cfg_.loginPort)) {
        state_ = State::Failed;
        return;
    }
    LogInfo("[net] reconnected to game server.\n");

    if (cfg_.sendSeed) {
        u8 seedbuf[4];
        build::Seed(seedbuf, gameSeed_);
        sock_.SendAll(seedbuf, sizeof(seedbuf));
        LogInfo("[seed] game-server seed 0x%08X sent\n", gameSeed_);
    }
  */
    {
        u8 buf[128];
        usize n = build::GameLogin(buf, gameSeed_, cfg_.username, cfg_.password);
        Send(buf, n, "0x91 GameLogin");
    }
    huff_.Reset();
    decompress_ = true;
    state_ = State::GameHandshake;
}

// ---------------------------------------------------------------------------
// 0xA9 Character List (variable). Layout from Packet_HandleCharacterList
// @ 0x422AF0:
//   [0]   cmd 0xA9
//   [1-2] length (BE)
//   [3]   charSlotCount (often 5; loops up to 5 slots regardless)
//   per slot (60 bytes), starting at [4]:
//     [+0..29]  character name (NUL-padded ASCII, 30 bytes)
//     [+30..59] password placeholder (always 30 zeros)
// (Starting cities follow; M1 ignores them.)
// ---------------------------------------------------------------------------
void Client::OnCharacterList(const u8* data, usize size) {
    if (size < 4) return;
    const u8 slotCount = data[3];
    charCount_ = (slotCount < 5) ? slotCount : 5;

    int populated = 0;
    for (int i = 0; i < charCount_; ++i) {
        const usize off = 4 + static_cast<usize>(i) * 60;
        if (off + 60 > size) break;
        std::memcpy(charSlots_[i].name, data + off, 30);
        charSlots_[i].name[30] = '\0';
        if (charSlots_[i].name[0]) ++populated;
    }

    LogInfo("[0xA9] %d slot(s) (populated %d):\n", charCount_, populated);
    for (int i = 0; i < charCount_; ++i) {
        const char* nm = charSlots_[i].name[0] ? charSlots_[i].name : "<empty>";
        LogInfo("  [%d] %s\n", i, nm);
    }

    if (populated == 0) {
        LogWarn( "[ui] no characters on this shard; aborting\n");
        state_ = State::Failed;
        return;
    }

    selectedChar_ = 0; // PromptCharacterSelection();
    if (selectedChar_ < 0 || selectedChar_ >= charCount_ ||
        !charSlots_[selectedChar_].name[0]) {
        LogWarn( "[ui] invalid character slot\n");
        state_ = State::Failed;
        return;
    }

    u8 buf[128];
    // slotOrFlag low byte = slot index; clientIP = 0 (we're headless).
    usize n = build::PlayCharacter(buf,
                                   charSlots_[selectedChar_].name,
                                   static_cast<u32>(selectedChar_),
                                   0u);
    if (!Send(buf, n, "0x5D PlayCharacter")) {
        state_ = State::Failed;
        return;
    }
    state_ = State::AwaitingLoginConfirm;
}

// ---------------------------------------------------------------------------
// 0x1B Login Confirm (37 bytes). Reads only the bits M1 cares about:
//   [0]    cmd
//   [1-4]  playerSerial (BE)
//   [5-8]  unused
//   [9-10] body
//   [11-12] x (BE)
//   [13-14] y (BE)
//   [15-16] z (BE, low byte)
//   [17]   direction
//   ...
// ---------------------------------------------------------------------------
void Client::OnLoginConfirm(const u8* data, usize size) {
    if (size < 18) return;
    playerSerial_ = LoadBE32(data + 1) & 0x7FFFFFFFu;
    u16 body = LoadBE16(data + 9);
    u16 x    = LoadBE16(data + 11);
    u16 y    = LoadBE16(data + 13);
    u16 z    = LoadBE16(data + 15);  // low byte is z; high byte usually 0
    u8 dir   = data[17];
    playerX_ = static_cast<i32>(x);
    playerY_ = static_cast<i32>(y);
    playerZ_ = static_cast<i8>(z & 0xFF);
    playerFacing_ = dir & 0x07;
    playerRunning_ = false;
    playerBody_ = body;
    player_.serial = playerSerial_;
    player_.body = body;
    player_.x = playerX_;
    player_.y = playerY_;
    player_.z = playerZ_;
    player_.facing = playerFacing_;
    player_.running = playerRunning_;
    LogInfo("[0x1B] serial=0x%08X body=0x%04X pos=(%d,%d,%d)\n",
                playerSerial_, body,
                playerX_, playerY_, static_cast<int>(playerZ_));
    char ev[160];
    std::snprintf(ev, sizeof(ev),
                  "serial=0x%08X body=0x%04X pos=(%d,%d,%d)",
                  playerSerial_, body,
                  playerX_, playerY_, static_cast<int>(playerZ_));
    LogEvent("login_confirm", ev);
}

// ---------------------------------------------------------------------------
// 0x55 Login Complete (1 byte). World is up; we are live.
// ---------------------------------------------------------------------------
void Client::OnLoginComplete(const u8* data, usize size) {
    (void)data; (void)size;
    LogInfo("[0x55] login complete — entering world\n");
    state_ = State::InWorld;
    LogEvent("in_world", "0x55 received");
    // Initialise the keepalive timer so the first keepalive fires
    // exactly 60s after entering the world (matches the original).
    lastActivityMs_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    StartStdinThread();
    LogInfo("\nType to chat (Enter to send). Ctrl-C to quit.\n\n");
}

// ---------------------------------------------------------------------------
// 0x82 Login Denied (2 bytes). Reason byte (0..3 documented).
// ---------------------------------------------------------------------------
void Client::OnLoginDenied(const u8* data, usize size) {
    if (size < 2) return;
    const u8 reason = data[1];
    const char* msg;
    switch (reason) {
        case 0: msg = "account doesn't exist or password incorrect"; break;
        case 1: msg = "account already in use";                       break;
        case 2: msg = "account blocked";                              break;
        case 3: msg = "invalid credentials";                          break;
        default: msg = "communication error / other";                 break;
    }
    LogWarn( "[0x82] LOGIN DENIED (%u): %s\n", reason, msg);
    state_ = State::Failed;
}

// ---------------------------------------------------------------------------
// 0xBD Client Version Query (variable, often 3 bytes empty). Reply with
// the version string from cfg_.version. Mirrors
// Packet_HandleClientVersion @ 0x429DC0.
// ---------------------------------------------------------------------------
void Client::OnClientVersionQuery(const u8* data, usize size) {
    (void)data; (void)size;
    u8 buf[64];
    usize n = build::ClientVersion(buf, cfg_.version ? cfg_.version : "2.0.7");
    Send(buf, n, "0xBD ClientVersion (reply)");
}

// ---------------------------------------------------------------------------
// 0xB9 Supported Features (3 bytes). We just record + log.
// ---------------------------------------------------------------------------
void Client::OnFeatures(const u8* data, usize size) {
    if (size < 3) return;
    u16 flags = LoadBE16(data + 1);
    LogInfo("[0xB9] server features = 0x%04X\n", flags);
}

// ---------------------------------------------------------------------------
// 0xC8 Client View Range (2 bytes). Server tells client what the active
// range is. We just log; original client also has nothing to "ack" here.
// ---------------------------------------------------------------------------
void Client::OnViewRange(const u8* data, usize size) {
    if (size < 2) return;
    LogInfo("[0xC8] view range = %u\n", data[1]);
}

// ---------------------------------------------------------------------------
// 0xA1 Update Mobile Hits (9 bytes): cmd, serial(4 BE), maxHp(2 BE),
// curHp(2 BE). For our own serial we track HP so a drop while travelling
// trips the combat-interrupt hook (actual engage/flee/recall are TODO).
// ---------------------------------------------------------------------------
void Client::OnMobileHp(const u8* data, usize size) {
    if (size < 9) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    if (serial != playerSerial_) return;
    const i32 curHp = static_cast<i32>(LoadBE16(data + 7));
    if (player_.hpCur >= 0 && curHp < player_.hpCur &&
        (nav_.bot.active || !nav_.movement.pending.empty())) {
        char reason[48];
        std::snprintf(reason, sizeof(reason), "HP %d -> %d", player_.hpCur, curHp);
        BotInterruptForThreat(reason);
    }
    player_.serial = serial;
    player_.hpCur = curHp;
    player_.hpMax = static_cast<i32>(LoadBE16(data + 5));
}

void Client::OnMobileMana(const u8* data, usize size) {
    if (size < 9) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    if (serial != playerSerial_) return;
    player_.serial = serial;
    player_.manaMax = static_cast<i32>(LoadBE16(data + 5));
    player_.manaCur = static_cast<i32>(LoadBE16(data + 7));
}

void Client::OnMobileStamina(const u8* data, usize size) {
    if (size < 9) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    if (serial != playerSerial_) return;
    player_.serial = serial;
    player_.stamMax = static_cast<i32>(LoadBE16(data + 5));
    player_.stamCur = static_cast<i32>(LoadBE16(data + 7));
}

// 0x2D Mob Attributes: cmd, serial, hitsMax/hitsCur, manaMax/manaCur,
// stamMax/stamCur. For the local player this is a compact stat refresh.
void Client::OnMobileAttributes(const u8* data, usize size) {
    if (size < 17) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    if (serial != playerSerial_) return;
    player_.serial = serial;
    player_.hpMax = static_cast<i32>(LoadBE16(data + 5));
    player_.hpCur = static_cast<i32>(LoadBE16(data + 7));
    player_.manaMax = static_cast<i32>(LoadBE16(data + 9));
    player_.manaCur = static_cast<i32>(LoadBE16(data + 11));
    player_.stamMax = static_cast<i32>(LoadBE16(data + 13));
    player_.stamCur = static_cast<i32>(LoadBE16(data + 15));
}

// ---------------------------------------------------------------------------
// 0x1A Object Info (variable). Layout from PacketManager_MakePacket_MOVE:
//   serial(4 BE)  [bit 0x80000000 -> amount field present]
//   graphic(2 BE) [bit 0x8000 stackable, 0x4000 multi]
//   [stackable] stack(1)   [amount present] amount(2)
//   x(2 BE) [bit 0x8000 -> direction byte present]
//   y(2 BE) [bit 0x8000 hue present, 0x4000 status flags present]
//   [dir present] dir(1)   z(1)   [hue] hue(2)   [flags] flags(1)
// ---------------------------------------------------------------------------
void Client::OnObjectInfo(const u8* data, usize size) {
    usize p = 3;  // skip cmd + length
    auto avail = [&](usize n) { return p + n <= size; };

    if (!avail(4)) return;
    const u32 serialRaw = LoadBE32(data + p); p += 4;
    const bool hasAmount = (serialRaw & 0x80000000u) != 0;
    const u32 serial = serialRaw & 0x7FFFFFFFu;

    if (!avail(2)) return;
    const u16 g = LoadBE16(data + p); p += 2;
    // graphic high bit => a graphic-increment byte (itemIdOffset) follows
    // immediately; it selects a door's open/closed frame (drawn = id + offset).
    const bool hasOffset = (g & 0x8000) != 0;
    const u16 itemId = static_cast<u16>(g & 0x3FFF);

    u8 gfxOffset = 0;
    if (hasOffset) { if (!avail(1)) return; gfxOffset = data[p]; p += 1; }
    if (hasAmount) { if (!avail(2)) return; p += 2; }   // stack count

    if (!avail(2)) return;
    const u16 xw = LoadBE16(data + p); p += 2;
    const bool hasDir = (xw & 0x8000) != 0;
    const i32 x = xw & 0x7FFF;

    if (!avail(2)) return;
    const u16 yw = LoadBE16(data + p); p += 2;
    const i32 y = yw & 0x3FFF;

    // direction (facing) byte — present when x has 0x8000. It is the entity's
    // facing, NOT a graphic offset, so it must be skipped here, not added to
    // the art (adding it is what turned lamps into logs).
    if (hasDir) { if (!avail(1)) return; p += 1; }
    if (!avail(1)) return;
    const i8 z = static_cast<i8>(data[p]);

    // Track every world item so the renderer can draw dynamic server objects
    // (lamp posts, doors, decor). Keyed by serial; removed on 0x1D.
    const bool isNewItem = items_.find(serial) == items_.end();
    items_[serial] = ItemObj{itemId, x, y, z, gfxOffset};
    if (isNewItem) itemOrder_.push_back(serial);
    while (items_.size() > kMaxItemCache && !itemOrder_.empty()) {
        const u32 oldSerial = itemOrder_.front();
        itemOrder_.pop_front();
        items_.erase(oldSerial);
    }

}

// 0x1D Delete Object (5 bytes): cmd + serial(4 BE). Drop it from both caches.
void Client::OnDeleteObject(const u8* data, usize size) {
    if (size < 5) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    items_.erase(serial);
    itemOrder_.erase(std::remove(itemOrder_.begin(), itemOrder_.end(), serial),
                     itemOrder_.end());
    for (auto it = mobileCache_.begin(); it != mobileCache_.end(); ++it) {
        if (it->serial == serial) { mobileCache_.erase(it); break; }
    }
    mobileNames_.erase(serial);
}

const char* Client::MobileName(u32 serial) const {
    auto it = mobileNames_.find(serial);
    return (it == mobileNames_.end()) ? nullptr : it->second.c_str();
}

u32 Client::ResolveFollowSerialByName(const char* name) const {
    if (!name || !name[0]) return 0;
    auto lower = [](const std::string& s) {
        std::string out = s;
        for (char& ch : out)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return out;
    };
    const std::string want = lower(name);

    // Prefer exact case-insensitive match; if several exist, pick nearest.
    u32 bestSerial = 0;
    i32 bestDist = 0x7FFFFFFF;
    for (const auto& m : mobileCache_) {
        const char* nm = MobileName(m.serial);
        if (!nm || !nm[0]) continue;
        if (lower(nm) != want) continue;
        const i32 dx = (m.x > playerX_) ? (m.x - playerX_) : (playerX_ - m.x);
        const i32 dy = (m.y > playerY_) ? (m.y - playerY_) : (playerY_ - m.y);
        const i32 dist = (dx > dy) ? dx : dy;
        if (dist < bestDist) {
            bestDist = dist;
            bestSerial = m.serial;
        }
    }
    return bestSerial;
}

void Client::RememberMobileName(u32 serial, const char* name) {
    if (serial == 0 || !name || !name[0]) return;
    mobileNames_[serial] = name;
}

void Client::RememberJournalMessage(u32 sourceSerial, u16 sourceBody, u8 type,
                                    u16 hue, u16 font, const char* speaker,
                                    const char* text) {
    JournalEntry e{};
    e.timeMs = NowMs();
    e.sourceSerial = sourceSerial;
    e.sourceBody = sourceBody;
    e.type = type;
    e.hue = hue;
    e.font = font;
    e.ownerKind = JournalOwnerKind::Unknown;
    e.hasPosition = false;
    e.x = 0; e.y = 0; e.z = 0;
    e.speaker = speaker ? speaker : "";
    e.text = text ? text : "";

    const u32 serial = sourceSerial & 0x7FFFFFFFu;
    if (sourceSerial == 0xFFFFFFFFu || sourceSerial == 0) {
        e.ownerKind = JournalOwnerKind::System;
    } else if (serial == playerSerial_) {
        e.ownerKind = JournalOwnerKind::Player;
        e.hasPosition = true;
        e.x = playerX_;
        e.y = playerY_;
        e.z = playerZ_;
    } else if (const MobileObj* m = FindMobileBySerial(serial)) {
        e.ownerKind = JournalOwnerKind::Mobile;
        e.hasPosition = true;
        e.x = m->x;
        e.y = m->y;
        e.z = m->z;
    } else if (auto it = items_.find(serial); it != items_.end()) {
        e.ownerKind = JournalOwnerKind::Item;
        e.hasPosition = true;
        e.x = it->second.x;
        e.y = it->second.y;
        e.z = it->second.z;
    }

    journal_.push_back(std::move(e));
    while (journal_.size() > kMaxJournalEntries)
        journal_.pop_front();
}

void Client::UpdateMobile(u32 serial, i32 x, i32 y, i8 z, u8 dir, u16 body) {
    if (serial == playerSerial_) {
        // Don't treat ourselves as an obstacle, but do learn our own body so
        // the renderer can draw the local player (and facing for arrow walk).
        if (body) playerBody_ = body;
        playerFacing_ = static_cast<u8>(dir & 0x07);
        player_.serial = serial;
        if (body) player_.body = body;
        player_.x = x;
        player_.y = y;
        player_.z = z;
        player_.facing = playerFacing_;
        player_.running = false;
        return;
    }
    const i64 now = NowMs();
    for (auto& m : mobileCache_) {
        if (m.serial == serial) {
            m.x = x;
            m.y = y;
            m.z = z;
            m.dir = static_cast<u8>(dir & 0x07);
            if (body) m.body = body;
            m.seenMs = now;
            return;
        }
    }
    if (mobileCache_.size() >= kMobileCacheMax) mobileCache_.pop_front();
    mobileCache_.push_back({serial, x, y, z, static_cast<u8>(dir & 0x07), body, now});
}

// 0x77 Mobile Move (17 bytes): cmd, serial(4), body(2), x(2), y(2), z(1), dir(1) ...
void Client::OnMobileMove(const u8* data, usize size) {
    if (size < 13) return;
    UpdateMobile(LoadBE32(data + 1), LoadBE16(data + 7), LoadBE16(data + 9),
                 static_cast<i8>(data[11]), data[12], LoadBE16(data + 5));
}

// 0x78 Mobile Incoming (variable): cmd, len(2), serial(4), body(2), x(2),
// y(2), z(1), dir(1), ... (equipment list follows; we only need position).
void Client::OnMobileIncoming(const u8* data, usize size) {
    if (size < 15) return;
    UpdateMobile(LoadBE32(data + 3), LoadBE16(data + 9), LoadBE16(data + 11),
                 static_cast<i8>(data[13]), data[14], LoadBE16(data + 7));
}

// 0x98 AllNames / MobName reply:
//   [0] cmd
//   [1-2] len (BE) (usually 0x23)
//   [3-6] serial (BE)
//   [7-36] name (ASCII, NUL-padded)
void Client::OnMobName(const u8* data, usize size) {
    if (size < 37) return;
    const u32 serial = LoadBE32(data + 3) & 0x7FFFFFFFu;
    char name[31];
    std::memcpy(name, data + 7, 30);
    name[30] = '\0';
    if (name[0]) {
        RememberMobileName(serial, name);
        if (verboseConsole_)
            LogInfo("[0x98] name 0x%08X = %s\n", serial, name);
    }
    if (mobilesListPending_) {
        mobilesListAwaiting_.erase(serial);
        if (mobilesListAwaiting_.empty()) FlushPendingMobilesList();
    }
}

const Client::MobileObj* Client::FindMobileAt(i32 x, i32 y, i8 z) const {
    for (const auto& m : mobileCache_) {
        if (m.x != x || m.y != y) continue;
        i32 dz = static_cast<i32>(z) - static_cast<i32>(m.z);
        if (dz < 0) dz = -dz;
        if (dz <= 8) return &m;   // same floor
    }
    return nullptr;
}

const Client::MobileObj* Client::FindMobileBySerial(u32 serial) const {
    for (const auto& m : mobileCache_) {
        if (m.serial == serial) return &m;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 0x73 Server Ping (2 bytes: cmd + sequence). Original handler is
// nullsub_2 (silently consumed). Many shards still expect an echo to
// keep the connection alive, so we echo. Sequence byte is forwarded.
// ---------------------------------------------------------------------------
void Client::OnPing(const u8* data, usize size) {
    if (size < 2) return;
    u8 buf[2];
    usize n = build::PingReply(buf, data[1]);
    Send(buf, n, "0x73 PingReply");
}

// ---------------------------------------------------------------------------
// 0x11 Stats — we just print HP/Mana/Stam summary for sanity.
// Layout (post-AOS variant supported by the original 2.0.7 binary, fields
// the new client cares about):
//   [0]    cmd
//   [1-2]  length (BE)
//   [3-6]  serial (BE)
//   [7-36] name (30 ASCII)
//   [37-38] HP cur, [39-40] HP max  (BE), then flags and extended stats.
// ---------------------------------------------------------------------------
void Client::OnStats(const u8* data, usize size) {
    if (size < 41) return;
    const u32 serial = LoadBE32(data + 3) & 0x7FFFFFFFu;
    if (playerSerial_ != 0 && serial != playerSerial_) return;
    player_.serial = serial;
    playerSerial_ = serial;
    player_.name = PacketString(data + 7, 30);
    player_.hpCur = static_cast<i32>(LoadBE16(data + 37));
    player_.hpMax = static_cast<i32>(LoadBE16(data + 39));

    if (size >= 66) {
        player_.nameChangeFlag = data[41];
        player_.statusFlag = data[42];
        player_.sexRace = data[43];
        player_.strength = static_cast<i32>(LoadBE16(data + 44));
        player_.dexterity = static_cast<i32>(LoadBE16(data + 46));
        player_.intelligence = static_cast<i32>(LoadBE16(data + 48));
        player_.stamCur = static_cast<i32>(LoadBE16(data + 50));
        player_.stamMax = static_cast<i32>(LoadBE16(data + 52));
        player_.manaCur = static_cast<i32>(LoadBE16(data + 54));
        player_.manaMax = static_cast<i32>(LoadBE16(data + 56));
        player_.gold = static_cast<i32>(LoadBE32(data + 58));
        player_.armor = static_cast<i32>(LoadBE16(data + 62));
        player_.weight = static_cast<i32>(LoadBE16(data + 64));
    }

    usize p = 66;
    if (player_.statusFlag >= 5 && p + 3 <= size) {
        player_.maxWeight = static_cast<i32>(LoadBE16(data + p)); p += 2;
        player_.race = data[p++];
    }
    if (player_.statusFlag >= 3 && p + 4 <= size) {
        player_.statsCap = static_cast<i32>(LoadBE16(data + p)); p += 2;
        player_.followers = data[p++];
        player_.followersMax = data[p++];
    }

    if (verboseConsole_)
        LogInfo("[0x11] %s  HP=%d/%d STR=%d DEX=%d INT=%d STAM=%d/%d MANA=%d/%d\n",
                player_.name.c_str(), player_.hpCur, player_.hpMax,
                player_.strength, player_.dexterity, player_.intelligence,
                player_.stamCur, player_.stamMax, player_.manaCur, player_.manaMax);
}

// 0x3A Send Skills. Server sends either a full skill table or a single skill
// update. Values are fixed-point tenths (e.g. 512 == 51.2).
void Client::OnSkills(const u8* data, usize size) {
    if (size < 4) return;
    const u8 type = data[3];
    usize p = 4;
    const bool hasCap = (type == 0x02 || type == 0xDF);
    const bool single = (type == 0xFF || type == 0xDF);

    auto readOne = [&]() -> bool {
        const usize need = hasCap ? 9u : 7u;
        if (p + need > size) return false;
        const u16 id = LoadBE16(data + p); p += 2;
        if (!single && id == 0) return false;
        PlayerSkill skill{};
        skill.id = id;
        skill.valueTenths = LoadBE16(data + p); p += 2;
        skill.baseTenths = LoadBE16(data + p); p += 2;
        skill.lock = data[p++];
        skill.hasCap = hasCap;
        skill.capTenths = 0;
        if (hasCap) {
            skill.capTenths = LoadBE16(data + p);
            p += 2;
        }
        player_.skills[id] = skill;
        return true;
    };

    if (type == 0x00 || type == 0x02) {
        while (readOne()) {}
    } else if (single) {
        readOne();
    }

    if (verboseConsole_)
        LogInfo("[0x3A] skills update type=0x%02X known=%zu\n",
                type, player_.skills.size());
}

// ---------------------------------------------------------------------------
// 0x1C ASCII Message (variable):
//   [0]    cmd
//   [1-2]  length (BE)
//   [3-6]  source serial
//   [7-8]  source body
//   [9]    type
//   [10-11] hue (BE)
//   [12-13] font (BE)
//   [14-43] speaker name (30 ASCII)
//   [44+]  text (NUL-terminated ASCII, up to end of packet)
// ---------------------------------------------------------------------------
void Client::OnAsciiMessage(const u8* data, usize size) {
    if (size < 45) return;
    const u32 sourceSerial = LoadBE32(data + 3);
    const u16 sourceBody = LoadBE16(data + 7);
    const u8 type = data[9];
    const u16 hue = LoadBE16(data + 10);
    const u16 font = LoadBE16(data + 12);
    const std::string speaker = PacketString(data + 14, 30);
    if (sourceSerial != 0 && sourceSerial != 0xFFFFFFFFu)
        RememberMobileName(sourceSerial & 0x7FFFFFFFu, speaker.c_str());
    const std::string text = PacketString(data + 44, size - 44);
    RememberJournalMessage(sourceSerial, sourceBody, type, hue, font,
                           speaker.c_str(), text.c_str());
    LogInfo("[chat ascii] %s: %s\n", speaker.c_str(), text.c_str());

    // Stamina signal: the server denies movement and says "too fatigued to
    // move" when stamina is spent. Record it so a reject right after is
    // treated as fatigue (wait to regen), not as an obstacle to avoid.
    if (std::strstr(text.c_str(), "fatigued")) {
        BotNoteFatigueMessage();
        LogInfo("[bot] fatigue detected; rejects will wait for stamina regen\n");
    }
}

// ---------------------------------------------------------------------------
// 0xAE Unicode Message (variable). Speaker name at offset 14 ASCII,
// text at offset 48 as UTF-16BE (NUL-terminated). For the M1 log we
// degrade UTF-16 to ASCII (best-effort) so the console output stays
// readable.
// ---------------------------------------------------------------------------
void Client::OnUnicodeMessage(const u8* data, usize size) {
    if (size < 50) return;
    const u32 sourceSerial = LoadBE32(data + 3);
    const u16 sourceBody = LoadBE16(data + 7);
    const u8 type = data[9];
    const u16 hue = LoadBE16(data + 10);
    const u16 font = LoadBE16(data + 12);
    const std::string speaker = PacketString(data + 14, 30);
    if (sourceSerial != 0 && sourceSerial != 0xFFFFFFFFu)
        RememberMobileName(sourceSerial & 0x7FFFFFFFu, speaker.c_str());

    char buf[256];
    usize n = 0;
    for (usize i = 48; i + 1 < size && n + 1 < sizeof(buf); i += 2) {
        u16 ch = LoadBE16(data + i);
        if (ch == 0) break;
        buf[n++] = (ch < 0x80) ? static_cast<char>(ch) : '?';
    }
    buf[n] = '\0';
    RememberJournalMessage(sourceSerial, sourceBody, type, hue, font,
                           speaker.c_str(), buf);
    LogInfo("[chat uni  ] %s: %s\n", speaker.c_str(), buf);
}

void Client::OnUnknown(const u8* data, usize size) {
    if (!verboseConsole_) return;  // every packet is in the JSON log already
    LogWarn(
        "[?] unhandled packet id=0x%02X len=%zu in state=%s\n",
        data[0], size, StateName(static_cast<int>(state_)));
}

// ---------------------------------------------------------------------------
// 0x20 Draw Game Player (19 bytes). Full local-player update; fires on
// teleport, server-side resync, after 0x55 entering world.
//   [0]    cmd
//   [1-4]  serial
//   [5-6]  body
//   [7]    unused
//   [8-9]  skin/hue
//   [10]   flags
//   [11-12] x BE
//   [13-14] y BE
//   [15-16] server id
//   [17]   direction
//   [18]   z (signed)
// ---------------------------------------------------------------------------
void Client::OnDrawGamePlayer(const u8* data, usize size) {
    if (size < 19) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    const u16 body    = LoadBE16(data + 5);
    const u16 x      = LoadBE16(data + 11);
    const u16 y      = LoadBE16(data + 13);
    const u8  dir    = data[17];
    const i8  z      = static_cast<i8>(data[18]);
    if (serial == playerSerial_) {
        playerX_ = static_cast<i32>(x);
        playerY_ = static_cast<i32>(y);
        playerZ_ = z;
        playerFacing_  = dir & 0x07;
        playerRunning_ = false;
        playerBody_ = body;
        player_.serial = serial;
        player_.body = body;
        player_.x = playerX_;
        player_.y = playerY_;
        player_.z = playerZ_;
        player_.facing = playerFacing_;
        player_.running = playerRunning_;
        // A full resync (teleport / server correction) invalidates every
        // predicted-but-unacked move and any path planned off the old pose.
        if (!nav_.movement.pending.empty() || nav_.bot.active || !nav_.bot.path.empty()) {
            BotResetMovement();
            if (nav_.bot.active || !nav_.bot.path.empty()) {
                LogWarn( "[bot] 0x20 resync; aborting path\n");
                BotAbortPath("0x20 resync");
            }
        }
        LogInfo("[0x20] player @(%d,%d,%d) facing=%u\n",
                    playerX_, playerY_, static_cast<int>(playerZ_),
                    playerFacing_);
    }
}
// ---------------------------------------------------------------------------
// Console UI
// ---------------------------------------------------------------------------
int Client::PromptServerSelection() {
    int sel = -1;
    LogInfo("> select server [0..%d]: ", serverCount_ - 1);
    std::fflush(stdout);
    if (!(std::cin >> sel)) return -1;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return sel;
}

int Client::PromptCharacterSelection() {
    int sel = -1;
    LogInfo("> select character [0..%d]: ", charCount_ - 1);
    std::fflush(stdout);
    if (!(std::cin >> sel)) return -1;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return sel;
}

// ---------------------------------------------------------------------------
// Stdin reader thread — pushes whole lines into stdin_lines_. Main loop
// drains the queue and sends each as 0x03 ASCII speech.
// ---------------------------------------------------------------------------
void Client::StartStdinThread() {
    if (stdin_thread_.joinable()) return;
    stop_stdin_ = false;
    stdin_thread_ = std::thread([this]() {
        std::string line;
        while (!stop_stdin_.load()) {
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;
            std::lock_guard<std::mutex> lk(stdin_mtx_);
            stdin_lines_.push(std::move(line));
            line.clear();
        }
    });
}

void Client::StopStdinThread() {
    stop_stdin_ = true;
    if (stdin_thread_.joinable()) {
        // We can't really interrupt a blocking getline on Windows
        // cleanly; let the OS reap it on process exit.
        stdin_thread_.detach();
    }
}

void Client::PumpStdinCommand() {
    std::string line;
    {
        std::lock_guard<std::mutex> lk(stdin_mtx_);
        if (stdin_lines_.empty()) return;
        line = std::move(stdin_lines_.front());
        stdin_lines_.pop();
    }
    HandleStdinLine(line.c_str());
}

bool Client::ParseSerial(const char* text, u32* out) const {
    if (!text || !*text || !out) return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (!*text) return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text, &end, 0);
    if (end == text) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0' || v > 0xFFFFFFFFull) return false;
    *out = static_cast<u32>(v);
    return true;
}

bool Client::ParseDistance(const char* text, u32* out) const {
    if (!text || !*text || !out) return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (!*text) return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text, &end, 10);
    if (end == text) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0' || v == 0 || v > 32ull) return false;
    *out = static_cast<u32>(v);
    return true;
}

void Client::PrintNearbyMobiles() {
    if (mobilesListPending_) {
        mobilesListPending_ = false;
        mobilesListSerials_.clear();
        mobilesListAwaiting_.clear();
    }
    if (mobileCache_.empty()) {
        LogInfo("[mobiles] no mobiles cached yet\n");
        return;
    }
    struct Row {
        const MobileObj* m;
        i32 dist;
    };
    std::vector<Row> rows;
    rows.reserve(mobileCache_.size());
    for (const auto& m : mobileCache_) {
        const i32 dx = (m.x > playerX_) ? (m.x - playerX_) : (playerX_ - m.x);
        const i32 dy = (m.y > playerY_) ? (m.y - playerY_) : (playerY_ - m.y);
        const i32 dist = (dx > dy) ? dx : dy;
        if (dist > 18) continue;  // active visual range vicinity
        rows.push_back({&m, dist});
    }
    if (rows.empty()) {
        LogInfo("[mobiles] no mobiles in range\n");
        return;
    }

    mobilesListPending_ = true;
    mobilesListDeadlineMs_ = NowMs() + kMobilesNamesTimeoutMs;
    mobilesListSerials_.clear();
    mobilesListAwaiting_.clear();

    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.dist < b.dist; });

    // Ask the shard for every nearby mobile name first (0x98 AllNames query).
    for (const auto& row : rows) {
        mobilesListSerials_.push_back(row.m->serial);
        mobilesListAwaiting_.insert(row.m->serial);
        u8 pkt[8];
        const usize n = build::MobNameQuery(pkt, row.m->serial);
        Send(pkt, n, "0x98 AllNames (mob name query)");
    }
}

void Client::FlushPendingMobilesList() {
    if (!mobilesListPending_) return;
    mobilesListPending_ = false;
    LogInfo("[mobiles] nearby (%zu):\n", mobilesListSerials_.size());
    for (u32 serial : mobilesListSerials_) {
        const char* name = MobileName(serial);
        LogInfo("  %s 0x%08X\n", name ? name : "<unknown>", serial);
    }
    mobilesListSerials_.clear();
    mobilesListAwaiting_.clear();
}

void Client::HandleStdinLine(const char* line) {
    if (!line || !line[0]) return;

    // `goto X Y [Z]` — kick the A* bot toward (X, Y), optionally pinning the
    // destination floor Z (for columns walkable at several levels). Accepts
    // comma and/or space separators: `goto x,y`, `goto x y z`, `goto x,y,z`.
    if (std::strncmp(line, "goto", 4) == 0 &&
        (line[4] == ' ' || line[4] == '\t' || line[4] == ',')) {
        char args[128];
        std::strncpy(args, line + 4, sizeof(args) - 1);
        args[sizeof(args) - 1] = '\0';
        for (char* p = args; *p; ++p) if (*p == ',') *p = ' ';
        i32 tx = 0, ty = 0, tz = 0;
        const int got = std::sscanf(args, "%d %d %d", &tx, &ty, &tz);
        if (got >= 2) {
            BotStartGoto(tx, ty, got >= 3, tz);
        } else {
            LogWarn(
                "[cmd] usage: goto <x> <y> [z]  (comma or space separated)\n");
        }
        return;
    }

    // `stop` — drop the current path.
    if (std::strcmp(line, "stop") == 0) {
        if (nav_.follow.active) BotStopFollow("stopped by user");
        if (!nav_.bot.path.empty() || nav_.bot.planning) {
            LogInfo("[bot] path cleared (%zu steps left, planning=%s)\n",
                        nav_.bot.path.size(), nav_.bot.planning ? "yes" : "no");
        }
        BotAbortPath("stopped by user");
        return;
    }

    // `mobiles` — list nearby mobiles as `name serialId`.
    if (std::strcmp(line, "mobiles") == 0) {
        PrintNearbyMobiles();
        return;
    }

    // `follow <name|0xserial> [distance]` — follow mobile by name or serial.
    if (std::strncmp(line, "follow", 6) == 0 &&
        (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        const char* arg = line + 6;
        while (*arg == ' ' || *arg == '\t') ++arg;
        if (std::strcmp(arg, "off") == 0 || std::strcmp(arg, "stop") == 0) {
            BotStopFollow("follow disabled");
            return;
        }
        char args[160];
        std::strncpy(args, arg, sizeof(args) - 1);
        args[sizeof(args) - 1] = '\0';

        // Optional trailing distance: `follow <target> <distance>`.
        u32 followDist = 1;
        char* end = args + std::strlen(args);
        while (end > args && (end[-1] == ' ' || end[-1] == '\t')) --end;
        *end = '\0';
        char* tok = end;
        while (tok > args && tok[-1] != ' ' && tok[-1] != '\t') --tok;
        if (tok > args) {
            u32 parsedDist = 0;
            if (ParseDistance(tok, &parsedDist)) {
                followDist = parsedDist;
                char* cut = tok;
                while (cut > args && (cut[-1] == ' ' || cut[-1] == '\t')) --cut;
                *cut = '\0';
            }
        }
        char* target = args;
        while (*target == ' ' || *target == '\t') ++target;
        if (!target[0]) {
            LogWarn( "[cmd] usage: follow <name|0xserial> [distance]|off\n");
            return;
        }

        u32 serial = 0;
        if ((target[0] == '0') && (target[1] == 'x' || target[1] == 'X')) {
            if (!ParseSerial(target, &serial) || serial == 0) {
                LogWarn( "[cmd] invalid serial: %s\n", target);
                return;
            }
        } else {
            serial = ResolveFollowSerialByName(target);
            if (serial == 0) {
                LogWarn(
                    "[cmd] mobile '%s' not found in cache; run `mobiles` and retry\n", target);
                return;
            }
        }
        BotStartFollow(serial & 0x7FFFFFFFu, followDist);
        return;
    }

    // `pos` — print current position.
    if (std::strcmp(line, "pos") == 0) {
        LogInfo("[pos] (%d,%d,%d) facing=%u%s\n",
                    playerX_, playerY_, static_cast<int>(playerZ_),
                    playerFacing_, playerRunning_ ? " run" : "");
        return;
    }

    // `verbose [on|off]` — toggle the per-packet console chatter (the JSON
    // log keeps everything regardless). No argument flips the current state.
    if (std::strncmp(line, "verbose", 7) == 0 &&
        (line[7] == '\0' || line[7] == ' ')) {
        const char* arg = line + 7;
        while (*arg == ' ') ++arg;
        if (std::strcmp(arg, "on") == 0)       verboseConsole_ = true;
        else if (std::strcmp(arg, "off") == 0) verboseConsole_ = false;
        else                                   verboseConsole_ = !verboseConsole_;
        LogInfo("[cmd] verbose console %s\n", verboseConsole_ ? "ON" : "off");
        return;
    }

    // Otherwise: speak it.
    u8 buf[512];
    usize n = build::SpeechAscii(buf,
                                 /*type=*/0x00,
                                 /*hue=*/0x0040,
                                 /*font=*/0x0003,
                                 line);
    if (n > sizeof(buf)) return;
    Send(buf, n, "0x03 SpeechAscii");
}
i64 Client::NowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
