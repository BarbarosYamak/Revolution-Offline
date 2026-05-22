#pragma once

#include "bot/Blacklist.h"
#include "net/Huffman.h"
#include "net/PacketStream.h"
#include "net/Socket.h"
#include "uo/log.h"
#include "uo/types.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uo::tiledata { class TileDataLoader; }
namespace uo::map      { class Map; }
namespace uo::world    { class World; }
namespace uo::art      { class ArtLoader; }
namespace uo::texmap   { class TexmapLoader; }
namespace uo::render   { class Renderer; }

namespace uo {

struct ServerEntry {
    char name[33];   // ASCII, NUL-terminated
    u16 index;
    u8  percentFull;
    u8  timezone;
    u32 ip;          // host-order IPv4
    u16 port;        // host-order
};

struct CharEntry {
    char name[31];   // ASCII, NUL-terminated; empty -> unused slot
};

class Client {
public:
    struct Config {
        const char* loginHost;
        u16         loginPort;
        const char* username;
        const char* password;
        const char* version;          // e.g. "2.0.7"
        const char* logFile;          // JSON packet log path
        u32         plaintextSeed;
        u16         gamePortOverride; // 0 = trust shard's 0x8C value
        const char* gameHostOverride; // nullptr/"" = trust shard's value
        bool        sendSeed;         // false = skip the 4-byte seed
                                      // prefix on every TCP connect
        // Bot data files (M3) — loaded lazily on first `goto` command.
        const char* tiledataPath;
        const char* mapPath;
        const char* staidxPath;
        const char* staticsPath;
        const char* verdataPath;      // optional verdata.mul overlay (nullptr = none)
        bool        legacyMovePacket; // pre-T2A 3-byte 0x02 (UO Demo)
        bool        enableKeepalive;  // false for UO Demo (no 0x73 from client)
        bool        acceptDoors;      // A* routes through door tiles, opened at runtime
        // Renderer — optional MiniFB world window (camera follows the player).
        bool        enableRenderer;   // open a window and draw the world each tick
        const char* artIdxPath;       // artidx.mul (tile bitmap index)
        const char* artPath;          // art.mul (tile bitmaps)
        const char* texIdxPath;       // texidx.mul (land texture index)
        const char* texPath;          // texmaps.mul (land textures, sloped tiles)
        int         renderWidth;      // window framebuffer width  (<=0 -> 512)
        int         renderHeight;     // window framebuffer height (<=0 -> 384)
        int         renderScale;      // integer upscale factor    (<=0 -> 2)
    };

    explicit Client(const Config& cfg);
    ~Client();

    int Run();

private:
    enum class State : u8 {
        Disconnected,
        LoginHandshake,
        AwaitingServerList,
        AwaitingGameServer,
        ConnectingToGameServer,
        GameHandshake,
        AwaitingCharacterList,
        AwaitingLoginConfirm,
        InWorld,
        Failed,
    };

    // --- main pump --------------------------------------------------------
    bool ConnectAndSendSeed(const char* host, u16 port);
    bool PumpUntilDisconnected();
    void Dispatch(const u8* data, usize size);

    // --- outbound ---------------------------------------------------------
    bool Send(const u8* data, usize size, const char* note = nullptr);

    // --- inbound handlers (1:1 with dispatcher switch cases we care about)
    void OnServerList         (const u8* data, usize size);
    void OnLegacyCharList     (const u8* data, usize size);  // 0x81 (UO Demo)
    void OnConnectToGameServer(const u8* data, usize size);
    void OnCharacterList      (const u8* data, usize size);
    void OnLoginConfirm       (const u8* data, usize size);
    void OnLoginComplete      (const u8* data, usize size);
    void OnLoginDenied        (const u8* data, usize size);
    void OnClientVersionQuery (const u8* data, usize size);
    void OnFeatures           (const u8* data, usize size);
    void OnViewRange          (const u8* data, usize size);
    void OnPing               (const u8* data, usize size);
    void OnStats              (const u8* data, usize size);
    void OnMobileHp           (const u8* data, usize size);  // 0xA1
    void OnObjectInfo         (const u8* data, usize size);  // 0x1A
    void OnDeleteObject       (const u8* data, usize size);  // 0x1D
    void OnMobileMove         (const u8* data, usize size);  // 0x77
    void OnMobileIncoming     (const u8* data, usize size);  // 0x78
    void OnMobName            (const u8* data, usize size);  // 0x98
    void OnAsciiMessage       (const u8* data, usize size);
    void OnUnicodeMessage     (const u8* data, usize size);
    void OnUnknown            (const u8* data, usize size);

