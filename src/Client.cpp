#include "Client.h"

#include "bot/Scenario.h"
#include "uo/sphere_rules.h"

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
#include "uo/animinfo.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "render/Minimap.h"
#include "render/RadarColors.h"
#include "win32/MiniFB.h"
#include "js/ClientBindings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdarg>
#include <iostream>
#include <limits>
#include <utility>

#include <winsock2.h>

namespace uo {

namespace {

// Recent mobile cache and delayed name query handling.
constexpr usize kMobileCacheMax = 512;
constexpr i64   kMobilesNamesTimeoutMs = 500;
// UO Demo / Sphere-style shards kick the connection after ~60s of
// client-side silence. Stay well inside the window: 20s gap.
constexpr i64   kKeepaliveIntervalMs = 20000;
// Equipment layers used by use/arm/disarm (UOLayer in client_2.0.7).
constexpr u8    kLayerOneHanded = 1;     // right hand (weapon)
constexpr u8    kLayerTwoHanded = 2;     // left hand (shield / 2H weapon)
constexpr u8    kLayerBackpack  = 0x15;  // 21
// How long to wait for the server's 0xD1 before closing anyway.
constexpr i64   kLogoutGraceMs = 2000;
// Never echo an unsolicited server ping more than once per second.
constexpr i64   kPingEchoMinGapMs = 1000;
// "search only backpack/equipment" scope keyword for use/equip commands.
bool IsPackScope(const char* s) {
    return std::strcmp(s, "pack") == 0 || std::strcmp(s, "inv") == 0 ||
           std::strcmp(s, "self") == 0 || std::strcmp(s, "me") == 0;
}
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
      chatInputActive_(false),
      playerBody_(0x0190), lastManualMoveMs_(0),
      nav_{},
      mobilesListPending_(false), mobilesListDeadlineMs_(0),
      lastStatusProbeMs_(0),
      verboseConsole_(false),
      stop_stdin_(false),
      lastActivityMs_(0) {
    nav_.rng.seed(static_cast<u32>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    // One gait for every movement source. A* used to run unconditionally while
    // scripted walks honoured the config; with a single movement controller the
    // choice belongs in one place. Walking (the default) never reaches Sphere's
    // walk-buffer speedhack check, which only runs for running steps
    // (CClient::Event_Walk, src/game/clients/CClientEvent.cpp:905-935).
    nav_.movement.run = cfg_.runWhenWalking;
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
    js_.SetBindingInstaller(
        [this](JSContext* c) {
            // Refused when another session already owns the JS bindings; the
            // error is logged there and this session simply has no scripting.
            (void)uo::js::InstallClientBindings(c, this);
        });
    js_.SetBindingTeardown([]() { uo::js::DetachClientBindings(); });
}

Client::~Client() {
    if (renderWindowOpen_) { mfb_close(); renderWindowOpen_ = false; }
    StopStdinThread();
    sock_.Close();
    log_.Close();
    // Deliberately NOT closing Logger::Instance() and NOT calling
    // WSACleanupOnce(): both are process-wide and another session may still
    // be running. Winsock teardown belongs to the process (main), not to one
    // session. (M1.5 state audit items 7 and 8.)
}

// --- per-session logging ---------------------------------------------------
// Each forwards to this session's Logger, which prefixes the session tag.

void Client::LogInfo(const char* fmt, ...) const {
    std::va_list ap; va_start(ap, fmt);
    log_.WriteV(LogLevel::Info, LogSink::Both, fmt, ap);
    va_end(ap);
}

void Client::LogWarn(const char* fmt, ...) const {
    std::va_list ap; va_start(ap, fmt);
    log_.WriteV(LogLevel::Warn, LogSink::Both, fmt, ap);
    va_end(ap);
}

void Client::LogError(const char* fmt, ...) const {
    std::va_list ap; va_start(ap, fmt);
    log_.WriteV(LogLevel::Error, LogSink::Both, fmt, ap);
    va_end(ap);
}

void Client::LogEvent(const char* kind, const char* detail) const {
    log_.Event(kind, detail);
}

void Client::LogPacket(Direction dir, const u8* data, usize size,
                       const char* note) const {
    log_.Packet(dir, data, size, note);
}

int Client::Run() {
    if (!Start()) return exitCode_;
    while (!finished_) Tick(50);
    return exitCode_;
}

// Bring the session up to the point where it is waiting on the server:
// connected, seeded, 0x80 sent, scenario loaded. Never blocks on the network
// beyond the TCP connect itself.
bool Client::Start() {
    if (started_) return !finished_;
    started_ = true;

    // Winsock init is idempotent and refcount-free; the matching cleanup is
    // the process's job, not the session's.
    if (!net::Socket::WSAStart()) { exitCode_ = 1; finished_ = true; return false; }

    sessionTag_ = (cfg_.sessionTag && cfg_.sessionTag[0]) ? cfg_.sessionTag : "";
    log_.SetTag(sessionTag_);
    if (cfg_.logFile && cfg_.logFile[0]) {
        if (!log_.OpenFile(cfg_.logFile)) {
            LogWarn("warning: cannot open log file '%s'\n", cfg_.logFile);
        } else {
            LogEvent("session_start", "text log opened");
        }
    }

    if (!ConnectAndSendSeed(cfg_.loginHost, cfg_.loginPort)) {
        exitCode_ = 2; finished_ = true; return false;
    }

    // Send 0x80 immediately after the seed; the original client does the
    // same — there is no server-side ack between seed and login.
    u8 buf[256];
    const usize n = build::LoginRequest(buf, cfg_.username, cfg_.password);
    if (!Send(buf, n, "0x80 LoginRequest")) {
        exitCode_ = 3; finished_ = true; return false;
    }
    state_ = State::AwaitingServerList;

    if (cfg_.scenarioPath && cfg_.scenarioPath[0]) {
        scenario_ = std::make_unique<bot::Scenario>();
        std::string err;
        if (!scenario_->Load(cfg_.scenarioPath, &err)) {
            LogError("[scenario] %s\n", err.c_str());
            LogEvent("scenario_load_failed", err.c_str());
            scenario_.reset();
            exitCode_ = 5; finished_ = true; return false;
        }
        LogInfo("[scenario] loaded '%s'\n", cfg_.scenarioPath);
    }

    lastActivity_ = std::chrono::steady_clock::now();
    return true;
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
    LogPacketRedacted(Direction::Out, data, size, note);
    return true;
}

// Packet logging with credentials masked. 0x80 (account login) and 0x91 (game
// login) carry the password in clear on the wire; the hex dump would otherwise
// put it in the log file. The password field is replaced with 0xEE filler --
// length and every other field stay intact for diagnosis.
void Client::LogPacketRedacted(Direction dir, const u8* data, usize size,
                               const char* note) {
    if (!cfg_.logPackets) return;

    const usize passOff = sphere::CredentialPasswordOffset(data, size);

    if (passOff == 0) {
        LogPacket(dir, data, size, note);
        return;
    }

    u8 scratch[256];
    const usize n = (size < sizeof(scratch)) ? size : sizeof(scratch);
    std::memcpy(scratch, data, n);
    for (usize i = passOff;
         i < passOff + sphere::kCredentialFieldLen && i < n; ++i)
        scratch[i] = sphere::kRedactionFill;
    char redacted[160];
    std::snprintf(redacted, sizeof(redacted), "%s [password redacted]",
                  note ? note : "");
    LogPacket(dir, scratch, n, redacted);
}

// One iteration of the session pump. Waits up to `waitMs` for socket data,
// then services whatever is due. A host with several sessions calls this on
// each of them in turn.
void Client::Tick(int waitMs) {
    if (finished_) return;
    if (state_ == State::Failed || sock_.Closed()) {
        finished_ = true;
        if (exitCode_ == 0 && state_ == State::Failed) exitCode_ = 4;
        return;
    }

    u8 rxbuf[8192];
    {
        // Liveness heartbeat — surface server silence after 5s.
        auto now = std::chrono::steady_clock::now();
        auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                        now - lastActivity_).count();
        if (!stalledLogged_ && idle >= 5 &&
            state_ != State::InWorld &&
            state_ != State::AwaitingServerList &&
            state_ != State::AwaitingCharacterList) {
            LogWarn(
                "[stall] no packets for %llds, state=%s, "
                "tcp still open=%d\n",
                static_cast<long long>(idle),
                StateName(static_cast<int>(state_)),
                sock_.IsOpen() && !sock_.Closed() ? 1 : 0);
            stalledLogged_ = true;
        }
        // Wait briefly for socket data; periodically pump stdin speech.
        int rd = sock_.WaitReadable(waitMs);
        if (rd < 0) {
            LogWarn("[net] select error; bailing\n");
            exitCode_ = 4; finished_ = true; return;
        }
        if (rd > 0) {
            int n = sock_.RecvSome(rxbuf, sizeof(rxbuf));
            if (n < 0) {
                LogWarn("[net] socket closed by peer.\n");
                LogEvent("disconnect", "recv returned -1 (RST or FIN)");
                finished_ = true;
                return;
            }
            if (n > 0) {
                lastActivity_ = std::chrono::steady_clock::now();
                stalledLogged_ = false;
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
                        LogWarn("[huffman] malformed compressed stream\n");
                        LogEvent("huffman_error", "malformed code in game stream");
                        exitCode_ = 4; finished_ = true; return;
                    }
                    feed = rxScratch_.data();
                    feed_n = rxScratch_.size();
                    // Partial Huffman block: nothing framed yet, so end this
                    // tick and let the next one add more bytes. (Was
                    // `continue` when this body was a while-loop.)
                    if (feed_n == 0) return;
                }
                if (!stream_.FeedBytes(feed, feed_n)) {
                    LogWarn("[net] stream buffer overflow\n");
                    exitCode_ = 4; finished_ = true; return;
                }
                for (;;) {
                    const u8* pkt = nullptr;
                    usize pkt_size = 0;
                    const char* err = nullptr;
                    if (!stream_.TryNext(&pkt, &pkt_size, &err)) {
                        if (err) {
                            ReportUnframeableStream(err);
                            exitCode_ = 4; finished_ = true; return;
                        }
                        break;
                    }
                    Dispatch(pkt, pkt_size);
                }
            }
        }

        // Logout: send 0xD1, give the server a moment to answer, then close
        // the socket -- the close is the logout as far as Sphere is concerned.
        if (loggingOut_) {
            const i64 waited = NowMs() - logoutSentMs_;
            if (logoutAcked_ || waited >= kLogoutGraceMs) {
                LogInfo("[net] closing connection (%s)\n",
                        logoutAcked_ ? "logout acknowledged"
                                     : "no ack within grace period");
                LogEvent("logout_complete",
                         logoutAcked_ ? "acked" : "grace timeout");
                sock_.Close();
                finished_ = true;
                return;
            }
        }

        if (state_ == State::InWorld) {
            PumpStdinCommand();
            PumpDirectSteps();
            if (scenario_) scenario_->Tick(*this, NowMs());
            BotTick();
            PurgeOutOfRange();  // cull mobiles/containers past viewRange_ (queues leave events)
            uo::js::TickClientEvents(NowMs());  // dispatch JS events + reject timeouts
            js_.Tick();   // drive script timers + drain the JS job queue
            RenderTick();

            // Keepalive — mirrors Packet_BuildKeepalive @ 0x4279B0 +
            // GameLoop_Update @ 0x4BF720: original client emits 0x73
            // (cmd + 0x00 sequence) whenever 60s pass without TCP
            // activity. Without it, UO Demo / Sphere shards RST the
            // connection on next client send.
            const auto now_ms2 =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            (void)now_ms2;
            PumpKeepalive();
        }
    }
}

