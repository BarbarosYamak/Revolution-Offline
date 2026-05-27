#pragma once

#include "net/Huffman.h"
#include "net/PacketStream.h"
#include "net/Socket.h"
#include "navigation/PathPlanner.h"
#include "navigation/NavigationState.h"
#include "uo/log.h"
#include "uo/types.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace uo::tiledata { class TileDataLoader; }
namespace uo::map      { class Map; }
namespace uo::world    { class World; }
namespace uo::art      { class ArtLoader; }
namespace uo::texmap   { class TexmapLoader; }
namespace uo::anim     { class AnimLoader; }
namespace uo::animdata { class AnimDataLoader; }
namespace uo::animinfo { class AnimInfoLoader; }
namespace uo::hues     { class HuesLoader; }
namespace uo::render   { class Renderer; class TextRenderer; class Minimap; class RadarColors; }

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
        const char* animIdxPath;      // anim.idx (body animation index)
        const char* animPath;         // anim.mul (body animations, still frames)
        const char* animDataPath;     // animdata.mul (animated static/dynamic art)
        const char* animInfoPath;     // animinfo.mul (mobile walk/run timing)
        const char* huesPath;         // hues.mul (object/mobile hue ramps)
        const char* radarcolPath;     // radarcol.mul (per-tile minimap colors)
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
    void OnMobileMana         (const u8* data, usize size);  // 0xA2
    void OnMobileStamina      (const u8* data, usize size);  // 0xA3
    void OnMobileAttributes   (const u8* data, usize size);  // 0x2D
    void OnSkills             (const u8* data, usize size);  // 0x3A
    void OnObjectInfo         (const u8* data, usize size);  // 0x1A
    void OnDeleteObject       (const u8* data, usize size);  // 0x1D
    void OnDrawContainer      (const u8* data, usize size);  // 0x24
    void OnAddItemToContainer (const u8* data, usize size);  // 0x25
    void OnContainerContents  (const u8* data, usize size);  // 0x3C
    void OnOverallLightLevel  (const u8* data, usize size);  // 0x4F
    void OnPersonalLightLevel (const u8* data, usize size);  // 0x4E
    void OnMobileMove         (const u8* data, usize size);  // 0x77
    void OnMobileIncoming     (const u8* data, usize size);  // 0x78
    void OnEquipItem          (const u8* data, usize size);  // 0x2E
    void OnCharacterAnimation (const u8* data, usize size);  // 0x6E
    void OnWarMode            (const u8* data, usize size);  // 0x72
    void OnDeathAnimation     (const u8* data, usize size);  // 0xAF
    void OnMobName            (const u8* data, usize size);  // 0x98
    void OnTargetCursor       (const u8* data, usize size);  // 0x6C
    void OnAsciiMessage       (const u8* data, usize size);
    void OnUnicodeMessage     (const u8* data, usize size);
    void OnUnknown            (const u8* data, usize size);
    void RememberJournalMessage(u32 sourceSerial, u16 sourceBody, u8 type,
                                u16 hue, u16 font, const char* speaker,
                                const char* text);

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
    void BotNoteFatigueMessage();
    void BotAbortPath(const char* reason);
    void BotResetMovement();
    void BotFollowTick();
    bool ChooseFollowGoal(i32* gx, i32* gy, i8* gz) const;
    void BotTick();           // called from PumpUntilDisconnected
    void RenderTick();        // draws the world window (no-op unless enabled)
    void HandleRenderChatInput();
    void HandleManualWalk();  // arrow-key steering from the render window
    void HandleWorldClick();  // right-click in the render window -> goto that cell
    void HandleItemClicks();  // left-click (look) + double-click (use/open) on world objects
    // Target cursor (0x6C): the server arms a target after a spell/item use; the
    // next world click (or `target` console command) resolves it. See OnTargetCursor.
    void TargetRespondObject(u32 serial);  // answer with a clicked mobile/item
    void TargetRespondGround(i32 x, i32 y, bool hasZ, i8 z);  // answer with a tile
    void CancelTargetCursor(const char* reason);             // Esc / right-click
    bool ResolveObjectTarget(u32 serial, i32* x, i32* y, i8* z, u16* model) const;
    void DrawStatusBars();
    void DrawContainers();    // simple text HUD listing open containers' items
    void DrawSystemLog();
    void DrawChatInput();
    void DrawOverheadText();
    void DrawCursorOverlay(); // UO directional walk cursor under the mouse
    void BotPumpMoves();      // sends moves while a flight slot + cadence allow
    void BotPredictStep(u8 dir);  // advance predicted pos/z for a confirmed step
    void BotPollPathPlanner();
    u32  BotMoveGapMs() const;
    bool BotReplanToGoal();   // queue a full A* replan from current pose
    bool BotLookaheadPatchPath(); // short local reroute around visible blockers
    bool BotIsRuntimeBlocked(i32 x, i32 y, i8 z) const;
    bool BotIsMobileBlocking(i32 x, i32 y, i8 z) const;
    bool BotIsDynamicItemBlocking(i32 x, i32 y, i8 z) const;
    bool BotStepNeedsDoorOpen(i8 fromZ, i32 toX, i32 toY, i8 toZ) const;
    bool BotIsRejectedEdge(i32 fromX, i32 fromY, i8 fromZ,
                           i32 toX, i32 toY, i8 toZ) const;
    bool BotDoorRetryWasTried(i32 fromX, i32 fromY, i8 fromZ,
                              i32 toX, i32 toY, i8 toZ) const;
    void BotRememberDoorRetry(i32 fromX, i32 fromY, i8 fromZ,
                              i32 toX, i32 toY, i8 toZ);
    static bool BotRuntimeBlockedForPath(i32 x, i32 y, i8 z, void* user);
    static bool BotRuntimeBlockedStepForPath(i32 fromX, i32 fromY, i8 fromZ,
                                             i32 toX, i32 toY, i8 toZ,
                                             void* user);
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

    struct PlayerSkill {
        u16 id;
        u16 valueTenths;
        u16 baseTenths;
        u16 capTenths;
        u8 lock;
        bool hasCap;
    };
    struct PlayerObj {
        u32 serial = 0;
        u16 body = 0;
        std::string name;
        i32 x = 0, y = 0;
        i8 z = 0;
        u8 facing = 0;
        bool running = false;
        i32 hpCur = -1, hpMax = -1;
        i32 manaCur = -1, manaMax = -1;
        i32 stamCur = -1, stamMax = -1;
        i32 strength = -1, dexterity = -1, intelligence = -1;
        i32 gold = -1, armor = -1, weight = -1, maxWeight = -1;
        u8 nameChangeFlag = 0, statusFlag = 0, sexRace = 0, race = 0;
        i32 statsCap = -1;
        u8 followers = 0, followersMax = 0;
        std::unordered_map<u16, PlayerSkill> skills;
    };
    PlayerObj player_;
    u32   playerSerial_;

    // M3 player state — populated from 0x1B/0x20/0x22.
    i32   playerX_;
    i32   playerY_;
    i8    playerZ_;
    u8    playerFacing_;        // 0..7 (low 3 bits of dir byte)
    bool  playerRunning_;
    bool  playerWarMode_ = false;
    // World light (0x4F overall, 0x4E personal). 0 = day/bright, 0x1F = black;
    // render darkness = clamp(overall - personal, 0..31). See Renderer::ApplyDarkness.
    u8    overallLightLevel_ = 0;
    u8    personalLightLevel_ = 0;
    bool  alwaysDay_ = true;    // force full daylight, ignore 0x4E/0x4F levels
                                // (toggle in-world with `day [on|off]`)
    i64   lastStepMs_ = 0;      // when we last actually changed cell (anim: walk vs idle)
    i32   prevPlayerX_ = 0; i32 prevPlayerY_ = 0;  // cell before the current step (slide interp)
    i64   playerMoveAnimTickMs_ = 0;
    u32   playerMoveAnimCounter_ = 0;
    struct IdleAnimState {
        i64 nextProbeMs = 0;
        i64 lastTickMs = 0;
        u8 action = 0;
        u16 maxFrames = 0;
        u16 delayPerFrame = 0;
        u16 maxDuration = 0;
        u16 currentFrame = 0;
        u16 currentDuration = 0;
        u16 pad = 0;
        u16 repeatCount = 0;
        u16 renderedFrame = 0;
        u8 renderedAction = 0;
        bool active = false;
        bool reverse = false;
        bool hasRenderedFrame = false;
    };
    struct ServerAnimState {
        i64 lastTickMs = 0;
        u8 action = 0;
        u16 maxFrames = 0;
        u16 delayPerFrame = 0;
        u16 maxDuration = 0;
        u16 currentFrame = 0;
        u16 currentDuration = 0;
        u16 pad = 0;
        u16 renderedFrame = 0;
        bool active = false;
        bool reverse = false;
        bool bounce = false;
        bool hasRenderedFrame = false;
    };
    IdleAnimState playerIdleAnim_;
    ServerAnimState playerServerAnim_;
    // Navigation owns predicted movement, bot route/follow state, and
    // learned transient blockers. Server packets still own authoritative
    // player position above.
    navigation::NavigationState nav_;

    // M3 bot data files + walker
    std::unique_ptr<uo::tiledata::TileDataLoader> tileData_;
    std::unique_ptr<uo::map::Map>                 worldMap_;
    std::unique_ptr<uo::world::World>             world_;
    std::unique_ptr<uo::navigation::PathPlanner>  pathPlanner_;
    bool worldLoaded_;

    // Renderer — lazily initialized on the first in-world tick when
    // cfg_.enableRenderer. Closing the window stops drawing but leaves the
    // bot running (cfg_.enableRenderer is cleared).
    std::unique_ptr<uo::art::ArtLoader>      art_;
    std::unique_ptr<uo::texmap::TexmapLoader> texmaps_;
    std::unique_ptr<uo::anim::AnimLoader>    anim_;
    std::unique_ptr<uo::animdata::AnimDataLoader> animData_;
    std::unique_ptr<uo::animinfo::AnimInfoLoader> animInfo_;
    std::unique_ptr<uo::hues::HuesLoader>    hues_;
    std::unique_ptr<uo::render::Renderer>    renderer_;
    std::unique_ptr<uo::render::TextRenderer> text_;
    std::unique_ptr<uo::render::Minimap>     minimap_;
    std::unique_ptr<uo::render::RadarColors> radarColors_;
    bool renderInit_;
    bool renderWindowOpen_;
    bool minimapVisible_;       // overlay minimap panel (toggle with 'M')
    bool minimapKeyDown_;       // 'M' edge-detect so a held key toggles once
    bool spaceKeyDown_;         // SPACE edge-detect (OpenDoor on press, once)
    bool tabKeyDown_ = false;   // TAB edge-detect (war/peace toggle)
    bool chatInputActive_;
    std::string chatInputLine_;

    // Mouse interaction (ported from the client's WorldGump click state machine).
    // hoverSerial_ is the object currently under the cursor — drawn with the
    // highlight hue so it "lights up", like g_ContextActionTargetSerial @hue 53.
    // A left-click is DEFERRED: the single-click "look" only fires once the
    // double-click window (mfb_double_click_ms) elapses with no second click, so
    // a double-click sends only the 0x06 use/open (matching WorldGump_OnHoverTick
    // @0x47A910 / WorldGump_OnLButtonUp @0x47A310 in client_2.0.7.exe).
    u32  hoverSerial_ = 0;
    // Target cursor armed by an inbound 0x6C (Packet_HandleTargetCursor @0x41E960).
    // While active, the next world click resolves into a 0x6C response; Esc/right-
    // click cancels. id/subtype are echoed back; type is the server's requested
    // mode (0=object, 1=ground) but the response type follows what we actually hit.
    bool targetCursorActive_ = false;
    u8   targetCursorType_ = 0;
    u32  targetCursorId_ = 0;
    u8   targetCursorSubtype_ = 0;
    bool escKeyDown_ = false;         // VK_ESCAPE edge-detect (cancel target once)
    bool pendingLClick_ = false;     // a single-click is waiting out the dbl-click window
    u32  pendingLClickSerial_ = 0;   // object picked on that press (0 = empty space)
    i64  pendingLClickMs_ = 0;       // press time, vs mfb_double_click_ms()

    // Single-click "look" name labels floated over world items. The 2.0.7 client
    // shows an item's name LOCALLY from tiledata on single-click — no packet —
    // as a short-lived overhead bark (Entity_ShowLocalLookMessage @0x4C95B0;
    // WorldGump_HandleTargetOrLookClick returns before the 0x09 for items, which
    // is mobile-only). We mirror that: an item click adds a label here; a mobile
    // click still sends 0x09. Drawn by DrawOverheadText, anchored to the item's
    // live position so it tracks if the item moves, expiring after kItemLabelMs.
    struct ItemLabel { u32 serial; i64 expireMs; std::string text; };
    std::vector<ItemLabel> itemLabels_;
    // Build an item's display name from tiledata, mirroring
    // Tiledata_FormatItemName @0x4C4870: article prefix from flags&0xC000
    // (a/an/the) plus the name with its %singular/plural% markup resolved.
    std::string FormatItemName(u16 graphic) const;
    void ShowItemLabel(u32 serial);
    u16  playerBody_;           // local player body graphic for the renderer
    struct EquipObj { u8 layer; u16 graphic; u16 hue; };
    std::vector<EquipObj> playerEquip_;  // own worn items (layer, graphic, hue)
    u16  playerHue_ = 0;
    u8   warModeArg1_ = 4;      // cached 0x72 trailing args, as in client 2.0.7
    u8   warModeArg2_ = 0;
    u8   warModeArg3_ = 0;
    i64  lastManualMoveMs_;     // arrow-key walk throttle (render window)

    // All world items seen via 0x1A (lamp posts, doors, decor, ...), keyed by
    // serial — fed to the renderer so dynamic server objects are drawn too.
    struct ItemObj { u16 itemId; i32 x; i32 y; i8 z; u8 gfxOffset; u16 hue; };
    static constexpr usize kMaxItemCache = 32768;
    std::unordered_map<u32, ItemObj> items_;
    std::deque<u32> itemOrder_;

    // Open containers (bank, backpack, chests, corpses) registered by 0x24, with
    // contents listed by 0x3C and patched by 0x25. We don't draw the real gump
    // art yet — DrawContainers() lists each contained item as text. gumpId is the
    // 0x24 "model type" (500/501 = bank, 10/48 = paperdoll, 0xFFFF = close).
    // Keyed by the raw container serial, matching what 0x3C/0x25 carry. Mirrors
    // Packet_HandleDrawContainer / Packet_HandleContainerItems in
    // client_2.0.7.exe (0x417f70 / 0x418990), minus the gump rendering.
    struct ContainerItem { u32 serial; u16 graphic; u8 gfxOffset; u16 amount; u16 x; u16 y; u16 hue; };
    struct OpenContainer { u32 serial; u16 gumpId; };
    std::vector<OpenContainer> openContainers_;
    std::unordered_map<u32, std::vector<ContainerItem>> containerItems_;

    // Recent mobiles (players/NPCs) from 0x77/0x78. A reject at a tile that
    // holds a mobile is a moving obstacle (or a stamina-gated shove), never a
    // wall — such tiles are never blacklisted.
    struct MobileObj {
        u32 serial; i32 x; i32 y; i8 z; u8 dir; u16 body; u16 hue; i64 seenMs;
        i64 movedMs = 0;     // when (x,y) last changed (anim: walk vs idle)
        i32 prevX = 0; i32 prevY = 0;  // cell before the current step (slide interp)
        bool running = false; // high bit of the server direction byte
        bool warMode = false; // 0x77/0x78 status flag bit 0x40
        i64 deadRemoveMs = 0;  // 0xAF keeps the mobile until death anim ends
        i64 moveAnimTickMs = 0;
        u32 moveAnimCounter = 0;
        IdleAnimState idleAnim;
        ServerAnimState serverAnim;
        // Worn items as (layer, item graphic, hue). Preserved across 0x77
        // position updates; rebuilt on 0x78, patched by 0x2E.
        std::vector<EquipObj> equip;
    };
    std::deque<MobileObj> mobileCache_;
    std::unordered_map<u32, std::string> mobileNames_;
    const MobileObj* FindMobileAt(i32 x, i32 y, i8 z) const;
    const MobileObj* FindMobileBySerial(u32 serial) const;
    void UpdateMobile(u32 serial, i32 x, i32 y, i8 z, u8 dir, u16 body,
                      u16 hue = 0, bool hasHue = false,
                      u8 statusFlags = 0, bool hasStatusFlags = false);
    void SetMobileEquip(u32 serial, std::vector<EquipObj> equip);
    void SetMobileEquipLayer(u32 serial, u8 layer, u16 graphic, u16 hue);
    bool mobilesListPending_;
    i64  mobilesListDeadlineMs_;
    std::vector<u32> mobilesListSerials_;
    std::unordered_set<u32> mobilesListAwaiting_;
    std::unordered_map<u32, i64> overheadNameProbeMs_;
    i64 lastStatusProbeMs_;     // 0x34 status request while HUD has no stats
    i64 lastActivityMs_;        // any TCP send/recv — for 0x73 keepalive

    // Console chatter gate. The JSON packet log always records everything;
    // the window shows only meaningful events unless this is toggled on
    // (stdin: `verbose on|off`). Keeps per-packet noise (0x11/0x20/0x21/0x22
    // and unhandled ids) out of the window by default.
    bool verboseConsole_;

    enum class JournalOwnerKind : u8 { System, Player, Mobile, Item, Unknown };
    struct JournalEntry {
        i64 timeMs;
        u32 sourceSerial;
        u16 sourceBody;
        u8 type;
        u16 hue;
        u16 font;
        JournalOwnerKind ownerKind;
        bool hasPosition;
        i32 x, y;
        i8 z;
        std::string speaker;
        std::string text;
    };
    static constexpr usize kMaxJournalEntries = 1024;
    std::deque<JournalEntry> journal_;

    std::thread             stdin_thread_;
    std::mutex              stdin_mtx_;
    std::queue<std::string> stdin_lines_;
    std::atomic<bool>       stop_stdin_;
};

}
