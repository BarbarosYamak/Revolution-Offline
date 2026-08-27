#include "Client.h"

#include "bot/Scenario.h"
#include "uo/actions.h"
#include "uo/sphere_rules.h"
#include "uo/vendor_policy.h"

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
    // choice belongs in one place. That place now defaults to Auto, i.e. run:
    // M1 defaulted to walking because walking skips Sphere's walk-buffer
    // speedhack check (CClient::Event_Walk, src/game/clients/CClientEvent.cpp:930),
    // but that check only rejects steps sent faster than 200ms apart
    // (CClient::Event_CheckWalkBuffer, src/game/clients/CClientEvent.cpp:760-768)
    // and the run cadence here is exactly 200ms with one step in flight, so
    // there is nothing to trip. Walking everywhere was a needless tell.
    nav_.movement.gait = cfg_.defaultGait;
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
            // Load the shared world once, on the first in-world tick. The
            // const query accessors (CurrentRegion, WithinPlace, ...) cannot
            // trigger the lazy load themselves, and a scenario is entitled to
            // ask "which region am I in" before it asks to travel anywhere.
            EnsureWorldKnowledge();
            // Travel drives the tile A* through ActionGoto, so it has to run
            // before BotTick pumps the steps it queued.
            TravelTick();
            WarModeTick();
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
            ActionTick();
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
        case 0x27: OnDragCancel(data, size); break;
        case 0xAA: OnAttackAck(data, size); break;
        case 0x9E: OnVendorSellList(data, size); break;

        // Common in-world packets we just log + ignore for M1.
        case 0x23: case 0x53:
        case 0x54: case 0x5B: case 0x65: case 0x6D:
        case 0x70:
        case 0x8B: case 0x97:
        case 0xB0: OnGenericGump(data, size); break;
        case 0x6F: OnSecureTrade(data, size); break;

        // Common in-world packets we just log + ignore for M1.
        case 0xBA: case 0xBC: case 0xBF: case 0xC0:
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
    // Ask what this character can do. Progression is the difference between a
    // build and the actual character, so every session needs its own skill
    // list before it can decide anything -- and Sphere only sends one when the
    // client asks for it.
    SendSkillsRequest();
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
    ActionOnManaChanged(player_.manaCur);
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
        // Our own corpse. A 2.0.x client is never told which corpse is its
        // own (0xAF explicitly excludes the dying client -- M2 finding 3), so
        // the identification is circumstantial and says so: a corpse that
        // appears next to where we just died, before we have claimed one.
        const travel::DeathRecord& d = knowledge_.LastDeath();
        const bool mineByLink = (c.deadMobile && c.deadMobile == playerSerial_);
        const bool mineByPlace =
            d.valid && d.corpseSerial == 0 &&
            NowMs() - d.timeMs < 60000 &&
            (x - d.x) * (x - d.x) + (y - d.y) * (y - d.y) <= 9;
        if (mineByLink || mineByPlace) {
            knowledge_.NoteCorpse(serial, x, y, static_cast<i8>(z));
            char ev[128];
            std::snprintf(ev, sizeof(ev), "corpse=0x%08X at=(%d,%d,%d)",
                          serial, x, y, static_cast<int>(z));
            LogEvent("corpse_located", ev);
        }
    }

    // A 2.0.7 client never receives a drop acknowledgement, so an item
    // appearing in the world IS the confirmation that a ground drop worked.
    ActionOnItemWorld(serial, x, y, static_cast<i8>(z));
}

// 0x1D Delete Object (5 bytes): cmd + serial(4 BE). Drop it from both caches.
void Client::OnDeleteObject(const u8* data, usize size) {
    if (size < 5) return;
    const u32 serial = LoadBE32(data + 1) & 0x7FFFFFFFu;
    ActionOnObjectDeleted(serial);
    // An item leaving the world is an item leaving a trade window, if that is
    // where it was.
    TradeNoteItemRemoved(serial);
    // A deleted mobile is a target that is provably gone -- the clearest
    // reason there is to stop standing around in war mode.
    war_.OnTargetGone(serial, NowMs());
    knowledge_.ForgetService(serial);
    // A mount item leaving the world IS the dismount signal.
    ForgetEquippedItem(serial);
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

    // Self-gate: normally only when the player has changed tile, because the
    // set of things in range cannot change if nothing moved... except that it
    // can, and assuming otherwise was a REAL BUG.
    //
    // THE STUCK-BOT DEADLOCK (M3.9 Phase 1). Mobiles move on their own. A bot
    // that is standing still -- crafting at a forge, waiting on a vendor, or
    // ALREADY TRAPPED -- never advances lastPurge, so this never runs, so a
    // mobile that has since walked away keeps its cached tile forever. A*
    // treats that tile as a wall.
    //
    // The failure is self-reinforcing, which is what made it so hard to see:
    // a bot that cannot move cannot purge, and cannot purge the very entry
    // preventing it from moving. M3.7 lost a miner inside the Minoc bank to
    // this and M3.8 papered over it with a one-shot "ignore mobiles" replan.
    //
    // So the gate now also expires. Standing still is cheap to poll -- the
    // scan is a single pass over a cache bounded by view range -- and the cost
    // of NOT polling is a permanent wall.
    const i64 nowMs = NowMs();
    const bool moved = (playerX_ != lastPurgeX_ || playerY_ != lastPurgeY_);
    const bool overdue = (nowMs - lastPurgeMs_) >= kStationaryPurgeMs;
    if (!moved && !overdue) return;
    lastPurgeX_ = playerX_;
    lastPurgeY_ = playerY_;
    lastPurgeMs_ = nowMs;

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
        // The 0x24 gump 0x30 is what terminates the 0x2E/0x3C/0x74 burst, so
        // this is the point where the offer is complete and an action waiting
        // for a vendor's wares can be answered.
        ActionOnVendorOffer(serial);
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
    ActionOnContainerOpened(serial, gumpId);
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
    ActionOnContainerContents(cleared.empty() ? 0u : *cleared.begin(), count);
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
    // A paperdoll title is how a player tells a healer from a tavernkeeper
    // (M2). Filing it as a live sighting is what lets TravelToService prefer
    // the NPC this character has actually seen over the spawner table.
    NoteServiceFromTitle(serial, title);
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
    ActionOnItemInContainer(ci.serial, cont);
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
    // Either direction is combat still happening, which keeps war mode alive.
    if (attacker == playerSerial_ || defender == playerSerial_)
        war_.OnCombatEvent(NowMs());
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
    if (mobile == playerSerial_ && layer == sphere::kLayerMount) {
        LogInfo("[move] mounted (0x%04X); step cadence now %u/%ums\n", graphic,
                sphere::MountedStepMs(nav_.movement.runStepMs, true),
                sphere::MountedStepMs(nav_.movement.walkStepMs, true));
        LogEvent("mount_state", "mounted");
    }
    ActionOnItemEquipped(mobile, itemSerial, layer);
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
    // The watchdog only ever learns war mode from here, never from the request
    // we sent: the server is the authority on whether the weapon is out.
    war_.OnServerWarMode(playerWarMode_, NowMs());
    if (verboseConsole_)
        LogInfo("[0x72] war mode %s args=%02X %02X %02X\n",
                playerWarMode_ ? "on" : "off", warModeArg1_, warModeArg2_, warModeArg3_);
}

// 0x2C Resurrection Menu (2B fixed): cmd, action. Server prompts the death /
// resurrect menu (action 0); we reply with 0x2C choice 1 (resurrect) or 2
// (ghost). Mirrors Packet_HandleResurrectionMenu @0x419080. We don't auto-reply
// here — the action is forwarded to JS so the bot decides (e.g. confirm a
// resurrection after asking a healer).
void Client::RecordOwnDeath(const char* how) {
    // Where we fell is where the corpse will be. Recorded now because the ghost
    // is about to walk away from it, and a later corpse run needs a destination
    // it did not have to guess.
    //
    // Idempotent by intent: if both death paths fire, the second call records
    // the same spot, because the ghost has not moved yet at the moment either
    // one arrives.
    const wm::Region* r = CurrentRegion();
    knowledge_.NoteDeath(playerX_, playerY_, playerZ_,
                         r ? r->id.c_str() : "", NowMs());
    char ev[192];
    std::snprintf(ev, sizeof(ev), "at=(%d,%d,%d) region=%s via=%s",
                  playerX_, playerY_, static_cast<int>(playerZ_),
                  r ? r->id.c_str() : "?", how ? how : "?");
    LogEvent("death_location", ev);
    if (journey_.Active()) TravelAbort("died");
}