// ---------------------------------------------------------------------------
// Dispatcher — mirrors Packet_Dispatch @ 0x42DD80 as a 1:1 switch. Every
// opcode the original handles by name appears here; anything else routes
// to OnUnknown so the new client logs but doesn't crash.
// ---------------------------------------------------------------------------
void Client::Dispatch(const u8* data, usize size) {
    if (cfg_.logPackets) LogPacket(Direction::In, data, size);

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
        case 0x24: OnDrawContainer(data, size); break;
        case 0x25: OnAddItemToContainer(data, size); break;
        case 0x3C: OnContainerContents(data, size); break;
        case 0x74: OnVendorShopData(data, size); break;
        case 0x3B: OnVendorOfferAccept(data, size); break;
        case 0x88: OnOpenPaperdoll(data, size); break;
        case 0x77: OnMobileMove(data, size); break;
        case 0x78: OnMobileIncoming(data, size); break;
        case 0x98: OnMobName(data, size); break;
        case 0x6C: OnTargetCursor(data, size); break;
        case 0x2D: OnMobileAttributes(data, size); break;
        case 0x2E: OnEquipItem(data, size); break;
        case 0x6E: OnCharacterAnimation(data, size); break;
        case 0x2F: OnSwing(data, size); break;
        case 0x72: OnWarMode(data, size); break;
        case 0x2C: OnResurrectionMenu(data, size); break;
        case 0x7C: OnOpenDialog(data, size); break;
        case 0xAF: OnDeathAnimation(data, size); break;
        case 0x3A: OnSkills(data, size); break;
        case 0x4E: OnPersonalLightLevel(data, size); break;
        case 0x4F: OnOverallLightLevel(data, size); break;
        case 0xD1: OnLogoutAck(data, size); break;

        // Common in-world packets we just log + ignore for M1.
        case 0x23: case 0x53:
        case 0x54: case 0x5B: case 0x65: case 0x6D:
        case 0x70:
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

    // Sphere/Source-X relay semantics (verified in the local checkout):
    //
    //  * Source-X advertises its own ServIP/ServPort here — for a
    //    single-server shard that is the endpoint we are already talking to.
    //    PacketServerRelay::onSent (src/network/send.cpp:2823-2830) calls
    //    m_Crypt.InitFast(customerId, CONNECT_GAME) "in case the client
    //    decides not to establish a new connection", so the existing socket
    //    is switched to the game protocol server-side. It then expects a bare
    //    0x91 as the next bytes on the wire.
    //
    //    Re-sending the 4-byte seed here (as a fresh TCP connection would)
    //    corrupts that: Source-X would read 0xAC.. as a packet opcode, find no
    //    handler and discard the whole buffer
    //    (src/network/CNetworkInput.cpp:399-406), stalling the login until
    //    DeadSocketTime. So: same endpoint -> no seed, 0x91 only.
    //
    //  * A different endpoint is a real relay: close, reconnect, send the
    //    seed the server just handed us, then 0x91.
    //
    // Nocrypt note: our 0x91 arrives in plaintext. Source-X routes it through
    // CCrypto::RelayGameCryptStart (src/common/crypto/CCrypto.cpp:318-405);
    // the no-crypt key is key index 0 with client version 0
    // (CCryptoKeysHolder::addNoCryptKey, :58-64), so the "< 2.0.4 does not
    // double-encrypt" branch runs and the packet passes through untouched.
    const char* connect_host = ip;
    if (cfg_.gameHostOverride && cfg_.gameHostOverride[0]) {
        connect_host = cfg_.gameHostOverride;
    } else if (gameServerIp_ == 0) {
        connect_host = cfg_.loginHost;
    }

    u16 connect_port = gameServerPort_;
    if (cfg_.gamePortOverride != 0) connect_port = cfg_.gamePortOverride;

    const bool overridden =
        (cfg_.gameHostOverride && cfg_.gameHostOverride[0]) ||
        (cfg_.gamePortOverride != 0);

    // Same endpoint as the socket we already hold? Then stay on it. Compared
    // against the socket's real peer, not the configured host string, so
    // "localhost" vs "127.0.0.1" cannot cause a spurious reconnect.
    // Rule + rationale: uo::sphere::StayOnLoginSocket (include/uo/sphere_rules.h).
    const bool same_endpoint =
        sphere::StayOnLoginSocket(gameServerIp_, connect_port,
                                  sock_.PeerIp(), sock_.PeerPort(), overridden);

    if (same_endpoint) {
        LogInfo("[0x8C] staying on the login socket "
                "(advertised %s:%u == peer 0x%08X:%u)\n",
                connect_host, connect_port, sock_.PeerIp(), sock_.PeerPort());
        LogEvent("relay_same_socket", "no seed re-sent; 0x91 follows");
        SendGameLogin();
        return;
    }

    LogInfo("[0x8C] relaying to %s:%u (new connection)\n",
            connect_host, connect_port);
    LogEvent("relay_reconnect", connect_host);
    state_ = State::ConnectingToGameServer;
    stream_.Reset();
    sock_.Close();
    if (!sock_.Connect(connect_host, connect_port)) {
        LogWarn("[net] relay connect to %s:%u failed\n", connect_host, connect_port);
        state_ = State::Failed;
        return;
    }
    {
        u8 seedbuf[4];
        build::Seed(seedbuf, gameSeed_);
        if (!sock_.SendAll(seedbuf, sizeof(seedbuf))) {
            state_ = State::Failed;
            return;
        }
        LogInfo("[seed] game-server seed 0x%08X sent\n", gameSeed_);
    }
    SendGameLogin();
}

// 0x91 on whatever socket we currently hold. Huffman starts the moment the
// server processes it: Source-X compresses every outbound packet once the
// connection is CONNECT_GAME (src/network/CNetworkOutput.cpp:416).
void Client::SendGameLogin() {
    u8 buf[128];
    const usize n = build::GameLogin(buf, gameSeed_, cfg_.username, cfg_.password);
    if (!Send(buf, n, "0x91 GameLogin")) {
        state_ = State::Failed;
        return;
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
        // No character on this account yet. Source-X has no console verb to
        // create one (CAccounts::sm_szVerbKeys, src/game/clients/CAccount.cpp:309),
        // so the supported path is the ordinary client creation packet: the
        // server validates and applies its own rules in PacketCreate::doCreate /
        // CChar::InitPlayer.
        if (cfg_.createCharIfMissing && !charCreateSent_) {
            SendCreateCharacter();
            return;
        }
        LogWarn("[ui] no characters on this account; aborting\n");
        LogEvent("charlist_empty", "no characters and creation disabled");
        state_ = State::Failed;
        return;
    }

    // Pick by name when configured, else by slot index
    // (uo::sphere::SelectCharacterSlot, include/uo/sphere_rules.h).
    const char* names[8] = {nullptr};
    for (int i = 0; i < charCount_ && i < 8; ++i) names[i] = charSlots_[i].name;
    selectedChar_ = sphere::SelectCharacterSlot(names, charCount_,
                                                cfg_.charName, cfg_.charSlot);
    if (cfg_.charName && cfg_.charName[0]) {
        if (selectedChar_ < 0) {
            if (cfg_.createCharIfMissing && !charCreateSent_) {
                LogInfo("[ui] '%s' not on this account; creating it\n",
                        cfg_.charName);
                SendCreateCharacter();
                return;
            }
            LogWarn("[ui] character '%s' not found on this account\n",
                    cfg_.charName);
            LogEvent("charlist_name_miss", cfg_.charName);
            state_ = State::Failed;
            return;
        }
    }

    if (selectedChar_ < 0 || selectedChar_ >= charCount_ ||
        !charSlots_[selectedChar_].name[0]) {
        LogWarn("[ui] invalid character slot %d (of %d)\n",
                selectedChar_, charCount_);
        LogEvent("charlist_bad_slot", "configured slot is empty or out of range");
        state_ = State::Failed;
        return;
    }
    LogInfo("[ui] playing slot %d ('%s')\n",
            selectedChar_, charSlots_[selectedChar_].name);

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
    if (cfg_.enableStdin) {
        // stdin has exactly one reader per process; a second session must not
        // race it for keystrokes (M1.5 state audit item 12).
        StartStdinThread();
        LogInfo("\nType to chat (Enter to send). Ctrl-C to quit.\n\n");
    }
    // Peek into the backpack so its contents are known up front. The worn
    // backpack serial may arrive with the player's 0x78 before or after this;
    // open now if known, else TryOpenBackpackOnLogin fires from SetMobileEquip.
    openBackpackPending_ = true;
    TryOpenBackpackOnLogin();
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
    // Server-set object update/cull radius (5..18 in the official client).
    // Drives PurgeOutOfRange; clamp to a sane window and ignore 0.
    if (data[1] >= 5 && data[1] <= 24) viewRange_ = data[1];
    LogInfo("[0xC8] view range = %u\n", data[1]);
}

// ---------------------------------------------------------------------------
// 0x4F Overall Light Level (2 bytes): cmd, level. 0x00 = day/bright,
// 0x1F = black. Mirrors Packet_HandleOverallLightLevel @0x4152F0. Drives the
// renderer's ambient darkening (Renderer::ApplyDarkness).
// ---------------------------------------------------------------------------
void Client::OnOverallLightLevel(const u8* data, usize size) {
    if (size < 2) return;
    overallLightLevel_ = data[1];
    if (verboseConsole_) LogInfo("[0x4F] overall light = %u\n", data[1]);
}

// ---------------------------------------------------------------------------
// 0x4E Personal Light Level (6 bytes): cmd, serial(4 BE), level. The original
// (Packet_HandlePersonalLightLevel @0x41D3C0 -> Entity_SetPersonalLightLevel)
// only feeds the global darkness when the target is the local player; we do the
// same. Higher personal light reduces darkness (night-sight / personal glow).
// ---------------------------------------------------------------------------
void Client::OnPersonalLightLevel(const u8* data, usize size) {
    if (size < 6) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    if (serial != playerSerial_) return;
    personalLightLevel_ = data[5];
    if (verboseConsole_) LogInfo("[0x4E] personal light = %u\n", data[5]);
}

// ---------------------------------------------------------------------------
// 0xA1 Update Mobile Hits (9 bytes): cmd, serial(4 BE), maxHp(2 BE),
// curHp(2 BE). For our own serial we track HP so a drop while travelling
// trips the combat-interrupt hook (actual engage/flee/recall are TODO).
// ---------------------------------------------------------------------------
void Client::OnMobileHp(const u8* data, usize size) {
    if (size < 9) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    if (serial != playerSerial_) {
        // Foreign mobile (e.g. a foe we are fighting): cache its health so the
        // bot can judge whether a fight is winnable. Often a 0..max ratio.
        for (auto& m : mobileCache_) {
            if (m.serial == serial) {
                m.hpMax = static_cast<i32>(LoadBE16(data + 5));
                m.hpCur = static_cast<i32>(LoadBE16(data + 7));
                break;
            }
        }
        return;
    }
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
    if (serial != playerSerial_) {
        for (auto& m : mobileCache_) {
            if (m.serial == serial) {
                m.hpMax = static_cast<i32>(LoadBE16(data + 5));
                m.hpCur = static_cast<i32>(LoadBE16(data + 7));
                break;
            }
        }
        return;
    }
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
    // stack count for piles; for a corpse (0x2006) this field is the dead body
    // graphic (CObjectManager_HandleMove copies it to CCorpse::stackCount).
    u16 amount = 0;
    if (hasAmount) { if (!avail(2)) return; amount = LoadBE16(data + p); p += 2; }

    if (!avail(2)) return;
    const u16 xw = LoadBE16(data + p); p += 2;
    const bool hasDir = (xw & 0x8000) != 0;
    const i32 x = xw & 0x7FFF;

    if (!avail(2)) return;
    const u16 yw = LoadBE16(data + p); p += 2;
    const bool hasHue = (yw & 0x8000) != 0;
    const bool hasFlags = (yw & 0x4000) != 0;
    const i32 y = yw & 0x3FFF;

    // direction (facing) byte — present when x has 0x8000. It is the entity's
    // facing, NOT a graphic offset, so it must be skipped here, not added to
    // the art (adding it is what turned lamps into logs). A corpse keeps it as
    // the pose facing (CCorpse stores it in facingFlags).
    u8 dir = 0;
    if (hasDir) { if (!avail(1)) return; dir = data[p]; p += 1; }
    if (!avail(1)) return;
    const i8 z = static_cast<i8>(data[p]);
    p += 1;
    u16 hue = 0;
    if (hasHue) { if (!avail(2)) return; hue = LoadBE16(data + p); p += 2; }
    if (hasFlags) { if (!avail(1)) return; p += 1; }

    // Track every world item so the renderer can draw dynamic server objects
    // (lamp posts, doors, decor). Keyed by serial; removed on 0x1D.
    const bool isNewItem = items_.find(serial) == items_.end();
    items_[serial] = ItemObj{itemId, x, y, z, gfxOffset, hue};
    if (isNewItem) itemOrder_.push_back(serial);
    while (items_.size() > kMaxItemCache && !itemOrder_.empty()) {
        const u32 oldSerial = itemOrder_.front();
        itemOrder_.pop_front();
        items_.erase(oldSerial);
    }

    // A corpse (0x2006) is rendered as the dead body's death-pose frame, not as
    // the flat item sprite. The amount field is the body graphic and the dir
    // byte is the facing it died at. deathAction/equip/deadMobile come from the
    // 0xAF that preceded this object (already in corpses_); a pre-existing
    // corpse (no death seen) defaults to the normal-death group for its body.
    if (itemId == 0x2006) {
        CorpseObj& c = corpses_[serial];
        c.body = amount;
        c.dir  = dir & 7u;
        c.hue  = hue;
        if (c.deathAction == 0)
            c.deathAction = DeathActionForBody(amount, true);
    }
}

// 0x1D Delete Object (5 bytes): cmd + serial(4 BE). Drop it from both caches.
void Client::OnDeleteObject(const u8* data, usize size) {
    if (size < 5) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    items_.erase(serial);
    corpses_.erase(serial);
    itemOrder_.erase(std::remove(itemOrder_.begin(), itemOrder_.end(), serial),
                     itemOrder_.end());
    bool delayedMobile = false;
    for (auto it = mobileCache_.begin(); it != mobileCache_.end(); ++it) {
        if (it->serial != serial) continue;
        if (it->deadRemoveMs != 0) {
            it->deadRemoveMs = NowMs() + 1500;
            delayedMobile = true;
        } else {
            mobileCache_.erase(it);
            uo::js::EmitMobileLeave(serial);  // -> World 'mobile_leave' (serial)
        }
        break;
    }
    if (!delayedMobile) mobileNames_.erase(serial);

    // Drop the object from any open container, and close it if it was itself a
    // container. Container item serials are full item serials (< 0x80000000),
    // so the 0x7FFFFFFF mask above is a no-op for them.
    for (auto& kv : containerItems_) {
        auto& list = kv.second;
        list.erase(std::remove_if(list.begin(), list.end(),
                       [&](const ContainerItem& e) { return e.serial == serial; }),
                   list.end());
    }
    containerItems_.erase(serial);
    openContainers_.erase(std::remove_if(openContainers_.begin(), openContainers_.end(),
                              [&](const OpenContainer& c) { return c.serial == serial; }),
                          openContainers_.end());
}