    // M3 — movement
    void OnDrawGamePlayer     (const u8* data, usize size);  // 0x20
    void OnMoveReject         (const u8* data, usize size);  // 0x21
    void OnMoveAck            (const u8* data, usize size);  // 0x22

    // --- console UI -------------------------------------------------------
    int  PromptServerSelection();
    int  PromptCharacterSelection();

    // --- stdin reader thread (in-world speech + bot commands) -------------
    void StartStdinThread();
    void StopStdinThread();
    void PumpStdinCommand();
    void HandleStdinLine(const char* line);
    void PrintNearbyMobiles();
    void FlushPendingMobilesList();
    const char* MobileName(u32 serial) const;
    u32 ResolveFollowSerialByName(const char* name) const;
    void RememberMobileName(u32 serial, const char* name);
    bool ParseSerial(const char* text, u32* out) const;
    bool ParseDistance(const char* text, u32* out) const;

    // --- M3 bot -----------------------------------------------------------
    bool EnsureWorldLoaded();
    void BotStartGoto(i32 tx, i32 ty, bool hasZ = false, i32 tz = 0);
    void BotStartFollow(u32 serial, u32 followDistance);
    void BotStopFollow(const char* reason);
    void BotFollowTick();
    bool ChooseFollowGoal(i32* gx, i32* gy, i8* gz) const;
    void BotTick();           // called from PumpUntilDisconnected
    void RenderTick();        // draws the world window (no-op unless enabled)
    void BotPumpMoves();      // sends moves while a flight slot + cadence allow
    void BotPredictStep(u8 dir);  // advance predicted pos/z for a confirmed step
    bool BotReplanToGoal();   // re-run A* from current pose; false = gave up
    // Threat hook: called when we detect we're under attack mid-travel.
    // For now it just halts travel safely; engage/flee/recall are TODO.
    void BotInterruptForThreat(const char* reason);
    i64  NowMs() const;
    u8   NextSeq();

    Config cfg_;
    State  state_;

    net::Socket       sock_;
    net::PacketStream stream_;
    net::Huffman      huff_;
    PacketLogger      logger_;

    // Server->client game stream is Huffman-compressed once the server
    // processes our 0x91 GameLogin. Off during the (plaintext) login phase.
    bool              decompress_;
    std::vector<u8>   rxScratch_;  // decompressed bytes for one recv

    ServerEntry servers_[32];
    int         serverCount_;
    int         selectedServer_;

    CharEntry charSlots_[5];
    int       charCount_;     // total slots reported (incl. empty)
    int       selectedChar_;

    u32   gameSeed_;
    u32   gameServerIp_;      // host-order
    u16   gameServerPort_;    // host-order

    u32   playerSerial_;
    i32   lastHp_;              // last known own hit points (-1 = unknown)

    // M3 player state — populated from 0x1B/0x20/0x22.
    i32   playerX_;
    i32   playerY_;
    i8    playerZ_;
    u8    playerFacing_;        // 0..7 (low 3 bits of dir byte)
    bool  playerRunning_;
    u8    moveSeq_;             // next sequence to send (0 = resync, then 1..255 wrap->1)

    // Pipelined movement, mirroring the real client's CMovementManager
    // pending ring: send several moves ahead and never block on a per-step
    // ack. Each entry is a move sent but not yet acked (FIFO). Position is
    // predicted on send and reconciled on 0x22 ack / 0x21 reject.
    struct PendingMove { u8 seq; u8 dir; bool wasStep; i64 sentMs; };
    std::deque<PendingMove> pendingMoves_;
    bool  botRun_;              // send the 0x80 run bit and use run cadence

    // M3 bot data files + walker
    std::unique_ptr<uo::tiledata::TileDataLoader> tileData_;
    std::unique_ptr<uo::map::Map>                 worldMap_;
    std::unique_ptr<uo::world::World>             world_;
    bool worldLoaded_;