void Client::OnResurrectionMenu(const u8* data, usize size) {
    if (size < 2) return;
    const u8 action = data[1];
    LogInfo("[0x2C] resurrection menu action=%u\n", action);
    LogEvent("resurrect_menu", action == 1 ? "resurrect" : "prompt");

    // This packet IS the death notification for our own character. Source-X
    // sends 0xAF only to bystanders (src/game/chars/CCharAct.cpp:4446) and the
    // switch to the ghost body emits no packet at all (:4493-4494), so 0x2C is
    // the first and only thing that tells us we died.
    if (life_ != act::LifeState::Dead) {
        life_ = act::LifeState::Dead;
        LogInfo("[STATE] dead (0x2C resurrect menu)\n");
        LogEvent("state_dead", "0x2C received");
        // Record the corpse location HERE too. This line is the whole fix for a
        // bug that made travel_corpse permanently unusable: the death location
        // was only recorded in the body-change handler, but as the comment above
        // says, the ghost-body switch emits no packet, so that handler never
        // fires for our own death. Every corpse run therefore failed with "this
        // character has not died" -- immediately after dying.
        //
        // It took a lethal world to surface it. Until M3.9 populated the
        // graveyards nothing on this shard had ever killed a bot outside a
        // controlled M2 test, so the corpse path was never exercised for real.
        RecordOwnDeath("0x2C resurrect menu");
    }

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
// ---- M3.8 Phase 10: resolve a craft menu entry by NAME, never by index -----
//
// Case-insensitive substring, because the server's labels carry their cost --
// "nails (1 iron ingot)", "shirt (8 folded cloth, 1 spool of thread)" -- and a
// caller should ask for the thing, not for the whole rendered line.
usize Client::DialogIndexOf(const char* substring) const {
    if (!substring || !*substring || !activeDialog_.active) return 0;
    std::string want(substring);
    for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (usize i = 0; i < activeDialog_.options.size(); ++i) {
        std::string have = activeDialog_.options[i].text;
        for (char& c : have) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (have.find(want) != std::string::npos) return i + 1;   // 1-based on the wire
    }
    return 0;
}

std::vector<std::string> Client::CraftableNow() const {
    std::vector<std::string> out;
    if (!activeDialog_.active) return out;
    out.reserve(activeDialog_.options.size());
    for (const auto& o : activeDialog_.options) out.push_back(o.text);
    return out;
}

bool Client::ChooseDialogByName(const char* substring) {
    const usize idx = DialogIndexOf(substring);
    if (idx != 0) return AnswerDialog(static_cast<u16>(idx));

    // A miss must be DIAGNOSABLE. The interesting question is never "it failed"
    // but "what was actually on offer", because the answer is usually that the
    // character lacks a material and the server quietly filtered the entry out.
    LogWarn("[menu] '%s' is not in the live menu \"%s\" (%zu option(s)):\n",
            substring ? substring : "",
            activeDialog_.question.c_str(), activeDialog_.options.size());
    for (usize i = 0; i < activeDialog_.options.size(); ++i)
        LogWarn("        %zu) %s\n", i + 1, activeDialog_.options[i].text.c_str());
    return false;
}

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
// 0x9E Vendor Sell List (variable). What this vendor is willing to buy from
// us, drawn from its LAYER_VENDOR_BUYS box. Layout per
// PacketVendorSellList::fillSellList (Source-X src/network/send.cpp:3036-3135):
//   cmd, len(2), vendorCharSerial(4), count(2),
//   per item: serial(4), graphic(2), hue(2), amount(2), price(2),
//             nameLen(2), name[nameLen]
// Unlike the 0x74 buy list, this one carries serials, so no positional join
// with a 0x3C is needed.
// ---------------------------------------------------------------------------
void Client::OnVendorSellList(const u8* data, usize size) {
    if (size < 9) return;
    const u32 vendor = LoadBE32(data + 3);
    const u16 count = LoadBE16(data + 7);

    vendorSellOffer_.clear();
    usize p = 9;
    for (u16 i = 0; i < count; ++i) {
        if (p + 14 > size) break;
        VendorItem v{};
        v.serial  = LoadBE32(data + p); p += 4;
        v.graphic = LoadBE16(data + p); p += 2;
        p += 2;                                  // hue
        v.amount  = LoadBE16(data + p); p += 2;
        v.price   = LoadBE16(data + p); p += 2;
        const u16 nameLen = LoadBE16(data + p); p += 2;
        if (p + nameLen > size) break;
        v.name.assign(reinterpret_cast<const char*>(data + p),
                      nameLen ? nameLen - 1 : 0);   // trailing NUL
        p += nameLen;
        v.layer = 0;   // the sell request carries no layer byte
        vendorSellOffer_.push_back(std::move(v));
    }

    LogInfo("[VENDOR] sell list from 0x%08X: %zu item(s)\n",
            vendor, vendorSellOffer_.size());
    for (usize i = 0; i < vendorSellOffer_.size() && i < 8; ++i) {
        const VendorItem& v = vendorSellOffer_[i];
        LogInfo("[VENDOR]   0x%08X %-22s x%-3u %u gp\n",
                v.serial, v.name.c_str(), v.amount, v.price);
    }
    char ev[96];
    std::snprintf(ev, sizeof(ev), "vendor=0x%08X items=%zu",
                  vendor, vendorSellOffer_.size());
    LogEvent("vendor_sell_list", ev);

    if (action_.Active() && action_.kind == act::Kind::VendorSell &&
        action_.subject == action_.destination) {
        FinishAction(vendorSellOffer_.empty() ? act::Result::Unavailable
                                              : act::Result::Success,
                     "vendor sell list received");
    }
}

// ---------------------------------------------------------------------------
// 0x27 Drag Cancel (2 bytes: cmd + reason). The server refused a lift, so the
// item never left where it was. Without this the client would sit through the
// action's whole timeout believing a move might still succeed.
// ---------------------------------------------------------------------------
void Client::OnDragCancel(const u8* data, usize size) {
    const u8 reason = (size >= 2) ? data[1] : 0xFF;
    ActionOnDragCancel(reason);
}

// ---------------------------------------------------------------------------
// 0xAA Attack Acknowledge (5 bytes: cmd + serial). The server echoes the
// serial it accepted as our combat target, or 0 when it refused.
// ---------------------------------------------------------------------------
void Client::OnAttackAck(const u8* data, usize size) {
    const u32 serial = (size >= 5) ? LoadBE32(data + 1) : 0;
    if (serial) LogInfo("[0xAA] attacking 0x%08X\n", serial);
    else        LogWarn("[0xAA] attack refused\n");
    // An accepted attack is the one unambiguous "we are fighting THIS" signal
    // Sphere gives a 2.0.x client, so it is what arms the war-mode watchdog.
    if (serial) war_.OnCombatIntent(serial, NowMs());
    ActionOnAttackAck(serial);
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
    if (cfg_.createSkill[0] > 0) {
        p.skill1 = static_cast<u8>(cfg_.createSkill[0]);
        p.skill1Val = static_cast<u8>(cfg_.createSkillVal[0]);
        p.skill2 = static_cast<u8>(cfg_.createSkill[1]);
        p.skill2Val = static_cast<u8>(cfg_.createSkillVal[1]);
        p.skill3 = static_cast<u8>(cfg_.createSkill[2]);
        p.skill3Val = static_cast<u8>(cfg_.createSkillVal[2]);
        LogInfo("[0x00] requested skills %u:%u %u:%u %u:%u\n",
                p.skill1, p.skill1Val, p.skill2, p.skill2Val,
                p.skill3, p.skill3Val);
    }
    if (cfg_.createStr > 0) {
        // Source-X clamps each stat to 60 and the sum to 80 in
        // CChar::InitPlayer, exactly as it clamps the skills above. Asking is
        // free; what arrives is the server's answer.
        //
        // This exists because M3.7 proved STR is load-bearing rather than
        // cosmetic. i_pickaxe carries REQSTR=50, and Revolution's tiledata
        // gives i_shovel equip layer 0 -- unwearable -- so the pickaxe is the
        // ONLY digging tool that can go in a hand, and skill45_mining.scp gates
        // on SRC.WEAPON. A miner below STR 50 cannot mine at all on this shard.
        p.str   = static_cast<u8>(cfg_.createStr);
        p.dex   = static_cast<u8>(cfg_.createDex);
        p.intel = static_cast<u8>(cfg_.createInt);
        LogInfo("[0x00] requested stats STR %u DEX %u INT %u\n",
                p.str, p.dex, p.intel);
    }
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
    // Scripted step batches follow the session gait; a scenario that needs a
    // different one sets it with `gait` before the walk.
    directStepsGait_ = nav_.movement.gait;
    for (int i = 0; i < count; ++i) directSteps_.push_back(dir & 0x07);
    walkBatchStartX_ = playerX_;
    walkBatchStartY_ = playerY_;
    walkBatchActive_ = true;
    LogInfo("[action] walk dir=%u x%d (gait %s -> %s) from (%d,%d,%d)\n",
            dir & 0x07, count, sphere::GaitName(directStepsGait_),
            GaitResolvesToRun(directStepsGait_) ? "run" : "walk",
            playerX_, playerY_, static_cast<int>(playerZ_));
}

bool Client::WalkQueueBusy() const {
    return !directSteps_.empty() || !nav_.movement.pending.empty();
}

void Client::ActionGoto(i32 x, i32 y, bool hasZ, i8 z) {
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
    BotStartGoto(x, y, hasZ, z);
}

bool Client::MobilePosition(u32 serial, i32* x, i32* y, i8* z) const {
    for (const MobileObj& m : mobileCache_) {
        if (m.serial != serial) continue;
        if (x) *x = m.x;
        if (y) *y = m.y;
        if (z) *z = m.z;
        return true;
    }
    return false;
}

bool Client::ActionGotoMobile(u32 serial, int stopWithin) {
    i32 mx = 0, my = 0;
    if (!MobilePosition(serial, &mx, &my)) {
        LogWarn("[action] goto_mobile 0x%08X: not in the mobile cache\n", serial);
        return false;
    }
    // Stop a tile short so we end up beside it rather than trying to walk
    // onto its tile, which the server would refuse.
    const i32 dx = (mx > playerX_) ? -stopWithin : (mx < playerX_ ? stopWithin : 0);
    LogInfo("[action] goto_mobile 0x%08X at (%d,%d)\n", serial, mx, my);
    ActionGoto(mx + dx, my);
    return true;
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


// ===========================================================================
// M2 player action primitives
//
// Every action here follows the same shape:
//   1. validate locally (fail InvalidState rather than send a doomed packet)
//   2. send the packet a real client would send
//   3. record what server response counts as confirmation
//   4. let the packet handlers call FinishAction() when it arrives
//   5. time out cleanly otherwise
//
// The confirmation rules are the interesting part and are documented per
// action, with the Source-X behaviour they rely on.
// ===========================================================================

namespace {
// Deadlines. Generous enough for a loaded server, short enough that a
// scenario fails rather than hangs.
constexpr i64 kUseTimeoutMs      = 4000;
constexpr i64 kMoveTimeoutMs     = 4000;
constexpr i64 kEquipTimeoutMs    = 4000;
// Gathering skills are slow on purpose: Fishing's own DELAY is 8.0 seconds
// (skills/skill18_fishing.scp), so an 8 s action deadline expired at the exact
// moment the server was answering.
constexpr i64 kSkillTimeoutMs    = 15000;
constexpr i64 kCastTimeoutMs     = 12000;
constexpr i64 kAttackTimeoutMs   = 4000;
constexpr i64 kBankTimeoutMs     = 6000;
constexpr i64 kVendorTimeoutMs   = 8000;
constexpr i64 kBandageTimeoutMs  = 15000;
// Resurrection is driven by the world (a healer walking over, a shrine), so it
// gets a long window rather than a request/response deadline.
constexpr i64 kResurrectTimeoutMs = 900000;
// A lift must be followed by a drop; the server cancels a dangling lift.
constexpr i64 kDragSettleMs      = 250;
}  // namespace

void Client::BeginAction(act::Kind kind, i64 timeoutMs) {
    if (action_.Active()) {
        LogWarn("[action] %s superseded by %s\n",
                act::KindName(action_.kind), act::KindName(kind));
        FinishAction(act::Result::InvalidState, "superseded");
    }
    action_.Begin(kind, NowMs(), timeoutMs);
    manaAtActionStart_ = PlayerMana();
    goldAtActionStart_ = PlayerGold();
    LogInfo("[ACTION] %s start\n", act::KindName(kind));
}

void Client::FinishAction(act::Result r, const char* why) {
    if (!action_.Active()) return;
    const act::Kind kind = action_.kind;
    action_.Finish(r);
    const i64 tookMs = NowMs() - action_.startedMs;

    if (r == act::Result::Success) {
        LogInfo("[ACTION_RESULT] %s %s (%lldms) %s\n", act::KindName(kind),
                act::ResultName(r), static_cast<long long>(tookMs),
                why ? why : "");
    } else {
        LogWarn("[ACTION_RESULT] %s %s (%lldms) %s\n", act::KindName(kind),
                act::ResultName(r), static_cast<long long>(tookMs),
                why ? why : "");
    }
    char ev[192];
    std::snprintf(ev, sizeof(ev), "%s %s took=%lldms %s", act::KindName(kind),
                  act::ResultName(r), static_cast<long long>(tookMs),
                  why ? why : "");
    LogEvent("action_result", ev);
}

void Client::ActionTick() {
    if (action_.ExpireIfDue(NowMs())) {
        LogWarn("[ACTION_RESULT] %s timeout (no server confirmation)\n",
                act::KindName(action_.kind));
        char ev[96];
        std::snprintf(ev, sizeof(ev), "%s timeout", act::KindName(action_.kind));
        LogEvent("action_result", ev);
        // A timed-out drag must not leave the client believing it holds an
        // item: the server either never accepted the lift or cancelled it.
        if (drag_.InFlight()) {
            LogWarn("[ITEM] drag of 0x%08X abandoned after timeout\n",
                    drag_.Serial());
            drag_.Reset();
        }
        target_.OnCancelled();
    }
}

// --- helpers ---------------------------------------------------------------

i32 Client::PlayerMana() const { return player_.manaCur; }
i32 Client::PlayerHp() const { return player_.hpCur; }
i32 Client::PlayerHpMax() const { return player_.hpMax; }
i32 Client::PlayerGold() const { return player_.gold; }

bool Client::ContainerKnown(u32 serial) const {
    return containerItems_.find(serial) != containerItems_.end();
}

u32 Client::NearestMobile(int maxDist) const {
    u32 best = 0;
    int bestD = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (maxDist > 0 && d > maxDist) continue;
        if (!best || d < bestD) { best = m.serial; bestD = d; }
    }
    return best;
}

u32 Client::NearestMobileWithBody(u16 body, int maxDist) const {
    u32 best = 0;
    int bestD = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        if (m.body != body) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (maxDist > 0 && d > maxDist) continue;
        if (!best || d < bestD) { best = m.serial; bestD = d; }
    }
    return best;
}