// Range cull, mirroring CObjectManager_UpdateMovement @0x4c8b00: the official
// client drops every entity past Entity_IsWithinWorldRange (a Chebyshev/square
// radius of viewRange_ tiles around the player) and re-acquires it from the
// server's re-send when it comes back. We do the same for tracked mobiles and
// open world-containers so those caches reflect "what's around us" and never
// grow stale. JS holds serials, not pointers, so a purged record simply reads
// back as exists=false; we also emit a leave/close event for prompt cleanup.
void Client::PurgeOutOfRange() {
    if (state_ != State::InWorld) return;
    // Self-gate: only when the player has actually changed tile since last run.
    if (playerX_ == lastPurgeX_ && playerY_ == lastPurgeY_) return;
    lastPurgeX_ = playerX_;
    lastPurgeY_ = playerY_;

    const i32 r  = viewRange_ > 0 ? viewRange_ : 18;
    const i32 px = playerX_, py = playerY_;
    auto outOfRange = [&](i32 x, i32 y) {
        i32 dx = x - px; if (dx < 0) dx = -dx;
        i32 dy = y - py; if (dy < 0) dy = -dy;
        return dx > r || dy > r;  // Chebyshev, matching Entity_IsWithinWorldRange
    };

    // Mobiles — never the local player, and keep a mobile whose death animation
    // is still playing (its own deadRemoveMs timer removes it).
    for (auto it = mobileCache_.begin(); it != mobileCache_.end();) {
        if (it->serial != playerSerial_ && it->deadRemoveMs == 0 &&
            outOfRange(it->x, it->y)) {
            const u32 serial = it->serial;
            mobileNames_.erase(serial);
            it = mobileCache_.erase(it);
            uo::js::EmitMobileLeave(serial);  // -> World 'mobile_leave' (serial)
        } else {
            ++it;
        }
    }

    // Open world containers (chests/corpses) — close any whose backing world
    // item has left range. Player-owned containers (backpack/bank) have no
    // items_ entry, so they never match here and stay open.
    for (auto it = openContainers_.begin(); it != openContainers_.end();) {
        auto pos = items_.find(it->serial);
        if (pos != items_.end() && outOfRange(pos->second.x, pos->second.y)) {
            const u32 serial = it->serial;
            containerItems_.erase(serial);
            it = openContainers_.erase(it);
            uo::js::EmitContainerClose(serial);  // -> World 'container_close'
        } else {
            ++it;
        }
    }
}

// 0x24 Draw Container (7 bytes): cmd, serial(4 BE), gumpId(2 BE). The real
// client opens a gump bound to the container entity (Packet_HandleDrawContainer
// @ 0x417f70); we just register it so DrawContainers() can list the contents.
// gumpId 0xFFFF closes it; 500/501 = bank, 10/48 = paperdoll.
void Client::OnDrawContainer(const u8* data, usize size) {
    if (size < 7) return;
    const u32 serial = LoadBE32(data + 1);
    const u16 gumpId = LoadBE16(data + 5);

    auto sameSerial = [&](const OpenContainer& c) { return c.serial == serial; };
    if (gumpId == 0xFFFF) {  // close / clear
        openContainers_.erase(std::remove_if(openContainers_.begin(),
                                  openContainers_.end(), sameSerial),
                              openContainers_.end());
        containerItems_.erase(serial);
        return;
    }
    if (gumpId == 0x30) {  // vendor buy gump — `serial` is the vendor MOBILE
        uo::js::EmitVendorOffer(serial);  // builds payload from pendingVendor_ now
        pendingVendor_.clear();
        pendingVendorGroups_ = 0;
        LogInfo("[0x24] vendor buy gump for 0x%08X\n", serial);
        return;  // not a real container; don't register/draw it
    }
    auto it = std::find_if(openContainers_.begin(), openContainers_.end(), sameSerial);
    if (it == openContainers_.end())
        openContainers_.push_back(OpenContainer{serial, gumpId});
    else
        it->gumpId = gumpId;
    LogInfo("[0x24] open container 0x%08X gump=%u\n", serial, gumpId);
    uo::js::EmitContainerOpen(serial, gumpId);  // -> World 'container_open'
}

// 0x3C Container Contents (variable): count(2 BE), then `count` 19-byte records
// of serial(4) graphic(2) gfxOffset(1) amount(2) x(2) y(2) container(4) hue(2).
// This 2.0.7 format has NO grid/slot byte (Packet_HandleContainerItems
// @ 0x418990). A 0x3C is a full dump for its container(s), so each referenced
// container's list is cleared once before refilling.
void Client::OnContainerContents(const u8* data, usize size) {
    if (size < 5) return;
    const u16 count = LoadBE16(data + 3);
    usize p = 5;
    std::unordered_set<u32> cleared;
    for (u16 i = 0; i < count; ++i) {
        if (p + 19 > size) break;
        ContainerItem ci{};
        ci.serial    = LoadBE32(data + p); p += 4;
        ci.graphic   = LoadBE16(data + p); p += 2;
        ci.gfxOffset = data[p];            p += 1;
        ci.amount    = LoadBE16(data + p); p += 2;
        ci.x         = LoadBE16(data + p); p += 2;
        ci.y         = LoadBE16(data + p); p += 2;
        const u32 cont = LoadBE32(data + p); p += 4;
        ci.hue       = LoadBE16(data + p); p += 2;

        auto& list = containerItems_[cont];
        if (cleared.insert(cont).second) list.clear();
        list.push_back(ci);
        if (std::find_if(openContainers_.begin(), openContainers_.end(),
                [&](const OpenContainer& c) { return c.serial == cont; })
            == openContainers_.end())
            openContainers_.push_back(OpenContainer{cont, 0});
    }
    LogInfo("[0x3C] container contents: %u item(s)\n", count);
    const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
    if (backpack != 0 && cleared.count(backpack) != 0) {
        if (!backpackContentsKnown_) {
            char ev[96];
            std::snprintf(ev, sizeof(ev), "serial=0x%08X items=%u",
                          backpack, count);
            LogEvent("backpack_contents", ev);
        }
        backpackContentsKnown_ = true;
    }
}

// 0x74 SHOP_DATA (variable): the vendor's buy window prices/names. Layout
// (PacketManager_MakePacket_SHOP_DATA @0x0049C053): containerSerial(4 BE),
// count(1), then per item price(4 BE), nameLen(1), name. The matching 0x3C for
// this container arrived first, so we zip prices with the already-stored
// ContainerItem rows to recover each item's serial / graphic / amount — but
// MIRROR-ORDERED (see the loop). Rows accumulate in pendingVendor_ until the
// 0x24 gump 0x30 (vendor serial) finalizes the session in OnDrawContainer.
void Client::OnVendorShopData(const u8* data, usize size) {
    if (size < 8) return;
    const u32 contSerial = LoadBE32(data + 3);
    const u8 count = data[7];
    usize p = 8;
    const u8 layer = (pendingVendorGroups_ == 0) ? 0x1A : 0x1B;  // 26 stock / 27 offered
    ++pendingVendorGroups_;

    auto it = containerItems_.find(contSerial);
    const std::vector<ContainerItem>* stock =
        (it != containerItems_.end()) ? &it->second : nullptr;
    const std::size_t stockN = stock ? stock->size() : 0;

    // CRITICAL: 0x74 SHOP_DATA walks the vendor's contents list FORWARD
    // (spatialNext), but the 0x3C that preceded it was built in REVERSE
    // (PacketManager_MakePacket_MULTI_OBJ_TO_OBJ @0x00499EEB: backward pass via
    // spatialPrev). So the two packets are mirror-ordered: 0x74 row i pairs with
    // stock row (stockN-1-i), NOT stock[i]. Zipping by the same index sends the
    // wrong serial (we sent the Red-Potion serial for "bandage").
    for (u8 i = 0; i < count; ++i) {
        if (p + 5 > size) break;
        const u32 price = LoadBE32(data + p); p += 4;
        const u8 nameLen = data[p];           p += 1;
        if (p + nameLen > size) break;
        std::string name(reinterpret_cast<const char*>(data + p), nameLen);
        p += nameLen;
        if (const auto z = name.find('\0'); z != std::string::npos) name.resize(z);
        if (!stock || i >= stockN)
            continue;  // no 0x3C row to pair: can't buy without a serial
        const ContainerItem& ci = (*stock)[stockN - 1 - i];   // reverse pairing
        pendingVendor_.push_back(VendorItem{ci.serial, ci.graphic, ci.amount, price, layer, name});
    }
    LogInfo("[0x74] vendor shop data: cont=0x%08X %u item(s), layer=0x%02X\n",
            contSerial, count, layer);
}

// 0x3B OFFERACCEPT (variable, server->client): closes the vendor gump after a
// buy/sell completes (PacketManager_MakePacket_OFFERACCEPT @0x0049B398:
// vendorSerial(4 BE), flag(1)). We forward it as `vendor_done` so a parked buy
// flow can stop waiting. (The same 0x3B id is what we SEND to buy.)
void Client::OnVendorOfferAccept(const u8* data, usize size) {
    if (size < 8) return;
    const u32 vendor = LoadBE32(data + 3);
    const u8 flag = data[7];
    LogInfo("[0x3B] vendor transaction closed: vendor=0x%08X flag=%u\n", vendor, flag);
    uo::js::EmitVendorDone(vendor, flag);
}

// 0x88 OPEN_PAPERDOLL (66 bytes): serial(4 BE), title[60] (NUL-padded ASCII),
// flags(1). The title is "<name> the <job>" (CNPC_PaperdollTitle_VT), the only
// client-visible carrier of a vendor's job. Cache it and emit `paperdoll`.
void Client::OnOpenPaperdoll(const u8* data, usize size) {
    if (size < 66) return;
    const u32 serial = LoadBE32(data + 1);
    char title[61];
    std::memcpy(title, data + 5, 60);
    title[60] = '\0';
    paperdollTitles_[serial] = title;
    LogInfo("[0x88] paperdoll 0x%08X: \"%s\"\n", serial, title);
    uo::js::EmitPaperdoll(serial, title);
}

// 0x25 Add Single Item to Container (20 bytes): one 0x3C record without the
// leading count (Packet_HandleAddItemToContainer @ 0x418800). Upserts by serial.
void Client::OnAddItemToContainer(const u8* data, usize size) {
    if (size < 20) return;
    ContainerItem ci{};
    ci.serial    = LoadBE32(data + 1);
    ci.graphic   = LoadBE16(data + 5);
    ci.gfxOffset = data[7];
    ci.amount    = LoadBE16(data + 8);
    ci.x         = LoadBE16(data + 10);
    ci.y         = LoadBE16(data + 12);
    const u32 cont = LoadBE32(data + 14);
    ci.hue       = LoadBE16(data + 18);

    auto& list = containerItems_[cont];
    auto it = std::find_if(list.begin(), list.end(),
                  [&](const ContainerItem& e) { return e.serial == ci.serial; });
    if (it == list.end()) list.push_back(ci);
    else *it = ci;
    if (std::find_if(openContainers_.begin(), openContainers_.end(),
            [&](const OpenContainer& c) { return c.serial == cont; })
        == openContainers_.end())
        openContainers_.push_back(OpenContainer{cont, 0});
}

const char* Client::MobileName(u32 serial) const {
    auto it = mobileNames_.find(serial);
    return (it == mobileNames_.end()) ? nullptr : it->second.c_str();
}

const char* Client::PaperdollTitle(u32 serial) const {
    auto it = paperdollTitles_.find(serial);
    return (it == paperdollTitles_.end()) ? nullptr : it->second.c_str();
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

    uo::js::EmitJournalEvent(e.text.c_str(), e.type, e.sourceSerial, e.hue,
                             static_cast<int>(e.ownerKind));  // -> Player 'journal'
    journal_.push_back(std::move(e));
    while (journal_.size() > kMaxJournalEntries)
        journal_.pop_front();
}

void Client::UpdateMobile(u32 serial, i32 x, i32 y, i8 z, u8 dir, u16 body,
                          u16 hue, bool hasHue, u8 statusFlags,
                          bool hasStatusFlags, int notoriety) {
    const bool running = (dir & 0x80u) != 0;
    const bool warMode = (statusFlags & 0x40u) != 0;
    if (serial == playerSerial_) {
        // Don't treat ourselves as an obstacle, but do learn our own body so
        // the renderer can draw the local player (and facing for arrow walk).
        if (body) playerBody_ = body;
        if (hasHue) playerHue_ = hue;
        if (hasStatusFlags) playerWarMode_ = warMode;
        playerFacing_ = static_cast<u8>(dir & 0x07);
        player_.serial = serial;
        if (body) player_.body = body;
        player_.x = x;
        player_.y = y;
        player_.z = z;
        player_.facing = playerFacing_;
        player_.running = running;
        return;
    }
    const i64 now = NowMs();
    for (auto& m : mobileCache_) {
        if (m.serial == serial) {
            if (m.x != x || m.y != y) {
                // Real step -> walk anim + slide interp. Only a single-cell
                // step slides; a larger jump is a teleport/resync and snaps.
                const bool adjacent = (x - m.x <= 1 && m.x - x <= 1) &&
                                      (y - m.y <= 1 && m.y - y <= 1);
                m.prevX = adjacent ? m.x : x;
                m.prevY = adjacent ? m.y : y;
                m.movedMs = now;
            }
            m.x = x;
            m.y = y;
            m.z = z;
            m.dir = static_cast<u8>(dir & 0x07);
            m.running = running;
            if (hasStatusFlags) m.warMode = warMode;
            if (body) m.body = body;
            if (hasHue) m.hue = hue;
            m.deadRemoveMs = 0;
            m.seenMs = now;
            if (notoriety >= 0) m.noto = static_cast<u8>(notoriety);
            return;
        }
    }
    if (mobileCache_.size() >= kMobileCacheMax) mobileCache_.pop_front();
    mobileCache_.push_back({serial, x, y, z, static_cast<u8>(dir & 0x07),
                            body, hasHue ? hue : 0u, now});
    mobileCache_.back().running = running;
    mobileCache_.back().warMode = hasStatusFlags ? warMode : false;
    if (notoriety >= 0) mobileCache_.back().noto = static_cast<u8>(notoriety);
}

