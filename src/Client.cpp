#include "Client.h"

#include "bot/Pathfinding.h"
#include "uo/builders.h"
#include "uo/endian.h"
#include "uo/map.h"
#include "uo/packet_ids.h"
#include "uo/packet_lengths.h"
#include "uo/tiledata.h"
#include "uo/world.h"
#include "uo/art.h"
#include "uo/texmap.h"
#include "render/Renderer.h"
#include "win32/MiniFB.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <iostream>
#include <limits>

#include <winsock2.h>

namespace uo {

namespace {

// Depth-1 movement: this server's 0x22 ack carries no position, so deeper
// pipelines desync when a mid-flight move is rejected (we'd never learn where
// the surviving moves left us). One confirmed step at a time stays exact.
constexpr usize kMaxInFlight = 1;
// Per-trip A* replan budget — bounds obstacle-avoidance loops on an
// unreachable goal.
constexpr u32 kMaxReplans = 128;
// Extra A* cost for stepping onto open grass (vs the 10/14 straight/diag
// base) — biases travel toward roads/dirt where mobs are sparser.
constexpr u32 kGrassPenalty = 32;
// Extra A* cost for stepping into woods (a cell in/next to foliage). Heavier
// than open grass so the bot skirts forests instead of threading the trees.
constexpr u32 kForestPenalty = 64;
// Recent-doors cache size, how many open attempts to make at one cell before
// concluding it's not an openable door, and how long to wait after an open
// command for the door to actually swing (doors are NOT instantaneous).
constexpr usize kDoorCacheMax = 20;
constexpr u32   kMaxDoorTries = 4;     // blind open attempts before avoiding
constexpr u32   kMaxDoorGiveUp = 10;   // attempts with a door present -> stop trip
constexpr i64   kDoorWaitMs   = 700;
// Mobile cache + stamina/shove handling: a tile holding a mobile, or a reject
// right after a "too fatigued" message, is never blacklisted — we wait and
// retry (the mob moves, or stamina regenerates and the shove succeeds).
constexpr usize kMobileCacheMax = 64;
constexpr i64   kFatigueWindowMs = 1500;  // a reject this soon after a fatigue msg = stamina
constexpr i64   kStaminaWaitMs   = 2000;  // let stamina regen before retrying
constexpr i64   kMobileWaitMs    = 100;   // let the mobile step aside / shove cooldown
constexpr u32   kMobileRepathAfter = 15;  // after N mobile bumps, reroute around it
constexpr u32   kMaxStuckWaits   = 25;    // give up the trip (no blacklist) after this
constexpr i64   kMobilesNamesTimeoutMs = 500;
constexpr i64   kFollowReplanMinMs = 120;
constexpr i64   kFollowProbeMs     = 1200;
// Canonical openable door graphics (wood/metal/barred/gates, closed+open
// states). Tunable; covers town/building doors a traveller meets.
bool IsDoorGraphic(u16 id) { return id >= 0x0675 && id <= 0x06F6; }

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
      playerSerial_(0),
      lastHp_(-1),
      playerX_(0), playerY_(0), playerZ_(0),
      playerFacing_(0), playerRunning_(false),
      moveSeq_(0),
      botRun_(true),
      worldLoaded_(false),
      renderInit_(false), renderWindowOpen_(false),
      botGoalX_(0), botGoalY_(0), botGoalZ_(0), botHasGoalZ_(false),
      botActive_(false),
      botReplanCount_(0), botResumeAtMs_(0),
      rng_(static_cast<u32>(
          std::chrono::steady_clock::now().time_since_epoch().count())),
      doorCellX_(0), doorCellY_(0), doorCellZ_(0),
      doorAttempts_(0), awaitingDoorOpen_(false),
      followActive_(false), followSerial_(0), followDistance_(1),
      followLastReplanMs_(0), followLastProbeMs_(0),
      mobilesListPending_(false), mobilesListDeadlineMs_(0),
      lastFatigueMs_(0), stuckWaits_(0),
      lastMoveSentMs_(0),
      // Canonical UO on-foot step intervals — what the real client's
      // auto-walk pathfinding emits. Pacing at these rates (not faster)
      // is exactly how a legit client avoids tripping fastwalk/speedhack
      // checks; the server grants one fastwalk key per step at this cadence.
      //   foot walk = 400ms, foot run = 200ms (mounted is half each).
      walkThrottleMs_(400), runThrottleMs_(200),
      ackWatchdogMs_(5000),
      verboseConsole_(false),
      stop_stdin_(false),
      movesSinceClick_(0),
      lastActivityMs_(0),
      // UO Demo / Sphere-style shards kick the connection after ~60s
      // of client-side silence. Stay well inside the window: 20s gap.
      keepaliveIntervalMs_(20000) {
    std::memset(servers_, 0, sizeof(servers_));
    std::memset(charSlots_, 0, sizeof(charSlots_));
}

Client::~Client() {
    if (renderWindowOpen_) { mfb_close(); renderWindowOpen_ = false; }
    StopStdinThread();
    sock_.Close();
    logger_.Close();
    net::Socket::WSACleanupOnce();
}

int Client::Run() {
    if (!net::Socket::WSAStart()) return 1;

    if (cfg_.logFile && cfg_.logFile[0]) {
        if (!logger_.Open(cfg_.logFile)) {
            std::fprintf(stderr, "warning: cannot open log file '%s'\n", cfg_.logFile);
        } else {
            // Verbose from the start so the JSONL captures the full
            // handshake (seed → 0x80 → 0xA8 → 0xA0 → 0x8C → 0x91 → 0xA9
            // → 0x5D → 0x1B → 0x55). M3+ may flip it off until 0x55
            // for noise reduction, but for protocol bring-up we want
            // every byte.
            logger_.EnableVerbose();
            logger_.Event("session_start", "verbose log enabled from start");
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
    logger_.Event("net_connect_begin", ev);
    std::printf("[net] connecting to %s:%u ...\n", host, port);
    if (!sock_.Connect(host, port)) {
        logger_.Event("net_connect_fail", ev);
        state_ = State::Failed;
        return false;
    }
    logger_.Event("net_connected", ev);
    std::printf("[net] connected.\n");

    if (cfg_.sendSeed) {
        u8 seedbuf[4];
        build::Seed(seedbuf, cfg_.plaintextSeed);
        std::printf("[seed] sent 0x%08X (plaintext)\n", cfg_.plaintextSeed);
        // Seed is 4 raw bytes (not a UO packet) — log as event so the
        // JSONL captures the actual wire bytes.
        char hexbuf[20];
        std::snprintf(hexbuf, sizeof(hexbuf), "%02x%02x%02x%02x",
                      seedbuf[0], seedbuf[1], seedbuf[2], seedbuf[3]);
        char detail[64];
        std::snprintf(detail, sizeof(detail),
                      "0x%08X hex=%s", cfg_.plaintextSeed, hexbuf);
        logger_.Event("seed_out", detail);
        if (!sock_.SendAll(seedbuf, sizeof(seedbuf))) {
            logger_.Event("seed_out_failed", detail);
            state_ = State::Failed;
            return false;
        }
    } else {
        logger_.Event("seed_skipped", "cfg_.sendSeed=false");
        std::printf("[seed] skipped (cfg_.sendSeed=false)\n");
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
        logger_.Event("send_failed", detail);
        return false;
    }
    logger_.Log(Direction::Out, data, size, note);
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
            std::fprintf(stderr,
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
            std::fprintf(stderr, "[net] select error; bailing\n");
            return false;
        }
        if (rd > 0) {
            int n = sock_.RecvSome(rxbuf, sizeof(rxbuf));
            if (n < 0) {
                std::fprintf(stderr, "[net] socket closed by peer.\n");
                logger_.Event("disconnect", "recv returned -1 (RST or FIN)");
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
                        std::fprintf(stderr, "[huffman] malformed compressed stream\n");
                        logger_.Event("huffman_error", "malformed code in game stream");
                        return false;
                    }
                    feed = rxScratch_.data();
                    feed_n = rxScratch_.size();
                    if (feed_n == 0) continue;  // partial block; need more bytes
                }
                if (!stream_.FeedBytes(feed, feed_n)) {
                    std::fprintf(stderr, "[net] stream buffer overflow\n");
                    return false;
                }
                for (;;) {
                    const u8* pkt = nullptr;
                    usize pkt_size = 0;
                    const char* err = nullptr;
                    if (!stream_.TryNext(&pkt, &pkt_size, &err)) {
                        if (err) {
                            std::fprintf(stderr, "[stream] %s (pending=%zu)\n",
                                         err, stream_.Pending());
                            char detail[128];
                            std::snprintf(detail, sizeof(detail),
                                "%s (pending=%zu)", err, stream_.Pending());
                            logger_.Event("stream_error", detail);
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
                now_ms2 - lastActivityMs_ > static_cast<i64>(keepaliveIntervalMs_)) {
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
    logger_.Log(Direction::In, data, size);

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
        case 0x1A: OnObjectInfo(data, size); break;
        case 0x1D: OnDeleteObject(data, size); break;
        case 0x77: OnMobileMove(data, size); break;
        case 0x78: OnMobileIncoming(data, size); break;
        case 0x98: OnMobName(data, size); break;

        // Common in-world packets we just log + ignore for M1.
        case 0x23: case 0x25: case 0x2D: case 0x2E: case 0x2F:
        case 0x3A: case 0x3C: case 0x4E: case 0x4F: case 0x53:
        case 0x54: case 0x5B: case 0x65: case 0x6D: case 0x6E:
        case 0x70: case 0x72: case 0x88:
        case 0x8B: case 0x97: case 0xA2: case 0xA3:
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

    std::printf("[0xA8] %d server(s):\n", serverCount_);
    for (int i = 0; i < serverCount_; ++i) {
        char ip[32];
        IpToString(servers_[i].ip, ip, sizeof(ip));
        std::printf("  [%d] %-24s  %s  full=%u%%  tz=%u\n",
                    i, servers_[i].name, ip,
                    servers_[i].percentFull, servers_[i].timezone);
    }

    state_ = State::AwaitingServerList;
    selectedServer_ = 0; // PromptServerSelection();
    if (selectedServer_ < 0 || selectedServer_ >= serverCount_) {
        std::fprintf(stderr, "[ui] no server selected; aborting\n");
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

    std::printf("[0x81] legacy char list (flag=0x%02X proto=0x%02X) — %d slot(s) (populated %d):\n",
                data[3], data[4], charCount_, populated);
    for (int i = 0; i < charCount_; ++i) {
        const char* nm = charSlots_[i].name[0] ? charSlots_[i].name : "<empty>";
        std::printf("  [%d] %s\n", i, nm);
    }

    if (populated == 0) {
        std::fprintf(stderr, "[ui] no characters on this shard; aborting\n");
        state_ = State::Failed;
        return;
    }

    selectedChar_ = PromptCharacterSelection();
    if (selectedChar_ < 0 || selectedChar_ >= charCount_ ||
        !charSlots_[selectedChar_].name[0]) {
        std::fprintf(stderr, "[ui] invalid character slot\n");
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
    std::printf("[0x8C] game server = %s:%u  seed=0x%08X\n",
                ip, gameServerPort_, gameSeed_);
    char ev[160];
    std::snprintf(ev, sizeof(ev), "ip=%s port=%u seed=0x%08X",
                  ip, gameServerPort_, gameSeed_);
    logger_.Event("game_server_assigned", ev);

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
    std::printf("[net] reconnected to game server.\n");

    if (cfg_.sendSeed) {
        u8 seedbuf[4];
        build::Seed(seedbuf, gameSeed_);
        sock_.SendAll(seedbuf, sizeof(seedbuf));
        std::printf("[seed] game-server seed 0x%08X sent\n", gameSeed_);
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

    std::printf("[0xA9] %d slot(s) (populated %d):\n", charCount_, populated);
    for (int i = 0; i < charCount_; ++i) {
        const char* nm = charSlots_[i].name[0] ? charSlots_[i].name : "<empty>";
        std::printf("  [%d] %s\n", i, nm);
    }

    if (populated == 0) {
        std::fprintf(stderr, "[ui] no characters on this shard; aborting\n");
        state_ = State::Failed;
        return;
    }

    selectedChar_ = 0; // PromptCharacterSelection();
    if (selectedChar_ < 0 || selectedChar_ >= charCount_ ||
        !charSlots_[selectedChar_].name[0]) {
        std::fprintf(stderr, "[ui] invalid character slot\n");
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
    playerSerial_ = LoadBE32(data + 1);
    u16 body = LoadBE16(data + 9);
    u16 x    = LoadBE16(data + 11);
    u16 y    = LoadBE16(data + 13);
    u16 z    = LoadBE16(data + 15);  // low byte is z; high byte usually 0
    playerX_ = static_cast<i32>(x);
    playerY_ = static_cast<i32>(y);
    playerZ_ = static_cast<i8>(z & 0xFF);
    std::printf("[0x1B] serial=0x%08X body=0x%04X pos=(%d,%d,%d)\n",
                playerSerial_, body,
                playerX_, playerY_, static_cast<int>(playerZ_));
    char ev[160];
    std::snprintf(ev, sizeof(ev),
                  "serial=0x%08X body=0x%04X pos=(%d,%d,%d)",
                  playerSerial_, body,
                  playerX_, playerY_, static_cast<int>(playerZ_));
    logger_.Event("login_confirm", ev);
}

// ---------------------------------------------------------------------------
// 0x55 Login Complete (1 byte). World is up; we are live.
// ---------------------------------------------------------------------------
void Client::OnLoginComplete(const u8* data, usize size) {
    (void)data; (void)size;
    std::printf("[0x55] login complete — entering world\n");
    state_ = State::InWorld;
    logger_.EnableVerbose();
    logger_.Event("in_world", "0x55 received; verbose log enabled");
    // Initialise the keepalive timer so the first keepalive fires
    // exactly 60s after entering the world (matches the original).
    lastActivityMs_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    StartStdinThread();
    std::printf("\nType to chat (Enter to send). Ctrl-C to quit.\n\n");
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
    std::fprintf(stderr, "[0x82] LOGIN DENIED (%u): %s\n", reason, msg);
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
    std::printf("[0xB9] server features = 0x%04X\n", flags);
}

// ---------------------------------------------------------------------------
// 0xC8 Client View Range (2 bytes). Server tells client what the active
// range is. We just log; original client also has nothing to "ack" here.
// ---------------------------------------------------------------------------
void Client::OnViewRange(const u8* data, usize size) {
    if (size < 2) return;
    std::printf("[0xC8] view range = %u\n", data[1]);
}

// ---------------------------------------------------------------------------
// 0xA1 Update Mobile Hits (9 bytes): cmd, serial(4 BE), maxHp(2 BE),
// curHp(2 BE). For our own serial we track HP so a drop while travelling
// trips the combat-interrupt hook (actual engage/flee/recall are TODO).
// ---------------------------------------------------------------------------
void Client::OnMobileHp(const u8* data, usize size) {
    if (size < 9) return;
    const u32 serial = LoadBE32(data + 1);
    if (serial != playerSerial_) return;
    const i32 curHp = static_cast<i32>(LoadBE16(data + 7));
    if (lastHp_ >= 0 && curHp < lastHp_ && (botActive_ || !pendingMoves_.empty())) {
        char reason[48];
        std::snprintf(reason, sizeof(reason), "HP %d -> %d", lastHp_, curHp);
        BotInterruptForThreat(reason);
    }
    lastHp_ = curHp;
}

// ---------------------------------------------------------------------------
// 0x1A Object Info (variable). Layout from PacketManager_MakePacket_MOVE:
//   serial(4 BE)  [bit 0x80000000 -> amount field present]
//   graphic(2 BE) [bit 0x8000 stackable, 0x4000 multi]
//   [stackable] stack(1)   [amount present] amount(2)
//   x(2 BE) [bit 0x8000 -> direction byte present]
//   y(2 BE) [bit 0x8000 hue present, 0x4000 status flags present]
//   [dir present] dir(1)   z(1)   [hue] hue(2)   [flags] flags(1)
// We only care about doors — cache them by serial for the bot's bump logic.
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
    const bool stackable = (g & 0x8000) != 0;
    const u16 itemId = static_cast<u16>(g & 0x3FFF);

    if (stackable) { if (!avail(1)) return; p += 1; }
    if (hasAmount) { if (!avail(2)) return; p += 2; }

    if (!avail(2)) return;
    const u16 xw = LoadBE16(data + p); p += 2;
    const bool hasDir = (xw & 0x8000) != 0;
    const i32 x = xw & 0x7FFF;

    if (!avail(2)) return;
    const u16 yw = LoadBE16(data + p); p += 2;
    const i32 y = yw & 0x3FFF;

    if (hasDir) { if (!avail(1)) return; p += 1; }
    if (!avail(1)) return;
    const i8 z = static_cast<i8>(data[p]);

    // Track every world item so the renderer can draw dynamic server objects
    // (lamp posts, doors, decor). Keyed by serial; removed on 0x1D.
    items_[serial] = ItemObj{itemId, x, y, z};

    // Door-open confirmation: after we send an open command we wait for the
    // door to actually swing. The server announces that by updating the door
    // object at/near the blocked cell. Any object update within 2 tiles of it
    // means the door changed — resume the blocked step immediately.
    if (awaitingDoorOpen_) {
        i32 ax = (x > doorCellX_) ? x - doorCellX_ : doorCellX_ - x;
        i32 ay = (y > doorCellY_) ? y - doorCellY_ : doorCellY_ - y;
        i32 az = static_cast<i32>(z) - static_cast<i32>(doorCellZ_);
        if (az < 0) az = -az;
        // Same x,y vicinity AND our floor — a door stacked on another storey
        // (z far away) must not be mistaken for the one we're trying to pass.
        if (ax <= 2 && ay <= 2 && az <= 8) {
            awaitingDoorOpen_ = false;
            doorAttempts_ = 0;
            std::printf("[bot] door @(%d,%d,z%d) OPENED (object 0x%08X @%d,%d,z%d); resuming\n",
                        doorCellX_, doorCellY_, static_cast<int>(doorCellZ_),
                        serial, x, y, static_cast<int>(z));
            botResumeAtMs_ = NowMs();  // retry the blocked step now
        }
    }

    if (!IsDoorGraphic(itemId)) return;

    for (auto& d : doorCache_) {
        if (d.serial == serial) { d.itemId = itemId; d.x = x; d.y = y; d.z = z; return; }
    }
    if (doorCache_.size() >= kDoorCacheMax) doorCache_.pop_front();
    doorCache_.push_back({serial, itemId, x, y, z});
}

// 0x1D Delete Object (5 bytes): cmd + serial(4 BE). Drop it from both caches.
void Client::OnDeleteObject(const u8* data, usize size) {
    if (size < 5) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    items_.erase(serial);
    for (auto it = doorCache_.begin(); it != doorCache_.end(); ++it) {
        if (it->serial == serial) { doorCache_.erase(it); break; }
    }
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

void Client::UpdateMobile(u32 serial, i32 x, i32 y, i8 z, u8 dir) {
    if (serial == playerSerial_) return;  // never treat ourselves as an obstacle
    const i64 now = NowMs();
    for (auto& m : mobileCache_) {
        if (m.serial == serial) {
            m.x = x;
            m.y = y;
            m.z = z;
            m.dir = static_cast<u8>(dir & 0x07);
            m.seenMs = now;
            return;
        }
    }
    if (mobileCache_.size() >= kMobileCacheMax) mobileCache_.pop_front();
    mobileCache_.push_back({serial, x, y, z, static_cast<u8>(dir & 0x07), now});
}

// 0x77 Mobile Move (17 bytes): cmd, serial(4), body(2), x(2), y(2), z(1), ...
void Client::OnMobileMove(const u8* data, usize size) {
    if (size < 13) return;
    UpdateMobile(LoadBE32(data + 1), LoadBE16(data + 7), LoadBE16(data + 9),
                 static_cast<i8>(data[11]), data[12]);
}

// 0x78 Mobile Incoming (variable): cmd, len(2), serial(4), body(2), x(2),
// y(2), z(1), dir(1), ... (equipment list follows; we only need position).
void Client::OnMobileIncoming(const u8* data, usize size) {
    if (size < 15) return;
    UpdateMobile(LoadBE32(data + 3), LoadBE16(data + 9), LoadBE16(data + 11),
                 static_cast<i8>(data[13]), data[14]);
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
            std::printf("[0x98] name 0x%08X = %s\n", serial, name);
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

// Returns the door within `radius` tiles whose z is closest to `z` (and within
// the server's +/-8 reach). In multi-storey buildings doors stack at the same
// x,y on different floors, so picking the nearest by z keeps us on our floor.
const Client::DoorObj* Client::FindDoorAt(i32 x, i32 y, i8 z, i32 radius) const {
    const DoorObj* best = nullptr;
    i32 bestDz = 0x7FFFFFFF;
    for (const auto& d : doorCache_) {
        i32 ax = (x > d.x) ? x - d.x : d.x - x;
        i32 ay = (y > d.y) ? y - d.y : d.y - y;
        if (ax > radius || ay > radius) continue;
        i32 dz = static_cast<i32>(z) - static_cast<i32>(d.z);
        if (dz < 0) dz = -dz;
        if (dz > 8) continue;            // different floor / out of reach
        if (dz < bestDz) { bestDz = dz; best = &d; }
    }
    return best;
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
//   [37-38] HP cur, [39-40] HP max  (BE)
//   ... gender, str/dex/int, stam, mana, gold, ar, weight ...
// We only peek HP for liveness output.
// ---------------------------------------------------------------------------
void Client::OnStats(const u8* data, usize size) {
    if (size < 41) return;
    char name[31];
    std::memcpy(name, data + 7, 30);
    name[30] = '\0';
    u16 hp_cur = LoadBE16(data + 37);
    u16 hp_max = LoadBE16(data + 39);
    if (verboseConsole_)
        std::printf("[0x11] %s  HP=%u/%u\n", name, hp_cur, hp_max);
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
    char speaker[31];
    std::memcpy(speaker, data + 14, 30);
    speaker[30] = '\0';
    RememberMobileName(sourceSerial & 0x7FFFFFFFu, speaker);
    const char* text = reinterpret_cast<const char*>(data + 44);
    std::printf("[chat ascii] %s: %s\n", speaker, text);

    // Stamina signal: the server denies movement and says "too fatigued to
    // move" when stamina is spent. Record it so a reject right after is
    // treated as fatigue (wait to regen), not as an obstacle to avoid.
    if (std::strstr(text, "fatigued")) {
        lastFatigueMs_ = NowMs();
        std::printf("[bot] fatigue detected; rejects will wait for stamina regen\n");
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
    char speaker[31];
    std::memcpy(speaker, data + 14, 30);
    speaker[30] = '\0';
    RememberMobileName(sourceSerial & 0x7FFFFFFFu, speaker);

    char buf[256];
    usize n = 0;
    for (usize i = 48; i + 1 < size && n + 1 < sizeof(buf); i += 2) {
        u16 ch = LoadBE16(data + i);
        if (ch == 0) break;
        buf[n++] = (ch < 0x80) ? static_cast<char>(ch) : '?';
    }
    buf[n] = '\0';
    std::printf("[chat uni  ] %s: %s\n", speaker, buf);
}

void Client::OnUnknown(const u8* data, usize size) {
    if (!verboseConsole_) return;  // every packet is in the JSON log already
    std::fprintf(stderr,
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
    const u32 serial = LoadBE32(data + 1);
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
        // A full resync (teleport / server correction) invalidates every
        // predicted-but-unacked move and any path planned off the old pose.
        if (!pendingMoves_.empty() || botActive_ || !botPath_.empty()) {
            pendingMoves_.clear();
            moveSeq_ = 0;
            if (botActive_ || !botPath_.empty()) {
                std::fprintf(stderr, "[bot] 0x20 resync; aborting path\n");
                botPath_.clear();
                botActive_ = false;
            }
        }
        std::printf("[0x20] player @(%d,%d,%d) facing=%u\n",
                    playerX_, playerY_, static_cast<int>(playerZ_),
                    playerFacing_);
    }
}

// ---------------------------------------------------------------------------
// 0x21 Move Reject (8 bytes). Server rejected our move; here is your
// authoritative position.
//   [0]    cmd
//   [1]    sequence (the move being rejected)
//   [2-3]  x BE
//   [4-5]  y BE
//   [6]    direction
//   [7]    z (signed)
// ---------------------------------------------------------------------------
void Client::OnMoveReject(const u8* data, usize size) {
    if (size < 8) return;
    const u8  seq     = data[1];
    const u16 x       = LoadBE16(data + 2);
    const u16 y       = LoadBE16(data + 4);
    const u8  dirByte = data[6];
    const i8  z       = static_cast<i8>(data[7]);

    // Direction of the move that was rejected (prefer the queued move's dir;
    // fall back to the server's reported facing).
    u8 rdir = dirByte & 0x07;
    for (const auto& pm : pendingMoves_)
        if (pm.seq == seq) { rdir = pm.dir; break; }

    std::fprintf(stderr,
        "[0x21] move REJECTED seq=%u; server says (%u,%u,%d) facing=%u\n",
        seq, x, y, static_cast<int>(z), dirByte & 0x07);

    // Server is authoritative — snap prediction back to its reported pose and
    // drop the whole in-flight queue (anything we predicted past the reject is
    // stale). Classic UO: seq resets to 0.
    playerX_ = static_cast<i32>(x);
    playerY_ = static_cast<i32>(y);
    playerZ_ = z;
    playerFacing_ = dirByte & 0x07;
    playerRunning_ = false;
    pendingMoves_.clear();
    moveSeq_ = 0;

    if (!botActive_) return;

    // The cell we were blocked from entering, and its surface z.
    i32 dx, dy;
    bot::DirToDelta(rdir, &dx, &dy);
    const i32 bx = playerX_ + dx;
    const i32 by = playerY_ + dy;
    i32 bz = playerZ_;
    bool neighborStatic = false;
    if (world_) {
        world::WalkQuery q{};
        q.x = static_cast<u32>(bx);
        q.y = static_cast<u32>(by);
        q.fromZ = playerZ_;
        const auto r = world_->QueryCell(q);
        if (r.walkable) bz = r.standZ;
        // "Near block" check: is an adjacent tile impassable in our MUL data?
        for (u8 nd = 0; nd < 8 && !neighborStatic; ++nd) {
            i32 ndx, ndy;
            bot::DirToDelta(nd, &ndx, &ndy);
            world::WalkQuery nq{};
            nq.x = static_cast<u32>(bx + ndx);
            nq.y = static_cast<u32>(by + ndy);
            nq.fromZ = static_cast<i8>(bz);
            if (!world_->QueryCell(nq).walkable) neighborStatic = true;
        }
    }

    // Track repeated bumps at the same cell (and the floor we're on).
    if (bx != doorCellX_ || by != doorCellY_) {
        doorCellX_ = bx; doorCellY_ = by; doorAttempts_ = 0; stuckWaits_ = 0;
        awaitingDoorOpen_ = false;
    } else if (awaitingDoorOpen_) {
        std::printf("[bot] door @(%d,%d) did NOT open (no update after last try)\n", bx, by);
        awaitingDoorOpen_ = false;
    }
    doorCellZ_ = static_cast<i8>(bz);

    // (0) Stamina: a reject right after a "too fatigued" message is not an
    // obstacle — we're just spent. Wait for regen and retry; never blacklist.
    const bool fatigued = lastFatigueMs_ != 0 &&
                          (NowMs() - lastFatigueMs_) < kFatigueWindowMs;

    // (1) Is a mobile standing on the blocked cell? Walking into one is a
    // shove (succeeds once rested), so a reject there is a moving/stamina
    // obstacle, never a wall. Wait and retry; never blacklist.
    const MobileObj* mob = FindMobileAt(bx, by, static_cast<i8>(bz));

    if (fatigued || mob) {
        const bool blockedByMobile = (mob != nullptr) && !fatigued;
        if (++stuckWaits_ > kMaxStuckWaits) {
            std::fprintf(stderr,
                "[bot] (%d,%d) blocked by %s for too long; stopping (not blacklisted)\n",
                bx, by, fatigued ? "fatigue" : "a mobile");
            botPath_.clear();
            botActive_ = false;
            return;
        }
        if (blockedByMobile && stuckWaits_ >= kMobileRepathAfter) {
            // Don't stall indefinitely behind another mover: avoid this cell
            // for this trip and rebuild a full route around it.
            blacklist_.AddTransient(bx, by, bz, 0);
            std::printf("[bot] mobile still blocks (%d,%d) after %u waits; rerouting now\n",
                        bx, by, stuckWaits_);
            stuckWaits_ = 0;
            if (BotReplanToGoal())
                botResumeAtMs_ = NowMs() + 150;
            return;
        }
        const i64 wait = fatigued ? kStaminaWaitMs : kMobileWaitMs;
        std::printf("[bot] reject at (%d,%d): %s — waiting %lldms, retrying (%u) [no blacklist]\n",
                    bx, by, fatigued ? "fatigued (stamina)" : "mobile in the way",
                    static_cast<long long>(wait), stuckWaits_);
        if (BotReplanToGoal())
            botResumeAtMs_ = NowMs() + wait;
        return;
    }

    // Is there a known door within 1 tile of the blocked cell? Such a cell is
    // a real passage and is NEVER blacklisted — we only ever try to open it.
    const DoorObj* nearDoor = FindDoorAt(bx, by, static_cast<i8>(bz), 1);

    // Phase 1 — try to open a door. We keep opening while either a door is
    // known nearby (guard: never give that tile to the blacklist) or we're
    // still within the initial blind-try budget (the door's 0x1A may not have
    // arrived yet). The server-side OpenDoor action opens whatever door faces
    // us regardless of graphic/serial; we also double-click a known one.
    // Doors don't swing instantly, so wait kDoorWaitMs between tries.
    if (nearDoor != nullptr || doorAttempts_ < kMaxDoorTries) {
        ++doorAttempts_;
        u8 ob[8];
        Send(ob, build::OpenDoor(ob), "0x12 OpenDoor (0x58)");
        if (nearDoor) {
            u8 db[8];
            Send(db, build::DoubleClick(db, nearDoor->serial), "0x06 DoubleClick door");
        }
        // A door that won't budge after many tries is a dead end — stop the
        // trip rather than loop forever, but still never blacklist its tile.
        if (nearDoor != nullptr && doorAttempts_ > kMaxDoorGiveUp) {
            std::fprintf(stderr,
                "[bot] door @(%d,%d) won't open after %u tries; stopping "
                "(not blacklisted)\n", bx, by, doorAttempts_);
            botPath_.clear();
            botActive_ = false;
            return;
        }
        awaitingDoorOpen_ = true;  // confirmed (or timed out) via 0x1A near this cell
        std::printf("[bot] reject at (%d,%d,z%d): OpenDoor sent, awaiting confirm (try %u)%s\n",
                    bx, by, static_cast<int>(bz), doorAttempts_,
                    nearDoor ? " [door cached]" : "");
        if (BotReplanToGoal())  // path unchanged; we retry the same step
            botResumeAtMs_ = NowMs() + kDoorWaitMs;
        return;
    }

    // Phase 2 — no door nearby and the blind door budget is spent: a wall,
    // lamp post, or a mob in the way. Avoid it for THIS trip ONLY (transient,
    // never written to blacklist.mul, so a real passage is never permanently
    // poisoned) and reroute. Stop only if there's genuinely no other way.
    blacklist_.AddTransient(bx, by, bz, neighborStatic ? 1 : 0);
    std::printf("[bot] (%d,%d,%d) blocked after %u tries; avoiding (transient) "
                "+ rerouting\n", bx, by, static_cast<int>(bz), doorAttempts_);
    std::uniform_int_distribution<int> rd(200, 400);
    if (BotReplanToGoal())
        botResumeAtMs_ = NowMs() + rd(rng_);
}

// ---------------------------------------------------------------------------
// 0x22 Move Ack (3 bytes). Confirms the oldest in-flight move. Position was
// already advanced when we sent it (prediction), so the ack just frees a
// flight slot and lets the pipeline top up.
//   [0]    cmd
//   [1]    sequence (echoed)
//   [2]    notoriety
// ---------------------------------------------------------------------------
void Client::OnMoveAck(const u8* data, usize size) {
    if (size < 3) return;
    const u8 seq = data[1];
    if (pendingMoves_.empty()) {
        std::fprintf(stderr, "[0x22] unsolicited ack seq=%u\n", seq);
        return;
    }
    const PendingMove pm = pendingMoves_.front();
    pendingMoves_.pop_front();
    if (pm.seq != seq) {
        // Acks should arrive in send order; a mismatch means we lost sync.
        std::fprintf(stderr, "[0x22] ack seq=%u, expected %u — resyncing\n",
                     seq, pm.seq);
    }
    // Top up the pipeline immediately rather than waiting for the next tick.
    BotPumpMoves();
}

// ---------------------------------------------------------------------------
// Console UI
// ---------------------------------------------------------------------------
int Client::PromptServerSelection() {
    int sel = -1;
    std::printf("> select server [0..%d]: ", serverCount_ - 1);
    std::fflush(stdout);
    if (!(std::cin >> sel)) return -1;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return sel;
}

int Client::PromptCharacterSelection() {
    int sel = -1;
    std::printf("> select character [0..%d]: ", charCount_ - 1);
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
        std::printf("[mobiles] no mobiles cached yet\n");
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
        std::printf("[mobiles] no mobiles in range\n");
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
    std::printf("[mobiles] nearby (%zu):\n", mobilesListSerials_.size());
    for (u32 serial : mobilesListSerials_) {
        const char* name = MobileName(serial);
        std::printf("  %s 0x%08X\n", name ? name : "<unknown>", serial);
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
            std::fprintf(stderr,
                "[cmd] usage: goto <x> <y> [z]  (comma or space separated)\n");
        }
        return;
    }

    // `stop` — drop the current path.
    if (std::strcmp(line, "stop") == 0) {
        if (followActive_) BotStopFollow("stopped by user");
        if (!botPath_.empty()) {
            std::printf("[bot] path cleared (%zu steps left)\n",
                        botPath_.size());
        }
        botPath_.clear();
        botActive_ = false;
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
            std::fprintf(stderr, "[cmd] usage: follow <name|0xserial> [distance]|off\n");
            return;
        }

        u32 serial = 0;
        if ((target[0] == '0') && (target[1] == 'x' || target[1] == 'X')) {
            if (!ParseSerial(target, &serial) || serial == 0) {
                std::fprintf(stderr, "[cmd] invalid serial: %s\n", target);
                return;
            }
        } else {
            serial = ResolveFollowSerialByName(target);
            if (serial == 0) {
                std::fprintf(stderr,
                    "[cmd] mobile '%s' not found in cache; run `mobiles` and retry\n", target);
                return;
            }
        }
        BotStartFollow(serial & 0x7FFFFFFFu, followDist);
        return;
    }

    // `pos` — print current position.
    if (std::strcmp(line, "pos") == 0) {
        std::printf("[pos] (%d,%d,%d) facing=%u%s\n",
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
        std::printf("[cmd] verbose console %s\n", verboseConsole_ ? "ON" : "off");
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

// ---------------------------------------------------------------------------
// Bot — A* + step pump
// ---------------------------------------------------------------------------
// Classic UO sequence: starts at 0, wraps 0xFF -> 1 (0 reserved for resync).
u8 Client::NextSeq() {
    u8 s = moveSeq_;
    moveSeq_ = (moveSeq_ == 0xFF) ? 1 : (moveSeq_ + 1);
    return s;
}

bool Client::EnsureWorldLoaded() {
    if (worldLoaded_) return true;
    if (!cfg_.tiledataPath || !cfg_.mapPath ||
        !cfg_.staidxPath   || !cfg_.staticsPath) {
        std::fprintf(stderr,
            "[bot] MUL paths not configured; goto disabled\n");
        return false;
    }
    tileData_ = std::make_unique<tiledata::TileDataLoader>();
    if (!tileData_->Load(cfg_.tiledataPath)) {
        tileData_.reset();
        return false;
    }
    worldMap_ = std::make_unique<map::Map>();
    if (!worldMap_->Open(cfg_.mapPath, cfg_.staidxPath, cfg_.staticsPath,
                         map::kBritWidthBlocks, map::kBritHeightBlocks,
                         cfg_.verdataPath)) {
        worldMap_.reset();
        tileData_.reset();
        return false;
    }
    world_ = std::make_unique<world::World>(*tileData_, *worldMap_);
    world_->SetAcceptDoors(cfg_.acceptDoors);
    worldLoaded_ = true;

    // Learned static blocks persist in blacklist.mul (verdata format),
    // layered on top of the base statics. Load them so A* avoids known bad
    // tiles from the first step.
    blacklist_.Load("blacklist.mul", worldMap_->HeightBlocks());
    std::printf("[bot] world data loaded (%zu blacklisted spot(s)).\n",
                blacklist_.PersistentCount());
    return true;
}

void Client::RenderTick() {
    if (!cfg_.enableRenderer) return;

    if (!renderInit_) {
        renderInit_ = true;
        if (!EnsureWorldLoaded()) {
            std::fprintf(stderr, "[render] world data unavailable; renderer off\n");
            cfg_.enableRenderer = false;
            return;
        }
        art_ = std::make_unique<art::ArtLoader>();
        if (!cfg_.artIdxPath || !cfg_.artPath ||
            !art_->Open(cfg_.artIdxPath, cfg_.artPath)) {
            std::fprintf(stderr, "[render] failed to open art MULs; renderer off\n");
            art_.reset();
            cfg_.enableRenderer = false;
            return;
        }
        texmaps_ = std::make_unique<texmap::TexmapLoader>();
        if (!cfg_.texIdxPath || !cfg_.texPath ||
            !texmaps_->Open(cfg_.texIdxPath, cfg_.texPath)) {
            std::fprintf(stderr, "[render] failed to open texmaps; renderer off\n");
            art_.reset();
            texmaps_.reset();
            cfg_.enableRenderer = false;
            return;
        }
        const int rw = cfg_.renderWidth  > 0 ? cfg_.renderWidth  : 800;
        const int rh = cfg_.renderHeight > 0 ? cfg_.renderHeight : 600;
        const int sc = cfg_.renderScale  > 0 ? cfg_.renderScale  : 1;
        if (!mfb_open("uo-client world", rw, rh, sc, 15)) {
            std::fprintf(stderr, "[render] mfb_open failed; renderer off\n");
            art_.reset();
            cfg_.enableRenderer = false;
            return;
        }
        renderer_ = std::make_unique<render::Renderer>(rw, rh);
        renderWindowOpen_ = true;
        std::printf("[render] world window opened (%dx%d)\n", rw, rh);
    }

    if (!renderWindowOpen_ || !renderer_ || !worldMap_ || !tileData_) return;

    std::vector<render::DynItem> dyn;
    dyn.reserve(items_.size());
    for (const auto& kv : items_)
        dyn.push_back({kv.second.itemId, kv.second.x, kv.second.y, kv.second.z});

    renderer_->RenderWorld(*worldMap_, *art_, *tileData_, *texmaps_,
                           playerX_, playerY_, dyn.data(), dyn.size());
    if (!mfb_update(renderer_->Frame(), 0)) {
        // User closed the window — stop drawing, keep the bot running.
        mfb_close();
        renderWindowOpen_ = false;
        cfg_.enableRenderer = false;
        std::printf("[render] window closed; rendering disabled\n");
    }
}

i64 Client::NowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Client::BotPredictStep(u8 dir) {
    i32 dx, dy;
    bot::DirToDelta(dir, &dx, &dy);
    playerX_ += dx;
    playerY_ += dy;
    // Track the surface z so the next step's walk-check uses the right base.
    if (world_) {
        world::WalkQuery q{};
        q.x = static_cast<u32>(playerX_);
        q.y = static_cast<u32>(playerY_);
        q.fromZ = playerZ_;
        const auto r = world_->QueryCell(q);
        if (r.walkable) playerZ_ = r.standZ;
    }
}

bool Client::BotReplanToGoal() {
    if (++botReplanCount_ > kMaxReplans) {
        std::fprintf(stderr, "[bot] giving up after %u replans (unreachable?)\n",
                     botReplanCount_);
        botPath_.clear();
        botActive_ = false;
        return false;
    }
    bot::PathOptions opts;
    opts.blacklist = &blacklist_;
    // For follow we want the shortest valid path to keep up with a moving
    // target; road/grass bias only makes us lag behind.
    opts.grassPenalty   = followActive_ ? 0u : kGrassPenalty;
    opts.foliagePenalty = followActive_ ? 0u : kForestPenalty;
    opts.hasGoalZ = botHasGoalZ_;       // pin destination floor when given
    opts.goalZ    = botGoalZ_;

    // Scale the node-expansion budget with goal distance. The grass penalty
    // inflates step cost without the heuristic knowing, so on a long open
    // route A* loses its straight-line guidance and expands ~O(distance^2)
    // nodes — the fixed default cap then makes a reachable-but-far goal look
    // unreachable. Budget quadratically (with headroom for detours) but bound
    // it so a genuinely unreachable goal still fails in finite time/memory.
    const i32 adx = (botGoalX_ > playerX_) ? botGoalX_ - playerX_ : playerX_ - botGoalX_;
    const i32 ady = (botGoalY_ > playerY_) ? botGoalY_ - playerY_ : playerY_ - botGoalY_;
    const u64 cheb = static_cast<u64>(adx > ady ? adx : ady);
    u64 budget = cheb * cheb * 4 + 65536;
    if (budget > 2000000) budget = 2000000;
    opts.maxNodesExpanded = static_cast<u32>(budget);
    auto path = bot::FindPath(*world_, playerX_, playerY_, playerZ_,
                              botGoalX_, botGoalY_, opts);
    if (path.empty()) {
        std::fprintf(stderr,
            "[bot] no path to (%d,%d) avoiding %zu block(s); stopping\n",
            botGoalX_, botGoalY_, blacklist_.Count());
        botPath_.clear();
        botActive_ = false;
        return false;
    }
    botPath_.assign(path.begin(), path.end());
    return true;
}

// Combat-interrupt hook. Called when we detect we're under attack mid-travel.
// For now it just halts the path safely so we don't blindly run on while being
// hit. TODO: per-policy reaction — engage (war + attack), flee (path away from
// the threat), or recall ("kal ort por"); see task #6.
void Client::BotInterruptForThreat(const char* reason) {
    std::fprintf(stderr,
        "[bot] THREAT (%s): halting travel (TODO engage/flee/recall)\n",
        reason ? reason : "?");
    logger_.Event("threat", reason ? reason : "");
    botPath_.clear();
    pendingMoves_.clear();
    botActive_ = false;
    followActive_ = false;
    moveSeq_ = 0;
}

void Client::BotStopFollow(const char* reason) {
    if (!followActive_) return;
    std::printf("[follow] stopped (%s)\n", reason ? reason : "off");
    followActive_ = false;
    followSerial_ = 0;
    followDistance_ = 1;
    followLastReplanMs_ = 0;
    followLastProbeMs_ = 0;
    botPath_.clear();
    pendingMoves_.clear();
    botActive_ = false;
    moveSeq_ = 0;
}

void Client::BotStartFollow(u32 serial, u32 followDistance) {
    if (!EnsureWorldLoaded()) return;
    followActive_ = true;
    followSerial_ = serial;
    followDistance_ = followDistance ? followDistance : 1;
    followLastReplanMs_ = 0;
    followLastProbeMs_ = 0;
    botPath_.clear();
    pendingMoves_.clear();
    moveSeq_ = 0;
    botActive_ = false;
    const char* name = MobileName(serial);
    std::printf("[follow] tracking %s0x%08X (distance=%u)\n",
                name ? name : "", serial, followDistance_);
    u8 pkt[8];
    const usize n = build::MobNameQuery(pkt, serial);
    Send(pkt, n, "0x98 AllNames (follow start)");
}

bool Client::ChooseFollowGoal(i32* gx, i32* gy, i8* gz) const {
    if (!followActive_ || !gx || !gy || !gz || !world_) return false;
    const MobileObj* t = FindMobileBySerial(followSerial_);
    if (!t) return false;

    const u8 behind = static_cast<u8>((t->dir + 4) & 0x07);
    for (u8 rank = 0; rank < 8; ++rank) {
        const u8 d = (rank == 0) ? behind : static_cast<u8>((behind + rank) & 0x07);
        i32 dx, dy;
        bot::DirToDelta(d, &dx, &dy);
        const i32 tx = t->x + dx;
        const i32 ty = t->y + dy;
        if (FindMobileAt(tx, ty, t->z)) continue;

        world::WalkQuery q{};
        q.x = static_cast<u32>(tx);
        q.y = static_cast<u32>(ty);
        // Follow must stick to the target's floor. Using our current z here
        // picks the wrong layer in multi-storey columns (e.g. target fell
        // from a second floor and we're still above).
        q.fromZ = t->z;
        const auto wr = world_->QueryCell(q);
        if (!wr.walkable) continue;

        *gx = tx;
        *gy = ty;
        *gz = wr.standZ;
        return true;
    }
    return false;
}

void Client::BotFollowTick() {
    if (!followActive_) return;
    const i64 now = NowMs();

    const MobileObj* t = FindMobileBySerial(followSerial_);
    if (!t) {
        if (now - followLastProbeMs_ >= kFollowProbeMs) {
            u8 pkt[8];
            const usize n = build::MobNameQuery(pkt, followSerial_);
            Send(pkt, n, "0x98 AllNames (follow probe)");
            followLastProbeMs_ = now;
            std::printf("[follow] waiting for 0x%08X to appear in range\n", followSerial_);
        }
        return;
    }

    const i32 dx = (playerX_ > t->x) ? (playerX_ - t->x) : (t->x - playerX_);
    const i32 dy = (playerY_ > t->y) ? (playerY_ - t->y) : (t->y - playerY_);
    i32 dz = static_cast<i32>(playerZ_) - static_cast<i32>(t->z);
    if (dz < 0) dz = -dz;
    if (dx <= static_cast<i32>(followDistance_) &&
        dy <= static_cast<i32>(followDistance_) && dz <= 8) {
        // Already inside follow radius: don't keep replanning. Let any
        // in-flight move settle first to avoid stop/start jitter.
        if (pendingMoves_.empty()) {
            botPath_.clear();
            botActive_ = false;
        }
        return;
    }

    if (now - followLastReplanMs_ < kFollowReplanMinMs) return;
    i32 gx = 0, gy = 0;
    i8 gz = 0;
    if (!ChooseFollowGoal(&gx, &gy, &gz)) return;

    const bool goalChanged = (gx != botGoalX_ || gy != botGoalY_ ||
                              !botHasGoalZ_ || gz != static_cast<i8>(botGoalZ_));
    if (!goalChanged && (botActive_ || !pendingMoves_.empty())) return;

    botGoalX_ = gx;
    botGoalY_ = gy;
    botGoalZ_ = gz;
    botHasGoalZ_ = true;
    botActive_ = true;
    botReplanCount_ = 0;
    followLastReplanMs_ = now;
    BotReplanToGoal();
    BotPumpMoves();
}

void Client::BotStartGoto(i32 tx, i32 ty, bool hasZ, i32 tz) {
    if (!EnsureWorldLoaded()) return;
    followActive_ = false;
    if (!pendingMoves_.empty() || !botPath_.empty()) {
        std::fprintf(stderr,
            "[bot] busy (inflight=%zu path=%zu); type 'stop' first\n",
            pendingMoves_.size(), botPath_.size());
        return;
    }
    botGoalX_ = tx;
    botGoalY_ = ty;
    botGoalZ_ = tz;
    botHasGoalZ_ = hasZ;
    botActive_ = true;
    botReplanCount_ = 0;
    botResumeAtMs_ = 0;
    doorAttempts_ = 0;
    awaitingDoorOpen_ = false;
    stuckWaits_ = 0;
    blacklist_.ClearTransient();
    moveSeq_ = 0;  // fresh fastwalk sequence (0 = resync)

    if (botHasGoalZ_)
        std::printf("[bot] %s from (%d,%d,%d) to (%d,%d,z%d)\n",
                    botRun_ ? "running" : "walking",
                    playerX_, playerY_, static_cast<int>(playerZ_), tx, ty, tz);
    else
        std::printf("[bot] %s from (%d,%d,%d) to (%d,%d)\n",
                    botRun_ ? "running" : "walking",
                    playerX_, playerY_, static_cast<int>(playerZ_), tx, ty);
    if (!BotReplanToGoal()) return;
    std::printf("[bot] path: %zu steps\n", botPath_.size());
    BotPumpMoves();
}

// Sends queued steps while a flight slot is free and the step cadence has
// elapsed. Each move is predicted immediately (pos for a step, facing for a
// turn) and reconciled later by 0x22 / 0x21.
void Client::BotPumpMoves() {
    if (!botActive_) return;

    const i64 now_ms = NowMs();
    if (now_ms < botResumeAtMs_) return;  // human reaction pause after a bump
    const u32 needGap = botRun_ ? runThrottleMs_ : walkThrottleMs_;

    while (pendingMoves_.size() < kMaxInFlight && !botPath_.empty()) {
        if (lastMoveSentMs_ != 0 &&
            now_ms - lastMoveSentMs_ < static_cast<i64>(needGap)) {
            return;  // enforce only minimum legal step gap, no random jitter
        }

        const u8 dir = botPath_.front();
        const bool wasStep = (dir == playerFacing_);

        // Idle self-click every few moves — mimics a human and doubles as a
        // liveness ping on shards with bot heuristics.
#if 0
        if (movesSinceClick_ >= 3 && playerSerial_ != 0) {
            u8 cbuf[8];
            usize cn = build::SingleClick(cbuf, playerSerial_);
            Send(cbuf, cn, "0x09 SingleClick (anti-bot)");
            movesSinceClick_ = 0;
        }
#endif
        const u8 seq  = NextSeq();
        const u8 wire = botRun_ ? static_cast<u8>(dir | 0x80) : dir;
        u8 buf[16];
        usize n = build::MoveRequest(buf, wire, seq, 0u, cfg_.legacyMovePacket);
        char note[72];
        std::snprintf(note, sizeof(note), "0x02 Move dir=%u seq=%u %s%s",
                      dir, seq, wasStep ? "step" : "turn",
                      botRun_ ? " run" : "");
        if (!Send(buf, n, note)) {
            botActive_ = false; botPath_.clear(); pendingMoves_.clear();
            return;
        }

        pendingMoves_.push_back({seq, dir, wasStep, now_ms});
        lastMoveSentMs_ = now_ms;
        ++movesSinceClick_;

        if (wasStep) { botPath_.pop_front(); BotPredictStep(dir); }
        else         { playerFacing_ = dir; }  // turn: re-send same dir to step
    }

    if (botPath_.empty() && pendingMoves_.empty()) {
        botActive_ = false;
        std::printf("[bot] arrived at (%d,%d,%d)\n",
                    playerX_, playerY_, static_cast<int>(playerZ_));
    }
}

void Client::BotTick() {
    if (mobilesListPending_ && NowMs() >= mobilesListDeadlineMs_) {
        FlushPendingMobilesList();
    }
    if (followActive_) BotFollowTick();
    if (!botActive_) return;
    if (!pendingMoves_.empty()) {
        // Watchdog: the oldest in-flight move should ack quickly. If it
        // never does, the move was silently dropped — abort the path.
        const i64 now_ms = NowMs();
        if (now_ms - pendingMoves_.front().sentMs >
                static_cast<i64>(ackWatchdogMs_)) {
            std::fprintf(stderr,
                "[bot] watchdog: oldest move unacked %llds; aborting path\n",
                static_cast<long long>(
                    (now_ms - pendingMoves_.front().sentMs) / 1000));
            pendingMoves_.clear();
            moveSeq_ = 0;
            botPath_.clear();
            botActive_ = false;
            return;
        }
    }
    BotPumpMoves();
}

}