u32 Client::NearestMobileWithTrade(const char* trade) const {
    if (!trade || !trade[0]) return 0;
    auto lower = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };
    const std::string want = lower(trade);

    u32 best = 0;
    int bestD = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        const char* title = PaperdollTitle(m.serial);
        if (!title || !*title) continue;
        const std::string t = lower(title);
        const usize the = t.rfind(" the ");
        if (the == std::string::npos) continue;
        if (t.substr(the + 5) != want) continue;

        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (!best || d < bestD) { best = m.serial; bestD = d; }
    }
    return best;
}

u32 Client::NearestMobileNamed(const char* needle) const {
    if (!needle || !needle[0]) return 0;
    u32 best = 0;
    int bestD = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        const auto it = mobileNames_.find(m.serial);
        const char* nm = (it != mobileNames_.end()) ? it->second.c_str() : nullptr;
        const char* title = PaperdollTitle(m.serial);
        bool match = false;
        for (const char* hay : {nm, title}) {
            if (!hay) continue;
            for (const char* p = hay; *p && !match; ++p) {
                usize i = 0;
                while (needle[i] && p[i] &&
                       std::tolower(static_cast<unsigned char>(p[i])) ==
                       std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
                if (!needle[i]) match = true;
            }
        }
        if (!match) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (!best || d < bestD) { best = m.serial; bestD = d; }
    }
    return best;
}

u32 Client::FindBackpackItemByGraphic(u16 graphic) const {
    const u32 pack = PlayerEquipSerialAt(kLayerBackpack);
    if (!pack) return 0;
    const auto it = containerItems_.find(pack);
    if (it == containerItems_.end()) return 0;
    for (const ContainerItem& ci : it->second)
        if (ci.graphic == graphic) return ci.serial;
    return 0;
}

u32 Client::FindWorldItemByGraphic(u16 graphic, i32 maxDist) const {
    u32 best = 0;
    i32 bestD = maxDist + 1;
    for (const auto& kv : items_) {
        if (kv.second.itemId != graphic) continue;
        const i32 dx = kv.second.x - playerX_;
        const i32 dy = kv.second.y - playerY_;
        const i32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d < bestD) { bestD = d; best = kv.first; }
    }
    return best;
}

u32 Client::BackpackItemCount(u16 graphic) const {
    const u32 pack = PlayerEquipSerialAt(kLayerBackpack);
    if (!pack) return 0;
    const auto it = containerItems_.find(pack);
    if (it == containerItems_.end()) return 0;
    u32 total = 0;
    for (const ContainerItem& ci : it->second)
        if (ci.graphic == graphic) total += ci.amount ? ci.amount : 1;
    return total;
}

// --- object use ------------------------------------------------------------
// 0x06 double-click. What counts as confirmation depends on what was clicked,
// so any of these ends the action: a container opening (0x24), a target cursor
// (0x6C, e.g. a tool or bandage), a system message (0x1C), or the item
// changing/vanishing. That is exactly what a player sees happen.
void Client::ActionUseObject(u32 serial) {
    if (!serial) { BeginAction(act::Kind::UseObject, kUseTimeoutMs);
                   FinishAction(act::Result::InvalidState, "null serial"); return; }
    BeginAction(act::Kind::UseObject, kUseTimeoutMs);
    action_.subject = serial;
    LogInfo("[ACTION] use_object serial=0x%08X\n", serial);
    SendDoubleClick(serial);
}

void Client::ActionOpenContainer(u32 serial) {
    if (!serial) { BeginAction(act::Kind::OpenContainer, kUseTimeoutMs);
                   FinishAction(act::Result::InvalidState, "null serial"); return; }
    BeginAction(act::Kind::OpenContainer, kUseTimeoutMs);
    action_.subject = serial;
    LogInfo("[ACTION] open_container serial=0x%08X\n", serial);
    SendDoubleClick(serial);
}

// --- inventory -------------------------------------------------------------

bool Client::SendLift(u32 serial, u16 amount) {
    if (drag_.InFlight()) {
        LogWarn("[ITEM] a drag of 0x%08X is already in flight\n", drag_.Serial());
        return false;
    }
    u8 buf[16];
    const usize n = build::PickUpItem(buf, serial, amount);
    if (!Send(buf, n, "0x07 PickUpItem")) return false;
    drag_.BeginLift(serial, amount, NowMs());
    LogInfo("[ITEM] drag serial=0x%08X amount=%u\n", serial, amount);
    return true;
}

void Client::SendDropToContainer(u32 serial, u32 container) {
    u8 buf[24];
    // x/y 0xFFFF = "anywhere in the container", the classic client's own
    // behaviour when dropping onto a container gump rather than a slot.
    const usize n = build::DropItem(buf, serial, 0xFFFF, 0xFFFF, 0, container);
    Send(buf, n, "0x08 DropItem (container)");
    drag_.OnDropSent(container);
    LogInfo("[ITEM] drop container=0x%08X\n", container);
}

void Client::SendDropToGround(u32 serial, i32 x, i32 y, i8 z) {
    u8 buf[24];
    const usize n = build::DropItem(buf, serial, static_cast<u16>(x),
                                    static_cast<u16>(y), z, 0xFFFFFFFFu);
    Send(buf, n, "0x08 DropItem (ground)");
    drag_.OnDropSent(0xFFFFFFFFu);
    LogInfo("[ITEM] drop ground=(%d,%d,%d)\n", x, y, static_cast<int>(z));
}