// 0x77 Mobile Move (17 bytes): cmd, serial(4), body(2), x(2), y(2), z(1), dir(1) ...
void Client::OnMobileMove(const u8* data, usize size) {
    if (size < 13) return;
    const bool hasStatus = size >= 16;
    UpdateMobile(LoadBE32(data + 1), LoadBE16(data + 7), LoadBE16(data + 9),
                 static_cast<i8>(data[11]), data[12], LoadBE16(data + 5),
                 hasStatus ? LoadBE16(data + 13) : 0u, hasStatus,
                 hasStatus ? data[15] : 0u, hasStatus);
}

// 0x78 Mobile Incoming (variable): cmd, len(2), serial(4), body(2), x(2),
// y(2), z(1), dir(1), hue(2), flag(1), notoriety(1), then an equipment list of
// { serial(4), graphic(2), layer(1), hue(2 iff graphic&0x8000) } records ended
// by a zero serial. Layout verified vs Packet_HandleUpdatePlayer @0x4174C0
// (the real 0x78 parser; its "0x77" IDB label is wrong).
void Client::OnMobileIncoming(const u8* data, usize size) {
    if (size < 19) return;
    const u32 serial = LoadBE32(data + 3);
    const bool isNew = (serial != playerSerial_) && !FindMobileBySerial(serial);
    UpdateMobile(serial, LoadBE16(data + 9), LoadBE16(data + 11),
                 static_cast<i8>(data[13]), data[14], LoadBE16(data + 7),
                 LoadBE16(data + 15), true, data[17], true, data[18]);
    if (isNew)
        uo::js::EmitMobileEvent(serial);  // -> World 'mobile' (serial; use Mobiles.get)

    // Equipment list begins after the 19-byte header.
    std::vector<EquipObj> equip;
    usize p = 19;
    while (p + 7u <= size) {                  // serial(4)+graphic(2)+layer(1)
        const u32 itemSerial = LoadBE32(data + p);
        if (itemSerial == 0) break;           // zero serial terminates the list
        p += 4;
        u16 graphic = LoadBE16(data + p); p += 2;
        const u8 layer = data[p]; p += 1;
        const bool hasHue = (graphic & 0x8000) != 0;
        graphic &= 0x3FFFu;                   // item id (drop hue flag/high bits)
        u16 hue = 0;
        if (hasHue) { if (p + 2u > size) break; hue = LoadBE16(data + p); p += 2; }
        equip.push_back({layer, graphic, hue, itemSerial});
    }
    SetMobileEquip(serial, std::move(equip));
}

// 0x2F Swing / fight-occurring (10B): cmd, flag, attacker(4 BE), defender(4 BE).
// Server sends this on every weapon swing (combat.c PlaySwingAnimation). It is
// the earliest serial-bearing combat signal (long before HP changes), in EITHER
// direction:
//   - defender == us  -> a mob swung at us       -> 'attacked' (attacker serial)
//   - attacker == us  -> we are swinging at a foe -> 'combat'   (defender serial)
// The second case matters because this server surfaces an aggro'd fight as our
// own swings (also confirmed by 0xAA attack-approved); without it the bot would
// stand and trade blows while the script never noticed it was fighting.
void Client::OnSwing(const u8* data, usize size) {
    if (size < 10) return;
    const u32 attacker = LoadBE32(data + 2);
    const u32 defender = LoadBE32(data + 6);
    if (defender == playerSerial_ && attacker != 0)
        uo::js::EmitAttackedEvent(attacker);  // -> Player 'attacked' (serial)
    else if (attacker == playerSerial_ && defender != 0)
        uo::js::EmitCombatEvent(defender);    // -> Player 'combat' (serial)
}

// 0x2E Worn Item (15B fixed): cmd, item serial(4), graphic(2), pad(1),
// layer(1), mobile serial(4), hue(2). Layout per Packet_HandleWornItem
// @0x419910. Updates a single equipped layer on an already-cached mobile.
void Client::OnEquipItem(const u8* data, usize size) {
    if (size < 15) return;
    const u32 itemSerial = LoadBE32(data + 1);
    const u16 graphic = static_cast<u16>(LoadBE16(data + 5) & 0x3FFFu);
    const u8  layer   = data[8];
    const u32 mobile  = LoadBE32(data + 9);
    const u16 hue     = LoadBE16(data + 13);
    SetMobileEquipLayer(mobile, itemSerial, layer, graphic, hue);
}

// 0x6E Character Animation (14B fixed): cmd, serial(4), action(2),
// maxFrames(2), maxDuration(2), reverse(1), bounce(1), delay(1).
// Verified against client_2.0.7 Packet_HandleCharacterAnimation @0x41F450.
void Client::OnCharacterAnimation(const u8* data, usize size) {
    if (size < 14) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    const u16 action = LoadBE16(data + 5);
    const u16 maxFrames = LoadBE16(data + 7);
    const u16 maxDuration = LoadBE16(data + 9);
    const bool reverse = data[11] != 0;
    const bool bounce = data[12] != 0;
    const u16 delay = data[13];
    if (action > 0x22u) return;

    auto start = [&](ServerAnimState& a) {
        a.active = true;
        a.lastTickMs = NowMs();
        a.action = static_cast<u8>(action & 0xFFu);
        a.maxFrames = maxFrames;
        a.delayPerFrame = delay;
        a.maxDuration = maxDuration;
        a.currentFrame = (reverse && maxFrames != 0) ? static_cast<u16>(maxFrames - 1u) : 0u;
        a.currentDuration = (reverse && maxDuration != 0) ? static_cast<u16>(maxDuration - 1u) : 0u;
        a.pad = 0;
        a.renderedFrame = 0;
        a.reverse = reverse;
        a.bounce = bounce;
        a.hasRenderedFrame = false;
    };

    if (serial == playerSerial_) {
        start(playerServerAnim_);
    } else {
        for (auto& m : mobileCache_) {
            if (m.serial == serial) {
                start(m.serverAnim);
                m.lastAnimMs = NowMs();                       // for the JS threat meter
                m.lastAnimAction = static_cast<u8>(action & 0xFFu);
                break;
            }
        }
    }

    if (verboseConsole_) {
        LogInfo("[0x6E] anim serial=0x%08X action=%u frames=%u duration=%u rev=%u bounce=%u delay=%u\n",
                serial, static_cast<unsigned>(action), static_cast<unsigned>(maxFrames),
                static_cast<unsigned>(maxDuration), reverse ? 1u : 0u,
                bounce ? 1u : 0u, static_cast<unsigned>(delay));
    }
}

// 0x72 War Mode: cmd, mode, arg1, arg2, arg3. The original client stores the
// trailing args and reuses them on the next outbound war-mode request.
void Client::OnWarMode(const u8* data, usize size) {
    if (size < 5) return;
    playerWarMode_ = data[1] != 0;
    warModeArg1_ = data[2];
    warModeArg2_ = data[3];
    warModeArg3_ = data[4];
    if (verboseConsole_)
        LogInfo("[0x72] war mode %s args=%02X %02X %02X\n",
                playerWarMode_ ? "on" : "off", warModeArg1_, warModeArg2_, warModeArg3_);
}

// 0x2C Resurrection Menu (2B fixed): cmd, action. Server prompts the death /
// resurrect menu (action 0); we reply with 0x2C choice 1 (resurrect) or 2
// (ghost). Mirrors Packet_HandleResurrectionMenu @0x419080. We don't auto-reply
// here — the action is forwarded to JS so the bot decides (e.g. confirm a
// resurrection after asking a healer).
void Client::OnResurrectionMenu(const u8* data, usize size) {
    if (size < 2) return;
    const u8 action = data[1];
    LogInfo("[0x2C] resurrection menu action=%u\n", action);
    LogEvent("resurrect_menu", action == 1 ? "resurrect" : "prompt");
    uo::js::EmitResurrectMenu(action);  // -> Player 'resurrect_menu' ({action})
}

// 0x7C Open Dialog/Menu (variable): cmd, blockSize(2), dialogSerial(4 BE),
// menuId(2 BE), questionLen(1), question[], responseCount(1), then per option
// { model(2 BE), hue(2 BE), textLen(1), text[] }. Verified against
// Packet_HandleOpenDialog @0x420c80; the client answers with 0x7D
// (Packet_BuildDialogBoxResponse). Used here for the healer resurrect prompt.
// We parse it whole into activeDialog_ and forward it to JS (`dialog` event);
// nothing is auto-answered in C++ — the bot decides which option to pick.
void Client::OnOpenDialog(const u8* data, usize size) {
    // cmd(1) + blockSize(2) + serial(4) + menuId(2) + questionLen(1) = 10 min
    if (size < 10) return;
    usize p = 3;                                  // skip cmd + blockSize
    const u32 serial = LoadBE32(data + p); p += 4;
    const u16 menuId = LoadBE16(data + p); p += 2;
    const u8  qLen   = data[p];            p += 1;
    if (p + qLen + 1 > size) return;              // need question + responseCount

    ActiveDialog d;
    d.active = true;
    d.id = serial;
    d.menuId = menuId;
    d.question.assign(reinterpret_cast<const char*>(data + p), qLen);
    p += qLen;
    const u8 count = data[p]; p += 1;
    for (u8 i = 0; i < count; ++i) {
        if (p + 5 > size) break;                  // model(2)+hue(2)+textLen(1)
        DialogOption opt;
        opt.model = LoadBE16(data + p); p += 2;
        opt.hue   = LoadBE16(data + p); p += 2;
        const u8 tLen = data[p];        p += 1;
        if (p + tLen > size) break;
        opt.text.assign(reinterpret_cast<const char*>(data + p), tLen);
        p += tLen;
        d.options.push_back(std::move(opt));
    }

    activeDialog_ = std::move(d);
    LogInfo("[0x7C] dialog id=0x%08X menu=%u: \"%s\" (%zu options)\n",
            activeDialog_.id, activeDialog_.menuId, activeDialog_.question.c_str(),
            activeDialog_.options.size());
    for (usize i = 0; i < activeDialog_.options.size(); ++i)
        LogInfo("        %zu) %s\n", i + 1, activeDialog_.options[i].text.c_str());
    LogEvent("dialog", activeDialog_.question.c_str());
    uo::js::EmitDialogEvent();          // -> Player/World 'dialog' (built from activeDialog_)
}

// 0x7D answer to the 0x7C menu. index is 1-based (0 = cancel); model/hue echo
// the chosen option entry (the server matches the click that way).
void Client::SendDialogResponse(u32 id, u16 menuId, u16 index, u16 model, u16 hue) {
    u8 buf[13];
    Send(buf, build::DialogResponse(buf, id, menuId, index, model, hue),
         "0x7D DialogResponse");
}

// Answer the active dialog by 1-based option index (0 = cancel). Pulls model/hue
// from the stored option, sends 0x7D, and clears the dialog. Returns false if
// there is no active dialog or the index is out of range.
bool Client::AnswerDialog(u16 index) {
    if (!activeDialog_.active) {
        LogWarn("[0x7D] no active dialog to answer\n");
        return false;
    }
    u16 model = 0, hue = 0;
    if (index != 0) {
        if (index > activeDialog_.options.size()) {
            LogWarn("[0x7D] dialog index %u out of range (%zu options)\n",
                    index, activeDialog_.options.size());
            return false;
        }
        const DialogOption& opt = activeDialog_.options[index - 1];
        model = opt.model;
        hue = opt.hue;
    }
    LogInfo("[0x7D] answering dialog id=0x%08X menu=%u index=%u\n",
            activeDialog_.id, activeDialog_.menuId, index);
    SendDialogResponse(activeDialog_.id, activeDialog_.menuId, index, model, hue);
    activeDialog_.active = false;
    return true;
}

// Death anim group by body, verbatim from Mobile_PlayDeathAnimation @0x4C65C0.
// The same group (at its final frame) is the corpse's rendered pose.
u8 Client::DeathActionForBody(u16 body, bool normalDeath) {
    if (body < 0x96u)  return normalDeath ? 2u : 3u;
    if (body < 0xC8u)  return 8u;
    if (body < 0x190u) return normalDeath ? 8u : 12u;
    return normalDeath ? 21u : 22u;
}