    // Renderer — lazily initialized on the first in-world tick when
    // cfg_.enableRenderer. Closing the window stops drawing but leaves the
    // bot running (cfg_.enableRenderer is cleared).
    std::unique_ptr<uo::art::ArtLoader>      art_;
    std::unique_ptr<uo::texmap::TexmapLoader> texmaps_;
    std::unique_ptr<uo::render::Renderer>    renderer_;
    bool renderInit_;
    bool renderWindowOpen_;
    std::deque<u8> botPath_;    // directions still to execute
    i32 botGoalX_;
    i32 botGoalY_;
    i32 botGoalZ_;              // target floor z (valid only if botHasGoalZ_)
    bool botHasGoalZ_;         // goto specified an explicit z to pin
    bool botActive_;

    // Obstacle avoidance: learned/dynamic blocks A* must route around, the
    // earliest tick we may send the next step (human reaction delay after a
    // bump), and a per-trip replan budget so an unreachable goal can't loop.
    bot::Blacklist blacklist_;
    u32 botReplanCount_;
    i64 botResumeAtMs_;
    std::mt19937 rng_;

    // Recent doors seen in 0x1A object packets (dynamic server objects, often
    // sent well before we reach them). On a bump we scan this before deciding
    // a tile is a wall: if a door sits there we double-click it open + retry.
    // All world items seen via 0x1A (lamp posts, doors, decor, ...), keyed by
    // serial — fed to the renderer so dynamic server objects are drawn too.
    struct ItemObj { u16 itemId; i32 x; i32 y; i8 z; };
    std::unordered_map<u32, ItemObj> items_;

    struct DoorObj { u32 serial; u16 itemId; i32 x; i32 y; i8 z; };
    std::deque<DoorObj> doorCache_;
    const DoorObj* FindDoorAt(i32 x, i32 y, i8 z, i32 radius = 0) const;
    i32 doorCellX_;
    i32 doorCellY_;
    i8  doorCellZ_;           // surface z of the blocked cell (to pick the door on our floor)
    u32 doorAttempts_;
    bool awaitingDoorOpen_;   // sent an open command, waiting to confirm it swung

    // Recent mobiles (players/NPCs) from 0x77/0x78. A reject at a tile that
    // holds a mobile is a moving obstacle (or a stamina-gated shove), never a
    // wall — such tiles are never blacklisted.
    struct MobileObj { u32 serial; i32 x; i32 y; i8 z; u8 dir; i64 seenMs; };
    std::deque<MobileObj> mobileCache_;
    std::unordered_map<u32, std::string> mobileNames_;
    const MobileObj* FindMobileAt(i32 x, i32 y, i8 z) const;
    const MobileObj* FindMobileBySerial(u32 serial) const;
    void UpdateMobile(u32 serial, i32 x, i32 y, i8 z, u8 dir);
    bool followActive_;
    u32  followSerial_;
    u32  followDistance_;
    i64  followLastReplanMs_;
    i64  followLastProbeMs_;
    bool mobilesListPending_;
    i64  mobilesListDeadlineMs_;
    std::vector<u32> mobilesListSerials_;
    std::unordered_set<u32> mobilesListAwaiting_;
    i64 lastFatigueMs_;       // last "too fatigued to move" message time
    u32 stuckWaits_;          // consecutive wait-retries at the current bump cell

    i64 lastMoveSentMs_;        // for throttle + watchdog
    u32 walkThrottleMs_;        // min gap between move sends (default 500)
    u32 runThrottleMs_;         // min gap if player is running (default 250)
    u32 ackWatchdogMs_;         // clear stuck awaiting flag after N ms
    u32 movesSinceClick_;       // periodic 0x09 self-click anti-bot mask
    i64 lastActivityMs_;        // any TCP send/recv — for 0x73 keepalive
    u32 keepaliveIntervalMs_;   // default 60s, mirrors original client

    // Console chatter gate. The JSON packet log always records everything;
    // the window shows only meaningful events unless this is toggled on
    // (stdin: `verbose on|off`). Keeps per-packet noise (0x11/0x20/0x21/0x22
    // and unhandled ids) out of the window by default.
    bool verboseConsole_;

    std::thread             stdin_thread_;
    std::mutex              stdin_mtx_;
    std::queue<std::string> stdin_lines_;
    std::atomic<bool>       stop_stdin_;
};

}