// A UO move is lift + drop. Nothing is assumed moved until the server says so:
// success is 0x25 (item added to the destination container), failure is 0x27
// (drag cancel) with a reason code.
void Client::ActionMoveItem(u32 serial, u16 amount, u32 destContainer) {
    BeginAction(act::Kind::MoveItem, kMoveTimeoutMs);
    action_.subject = serial;
    action_.destination = destContainer;
    action_.amount = amount;
    if (!serial || !destContainer) {
        FinishAction(act::Result::InvalidState, "null serial/destination");
        return;
    }
    LogInfo("[ACTION] move_item serial=0x%08X amount=%u dest=0x%08X\n",
            serial, amount, destContainer);
    if (!SendLift(serial, amount)) {
        FinishAction(act::Result::InvalidState, "lift refused locally");
        return;
    }
    SendDropToContainer(serial, destContainer);
}

void Client::ActionDropGround(u32 serial, u16 amount, i32 x, i32 y, i8 z) {
    BeginAction(act::Kind::DropGround, kMoveTimeoutMs);
    action_.subject = serial;
    action_.amount = amount;
    action_.x = x; action_.y = y; action_.z = z;
    if (!serial) { FinishAction(act::Result::InvalidState, "null serial"); return; }
    LogInfo("[ACTION] drop_ground serial=0x%08X at (%d,%d,%d)\n",
            serial, x, y, static_cast<int>(z));
    if (!SendLift(serial, amount)) {
        FinishAction(act::Result::InvalidState, "lift refused locally");
        return;
    }
    SendDropToGround(serial, x, y, z);
}

// --- equipment -------------------------------------------------------------
// Equipping is lift + 0x13 wear. Confirmation is the server's 0x2E telling us
// the item is on that layer of OUR mobile -- never a local assumption.
void Client::ActionEquip(u32 serial, u8 layer) {
    BeginAction(act::Kind::Equip, kEquipTimeoutMs);
    action_.subject = serial;
    action_.layer = layer;
    if (!serial || !playerSerial_) {
        FinishAction(act::Result::InvalidState, "null serial");
        return;
    }
    LogInfo("[ACTION] equip serial=0x%08X layer=%u\n", serial, layer);
    if (!SendLift(serial, 1)) {
        FinishAction(act::Result::InvalidState, "lift refused locally");
        return;
    }
    u8 buf[16];
    const usize n = build::EquipItem(buf, serial, layer, playerSerial_);
    Send(buf, n, "0x13 EquipItem");
    drag_.OnDropSent(playerSerial_);
}

// Unequipping is lift off the layer + drop into the backpack; the server
// confirms with 0x25 into the pack.
void Client::ActionUnequip(u32 serial) {
    BeginAction(act::Kind::Unequip, kEquipTimeoutMs);
    action_.subject = serial;
    const u32 pack = PlayerEquipSerialAt(kLayerBackpack);
    action_.destination = pack;
    if (!serial || !pack) {
        FinishAction(act::Result::InvalidState, "no item or no backpack");
        return;
    }
    LogInfo("[ACTION] unequip serial=0x%08X -> backpack 0x%08X\n", serial, pack);
    if (!SendLift(serial, 1)) {
        FinishAction(act::Result::InvalidState, "lift refused locally");
        return;
    }
    SendDropToContainer(serial, pack);
}

// --- targeting -------------------------------------------------------------
// Answering a cursor is only allowed for the cursor that is actually live.
// The generation check is what stops a late reply from answering a different
// request than the one the caller meant.
bool Client::ActionTargetObject(u32 serial) {
    if (!target_.Active()) {
        LogWarn("[TARGET] reply refused: no cursor armed\n");
        return false;
    }
    LogInfo("[TARGET] reply serial=0x%08X gen=%u\n", serial,
            target_.Current().generation);
    TargetRespondObject(serial);
    return true;
}

bool Client::ActionTargetGround(i32 x, i32 y, i8 z) {
    if (!target_.Active()) {
        LogWarn("[TARGET] reply refused: no cursor armed\n");
        return false;
    }
    LogInfo("[TARGET] reply ground=(%d,%d,%d) gen=%u\n", x, y,
            static_cast<int>(z), target_.Current().generation);
    TargetRespondGround(x, y, true, z);
    return true;
}

bool Client::ActionMenuChoose(u16 index) {
    return AnswerDialog(index);
}

bool Client::ActionTargetStatic(i32 x, i32 y, i8 z, u16 graphic) {
    if (!target_.Active()) {
        LogWarn("[TARGET] static reply refused: no cursor armed\n");
        return false;
    }
    LogInfo("[TARGET] reply static=(%d,%d,%d) gfx=0x%04X gen=%u\n", x, y,
            static_cast<int>(z), graphic, target_.Current().generation);
    TargetRespondStatic(x, y, z, graphic);
    return true;
}

bool Client::ActionCancelTarget() {
    if (!target_.Active()) return false;
    LogInfo("[TARGET] cancel gen=%u\n", target_.Current().generation);
    CancelTargetCursor("action");
    // Whatever asked for this cursor cannot complete now.
    if (action_.Active() && action_.awaitingTarget)
        FinishAction(act::Result::Rejected, "target cancelled by the client");
    return true;
}

// A cursor arrived while an action was waiting for one. Actions that carry a
// target answer it themselves; the rest simply record that it happened.
void Client::OnTargetArmedForAction() {
    if (!action_.Active()) return;

    // A bare "use object" is answered by whatever the server does next. If it
    // arms a target cursor, the double-click was accepted and that IS the
    // confirmation -- a scroll, a tool or a bandage all behave this way. The
    // caller then answers the cursor itself. Actions that carry their own
    // target (skills, spells, bandages) fall through and auto-answer below.
    if (action_.kind == act::Kind::UseObject && !action_.awaitingTarget) {
        FinishAction(act::Result::Success, "server armed a target cursor");
        return;
    }
    if (!action_.awaitingTarget) return;
    action_.targetGeneration = target_.Current().generation;

    if (action_.destination != 0) {
        LogInfo("[TARGET] auto-reply for %s -> 0x%08X\n",
                act::KindName(action_.kind), action_.destination);
        ActionTargetObject(action_.destination);
        action_.awaitingTarget = false;
        // The action itself is confirmed by its own effect (message, mana,
        // bandage completion); the target reply is only a step along the way.
    }
}

// --- skills and magery -----------------------------------------------------
// Both go out as the 0x12 action request the 2.0.x client uses (cast = 0x56,
// skill = 0x24 with "<id> 0"). Confirmation is the observable consequence: a
// target cursor for targeted uses, or a system message / mana change.
void Client::ActionUseSkill(int skillId, u32 targetSerial) {
    BeginAction(act::Kind::UseSkill, kSkillTimeoutMs);
    action_.id = skillId;
    action_.destination = targetSerial;
    action_.awaitingTarget = true;
    LogInfo("[ACTION] use_skill id=%d target=0x%08X\n", skillId, targetSerial);
    u8 buf[64];
    const usize n = build::UseSkill(buf, skillId);
    Send(buf, n, "0x12 UseSkill");
}

void Client::ActionCastSpell(int spellId, u32 targetSerial) {
    BeginAction(act::Kind::CastSpell, kCastTimeoutMs);
    action_.id = spellId;
    action_.destination = targetSerial;
    action_.awaitingTarget = true;
    LogInfo("[ACTION] cast_spell id=%d target=0x%08X mana=%d\n",
            spellId, targetSerial, PlayerMana());
    u8 buf[64];
    const usize n = build::CastSpell(buf, spellId);
    Send(buf, n, "0x12 CastSpell");
}

// --- combat ----------------------------------------------------------------
void Client::ActionCastScroll(u32 scrollSerial, u32 targetSerial) {
    BeginAction(act::Kind::CastSpell, kCastTimeoutMs);
    action_.subject = scrollSerial;
    action_.destination = targetSerial ? targetSerial : playerSerial_;
    action_.awaitingTarget = true;
    if (!scrollSerial) {
        FinishAction(act::Result::InvalidState, "no scroll");
        return;
    }
    LogInfo("[ACTION] cast_spell from scroll=0x%08X target=0x%08X mana=%d\n",
            scrollSerial, action_.destination, PlayerMana());
    SendDoubleClick(scrollSerial);
}

void Client::ActionAttack(u32 serial) {
    BeginAction(act::Kind::Attack, kAttackTimeoutMs);
    action_.subject = serial;
    if (!serial) { FinishAction(act::Result::InvalidState, "null serial"); return; }
    LogInfo("[ACTION] attack serial=0x%08X\n", serial);
    SendAttack(serial);
}

// One path in and out of war mode, so the watchdog always knows why we are in
// it. Routing this straight at SetWarMode -- as it did before M2.5 -- left the
// watchdog holding a stale peaceful intent, and it dutifully sheathed the
// weapon a millisecond after the caller drew it.
void Client::ActionWarMode(bool on) {
    LogInfo("[ACTION] war_mode %s\n", on ? "on" : "off");
    if (on) EnterWarMode();
    else    ExitWarMode();
}

// --- bandages --------------------------------------------------------------
// A bandage is used like any other item (double-click) and then asks for a
// target. The targeting layer stays generic: the bandage-specific part is only
// that this action knows which serial to answer with.
void Client::ActionUseBandage(u32 bandageSerial, u32 targetSerial) {
    BeginAction(act::Kind::Bandage, kBandageTimeoutMs);
    action_.subject = bandageSerial;
    action_.destination = targetSerial ? targetSerial : playerSerial_;
    action_.awaitingTarget = true;
    if (!bandageSerial) {
        FinishAction(act::Result::InvalidState, "no bandage");
        return;
    }
    LogInfo("[ACTION] bandage item=0x%08X target=0x%08X\n",
            bandageSerial, action_.destination);
    SendDoubleClick(bandageSerial);
}