// 0xAF Death Animation (13B fixed): cmd, mobile serial(4), corpse serial(4),
// unknown(4). Verified against Packet_HandleDeathAnimation @0x424CD0 and
// Mobile_PlayDeathAnimation @0x4C65C0 in client_2.0.7.
void Client::OnDeathAnimation(const u8* data, usize size) {
    if (size < 13) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    const u32 corpseSerial = LoadBE32(data + 5) & 0x7FFFFFFFu;
    const u32 unknown = LoadBE32(data + 9);
    const bool normalDeath = unknown == 0;

    // frames: 15 for the 0x96..0xC7 class, 5 otherwise (Mobile_PlayDeathAnimation).
    auto pickDeathAnim = [&](u16 body, u8& action, u16& frames) {
        action = DeathActionForBody(body, normalDeath);
        frames = (body >= 0x96u && body < 0xC8u) ? 15u : 5u;
    };

    auto start = [&](ServerAnimState& a, u16 body) {
        u8 action = 0;
        u16 frames = 0;
        pickDeathAnim(body, action, frames);
        a.active = true;
        a.lastTickMs = NowMs();
        a.action = action;
        a.maxFrames = frames;
        a.delayPerFrame = 0;
        a.maxDuration = 1;
        a.currentFrame = 0;
        a.currentDuration = 0;
        a.pad = 0;
        a.renderedFrame = 0;
        a.reverse = false;
        a.bounce = false;
        a.hasRenderedFrame = false;
    };

    // Record the corpse->dead-mobile mapping, the death group, and a snapshot of
    // the worn gear so the corpse object (0x2006) can render the body's death
    // pose wearing the same equipment. Mirrors DeathAnimationQueue_Add @0x4C3B50,
    // which is keyed by corpse serial; the corpse object may arrive after the
    // dying mobile has already been pruned, so we capture the gear now.
    auto registerCorpse = [&](u16 body, const std::vector<EquipObj>& equip, u16 mobHue) {
        if (corpseSerial == 0) return;
        CorpseObj& c = corpses_[corpseSerial];
        c.deadMobile = serial;
        c.deathAction = DeathActionForBody(body, normalDeath);
        c.equip = equip;
        if (c.body == 0) c.body = body;   // until the 0x1A object sets it
        if (c.hue == 0) c.hue = mobHue;
    };

    if (serial == playerSerial_) {
        start(playerServerAnim_, playerBody_);
        registerCorpse(playerBody_, playerEquip_, playerHue_);
    } else {
        for (auto& m : mobileCache_) {
            if (m.serial != serial) continue;
            start(m.serverAnim, m.body);
            m.deadRemoveMs = NowMs() + 2500;
            registerCorpse(m.body, m.equip, m.hue);
            break;
        }
    }

    if (verboseConsole_) {
        LogInfo("[0xAF] death serial=0x%08X corpse=0x%08X normal=%u unknown=0x%08X\n",
                serial, corpseSerial, normalDeath ? 1u : 0u, unknown);
    }
}

void Client::SetMobileEquip(u32 serial, std::vector<EquipObj> equip) {
    if (serial == playerSerial_) {
        playerEquip_ = std::move(equip);
        TryOpenBackpackOnLogin();   // backpack serial may have just become known
        return;
    }
    for (auto& m : mobileCache_)
        if (m.serial == serial) { m.equip = std::move(equip); return; }
}

void Client::OpenBackpack() {
    const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
    if (backpack == 0) { LogWarn("[backpack] serial unknown\n"); return; }
    u8 buf[8];
    Send(buf, build::DoubleClick(buf, backpack), "0x06 DoubleClick (backpack)");
    LogInfo("[backpack] opening 0x%08X\n", backpack);
}

// 0xD1 Logout acknowledgement (2 bytes: cmd + accepted). Source-X answers
// every 0xD1 with PacketLogoutAck (src/network/send.cpp:4631); the socket
// close that follows is what it treats as the actual logout
// (CClient::CharDisconnect, src/game/clients/CClient.cpp:166-233).
void Client::OnLogoutAck(const u8* data, usize size) {
    const u8 accepted = (size >= 2) ? data[1] : 0;
    LogInfo("[0xD1] logout acknowledged (accepted=%u)\n", accepted);
    LogEvent("logout_ack", accepted ? "accepted" : "refused");
    logoutAcked_ = true;
}

// ---------------------------------------------------------------------------
// Sphere adapter -- session-level helpers
// ---------------------------------------------------------------------------

// 0x00 create character. Values are requests only: Source-X clamps stats and
// skills in CChar::InitPlayer (src/game/chars/CChar.cpp:1770-1800) and runs its
// own f_onchar_create scripts, so nothing here grants the bot anything.
void Client::SendCreateCharacter() {
    charCreateSent_ = true;
    build::CreateCharacterParams p;
    p.name = (cfg_.charName && cfg_.charName[0]) ? cfg_.charName : "Bot";
    p.slot = static_cast<u32>(cfg_.charSlot < 0 ? 0 : cfg_.charSlot);
    u8 buf[128];
    const usize n = build::CreateCharacter(buf, p);
    LogInfo("[0x00] creating character '%s' in slot %u\n", p.name, p.slot);
    if (!Send(buf, n, "0x00 CreateCharacter")) {
        state_ = State::Failed;
        return;
    }
    LogEvent("char_create_sent", p.name);
    // Source-X calls Setup_Start() on success, so 0x1B/0x55 follow just as
    // they would after 0x5D; on failure it answers 0x82.
    state_ = State::AwaitingLoginConfirm;
}

// Client-side keepalive. Source-X drops a connection that sends nothing for
// DeadSocketTime (default 5 min; src/network/CNetworkInput.cpp:159-172) and
// answers 0x73 with its own 0x73 (src/network/receive.cpp:1335-1344).
void Client::PumpKeepalive() {
    if (!cfg_.enableKeepalive) return;
    if (playerSerial_ == 0) return;
    const u32 interval =
        cfg_.keepaliveIntervalMs ? cfg_.keepaliveIntervalMs : kKeepaliveIntervalMs;
    const i64 now = NowMs();
    if (lastActivityMs_ == 0) { lastActivityMs_ = now; return; }
    if (now - lastActivityMs_ < static_cast<i64>(interval)) return;

    u8 buf[4];
    const usize n = build::PingRequest(buf, pingSeq_++);
    if (Send(buf, n, "0x73 Ping (keepalive)")) {
        lastActivityMs_ = now;
        if (pingOutstanding_ < 4) ++pingOutstanding_;
    }
}

// An opcode with no length entry cannot be framed, and neither can any byte
// after it -- the stream is unrecoverable from here. Dump what we have so the
// opcode can be added to the Sphere overlay, then end the session cleanly
// instead of dying mid-loop.
void Client::ReportUnframeableStream(const char* err) {
    const usize pending = stream_.Pending();
    const u8* head = stream_.PendingData();
    const u8 cmd = pending ? head[0] : 0;

    char hex[3 * 64 + 1];
    usize hn = 0;
    const usize dump = pending < 64 ? pending : 64;
    for (usize i = 0; i < dump && hn + 3 < sizeof(hex); ++i)
        hn += static_cast<usize>(std::snprintf(hex + hn, sizeof(hex) - hn,
                                               "%02x ", head[i]));
    hex[hn] = '\0';

    LogError("[stream] %s: opcode 0x%02X, %zu byte(s) buffered\n",
             err, cmd, pending);
    LogError("[stream] head: %s\n", hex);
    LogError("[stream] framing is unrecoverable -- ending this session. "
             "Add a length for 0x%02X to include/uo/packet_lengths_sphere.h "
             "if the server is expected to send it.\n", cmd);

    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "opcode=0x%02X pending=%zu head=%s", cmd, pending, hex);
    LogEvent("stream_unframeable", detail);
    state_ = State::Failed;
}

// ---------------------------------------------------------------------------
// Player action API -- the adapter boundary (see Client.h)
// ---------------------------------------------------------------------------

void Client::ActionWalk(u8 dir, int count) {
    if (count <= 0) return;
    directStepsRun_ = cfg_.runWhenWalking;
    for (int i = 0; i < count; ++i) directSteps_.push_back(dir & 0x07);
    walkBatchStartX_ = playerX_;
    walkBatchStartY_ = playerY_;
    walkBatchActive_ = true;
    LogInfo("[action] walk dir=%u x%d (%s) from (%d,%d,%d)\n", dir & 0x07, count,
            directStepsRun_ ? "run" : "walk",
            playerX_, playerY_, static_cast<int>(playerZ_));
}

bool Client::WalkQueueBusy() const {
    return !directSteps_.empty() || !nav_.movement.pending.empty();
}

void Client::ActionGoto(i32 x, i32 y) {
    gotoRequested_ = true;
    gotoArrived_ = false;
    gotoTargetX_ = x;
    gotoTargetY_ = y;
    LogInfo("[action] goto (%d,%d) from (%d,%d,%d)\n",
            x, y, playerX_, playerY_, static_cast<int>(playerZ_));
    char ev[96];
    std::snprintf(ev, sizeof(ev), "target=(%d,%d) from=(%d,%d)",
                  x, y, playerX_, playerY_);
    LogEvent("goto_start", ev);
    BotStartGoto(x, y);
}

bool Client::GotoBusy() {
    if (!gotoRequested_) return false;
    if (nav_.bot.planning || nav_.bot.active) return true;

    // The trip ended: A* is neither planning nor walking any more.
    gotoRequested_ = false;
    gotoArrived_ = (playerX_ == gotoTargetX_ && playerY_ == gotoTargetY_);
    const int dx = playerX_ - gotoTargetX_;
    const int dy = playerY_ - gotoTargetY_;
    const int off = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    LogInfo("[action] goto finished at (%d,%d,%d); target (%d,%d); %s "
            "(off by %d tile(s))\n",
            playerX_, playerY_, static_cast<int>(playerZ_),
            gotoTargetX_, gotoTargetY_,
            gotoArrived_ ? "ARRIVED" : "stopped short", off);
    char ev[128];
    std::snprintf(ev, sizeof(ev), "at=(%d,%d) target=(%d,%d) arrived=%d off=%d",
                  playerX_, playerY_, gotoTargetX_, gotoTargetY_,
                  gotoArrived_ ? 1 : 0, off);
    LogEvent("goto_done", ev);
    return false;
}

void Client::ActionSay(const char* text) {
    if (!text || !text[0]) return;
    LogInfo("[action] say: %s\n", text);
    SayAscii(text);
}

void Client::ActionOpenBackpack() {
    LogInfo("[action] open backpack\n");
    OpenBackpack();
}

// Sphere treats the socket close as the logout (CClient::CharDisconnect,
// src/game/clients/CClient.cpp:166-233); 0xD1 is the polite announcement and
// is answered with 0xD1 (PacketLogoutAck, src/network/send.cpp:4631).
void Client::ActionLogout() {
    if (loggingOut_) return;
    loggingOut_ = true;
    LogInfo("[action] logout requested\n");
    u8 buf[4];
    const usize n = build::LogoutRequest(buf);
    Send(buf, n, "0xD1 LogoutRequest");
    logoutSentMs_ = NowMs();
    LogEvent("logout_requested", "0xD1 sent; awaiting ack, then closing");
}

// One step at a time: send, then wait for the server's 0x22 (accept) or 0x21
// (reject) before the next. Sphere only inspects the sequence when it has been
// reset (PacketMovementReq::onReceive, src/network/receive.cpp:259-282), and
// its walk-buffer speedhack check only runs for running steps
// (CClient::Event_Walk, src/game/clients/CClientEvent.cpp:930-935) -- so a
// depth-1 pipeline at the canonical cadence cannot trip it.
// The one and only 0x02 sender. Every movement source funnels through here.
Client::StepSubmit Client::SubmitStep(u8 dir, bool run, const char* source) {
    dir &= 0x07;

    // 1. Outstanding-step limit: never exceed what the controller allows.
    if (nav_.movement.pending.size() >= nav_.movement.maxInFlight)
        return StepSubmit::InFlight;

    // 2. Pacing: never faster than the canonical cadence for this gait.
    const i64 now = NowMs();
    const u32 gap = run ? nav_.movement.runStepMs : nav_.movement.walkStepMs;
    if (nav_.movement.lastMoveSentMs != 0 &&
        now - nav_.movement.lastMoveSentMs < static_cast<i64>(gap))
        return StepSubmit::Throttled;

    // 3. Turn-then-step: a direction change is a separate move that the
    //    server acks without relocating the character.
    const bool isStep = (dir == playerFacing_);

    // 4. Sequence: allocated here and nowhere else.
    const u8 seq = NextSeq();
    const u8 wire = run ? static_cast<u8>(dir | 0x80) : dir;

    u8 buf[16];
    const usize n = build::MoveRequest(buf, wire, seq, 0u, cfg_.legacyMovePacket);
    char note[96];
    std::snprintf(note, sizeof(note), "0x02 Move dir=%u seq=%u %s%s src=%s",
                  dir, seq, isStep ? "step" : "turn", run ? " run" : "",
                  source ? source : "?");
    if (!Send(buf, n, note)) return StepSubmit::Failed;

    // 5. Bookkeeping the ack/reject handlers rely on.
    nav_.movement.pending.push_back({seq, dir, isStep, now});
    nav_.movement.lastMoveSentMs = now;
    lastDirectStepMs_ = now;

    if (isStep) {
        BotPredictStep(dir);
        return StepSubmit::Sent;
    }
    playerFacing_ = dir;
    player_.facing = dir;
    player_.running = false;
    return StepSubmit::Turned;
}

void Client::PumpDirectSteps() {
    if (directSteps_.empty()) {
        // Batch just drained: report where the character ended up. The server
        // is the authority here -- it only acks a step (0x22) after
        // CanMoveWalkTo and MoveToChar succeed
        // (CClient::Event_Walk, src/game/clients/CClientEvent.cpp:872-890).
        if (walkBatchActive_ && nav_.movement.pending.empty()) {
            walkBatchActive_ = false;
            const int dx = playerX_ - walkBatchStartX_;
            const int dy = playerY_ - walkBatchStartY_;
            const int tiles = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            LogInfo("[action] walk done: (%d,%d) -> (%d,%d,%d), %d tile(s)\n",
                    walkBatchStartX_, walkBatchStartY_,
                    playerX_, playerY_, static_cast<int>(playerZ_), tiles);
            char ev[128];
            std::snprintf(ev, sizeof(ev), "from=(%d,%d) to=(%d,%d,%d) tiles=%d",
                          walkBatchStartX_, walkBatchStartY_, playerX_, playerY_,
                          static_cast<int>(playerZ_), tiles);
            LogEvent("walk_batch_done", ev);
        }
        return;
    }
    const u8 dir = directSteps_.front();
    switch (SubmitStep(dir, directStepsRun_, "action")) {
        case StepSubmit::Sent:
            directSteps_.pop_front();
            break;
        case StepSubmit::Turned:
            // The turn was acknowledged as a move; the same direction is
            // offered again next tick as a real step.
            break;
        default:
            break;   // throttled / in flight / failed: nothing more this tick
    }
}