// --- banking ---------------------------------------------------------------
// Sphere opens the bank the way a player does it: speak the keyword near a
// banker. There is no client-side shortcut; the server decides.
void Client::ActionOpenBank(u32 bankerSerial, const char* phrase) {
    BeginAction(act::Kind::OpenBank, kBankTimeoutMs);
    action_.subject = bankerSerial;
    LogInfo("[ACTION] open_bank banker=0x%08X phrase='%s'\n",
            bankerSerial, phrase ? phrase : "bank");
    SayAscii(phrase && phrase[0] ? phrase : "bank");
}

// --- vendors ---------------------------------------------------------------
// A vendor shop is opened by speaking to the vendor. Confirmation is the
// 0x24 gump 0x30 that terminates the 0x2E/0x3C/0x74 burst, which the client
// already assembles into vendorOffer_.
// Address one named NPC rather than shouting at the street.
//
// Source-X walks every character in earshot and, for each, calls
// NPC_OnHearName (CClientEvent.cpp:1962). A name match sets bNamed and
// `break`s the loop, so exactly one NPC answers; an unnamed keyword instead
// falls through to "pick closest NPC", and the closest shopkeeper is not
// necessarily the one we walked to. m3_sell2 said a bare "buy" a tile from
// Jebidiah and got the shoemaker's stock list instead, from a vendor five
// tiles further off. The paperdoll title we already fetched to identify the
// vendor ("Shika, the fisherwoman") carries the name we need.
std::string Client::AddressMobile(u32 serial, const char* phrase) const {
    const std::string say = (phrase && phrase[0]) ? phrase : "";
    auto it = paperdollTitles_.find(serial);
    if (it == paperdollTitles_.end() || it->second.empty()) return say;
    const std::string& title = it->second;
    usize cut = title.find(',');
    if (cut == std::string::npos) cut = title.find(" the ");
    if (cut == std::string::npos || cut == 0) return say;
    if (say.empty()) return title.substr(0, cut);
    return title.substr(0, cut) + " " + say;
}

void Client::ActionVendorOpen(u32 vendorSerial, const char* phrase) {
    BeginAction(act::Kind::VendorBuy, kVendorTimeoutMs);
    action_.subject = vendorSerial;
    action_.destination = vendorSerial;
    vendorOffer_.clear();
    vendorOfferVendor_ = 0;
    const std::string say = AddressMobile(vendorSerial, phrase && phrase[0] ? phrase : "buy");
    LogInfo("[VENDOR] open vendor=0x%08X say='%s'\n", vendorSerial, say.c_str());
    SayAscii(say.c_str());
}

// --- NPC teaching ----------------------------------------------------------
//
// Sphere's Teaching system is real and stock: CChar::NPC_OnTrainHear
// (CCharNPCAct_Vendor.cpp:370) answers the speech verb TRAIN followed by a
// skill name, quotes a price, and remembers the offer. Handing the NPC gold
// then completes it (NPC_OnTrainPay, :273).
//
// What it will teach, from this runtime's own sphere.ini:
//     NPCTrainPercent=30   -> up to 30% of the TRAINER's own skill
//     NPCTrainMax=420      -> and never above 42.0 whatever the trainer knows
//     NPCTrainCost=1       -> 1gp per 0.1 skill
// so a GM trainer teaches to 30.0, and 0 -> 30.0 costs exactly 300 gold.
//
// Addressed by name for the reason M3 found the hard way: an unnamed keyword
// is answered by the nearest NPC, not the one we walked to.
void Client::ActionNpcTrain(u32 npcSerial, const char* skillKey) {
    BeginAction(act::Kind::NpcTrain, kVendorTimeoutMs);
    action_.subject = npcSerial;
    action_.destination = npcSerial;
    std::string phrase = "train ";
    phrase += (skillKey && skillKey[0]) ? skillKey : "";
    const std::string say = AddressMobile(npcSerial, phrase.c_str());
    LogInfo("[TRAIN] ask 0x%08X say='%s'\n", npcSerial, say.c_str());
    LogEvent("npc_train_ask", say.c_str());
    SayAscii(say.c_str());
}

// Hand an item (in practice, a counted stack of gold) to a mobile. This is an
// ordinary lift-and-drop onto the character, the same motion a player makes.
//
// It finishes as soon as the drop is away, and deliberately claims nothing
// about the outcome: the NPC's container is not ours to see. The proof that
// teaching happened is the server's own skill and gold numbers afterwards, not
// this result.
void Client::ActionNpcGive(u32 mobileSerial, u32 itemSerial, u16 amount) {
    LogInfo("[GIVE] 0x%08X x%u -> 0x%08X (gold %d)\n", itemSerial, amount,
            mobileSerial, PlayerGold());
    if (!SendLift(itemSerial, amount)) {
        LogWarn("[GIVE] could not lift 0x%08X\n", itemSerial);
        return;
    }
    SendDropToContainer(itemSerial, mobileSerial);
    char ev[96];
    std::snprintf(ev, sizeof(ev), "item=0x%08X amount=%u to=0x%08X", itemSerial,
                  amount, mobileSerial);
    LogEvent("npc_give", ev);
}

void Client::ActionVendorBuy(u32 vendorSerial, u32 itemSerial, u16 qty) {
    BeginAction(act::Kind::VendorBuy, kVendorTimeoutMs);
    action_.subject = itemSerial;
    action_.destination = vendorSerial;
    action_.amount = qty;

    u8 layer = 0x1A;
    u16 graphic = 0;
    bool found = false;
    for (const VendorItem& v : vendorOffer_) {
        if (v.serial == itemSerial) {
            layer = v.layer; graphic = v.graphic; found = true; break;
        }
    }
    if (!found) {
        FinishAction(act::Result::InvalidState, "item not in the vendor offer");
        return;
    }

    // --- M3.7 Revolution vendor authenticity policy -------------------------
    //
    // The M3.7 audit found that stock Sphere vendors sell nearly the whole raw
    // and intermediate production chain -- ore, logs, boards, wool, yarn,
    // thread, cloth, bolts, hides, blank scrolls, bottles and every reagent.
    // 284 of the 608 items on a working human vendor are goods a PLAYER makes.
    //
    // The shard is deliberately left untouched, so the vendor really will sell
    // these. The refusal therefore lives HERE, at the last moment before the
    // 0x3B goes out, and that ordering is the whole point of the proof: the
    // item is in the offer, the gold is in the pack, and the bot still declines.
    const econ::VendorRuling ruling = econ::CanUseNPCVendorForGraphic(graphic);
    const char* itemName = econ::ItemNameForGraphic(graphic);
    if (!ruling.allowed) {
        LogWarn("[policy] REFUSED NPC purchase of %s (0x%04X): %s [%s]\n",
                itemName ? itemName : "unmapped item", graphic,
                ruling.reason ? ruling.reason : "no reason",
                econ::VendorClassName(ruling.klass));
        if (ruling.authenticityGap) {
            // An UNKNOWN refusal is a RESEARCH GAP, not a decision. Logging it
            // apart from the ordinary refusals is what turns the accumulated
            // list into a backlog rather than a mystery.
            LogWarn("[policy] AUTHENTICITY GAP: no Revolution evidence either "
                    "way for whether an NPC sold %s\n",
                    itemName ? itemName : "this item");
        }
        FinishAction(act::Result::Rejected,
                     "Revolution vendor policy refuses this NPC purchase");
        return;
    }
    LogInfo("[policy] allowed NPC purchase of %s (0x%04X): %s [%s]\n",
            itemName ? itemName : "item", graphic,
            ruling.reason ? ruling.reason : "", econ::VendorClassName(ruling.klass));

    LogInfo("[VENDOR] buy item=0x%08X qty=%u from vendor=0x%08X gold=%d\n",
            itemSerial, qty, vendorSerial, PlayerGold());
    std::vector<VendorBuyReq> req;
    req.push_back(VendorBuyReq{itemSerial, qty, layer});
    SendVendorBuy(vendorSerial, req);
}

// "sell" opens the other half of the shop. Source-X answers with 0x9E
// (PacketVendorSellList, src/network/send.cpp:3028) and, unlike the buy flow,
// sends no 0x24 gump -- so the sell list itself is the confirmation.
void Client::ActionVendorSellOpen(u32 vendorSerial, const char* phrase) {
    BeginAction(act::Kind::VendorSell, kVendorTimeoutMs);
    action_.subject = vendorSerial;
    action_.destination = vendorSerial;
    vendorSellOffer_.clear();
    const std::string say = AddressMobile(vendorSerial, phrase && phrase[0] ? phrase : "sell");
    LogInfo("[VENDOR] open sell vendor=0x%08X say='%s'\n", vendorSerial, say.c_str());
    SayAscii(say.c_str());
}

void Client::ActionVendorSell(u32 vendorSerial, u32 itemSerial, u16 qty) {
    BeginAction(act::Kind::VendorSell, kVendorTimeoutMs);
    action_.subject = itemSerial;
    action_.destination = vendorSerial;
    action_.amount = qty;

    bool offered = false;
    for (const VendorItem& v : vendorSellOffer_) {
        if (v.serial == itemSerial) { offered = true; break; }
    }
    if (!offered) {
        FinishAction(act::Result::InvalidState,
                     "the vendor did not offer to buy that item");
        return;
    }
    LogInfo("[VENDOR] sell item=0x%08X qty=%u to vendor=0x%08X gold=%d\n",
            itemSerial, qty, vendorSerial, PlayerGold());

    build::VendorSellEntry e{itemSerial, qty};
    u8 buf[64];
    const usize n = build::VendorSell(buf, vendorSerial, &e, 1);
    Send(buf, n, "0x9F VendorSell");
}

// --- resurrection ----------------------------------------------------------
// Resurrection is the SERVER's decision. Replying to the 0x2C menu does NOT
// resurrect on Source-X: both choices take the same branch and only re-send
// the ghost's world state (src/network/receive.cpp:616-639). The real paths a
// player has are walking a ghost to a healer NPC (which resurrects on its own,
// src/game/chars/CCharNPCAct.cpp:895-937) or double-clicking a shrine
// (src/game/clients/CClientUse.cpp:326-332).
//
// So this action announces the ghost to the server and then waits for the
// server to bring the character back to life; the confirmation is the body
// change in 0x20/0x78.
void Client::ActionResurrectAccept() {
    BeginAction(act::Kind::Resurrect, kResurrectTimeoutMs);
    if (life_ != act::LifeState::Dead) {
        FinishAction(act::Result::InvalidState, "not dead");
        return;
    }
    LogInfo("[ACTION] resurrect: waiting for the server to raise us\n");
    SendResurrectChoice(2);   // acknowledge the ghost state
}

// ===========================================================================
// Confirmation hooks -- called from the packet handlers
// ===========================================================================

void Client::ActionOnContainerOpened(u32 serial, u16 gumpId) {
    // The bank box arrives as a container we did not double-click, while an
    // open_bank action is outstanding.
    if (action_.Active() && action_.kind == act::Kind::OpenBank) {
        bankContainer_ = serial;
        LogInfo("[STATE] bank container=0x%08X gump=0x%04X\n", serial, gumpId);
        LogEvent("bank_opened", "container recognised");
        FinishAction(act::Result::Success, "bank container opened");
        return;
    }
    if (!action_.Active()) return;
    if (action_.kind == act::Kind::OpenContainer ||
        action_.kind == act::Kind::UseObject) {
        if (action_.subject == serial || action_.subject == 0)
            FinishAction(act::Result::Success, "container opened");
    }
}

void Client::ActionOnContainerContents(u32 container, u16 count) {
    (void)count;
    if (!action_.Active()) return;
    if ((action_.kind == act::Kind::OpenContainer ||
         action_.kind == act::Kind::UseObject) &&
        action_.subject == container) {
        FinishAction(act::Result::Success, "contents received");
    }
}

void Client::ActionOnItemInContainer(u32 item, u32 container) {
    // A trade window is an ordinary container as far as the protocol is
    // concerned, so this is where the offer contents come from.
    TradeNoteItemAdded(container, item);
    if (drag_.InFlight() && drag_.Serial() == item) drag_.Reset();
    if (!action_.Active()) return;

    const bool isOurItem = (action_.subject == item);
    if (!isOurItem) return;

    if (action_.kind == act::Kind::MoveItem ||
        action_.kind == act::Kind::Unequip) {
        if (action_.destination == container || action_.destination == 0) {
            FinishAction(act::Result::Success, "item is in the destination");
        } else {
            FinishAction(act::Result::ServerFailure,
                         "item landed in a different container");
        }
    } else if (action_.kind == act::Kind::VendorBuy) {
        FinishAction(act::Result::Success, "purchased item delivered");
    }
}

void Client::ActionOnItemEquipped(u32 mobile, u32 item, u8 layer) {
    if (drag_.InFlight() && drag_.Serial() == item) drag_.Reset();
    if (!action_.Active() || action_.kind != act::Kind::Equip) return;
    if (mobile != playerSerial_ || item != action_.subject) return;
    if (layer == action_.layer || action_.layer == 0) {
        FinishAction(act::Result::Success, "worn on the requested layer");
    } else {
        FinishAction(act::Result::ServerFailure, "worn on a different layer");
    }
}

void Client::ActionOnItemWorld(u32 item, i32 x, i32 y, i8 z) {
    if (drag_.InFlight() && drag_.Serial() == item) drag_.Reset();
    if (!action_.Active() || action_.kind != act::Kind::DropGround) return;
    if (item != action_.subject) return;
    (void)z;
    if (x == action_.x && y == action_.y)
        FinishAction(act::Result::Success, "item is on the ground at the target");
    else
        FinishAction(act::Result::Success, "item is on the ground (server placed it)");
}

// 0x27: the server refused a lift. This is the case that used to corrupt
// client state -- the item never moved, so the drag is dropped and the action
// fails with a reason instead of timing out.
void Client::ActionOnDragCancel(u8 reason) {
    static const char* kReasons[] = {
        "cannot lift that",          // 0
        "out of range",              // 1
        "out of sight",              // 2
        "belongs to another",        // 3
        "already holding something", // 4
        "unspecified",               // 5
    };
    const char* why = (reason < 6) ? kReasons[reason] : "unknown reason";
    LogWarn("[ITEM] drag cancelled by server: %s (code %u)\n", why, reason);
    char ev[96];
    std::snprintf(ev, sizeof(ev), "reason=%u %s", reason, why);
    LogEvent("drag_cancel", ev);
    drag_.Reset();
    if (!action_.Active()) return;
    if (action_.kind == act::Kind::MoveItem ||
        action_.kind == act::Kind::DropGround ||
        action_.kind == act::Kind::Equip ||
        action_.kind == act::Kind::Unequip) {
        FinishAction(act::Result::Rejected, why);
    }
}

void Client::ActionOnAttackAck(u32 serial) {
    if (!action_.Active() || action_.kind != act::Kind::Attack) return;
    if (serial == 0) {
        FinishAction(act::Result::Rejected, "attack refused by the server");
        return;
    }
    if (serial == action_.subject)
        FinishAction(act::Result::Success, "server accepted the target");
}

void Client::ActionOnBodyChange(u16 body) {
    const act::LifeState next = act::LifeStateFromBody(body);
    if (next == life_) return;
    life_ = next;
    LogInfo("[STATE] %s (body 0x%04X)\n", act::LifeStateName(life_), body);
    LogEvent(life_ == act::LifeState::Dead ? "state_dead" : "state_resurrected",
             "server body change");
    if (life_ == act::LifeState::Dead) RecordOwnDeath("body change");
    if (life_ == act::LifeState::Alive && action_.Active() &&
        action_.kind == act::Kind::Resurrect) {
        FinishAction(act::Result::Success, "character is alive again");
    }
}

void Client::ActionOnVendorOffer(u32 vendorSerial) {
    vendorOfferVendor_ = vendorSerial;
    vendorOffer_ = pendingVendor_;
    LogInfo("[VENDOR] offer from 0x%08X: %zu item(s)\n",
            vendorSerial, vendorOffer_.size());
    for (usize i = 0; i < vendorOffer_.size() && i < 8; ++i) {
        const VendorItem& v = vendorOffer_[i];
        LogInfo("[VENDOR]   0x%08X %-22s x%-3u %u gp\n",
                v.serial, v.name.c_str(), v.amount, v.price);
    }
    char ev[96];
    std::snprintf(ev, sizeof(ev), "vendor=0x%08X items=%zu",
                  vendorSerial, vendorOffer_.size());
    LogEvent("vendor_offer", ev);

    if (action_.Active() && action_.kind == act::Kind::VendorBuy &&
        action_.subject == action_.destination) {
        // Asking to shop is SPEECH, and every vendor within three tiles
        // answers it (M2 finding 1) -- outside a Britain mage shop that means
        // the alchemist's potions arrive alongside the mage's reagents. Only
        // the vendor we actually addressed completes the action; the others
        // are recorded and ignored, or the bot ends up buying from whoever
        // happened to shout back first.
        if (action_.destination && vendorSerial != action_.destination) {
            LogInfo("[VENDOR] offer from 0x%08X is not the vendor we asked "
                    "(0x%08X); still waiting\n",
                    vendorSerial, action_.destination);
            return;
        }
        // This was a "show me your wares", not a purchase.
        FinishAction(vendorOffer_.empty() ? act::Result::Unavailable
                                          : act::Result::Success,
                     "vendor offer received");
    }
}