void Client::TryOpenBackpackOnLogin() {
    if (!openBackpackPending_) return;
    if (PlayerEquipSerialAt(kLayerBackpack) == 0) return;   // wait for the worn list
    openBackpackPending_ = false;
    OpenBackpack();
}

void Client::SetMobileEquipLayer(u32 mobileSerial, u32 itemSerial, u8 layer,
                                 u16 graphic, u16 hue) {
    auto upsert = [&](std::vector<EquipObj>& v) {
        for (auto& e : v)
            if (e.layer == layer) {
                e.graphic = graphic; e.hue = hue; e.serial = itemSerial; return;
            }
        v.push_back({layer, graphic, hue, itemSerial});
    };
    if (mobileSerial == playerSerial_) { upsert(playerEquip_); return; }
    for (auto& m : mobileCache_)
        if (m.serial == mobileSerial) { upsert(m.equip); return; }
}

u32 Client::PlayerEquipSerialAt(u8 layer) const {
    for (const auto& e : playerEquip_)
        if (e.layer == layer) return e.serial;
    return 0;
}

std::string Client::ItemNameLower(u16 graphic) const {
    if (!tileData_ || !tileData_->IsLoaded()) return std::string();
    const auto& st = tileData_->Static(graphic);
    char buf[21];
    std::memcpy(buf, st.name, 20);
    buf[20] = '\0';
    std::string s(buf);
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

u8 Client::ItemEquipLayer(u16 graphic) const {
    if (!tileData_ || !tileData_->IsLoaded()) return 0;
    return tileData_->Static(graphic).quality;
}

bool Client::ItemMatches(u16 graphic, bool hasType, u16 type,
                         const std::string& lowerName) const {
    if (hasType) return graphic == type;
    if (lowerName.empty()) return false;
    const std::string nm = ItemNameLower(graphic);
    return !nm.empty() && nm.find(lowerName) != std::string::npos;
}

u32 Client::FindItem(bool hasType, u16 type, const std::string& lowerName,
                     unsigned zones, u16* graphicOut, u8* layerOut,
                     const char** whereOut) const {
    auto emit = [&](u32 serial, u16 g, u8 layer, const char* w) -> u32 {
        if (graphicOut) *graphicOut = g;
        if (layerOut) *layerOut = layer;
        if (whereOut) *whereOut = w;
        return serial;
    };

    // 1. Backpack contents (direct children of the worn backpack).
    if (zones & kZoneBackpack) {
        const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
        if (backpack) {
            auto it = containerItems_.find(backpack);
            if (it != containerItems_.end())
                for (const auto& ci : it->second)
                    if (ItemMatches(ci.graphic, hasType, type, lowerName))
                        return emit(ci.serial, ci.graphic, 0, "backpack");
        }
    }

    // 2. Our worn equipment (e.g. the axe in hand).
    if (zones & kZoneEquip) {
        for (const auto& e : playerEquip_)
            if (ItemMatches(e.graphic, hasType, type, lowerName))
                return emit(e.serial, e.graphic, e.layer, "equipment");
    }

    // 3. Nearest matching world item.
    if (zones & kZoneWorld) {
        u32 best = 0;
        u16 bestGraphic = 0;
        i32 bestDist = 0x7FFFFFFF;
        for (const auto& kv : items_) {
            const ItemObj& o = kv.second;
            if (!ItemMatches(o.itemId, hasType, type, lowerName)) continue;
            const i32 dx = (o.x > playerX_) ? (o.x - playerX_) : (playerX_ - o.x);
            const i32 dy = (o.y > playerY_) ? (o.y - playerY_) : (playerY_ - o.y);
            const i32 d = (dx > dy) ? dx : dy;
            if (d < bestDist) { bestDist = d; best = kv.first; bestGraphic = o.itemId; }
        }
        if (best) return emit(best, bestGraphic, 0, "world");
    }
    return 0;
}

bool Client::ParseItemToken(const char* arg, u32* serialOut, bool* hasTypeOut,
                            u16* typeOut, std::string* lowerNameOut,
                            const char** restOut) const {
    *serialOut = 0;
    *hasTypeOut = false;
    *typeOut = 0;
    lowerNameOut->clear();
    while (*arg == ' ' || *arg == '\t') ++arg;
    if (!*arg) { *restOut = arg; return false; }

    std::string token;
    bool forcedName = false;
    if (*arg == '\'' || *arg == '"') {
        const char q = *arg++;
        const char* end = std::strchr(arg, q);
        if (!end) { *restOut = arg; return false; }
        token.assign(arg, static_cast<usize>(end - arg));
        forcedName = true;
        arg = end + 1;
    } else {
        const char* end = arg;
        while (*end && *end != ' ' && *end != '\t') ++end;
        token.assign(arg, static_cast<usize>(end - arg));
        arg = end;
    }
    while (*arg == ' ' || *arg == '\t') ++arg;
    *restOut = arg;
    if (token.empty()) return false;

    bool numeric = false, hex = false;
    if (!forcedName) {
        if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
            numeric = hex = true;
        } else {
            numeric = true;
            for (char c : token)
                if (!std::isdigit(static_cast<unsigned char>(c))) { numeric = false; break; }
        }
    }

    if (numeric) {
        u32 val = 0;
        if (std::sscanf(token.c_str(), hex ? "%x" : "%u", &val) != 1) return false;
        if (val >= 0x40000000u) *serialOut = val;
        else { *hasTypeOut = true; *typeOut = static_cast<u16>(val); }
    } else {
        *lowerNameOut = token;
        for (char& c : *lowerNameOut)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return true;
}

// Move the weapon (layer 1) or shield (layer 2) between hand and backpack,
// remembering the disarmed serial so a later `arm` re-equips it. Models
// Macro_ActionArmDisarm_Validate @0x4b6c90: PickUp (0x07) then Drop (0x08) to
// the backpack on disarm, PickUp then Equip (0x13) on arm.
void Client::ArmDisarmHand(bool doArm, u8 layer) {
    const char* hand = (layer == kLayerOneHanded) ? "weapon" : "shield";
    u8 buf[16];
    if (doArm) {
        const u32 saved = armSavedSerial_[layer];
        if (saved == 0) {
            LogWarn("[cmd] arm: no %s remembered (disarm one first)\n", hand);
            return;
        }
        if (PlayerEquipSerialAt(layer) != 0) {
            LogInfo("[cmd] arm: %s slot already occupied\n", hand);
            return;
        }
        Send(buf, build::PickUpItem(buf, saved, 0), "0x07 PickUp (arm)");
        Send(buf, build::EquipItem(buf, saved, layer, playerSerial_),
             "0x13 Equip (arm)");
        LogInfo("[cmd] arm %s 0x%08X\n", hand, saved);
        armSavedSerial_[layer] = 0;
        return;
    }
    // Disarm.
    const u32 equipped = PlayerEquipSerialAt(layer);
    if (equipped == 0) {
        LogInfo("[cmd] disarm: no %s equipped\n", hand);
        return;
    }
    const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
    if (backpack == 0) {
        LogWarn("[cmd] disarm: backpack serial unknown\n");
        return;
    }
    Send(buf, build::PickUpItem(buf, equipped, 0), "0x07 PickUp (disarm)");
    Send(buf, build::DropItem(buf, equipped, 0xFFFF, 0xFFFF, 0, backpack),
         "0x08 Drop (disarm)");
    armSavedSerial_[layer] = equipped;
    LogInfo("[cmd] disarm %s 0x%08X -> backpack\n", hand, equipped);
}

// `use` command: resolve the target by serial / graphic type / tiledata name,
// then double-click it (0x06). See HandleStdinLine for the entry point.
void Client::HandleUseCommand(const char* arg) {
    if (!arg || !arg[0]) {
        LogInfo(
            "[cmd] use - double-click an item (sends 0x06)\n"
            "  use <0xserial>     a specific object by serial\n"
            "  use <type>         by graphic id (decimal or 0x.. below 0x40000000)\n"
            "  use '<name>'       by tiledata name substring (e.g. use 'bandage')\n"
            "  use <name>         same, unquoted (may be several words)\n"
            "  ... pack           add as the last word to skip the world search\n"
            "  order: backpack -> worn equipment -> nearest world item\n");
        return;
    }

    std::string query;
    bool inventoryOnly = false;
    bool forcedName = false;

    if (*arg == '\'' || *arg == '"') {
        const char q = *arg++;
        const char* end = std::strchr(arg, q);
        if (!end) { LogWarn("[cmd] use: unterminated quote\n"); return; }
        query.assign(arg, static_cast<usize>(end - arg));
        forcedName = true;
        const char* tail = end + 1;
        while (*tail == ' ' || *tail == '\t') ++tail;
        if (tail[0]) inventoryOnly = IsPackScope(tail);
    } else {
        std::string s(arg);
        usize e = s.size();
        while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
        s.resize(e);
        const usize sp = s.find_last_of(" \t");
        if (sp != std::string::npos && IsPackScope(s.c_str() + sp + 1)) {
            inventoryOnly = true;
            s.resize(sp);
            usize e2 = s.size();
            while (e2 > 0 && (s[e2 - 1] == ' ' || s[e2 - 1] == '\t')) --e2;
            s.resize(e2);
        }
        query = s;
    }

    if (query.empty()) { LogWarn("[cmd] use: empty target\n"); return; }

    bool numeric = false, hex = false;
    if (query[0] == '0' && (query[1] == 'x' || query[1] == 'X')) {
        numeric = hex = true;
    } else {
        numeric = true;
        for (char c : query)
            if (!std::isdigit(static_cast<unsigned char>(c))) { numeric = false; break; }
    }

    u32 serial = 0;
    bool hasType = false;
    u16 type = 0;
    std::string lowerName;

    if (numeric && !forcedName) {
        u32 val = 0;
        if (std::sscanf(query.c_str(), hex ? "%x" : "%u", &val) != 1) {
            LogWarn("[cmd] use: bad number '%s'\n", query.c_str());
            return;
        }
        if (val >= 0x40000000u) serial = val;            // looks like a serial
        else { hasType = true; type = static_cast<u16>(val); }
    } else {
        lowerName = query;
        for (char& c : lowerName)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if ((!tileData_ || !tileData_->IsLoaded()) && !EnsureWorldLoaded()) {
            LogWarn("[cmd] use: tiledata not loaded; cannot match by name\n");
            return;
        }
    }

    if (serial == 0) {
        const char* where = "?";
        unsigned zones = kZoneBackpack | kZoneEquip;
        if (!inventoryOnly) zones |= kZoneWorld;
        serial = FindItem(hasType, type, lowerName, zones, nullptr, nullptr, &where);
        if (serial == 0) {
            LogWarn("[cmd] use: '%s' not found%s\n", query.c_str(),
                    inventoryOnly ? " in backpack/equipment" : "");
            return;
        }
        u8 buf[8];
        Send(buf, build::DoubleClick(buf, serial), "0x06 DoubleClick (use)");
        LogInfo("[cmd] use '%s' -> 0x%08X (%s)\n", query.c_str(), serial, where);
        return;
    }

    u8 buf[8];
    Send(buf, build::DoubleClick(buf, serial), "0x06 DoubleClick (use)");
    LogInfo("[cmd] use 0x%08X\n", serial);
}

// `pickup <name|type|0xserial>` — lift the nearest matching WORLD item (0x07)
// and drop it into the backpack (0x08). Loots a ground item into the pack.
void Client::HandlePickupCommand(const char* arg) {
    if (!arg || !arg[0]) {
        LogInfo("[cmd] usage: pickup <0xserial|type|'name'>  (nearest world item -> backpack)\n");
        return;
    }
    u32 serial = 0; bool hasType = false; u16 type = 0; std::string name; const char* rest = arg;
    if (!ParseItemToken(arg, &serial, &hasType, &type, &name, &rest)) {
        LogWarn("[cmd] pickup: bad target\n");
        return;
    }
    if (!name.empty() && (!tileData_ || !tileData_->IsLoaded()) && !EnsureWorldLoaded()) {
        LogWarn("[cmd] pickup: tiledata not loaded; cannot match by name\n");
        return;
    }
    const char* where = "?";
    if (serial == 0)
        serial = FindItem(hasType, type, name, kZoneWorld, nullptr, nullptr, &where);
    if (serial == 0) { LogWarn("[cmd] pickup: no matching world item\n"); return; }

    const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
    if (backpack == 0) { LogWarn("[cmd] pickup: backpack serial unknown\n"); return; }
    u8 buf[16];
    Send(buf, build::PickUpItem(buf, serial, 0), "0x07 PickUp (pickup)");
    Send(buf, build::DropItem(buf, serial, 0xFFFF, 0xFFFF, 0, backpack),
         "0x08 Drop (pickup->pack)");
    LogInfo("[cmd] pickup 0x%08X -> backpack\n", serial);
}

// `drop <target> <x> <y> [z]` | `drop <target> <0xcontainer>` — lift a backpack
// item (0x07) and drop it at world coords (container 0xFFFFFFFF) or into a
// container (0x08).
void Client::HandleDropCommand(const char* arg) {
    if (!arg || !arg[0]) {
        LogInfo("[cmd] usage: drop <0xserial|type|'name'> <x> <y> [z] | drop <target> <0xcontainer>\n");
        return;
    }
    u32 serial = 0; bool hasType = false; u16 type = 0; std::string name; const char* rest = arg;
    if (!ParseItemToken(arg, &serial, &hasType, &type, &name, &rest)) {
        LogWarn("[cmd] drop: bad target\n");
        return;
    }
    if (!name.empty() && (!tileData_ || !tileData_->IsLoaded()) && !EnsureWorldLoaded()) {
        LogWarn("[cmd] drop: tiledata not loaded; cannot match by name\n");
        return;
    }
    if (serial == 0)
        serial = FindItem(hasType, type, name, kZoneBackpack, nullptr, nullptr, nullptr);
    if (serial == 0) { LogWarn("[cmd] drop: item not found in backpack\n"); return; }

    if (!rest || !rest[0]) {
        LogWarn("[cmd] drop: need a destination (<x> <y> [z] or <0xcontainer>)\n");
        return;
    }

    u8 buf[16];
    u32 container = 0;
    if (ParseSerial(rest, &container) && container != 0) {
        Send(buf, build::PickUpItem(buf, serial, 0), "0x07 PickUp (drop)");
        Send(buf, build::DropItem(buf, serial, 0xFFFF, 0xFFFF, 0, container),
             "0x08 Drop (->container)");
        LogInfo("[cmd] drop 0x%08X -> container 0x%08X\n", serial, container);
        return;
    }
    char coords[64];
    std::strncpy(coords, rest, sizeof(coords) - 1);
    coords[sizeof(coords) - 1] = '\0';
    for (char* p = coords; *p; ++p) if (*p == ',') *p = ' ';
    i32 dx = 0, dy = 0, dz = 0;
    const int got = std::sscanf(coords, "%d %d %d", &dx, &dy, &dz);
    if (got < 2) {
        LogWarn("[cmd] drop: bad destination '%s'\n", rest);
        return;
    }
    Send(buf, build::PickUpItem(buf, serial, 0), "0x07 PickUp (drop)");
    Send(buf, build::DropItem(buf, serial, static_cast<u16>(dx), static_cast<u16>(dy),
                              static_cast<i8>(got >= 3 ? dz : 0), 0xFFFFFFFFu),
         "0x08 Drop (->ground)");
    LogInfo("[cmd] drop 0x%08X -> (%d,%d,%d)\n", serial, dx, dy, got >= 3 ? dz : 0);
}

// `equip <target> [pack]` — lift an item (0x07) and wear it (0x13) at the layer
// from tiledata. Searches backpack + nearest world by default; `pack` limits to
// the backpack.
void Client::HandleEquipCommand(const char* arg) {
    if (!arg || !arg[0]) {
        LogInfo("[cmd] usage: equip <0xserial|type|'name'> [pack]   (pack = backpack only)\n");
        return;
    }
    u32 serial = 0; bool hasType = false; u16 type = 0; std::string name; const char* rest = arg;
    if (!ParseItemToken(arg, &serial, &hasType, &type, &name, &rest)) {
        LogWarn("[cmd] equip: bad target\n");
        return;
    }
    bool packOnly = (rest && rest[0] && IsPackScope(rest));
    // Equip needs tiledata for the layer; name matching needs it too.
    if ((!tileData_ || !tileData_->IsLoaded()) && !EnsureWorldLoaded()) {
        LogWarn("[cmd] equip: tiledata not loaded\n");
        return;
    }
    unsigned zones = kZoneBackpack | (packOnly ? 0u : kZoneWorld);
    const char* where = "?";
    u16 graphic = 0;
    if (serial == 0) {
        serial = FindItem(hasType, type, name, zones, &graphic, nullptr, &where);
    } else {
        // Direct serial: find the graphic from caches so we can pick the layer.
        if (auto it = items_.find(serial); it != items_.end()) graphic = it->second.itemId;
        else {
            const u32 bp = PlayerEquipSerialAt(kLayerBackpack);
            if (auto ci = containerItems_.find(bp); ci != containerItems_.end())
                for (const auto& e : ci->second)
                    if (e.serial == serial) { graphic = e.graphic; break; }
        }
    }
    if (serial == 0) { LogWarn("[cmd] equip: item not found\n"); return; }

    const u8 layer = ItemEquipLayer(graphic);
    if (layer == 0) {
        LogWarn("[cmd] equip: 0x%04X is not equippable (no layer in tiledata)\n", graphic);
        return;
    }
    u8 buf[16];
    Send(buf, build::PickUpItem(buf, serial, 0), "0x07 PickUp (equip)");
    Send(buf, build::EquipItem(buf, serial, layer, playerSerial_), "0x13 Equip");
    LogInfo("[cmd] equip 0x%08X graphic=0x%04X layer=%u\n", serial, graphic, layer);
}

// `unequip <weapon|shield|target> [pack]` — lift a worn item (0x07) and drop it
// at the player's feet (world) by default, or into the backpack with `pack`.
void Client::HandleUnequipCommand(const char* arg) {
    if (!arg || !arg[0]) {
        LogInfo("[cmd] usage: unequip <weapon|shield|type|'name'> [pack]   (default drops to world)\n");
        return;
    }
    // Hand keywords resolve directly to a worn layer.
    std::string first;
    {
        const char* p = arg;
        while (*p && *p != ' ' && *p != '\t') ++p;
        first.assign(arg, static_cast<usize>(p - arg));
    }
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string firstLower = lower(first);

    u32 serial = 0;
    u16 graphic = 0;
    const char* rest = arg;

    // What's worn in each hand. A two-handed weapon (staff, bow, halberd) sits
    // on the LEFT/two-handed layer, not the one-handed layer, so `weapon` must
    // look at both. The graphic lets tiledata tell a weapon from a shield.
    u32 rightSerial = 0, leftSerial = 0;
    u16 rightGfx = 0, leftGfx = 0;
    for (const auto& e : playerEquip_) {
        if (e.layer == kLayerOneHanded) { rightSerial = e.serial; rightGfx = e.graphic; }
        else if (e.layer == kLayerTwoHanded) { leftSerial = e.serial; leftGfx = e.graphic; }
    }
    auto isWeapon = [&](u16 g) -> bool {
        return tileData_ && tileData_->IsLoaded() &&
               (tileData_->Static(g).flags & tiledata::kFlagWeapon) != 0;
    };

    if (firstLower == "weapon" || firstLower == "right") {
        if (rightSerial && isWeapon(rightGfx))      serial = rightSerial;
        else if (leftSerial && isWeapon(leftGfx))   serial = leftSerial;
        else serial = rightSerial ? rightSerial : leftSerial;  // fallback
        rest = arg + first.size();
    } else if (firstLower == "shield" || firstLower == "left") {
        serial = leftSerial;
        rest = arg + first.size();
    } else {
        u32 s = 0; bool hasType = false; u16 type = 0; std::string name;
        if (!ParseItemToken(arg, &s, &hasType, &type, &name, &rest)) {
            LogWarn("[cmd] unequip: bad target\n");
            return;
        }
        if (!name.empty() && (!tileData_ || !tileData_->IsLoaded()) && !EnsureWorldLoaded()) {
            LogWarn("[cmd] unequip: tiledata not loaded; cannot match by name\n");
            return;
        }
        serial = (s != 0) ? s
                          : FindItem(hasType, type, name, kZoneEquip, &graphic, nullptr, nullptr);
    }
    while (*rest == ' ' || *rest == '\t') ++rest;
    const bool toPack = (rest[0] && IsPackScope(rest));

    if (serial == 0) { LogWarn("[cmd] unequip: not currently worn\n"); return; }

    u8 buf[16];
    Send(buf, build::PickUpItem(buf, serial, 0), "0x07 PickUp (unequip)");
    if (toPack) {
        const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
        if (backpack == 0) { LogWarn("[cmd] unequip: backpack serial unknown\n"); return; }
        Send(buf, build::DropItem(buf, serial, 0xFFFF, 0xFFFF, 0, backpack),
             "0x08 Drop (unequip->pack)");
        LogInfo("[cmd] unequip 0x%08X -> backpack\n", serial);
    } else {
        Send(buf, build::DropItem(buf, serial, static_cast<u16>(playerX_),
                                  static_cast<u16>(playerY_), playerZ_, 0xFFFFFFFFu),
             "0x08 Drop (unequip->ground)");
        LogInfo("[cmd] unequip 0x%08X -> ground (%d,%d)\n", serial, playerX_, playerY_);
    }
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

// 0x6C Target Cursor (server -> client). The server arms a target after a
// spell cast or an item use that needs a target; we stash the cursor id/type/
// subtype and wait for the player to click (or a `target` console command) to
// build the 0x6C response. Mirrors Packet_HandleTargetCursor @0x41E960, minus
// the special house/boat-placement and auto-target callbacks.
void Client::OnTargetCursor(const u8* data, usize size) {
    if (size < 7) return;
    targetCursorType_    = data[1];
    targetCursorId_      = LoadBE32(data + 2);
    targetCursorSubtype_ = data[6];
    targetCursorActive_  = true;
    LogInfo("[0x6C] target cursor armed: id=0x%08X type=%u subtype=%u — "
            "click a target (right-click/Esc to cancel)\n",
            targetCursorId_, targetCursorType_, targetCursorSubtype_);
    uo::js::EmitTargetEvent(targetCursorId_, targetCursorType_);  // -> Player 'target'
}

// Look up a known object's position and graphic for an object-target reply.
// Checks the local player, dynamic items (0x1A), then cached mobiles (0x77/78).
bool Client::ResolveObjectTarget(u32 serial, i32* x, i32* y, i8* z,
                                 u16* model) const {
    if (serial == playerSerial_) {
        *x = playerX_; *y = playerY_; *z = playerZ_; *model = playerBody_;
        return true;
    }
    if (auto it = items_.find(serial); it != items_.end()) {
        *x = it->second.x; *y = it->second.y; *z = it->second.z;
        *model = it->second.itemId;
        return true;
    }
    if (const MobileObj* m = FindMobileBySerial(serial)) {
        *x = m->x; *y = m->y; *z = m->z; *model = m->body;
        return true;
    }
    return false;
}

void Client::TargetRespondObject(u32 serial) {
    if (!targetCursorActive_) {
        LogWarn("[target] no target cursor active\n");
        return;
    }
    i32 x = 0, y = 0; i8 z = 0; u16 model = 0;
    ResolveObjectTarget(serial, &x, &y, &z, &model);  // best-effort coords/graphic
    u8 buf[19];
    const usize n = build::TargetCursorObject(
        buf, targetCursorId_, targetCursorSubtype_, serial,
        static_cast<u16>(x), static_cast<u16>(y), z, model);
    Send(buf, n, "0x6C TargetCursor (object)");
    LogInfo("[target] object 0x%08X (%d,%d,%d) model 0x%04X\n",
            serial, x, y, static_cast<int>(z), model);
    targetCursorActive_ = false;
    targetCursorSubtype_ = 0;
}

void Client::TargetRespondGround(i32 x, i32 y, bool hasZ, i8 z) {
    if (!targetCursorActive_) {
        LogWarn("[target] no target cursor active\n");
        return;
    }
    if (!hasZ) {
        z = playerZ_;
        if (world_ && x >= 0 && y >= 0) {
            world::WalkQuery q;
            q.x = static_cast<u32>(x);
            q.y = static_cast<u32>(y);
            q.fromZ = playerZ_;
            const world::WalkResult r = world_->QueryCell(q);
            if (r.walkable) z = r.standZ;
        }
    }
    u8 buf[19];
    const usize n = build::TargetCursorGround(
        buf, targetCursorId_, targetCursorSubtype_,
        static_cast<u16>(x), static_cast<u16>(y), z, 0);
    Send(buf, n, "0x6C TargetCursor (ground)");
    LogInfo("[target] ground (%d,%d,%d)\n", x, y, static_cast<int>(z));
    targetCursorActive_ = false;
    targetCursorSubtype_ = 0;
}

void Client::TargetRespondStatic(i32 x, i32 y, i8 z, u16 graphic) {
    if (!targetCursorActive_) {
        LogWarn("[target] no target cursor active\n");
        return;
    }
    // Static target: tile reply (type=1, serial=0) carrying the static's
    // graphic in modelID — that's how the server knows it's a tree, not bare
    // ground (a model of 0 yields "you can't use an axe on that").
    u8 buf[19];
    const usize n = build::TargetCursorGround(
        buf, targetCursorId_, targetCursorSubtype_,
        static_cast<u16>(x), static_cast<u16>(y), z, graphic);
    Send(buf, n, "0x6C TargetCursor (static)");
    LogInfo("[target] static 0x%04X (%d,%d,%d)\n", graphic, x, y,
            static_cast<int>(z));
    targetCursorActive_ = false;
    targetCursorSubtype_ = 0;
}

void Client::CancelTargetCursor(const char* reason) {
    if (!targetCursorActive_) return;
    u8 buf[19];
    const usize n = build::TargetCursorCancel(buf, targetCursorId_,
                                              targetCursorSubtype_);
    Send(buf, n, "0x6C TargetCursor (cancel)");
    LogInfo("[target] cancelled (%s)\n", reason ? reason : "");
    targetCursorActive_ = false;
    targetCursorSubtype_ = 0;
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
// 0x73 Ping (2 bytes: cmd + sequence).
//
// Two different meanings, and telling them apart matters:
//
//  * An ANSWER to the keepalive we sent. Sphere only ever creates
//    PacketPingAck from inside PacketPingReq::onReceive
//    (Source-X src/network/receive.cpp:1335-1344; the constructor is
//    src/network/send.cpp:2266) -- it never pings first. Echoing this reply
//    makes the server answer again, and the two sides ping-pong as fast as
//    the socket allows. That is exactly what happened on the first
//    10-minute run here: ~24,000 exchanges in 100 seconds.
//
//  * An UNSOLICITED ping from a shard that expects the client to echo
//    (the UO Demo behaviour this client was originally written against).
//
// So: consume it as the answer when a keepalive is outstanding, otherwise
// echo once -- rate limited, so no peer can drive a storm either way.
// ---------------------------------------------------------------------------
void Client::OnPing(const u8* data, usize size) {
    if (size < 2) return;

    const i64 now = NowMs();
    switch (sphere::DecidePing(pingOutstanding_, now, lastPingEchoMs_,
                               kPingEchoMinGapMs)) {
        case sphere::PingAction::ConsumeAsReply:
            --pingOutstanding_;
            return;               // the reply to our keepalive; send nothing
        case sphere::PingAction::Ignore:
            return;               // unsolicited, but too soon to echo again
        case sphere::PingAction::Echo:
            break;
    }
    lastPingEchoMs_ = now;

    u8 buf[2];
    const usize n = build::PingReply(buf, data[1]);
    Send(buf, n, "0x73 PingReply (unsolicited server ping)");
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
    if (playerSerial_ != 0 && serial != playerSerial_) {
        // Status of another mobile (from our 0x34 query): cache its HP so the bot
        // can judge a fight before committing. curHp@37, maxHp@39 are in the fixed
        // header; the rest of the extended block is only sent for self/editing.
        for (auto& m : mobileCache_) {
            if (m.serial == serial) {
                m.hpCur = static_cast<i32>(LoadBE16(data + 37));
                m.hpMax = static_cast<i32>(LoadBE16(data + 39));
                break;
            }
        }
        return;
    }
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
    const u8  flags   = data[10];
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
        playerWarMode_ = (flags & 0x40u) != 0;
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

    // `run <script.js>` — (re)load and execute a JS bot script in a fresh
    // runtime. Script/JS errors are caught and printed to stderr ([js]); they
    // never crash the client. Edit the file and `run` again for a clean slate.
    if (std::strncmp(line, "run", 3) == 0 &&
        (line[3] == ' ' || line[3] == '\t')) {
        const char* path = line + 3;
        while (*path == ' ' || *path == '\t') ++path;
        if (!*path) {
            LogWarn("[cmd] usage: run <script.js>\n");
        } else {
            js_.Run(path);
        }
        return;
    }

    // `js stop` — tear down the running script (clean slate).
    if (std::strcmp(line, "js stop") == 0) {
        js_.Stop();
        LogInfo("[js] stopped\n");
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

    // `dialog` — show the active 0x7C menu; `dialog <index>` answers it (1-based,
    // 0/`cancel` cancels). The bot normally auto-answers via JS; this is manual.
    if (std::strncmp(line, "dialog", 6) == 0 &&
        (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        const char* arg = line + 6;
        while (*arg == ' ' || *arg == '\t') ++arg;
        if (!activeDialog_.active) {
            LogInfo("[dialog] none active\n");
            return;
        }
        if (*arg == '\0') {
            LogInfo("[dialog] id=0x%08X menu=%u: \"%s\"\n", activeDialog_.id,
                    activeDialog_.menuId, activeDialog_.question.c_str());
            for (usize i = 0; i < activeDialog_.options.size(); ++i)
                LogInfo("         %zu) %s\n", i + 1, activeDialog_.options[i].text.c_str());
            return;
        }
        const u16 index = (std::strcmp(arg, "cancel") == 0 || std::strcmp(arg, "off") == 0)
                              ? 0u
                              : static_cast<u16>(std::atoi(arg));
        AnswerDialog(index);
        return;
    }

    // `target ...` — answer a pending 0x6C target cursor (armed by the server
    // after a spell/item use). Forms:
    //   target cancel|off       cancel the cursor (0x6C with x/y = 0xFFFF)
    //   target self             our own character
    //   target 0xSERIAL         a mobile/item by serial
    //   target <x> <y> [z]      a ground tile (z auto-resolved if omitted)
    if (std::strncmp(line, "target", 6) == 0 &&
        (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        const char* arg = line + 6;
        while (*arg == ' ' || *arg == '\t') ++arg;
        if (!targetCursorActive_) {
            LogWarn("[cmd] no target cursor active\n");
            return;
        }
        if (std::strcmp(arg, "cancel") == 0 || std::strcmp(arg, "off") == 0) {
            CancelTargetCursor("user");
            return;
        }
        if (std::strcmp(arg, "self") == 0) {
            TargetRespondObject(playerSerial_);
            return;
        }
        if ((arg[0] == '0') && (arg[1] == 'x' || arg[1] == 'X')) {
            u32 serial = 0;
            if (!ParseSerial(arg, &serial) || serial == 0) {
                LogWarn("[cmd] invalid serial: %s\n", arg);
                return;
            }
            TargetRespondObject(serial);
            return;
        }
        char args[128];
        std::strncpy(args, arg, sizeof(args) - 1);
        args[sizeof(args) - 1] = '\0';
        for (char* p = args; *p; ++p) if (*p == ',') *p = ' ';
        i32 tx = 0, ty = 0, tz = 0;
        const int got = std::sscanf(args, "%d %d %d", &tx, &ty, &tz);
        if (got >= 2) {
            TargetRespondGround(tx, ty, got >= 3, static_cast<i8>(tz));
        } else {
            LogWarn("[cmd] usage: target cancel|self|<0xserial>|<x> <y> [z]\n");
        }
        return;
    }

    // `use ...` — double-click an object (sends 0x06) by serial, by tiledata
    // name, or by graphic type, searching backpack -> worn gear -> world.
    if (std::strncmp(line, "use", 3) == 0 &&
        (line[3] == '\0' || line[3] == ' ' || line[3] == '\t')) {
        const char* arg = line + 3;
        while (*arg == ' ' || *arg == '\t') ++arg;
        HandleUseCommand(arg);
        return;
    }

    // `disarm [weapon|shield|both]` / `arm [weapon|shield|both]` — move the
    // weapon (right hand) and/or shield (left hand) to the backpack and back.
    if ((std::strncmp(line, "arm", 3) == 0 &&
         (line[3] == '\0' || line[3] == ' ' || line[3] == '\t')) ||
        (std::strncmp(line, "disarm", 6) == 0 &&
         (line[6] == '\0' || line[6] == ' ' || line[6] == '\t'))) {
        const bool doArm = (line[0] == 'a');
        const char* arg = line + (doArm ? 3 : 6);
        while (*arg == ' ' || *arg == '\t') ++arg;
        bool weapon = false, shield = false;
        if (!arg[0] || std::strcmp(arg, "both") == 0) { weapon = shield = true; }
        else if (std::strcmp(arg, "weapon") == 0 || std::strcmp(arg, "right") == 0) weapon = true;
        else if (std::strcmp(arg, "shield") == 0 || std::strcmp(arg, "left") == 0)  shield = true;
        else {
            LogWarn("[cmd] usage: %s [weapon|shield|both]\n", doArm ? "arm" : "disarm");
            return;
        }
        if (weapon) ArmDisarmHand(doArm, kLayerOneHanded);
        if (shield) ArmDisarmHand(doArm, kLayerTwoHanded);
        return;
    }

    // `pickup ...` — lift a nearby world item into the backpack.
    if (std::strncmp(line, "pickup", 6) == 0 &&
        (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        const char* arg = line + 6;
        while (*arg == ' ' || *arg == '\t') ++arg;
        HandlePickupCommand(arg);
        return;
    }

    // `drop ...` — move a backpack item to world coords or a container.
    if (std::strncmp(line, "drop", 4) == 0 &&
        (line[4] == '\0' || line[4] == ' ' || line[4] == '\t')) {
        const char* arg = line + 4;
        while (*arg == ' ' || *arg == '\t') ++arg;
        HandleDropCommand(arg);
        return;
    }

    // `unequip ...` — take a worn item off (to world, or backpack with `pack`).
    // Checked before `equip` so the "equip" prefix test doesn't swallow it.
    if (std::strncmp(line, "unequip", 7) == 0 &&
        (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        const char* arg = line + 7;
        while (*arg == ' ' || *arg == '\t') ++arg;
        HandleUnequipCommand(arg);
        return;
    }

    // `equip ...` — wear an item from backpack (or world unless `pack`).
    if (std::strncmp(line, "equip", 5) == 0 &&
        (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        const char* arg = line + 5;
        while (*arg == ' ' || *arg == '\t') ++arg;
        HandleEquipCommand(arg);
        return;
    }

    // `cast <spellId>` — cast a spell by its 1-based number via the official
    // 0x12 action packet (subcommand 0x56). Spells that need a target arm an
    // inbound 0x6C cursor; answer it with the `target` command.
    if (std::strncmp(line, "cast", 4) == 0 &&
        (line[4] == ' ' || line[4] == '\t')) {
        const char* arg = line + 4;
        while (*arg == ' ' || *arg == '\t') ++arg;
        int spellId = 0;
        if (std::sscanf(arg, "%d", &spellId) != 1 || spellId <= 0) {
            LogWarn("[cmd] usage: cast <spellId>  (1-based spell number)\n");
            return;
        }
        u8 buf[32];
        const usize n = build::CastSpell(buf, spellId);
        Send(buf, n, "0x12 CastSpell (0x56)");
        LogInfo("[cmd] cast spell %d\n", spellId);
        return;
    }

    // `skill <skillId>` — use a skill by its 0-based index via the official
    // 0x12 action packet (subcommand 0x24, payload "<id> 0").
    if (std::strncmp(line, "skill", 5) == 0 &&
        (line[5] == ' ' || line[5] == '\t')) {
        const char* arg = line + 5;
        while (*arg == ' ' || *arg == '\t') ++arg;
        int skillId = -1;
        if (std::sscanf(arg, "%d", &skillId) != 1 || skillId < 0) {
            LogWarn("[cmd] usage: skill <skillId>  (0-based skill index)\n");
            return;
        }
        u8 buf[32];
        const usize n = build::UseSkill(buf, skillId);
        Send(buf, n, "0x12 UseSkill (0x24)");
        LogInfo("[cmd] use skill %d\n", skillId);
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

    // `day [on|off]` — when on, force full daylight and ignore server light
    // levels (0x4E/0x4F). Off lets the server's night/cave darkness show. No
    // argument flips the current state.
    if (std::strncmp(line, "day", 3) == 0 &&
        (line[3] == '\0' || line[3] == ' ')) {
        const char* arg = line + 3;
        while (*arg == ' ') ++arg;
        if (std::strcmp(arg, "on") == 0)       alwaysDay_ = true;
        else if (std::strcmp(arg, "off") == 0) alwaysDay_ = false;
        else                                   alwaysDay_ = !alwaysDay_;
        LogInfo("[cmd] always-day %s\n", alwaysDay_ ? "ON" : "off");
        return;
    }

    // Otherwise: speak it.
    SayAscii(line);
}

void Client::SayAscii(const char* text) {
    if (!text || !text[0]) return;
    u8 buf[512];
    const usize n = build::SpeechAscii(buf,
                                       /*type=*/0x00,
                                       /*hue=*/0x0040,
                                       /*font=*/0x0003,
                                       text);
    if (n > sizeof(buf)) return;
    Send(buf, n, "0x03 SpeechAscii");
}

void Client::SendAttack(u32 serial) {
    if (!serial) return;
    u8 buf[5];
    Send(buf, build::Attack(buf, serial), "0x05 AttackRequest");
}

void Client::SendDoubleClick(u32 serial) {
    if (!serial) return;
    u8 buf[5];
    Send(buf, build::DoubleClick(buf, serial), "0x06 DoubleClick (js)");
}

void Client::SendTakeToBackpack(u32 serial, u16 qty) {
    if (!serial) return;
    const u32 backpack = PlayerEquipSerialAt(kLayerBackpack);
    if (!backpack) { LogWarn("[take] backpack serial unknown\n"); return; }
    u8 buf[16];
    Send(buf, build::PickUpItem(buf, serial, qty), "0x07 PickUp (take)");
    Send(buf, build::DropItem(buf, serial, 0xFFFF, 0xFFFF, 0, backpack), "0x08 Drop (take->pack)");
}

void Client::SendVendorBuy(u32 vendor, const std::vector<VendorBuyReq>& items) {
    if (!vendor || items.empty()) return;
    std::vector<build::VendorBuyEntry> entries;
    entries.reserve(items.size());
    for (const VendorBuyReq& r : items) {
        if (r.qty == 0 || !r.serial) continue;
        entries.push_back(build::VendorBuyEntry{r.layer ? r.layer : u8(0x1A), r.serial, r.qty});
    }
    if (entries.empty()) return;
    u8 buf[256];
    const usize n = build::VendorBuy(buf, vendor, entries.data(), entries.size());
    if (n > sizeof(buf)) return;
    Send(buf, n, "0x3B VendorBuy");
}

// 0x34 status query (subtype 4). The server replies with a 0x11 status (carrying
// the mob's HP) AND adds us to that mob's 8-slot target history, after which it
// auto-pushes 0xA1 HP updates whenever the mob's HP changes (within 18 tiles) —
// no polling needed. Unlike 0x05, this does NOT aggro the mob. (Server:
// HandlePacket_CLIENTQUERY case 0x04 -> CPlayer_SetLastTarget + SendStatusToPlayer;
// CMobile_BroadcastStatUpdate gates pushes on CPlayer_HasTargetedSerial.)
void Client::SendStatusRequest(u32 serial) {
    if (!serial) return;
    u8 buf[10];
    Send(buf, build::GetPlayerStatus(buf, 4, serial), "0x34 StatusRequest");
}

void Client::SetWarMode(bool on) {
    u8 buf[5];
    Send(buf, build::WarMode(buf, on), "0x72 WarMode");
    playerWarMode_ = on;  // predict; server confirms via 0x72
}

void Client::SendResurrectChoice(u8 choice) {
    u8 buf[2];
    Send(buf, build::ResurrectChoice(buf, choice), "0x2C ResurrectChoice");
}

i64 Client::NowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