// Sphere reports what an action actually did as text: a system line for
// refusals ("This is beyond your ability."), and a line spoken by our own
// character for a skill's result ("...looks to be of normal strength...",
// "You have hidden yourself well"). Reading those is what turns a skill or a
// spell from "packet sent" into a server-confirmed outcome.
//
// `type` is the 0x1C talk mode. Mode 10 (TALKMODE_SPELL) is the words of
// power the server makes us speak while casting -- progress, not a result.
void Client::ActionOnSysMessage(const char* text, u32 sourceSerial, u8 type) {
    if (!text || !text[0] || !action_.Active()) return;
    constexpr u8 kTalkModeSpell = 10;

    auto contains = [&](const char* needle) {
        const usize n = std::strlen(needle);
        for (const char* p = text; *p; ++p) {
            usize i = 0;
            while (i < n && p[i] &&
                   std::tolower(static_cast<unsigned char>(p[i])) ==
                   std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
            if (i == n) return true;
        }
        return false;
    };

    if (contains("you cannot reach") || contains("out of range") ||
        contains("too far away") || contains("cannot see")) {
        FinishAction(act::Result::Rejected, text);
        return;
    }
    if (contains("you have no line of sight")) {
        FinishAction(act::Result::Rejected, text);
        return;
    }
    if (contains("more reagents") || contains("not enough mana") ||
        contains("lack the mana") || contains("fizzle")) {
        FinishAction(act::Result::ServerFailure, text);
        return;
    }
    // Sphere's refusal when the spell is not castable at all -- no spellbook
    // holding it, or not enough skill (CChar::Spell_CanCast,
    // src/game/chars/CCharSpell.cpp:2517-2529 and the skill requirement check
    // just above it).
    if (contains("beyond your ability") || contains("you have no spellbook") ||
        contains("not in your spellbook") || contains("do not have that spell")) {
        FinishAction(act::Result::ServerFailure, text);
        return;
    }
    if (contains("you must wait") || contains("cannot use that skill") ||
        contains("there is no such skill")) {
        FinishAction(act::Result::ServerFailure, text);
        return;
    }
    // Meditation ends the moment it starts when the skill is too low, and the
    // outcome is a message rather than a packet. Classifying it is what turns
    // a training loop from one attempt per action-timeout (8 s) into one per
    // attempt (~2 s) -- and a failed attempt still earns skill, because
    // CChar::Skill_Fail calls Skill_Experience just as success does.
    if (action_.kind == act::Kind::UseSkill) {
        // Gathering. Sphere reports the outcome as text (core/messages.scp
        // fishing_*), so these phrases are the result packet.
        if (contains("you pull out")) {
            FinishAction(act::Result::Success, text);
            return;
        }
        if (contains("fail to catch anything") ||
            contains("there are no fish here") ||
            contains("try fishing elsewhere")) {
            // A failed gather still earns skill -- Skill_Fail calls
            // Skill_Experience -- so this is an outcome, not an error.
            FinishAction(act::Result::Success, text);
            return;
        }
        if (contains("try fishing in water") ||
            contains("can't fish from where") ||
            contains("cannot fish so close") ||
            contains("that is too far away")) {
            FinishAction(act::Result::Rejected, text);
            return;
        }
        if (contains("lose your concentration") ||
            contains("stop concentrating")) {
            FinishAction(act::Result::ServerFailure, text);
            return;
        }
        if (contains("meditative trance") || contains("you enter a trance")) {
            FinishAction(act::Result::Success, text);
            return;
        }
        if (contains("you are at peace")) {
            // Sphere's "nothing to meditate for": mana is already full.
            FinishAction(act::Result::Success, text);
            return;
        }
    }
    // The trainer's answer. Its price quote is the confirmation that the offer
    // stands and that gold will be accepted; everything else it can say is a
    // refusal, and each one is a distinct, useful fact for a bot deciding
    // whether to pay a teacher or grind the skill itself.
    if (action_.kind == act::Kind::NpcTrain) {
        if (contains("i will train you in all i know")) {
            FinishAction(act::Result::Success, text);
            return;
        }
        if (contains("you already know as much as i can teach") ||
            contains("you know more about") ||
            contains("there is nothing that i can teach you") ||
            contains("that is all i can teach")) {
            // Not an error: the honest answer is "this teacher is done with
            // you", which is exactly what a TrainerDecision needs to hear.
            FinishAction(act::Result::InvalidState, text);
            return;
        }
        if (contains("i know nothing about") ||
            contains("i would never train the likes of you")) {
            FinishAction(act::Result::ServerFailure, text);
            return;
        }
    }
    // Bandaging someone who is not hurt: the server refuses rather than
    // consuming the bandage.
    if (contains("you are healthy") || contains("they are healthy") ||
        contains("at full health")) {
        FinishAction(act::Result::ServerFailure, text);
        return;
    }
    if (contains("seem to hide") || contains("cannot hide")) {
        FinishAction(act::Result::ServerFailure, text);
        return;
    }
    if (contains("you are already hidden") || contains("you must be dead")) {
        FinishAction(act::Result::ServerFailure, text);
        return;
    }

    // A line spoken by our OWN character is the result of what we just did:
    // the skill's finding, or the outcome of a heal. Words of power are the
    // exception -- they are the cast still running.
    if (sourceSerial != 0 && sourceSerial == playerSerial_ &&
        type != kTalkModeSpell) {
        if (action_.kind == act::Kind::UseSkill ||
            action_.kind == act::Kind::Bandage) {
            FinishAction(act::Result::Success, text);
            return;
        }
    }
}

// Mana leaving the pool is the clearest confirmation that a spell actually
// went off: Sphere only deducts it in Spell_CastDone
// (src/game/chars/CCharSpell.cpp:3054 -> Spell_CanCast with fTest=false).
// The server deletes a scroll when its spell is cast (Spell_CastDone consumes
// the charge), so a 0x1D for the scroll we used confirms the cast completed.
void Client::ActionOnObjectDeleted(u32 serial) {
    if (!action_.Active() || action_.kind != act::Kind::CastSpell) return;
    if (serial != action_.subject) return;
    FinishAction(act::Result::Success, "scroll consumed by the cast");
}

// A completed sale shows up as gold arriving. Source-X only credits it after
// the items have actually changed hands (CClientEvent.cpp:1451-1566).
void Client::ActionOnGoldChanged(i32 gold) {
    if (!action_.Active() || action_.kind != act::Kind::VendorSell) return;
    if (action_.subject == action_.destination) return;   // still just browsing
    if (goldAtActionStart_ < 0 || gold <= goldAtActionStart_) return;
    char why[96];
    std::snprintf(why, sizeof(why), "gold %d -> %d", goldAtActionStart_, gold);
    FinishAction(act::Result::Success, why);
}

void Client::ActionOnManaChanged(i32 mana) {
    if (!action_.Active() || action_.kind != act::Kind::CastSpell) return;
    if (manaAtActionStart_ < 0 || mana >= manaAtActionStart_) return;
    char why[96];
    std::snprintf(why, sizeof(why), "mana %d -> %d", manaAtActionStart_, mana);
    FinishAction(act::Result::Success, why);
}

void Client::ActionScanMobiles() {
    LogInfo("[action] scanning nearby mobiles for names\n");
    PrintNearbyMobiles();

    // 0x98 only returns an NPC's first name ("Lillie"), but the trade lives in
    // the paperdoll title ("Lillie the provisioner"), which arrives as 0x88
    // after a double-click. That is exactly how a human player tells one
    // shopkeeper from another, so do the same: click each nearby mobile once.
    //
    // HUMAN BODIES ONLY, AND THIS IS NOT A TIDINESS RULE. A double-click is not
    // an inspection -- it is whatever that mobile does when clicked, and
    // Source-X mounts any non-human NPC you click (CClientEvent.cpp:2378):
    //
    //   if ( pChar->m_pNPC && (pChar->GetNPCBrainGroup() != NPCBRAIN_HUMAN) )
    //       if ( m_pChar->Horse_Mount(pChar) ) return true;
    //
    // So scanning a street with a horse in it MOUNTED THE HORSE. It cost this
    // milestone several runs: a character dismounted, scanned three seconds
    // later, and was back on the animal before the next step --
    //
    //   event mount_state: dismounted     14:27:37.570
    //   event mount_state: mounted        14:27:40.604
    //
    // -- and an earlier run mounted a horse it had never asked to ride at all.
    // A bot walking a decorated town would silently climb onto every llama and
    // ostard it passed.
    //
    // Trade titles only exist on humans, so restricting the click to human
    // bodies loses nothing and removes the side effect entirely.
    int clicked = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        if (!sphere::IsHumanBody(m.body)) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d > 12) continue;
        if (PaperdollTitle(m.serial)) continue;   // already known
        SendDoubleClick(m.serial);
        if (++clicked >= 8) break;
    }
    if (clicked)
        LogInfo("[action] requested %d paperdoll(s) for trade titles\n",
                clicked);
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
// Resolves a requested gait against the session gait and the server-driven
// stats it depends on. Auto is the only value that can change its mind.
bool Client::GaitResolvesToRun(sphere::Gait gait) const {
    if (gait == sphere::Gait::Auto) gait = nav_.movement.gait;
    return sphere::GaitWantsRun(gait, player_.stamCur, player_.stamMax,
                                player_.weight, player_.maxWeight);
}

void Client::SetMovementGait(sphere::Gait g) {
    if (nav_.movement.gait == g) return;
    nav_.movement.gait = g;
    LogInfo("[move] gait -> %s (now %s)\n", sphere::GaitName(g),
            GaitResolvesToRun(sphere::Gait::Auto) ? "running" : "walking");
    LogEvent("gait_changed", sphere::GaitName(g));
}

// The one and only 0x02 sender. Every movement source funnels through here.
// `gait` is a per-step request: Auto (the normal case) defers to the session
// gait, Walk/Run pin this step. Nothing else builds or sends a 0x02, so the
// run bit cannot enter the stream behind the controller's back.
Client::StepSubmit Client::SubmitStep(u8 dir, sphere::Gait gait,
                                      const char* source) {
    dir &= 0x07;

    // 1. Outstanding-step limit: never exceed what the controller allows.
    if (nav_.movement.pending.size() >= nav_.movement.maxInFlight)
        return StepSubmit::InFlight;

    // 2. Gait, resolved once per step so pacing and the wire bit cannot
    //    disagree. Sending the 0x80 bit at the walk cadence would be a lie the
    //    server never checks, but the anim/prediction side would still act on it.
    const bool run = GaitResolvesToRun(gait);

    // 3. Pacing: never faster than the canonical cadence for this gait. 200ms
    //    for a run is Sphere's own floor for an on-foot player
    //    (Event_CheckWalkBuffer, src/game/clients/CClientEvent.cpp:766-767;
    //    Event_Walk, :920), so we sit exactly on it rather than under it.
    //    A mount halves that floor -- Event_CheckWalkBuffer reads
    //    STATF_ONHORSE and drops iTimeMin from 200 to 100
    //    (src/game/clients/CClientEvent.cpp:759-762). See sphere_rules.h for
    //    why that path, and not Event_Walk's copy, is the one in force here.
    const i64 now = NowMs();
    const u32 base = run ? nav_.movement.runStepMs : nav_.movement.walkStepMs;
    const u32 gap = sphere::MountedStepMs(base, PlayerIsMounted());
    if (nav_.movement.lastMoveSentMs != 0 &&
        now - nav_.movement.lastMoveSentMs < static_cast<i64>(gap))
        return StepSubmit::Throttled;

    // 4. Turn-then-step: a direction change is a separate move that the
    //    server acks without relocating the character.
    const bool isStep = (dir == playerFacing_);

    // 5. Sequence: allocated here and nowhere else.
    const u8 seq = NextSeq();
    // Source-X reads the facing as `rawdir & 0xF` and the gait as
    // `rawdir & DIR_MASK_RUNNING` (CClient::Event_Walk,
    // src/game/clients/CClientEvent.cpp:862,904); the sequence check in
    // PacketMovementReq::onReceive (src/network/receive.cpp:270-282) ignores
    // the bit entirely, so gait never affects accept/reject bookkeeping.
    const u8 wire = sphere::MoveDirectionByte(dir, run);

    u8 buf[16];
    const usize n = build::MoveRequest(buf, wire, seq, 0u, cfg_.legacyMovePacket);
    char note[96];
    std::snprintf(note, sizeof(note), "0x02 Move dir=%u seq=%u %s%s src=%s",
                  dir, seq, isStep ? "step" : "turn", run ? " run" : "",
                  source ? source : "?");
    if (!Send(buf, n, note)) return StepSubmit::Failed;

    // 6. Bookkeeping the ack/reject handlers rely on.
    nav_.movement.pending.push_back({seq, dir, isStep, now});
    nav_.movement.lastMoveSentMs = now;
    lastDirectStepMs_ = now;

    if (isStep) {
        BotPredictStep(dir, run);
        return StepSubmit::Sent;
    }
    playerFacing_ = dir;
    player_.facing = dir;
    // A turn relocates nothing, so the character is standing however it was
    // standing; STATF_FLY is only modified on a real step (Event_Walk,
    // src/game/clients/CClientEvent.cpp:904, inside the `dir == m_dirFace` arm).
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
    switch (SubmitStep(dir, directStepsGait_, "action")) {
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

// Dismount by double-clicking YOURSELF. Source-X spells this out in
// CClient::Event_DoubleClick (src/game/clients/CClientEvent.cpp:2362-2375):
//
//   if ( pChar == m_pChar )
//       if ( pChar->IsStatFlag(STATF_ONHORSE) )
//           ... else if ( pChar->Horse_UnMount() ) return true;
//
// and the very next block mounts, by double-clicking a non-human NPC:
//
//   if ( pChar->m_pNPC && (pChar->GetNPCBrainGroup() != NPCBRAIN_HUMAN) )
//       if ( m_pChar->Horse_Mount(pChar) ) return true;
//
// So both halves are the same gesture aimed at different targets, and neither
// is a packet of its own.
//
// AN EARLIER VERSION DOUBLE-CLICKED THE MOUNT ITEM ON LAYER 25, reasoning that
// the animal no longer exists so the item must be the handle. It is not: the
// server accepted the click and left the character mounted, and the run died
// three seconds later on "EXPECT on foot but the character is mounted".
//
// WAR MODE CAN REFUSE THIS. With COMBAT_DCLICKSELF_UNMOUNTS unset, a character
// in war mode holding a fight memory gets its paperdoll instead of a dismount
// -- deliberately, so nobody falls off mid-fight. Callers that need to be sure
// should make peace first.
//
// This exists because MOUNTS SURVIVE LOGOUT. A scenario that mounts and logs
// out comes back mounted, so a later run measuring an on-foot baseline fails
// before it walks a tile. A bot that wants a known gait has to put itself in
// one rather than inherit whatever it left with.
void Client::ActionDismount() {
    if (!PlayerIsMounted()) { LogInfo("[move] dismount: already on foot\n"); return; }
    LogInfo("[ACTION] dismount (double-click self 0x%08X)\n", playerSerial_);
    ActionUseObject(playerSerial_);
}

// Mounted iff something sits on layer 25. Sphere mounts by DELETING the animal
// (0x1D) and equipping a mount item on that layer (0x2E); dismounting reverses
// it. So this is the same fact the server is acting on when it prices a step at
// 100ms instead of 200 -- not a guess from an animation or a body id.
bool Client::PlayerIsMounted() const {
    return PlayerEquipSerialAt(sphere::kLayerMount) != 0;
}

// Equipment must be REMOVED as well as upserted, or a dismount never lands.
// SetMobileEquipLayer only ever upserts, so without this the layer-25 entry
// from the first mount would persist for the rest of the session and the bot
// would keep pacing at the mounted cadence on foot -- steps the server would
// price as too fast.
void Client::ForgetEquippedItem(u32 itemSerial) {
    auto drop = [&](std::vector<EquipObj>& v) {
        for (auto it = v.begin(); it != v.end(); ++it)
            if (it->serial == itemSerial) {
                const bool wasMount = (it->layer == sphere::kLayerMount);
                v.erase(it);
                return wasMount;
            }
        return false;
    };
    if (drop(playerEquip_)) {
        LogInfo("[move] dismounted; step cadence back to %u/%ums\n",
                nav_.movement.runStepMs, nav_.movement.walkStepMs);
        LogEvent("mount_state", "dismounted");
    }
    for (auto& m : mobileCache_) drop(m.equip);
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
    const u8  type    = data[1];
    const u32 id      = LoadBE32(data + 2);
    const u8  subtype = data[6];

    const bool superseded = target_.Active();
    target_.OnArmed(id, type, subtype, NowMs());

    LogInfo("[0x6C] TARGET requested id=0x%08X type=%s subtype=%u gen=%u%s\n",
            id, type == 1 ? "ground" : "object", subtype, target_.Generation(),
            superseded ? " (supersedes an unanswered cursor)" : "");
    char ev[128];
    std::snprintf(ev, sizeof(ev), "id=0x%08X type=%u subtype=%u gen=%u",
                  id, type, subtype, target_.Generation());
    LogEvent("target_requested", ev);

    // An action that asked for a target can now answer it.
    OnTargetArmedForAction();

    uo::js::EmitTargetEvent(id, type);  // -> Player 'target'
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
    if (!target_.Active()) {
        LogWarn("[target] no target cursor active\n");
        return;
    }
    i32 x = 0, y = 0; i8 z = 0; u16 model = 0;
    ResolveObjectTarget(serial, &x, &y, &z, &model);  // best-effort coords/graphic
    u8 buf[19];
    const usize n = build::TargetCursorObject(
        buf, target_.Current().id, target_.Current().subtype, serial,
        static_cast<u16>(x), static_cast<u16>(y), z, model);
    Send(buf, n, "0x6C TargetCursor (object)");
    LogInfo("[target] object 0x%08X (%d,%d,%d) model 0x%04X\n",
            serial, x, y, static_cast<int>(z), model);
    target_.OnReplied();
}

void Client::TargetRespondGround(i32 x, i32 y, bool hasZ, i8 z) {
    if (!target_.Active()) {
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
        buf, target_.Current().id, target_.Current().subtype,
        static_cast<u16>(x), static_cast<u16>(y), z, 0);
    Send(buf, n, "0x6C TargetCursor (ground)");
    LogInfo("[target] ground (%d,%d,%d)\n", x, y, static_cast<int>(z));
    target_.OnReplied();
}

void Client::TargetRespondStatic(i32 x, i32 y, i8 z, u16 graphic) {
    if (!target_.Active()) {
        LogWarn("[target] no target cursor active\n");
        return;
    }
    // Static target: tile reply (type=1, serial=0) carrying the static's
    // graphic in modelID — that's how the server knows it's a tree, not bare
    // ground (a model of 0 yields "you can't use an axe on that").
    u8 buf[19];
    const usize n = build::TargetCursorGround(
        buf, target_.Current().id, target_.Current().subtype,
        static_cast<u16>(x), static_cast<u16>(y), z, graphic);
    Send(buf, n, "0x6C TargetCursor (static)");
    LogInfo("[target] static 0x%04X (%d,%d,%d)\n", graphic, x, y,
            static_cast<int>(z));
    target_.OnReplied();
}

void Client::CancelTargetCursor(const char* reason) {
    if (!target_.Active()) return;
    u8 buf[19];
    const usize n = build::TargetCursorCancel(buf, target_.Current().id,
                                              target_.Current().subtype);
    Send(buf, n, "0x6C TargetCursor (cancel)");
    LogInfo("[target] cancelled (%s)\n", reason ? reason : "");
    target_.OnReplied();
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
        ActionOnManaChanged(player_.manaCur);
        player_.manaMax = static_cast<i32>(LoadBE16(data + 56));
        player_.gold = static_cast<i32>(LoadBE32(data + 58));
        ActionOnGoldChanged(player_.gold);
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
    // Sphere reports most action failures as plain system messages; let the
    // action layer turn the obvious ones into a result instead of a timeout.
    ActionOnSysMessage(text.c_str(), sourceSerial & 0x7FFFFFFFu, type);
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
        ActionOnBodyChange(body);
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
        if (!target_.Active()) {
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

// 0x34 subtype 5 -- "show me my skills". This is the packet the real client
// sends when the player opens the skills gump; Source-X answers it with the
// full 0x3A list (`PacketObjStatusReq::onReceive` -> addSkillWindow). Without
// asking, a client is simply never told what it can do: a fresh session
// reports zero skills, which is what the first M3 audit run found.
void Client::SendSkillsRequest() {
    if (!playerSerial_) return;
    u8 buf[10];
    Send(buf, build::GetPlayerStatus(buf, 5, playerSerial_),
         "0x34 SkillsRequest");
}

void Client::ActionRequestSkills() {
    LogInfo("[action] requesting the skill list\n");
    SendSkillsRequest();
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
