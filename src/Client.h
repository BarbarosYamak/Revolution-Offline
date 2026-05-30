#pragma once

#include "net/Huffman.h"
#include "net/PacketStream.h"
#include "net/Socket.h"
#include "navigation/PathPlanner.h"
#include "navigation/NavigationState.h"
#include "js/JsEngine.h"
#include "uo/log.h"
#include "uo/types.h"

#include <atomic>
#include <deque>
#include <functional>
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

namespace js { struct ClientBindings; }  // friend: live JS bindings (ClientBindings.cpp)

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
    // Live JS Player getters read private player state directly (no snapshot).
    // Defined in src/js/ClientBindings.cpp; keeps quickjs.h out of this header.
    friend struct js::ClientBindings;
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
    void OnVendorShopData     (const u8* data, usize size);  // 0x74 buy-window prices
    void OnVendorOfferAccept  (const u8* data, usize size);  // 0x3B vendor transaction closed
    void OnOpenPaperdoll      (const u8* data, usize size);  // 0x88 paperdoll (carries title)
    void OnOverallLightLevel  (const u8* data, usize size);  // 0x4F
    void OnPersonalLightLevel (const u8* data, usize size);  // 0x4E
    void OnMobileMove         (const u8* data, usize size);  // 0x77
    void OnMobileIncoming     (const u8* data, usize size);  // 0x78
    void OnSwing              (const u8* data, usize size);  // 0x2F fight/swing
    void OnEquipItem          (const u8* data, usize size);  // 0x2E
    void OnCharacterAnimation (const u8* data, usize size);  // 0x6E
    void OnWarMode            (const u8* data, usize size);  // 0x72
    void OnResurrectionMenu   (const u8* data, usize size);  // 0x2C
    void OnOpenDialog         (const u8* data, usize size);  // 0x7C menu/dialog
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
    void SayAscii(const char* text);   // send 0x03 ascii speech (console + JS Player.say)
    void SendAttack(u32 serial);       // 0x05 attack request (JS Player.attack)
    void SendDoubleClick(u32 serial);  // 0x06 raw double-click by serial (JS Player.doubleClick);
                                       // double-clicking an NPC opens its paperdoll (-> 0x88 title)
    void SendTakeToBackpack(u32 serial, u16 qty); // 0x07+0x08 lift item from any open container -> backpack (JS Player.take)
    void SendStatusRequest(u32 serial); // 0x34 status query (JS Player.requestStatus);
                                        // server then pushes 0xA1 HP updates for it
    void SetWarMode(bool on);          // 0x72 war-mode toggle (JS Player.warMode)
    void SendResurrectChoice(u8 choice); // 0x2C resurrect menu reply (JS Player.resurrect)
    bool PlayerIsGhost() const {       // dead == ghost body (Mobile_IsGhostForm @0x4c6930)
        return playerBody_ == 0x192 || playerBody_ == 0x193;
    }
    // 0x7C Open Dialog/Menu (e.g. the healer resurrect prompt). The whole dialog
    // is parsed into activeDialog_ and forwarded to JS as the `dialog` event.
    struct DialogOption {
        u16 model = 0;       // option's display model (0 for a text-only list)
        u16 hue = 0;
        std::string text;    // option label as sent (e.g. "YES - You choose...")
    };
    struct ActiveDialog {
        bool active = false;
        u32  id = 0;         // dialogSerial (echoed back in 0x7D)
        u16  menuId = 0;
        std::string question;
        std::vector<DialogOption> options;   // 1-based on the wire; [0] = option 1
    };
    const ActiveDialog& Dialog() const { return activeDialog_; }
    void SendDialogResponse(u32 id, u16 menuId, u16 index, u16 model, u16 hue); // 0x7D
    bool AnswerDialog(u16 index);      // answer activeDialog_ by 1-based index (0 = cancel)
    void OpenBackpack();               // 0x06 double-click the worn backpack
    void TryOpenBackpackOnLogin();     // open it once, as soon as its serial is known
    void PrintNearbyMobiles();
    void FlushPendingMobilesList();
    const char* MobileName(u32 serial) const;
    u32 ResolveFollowSerialByName(const char* name) const;
    void RememberMobileName(u32 serial, const char* name);
    bool ParseSerial(const char* text, u32* out) const;
    bool ParseDistance(const char* text, u32* out) const;

    // --- M3 bot -----------------------------------------------------------
    bool EnsureWorldLoaded();
    // Show a chopped/depleted tree at (x,y,z) as a stump in the world view for
    // `ttlMs` (<=0 uses the default). In-memory only (uo::map::StumpOverlay);
    // it reverts on its own once the TTL lapses. `treeGraphic` is the live tree
    // static id to replace. Exposed to JS as World.markStump.
    void MarkStump(i32 x, i32 y, i8 z, u16 treeGraphic, i64 ttlMs = 0);
    void BotStartGoto(i32 tx, i32 ty, bool hasZ = false, i32 tz = 0,
                      bool terrainBias = true);
    void BotStartFollow(u32 serial, u32 followDistance);
    void BotStopFollow(const char* reason);
    void BotNoteFatigueMessage();
    void BotAbortPath(const char* reason);
    void BotResetMovement();
    // Scripted-trip completion bridge: a JS `await goto()` registers a one-shot
    // callback that the trip end fires exactly once (arrived -> success, any
    // abort -> failure with reason). Settled by uo::js::ClientBindings.
    void BotSetDoneCb(std::function<void(bool success, const char* reason)> cb);
    void BotClearDoneCb();
    void BotSignalDone(bool success, const char* reason);
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
    void TargetRespondStatic(i32 x, i32 y, i8 z, u16 graphic);  // answer with a static (tree)
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
    ActiveDialog activeDialog_;   // latest 0x7C menu, until answered (0x7D)
    bool openBackpackPending_ = false;   // open the backpack once after login

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
    std::function<void(bool, const char*)> botDoneCb_;  // one-shot scripted-trip completion

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
    struct EquipObj { u8 layer; u16 graphic; u16 hue; u32 serial = 0; };
    std::vector<EquipObj> playerEquip_;  // own worn items (layer, graphic, hue)
    u16  playerHue_ = 0;
    // Disarm memory: serial last taken off each hand layer so `arm` can put it
    // back. Indexed by layer (1 = one-handed/weapon, 2 = two-handed/shield);
    // index 0 unused. Mirrors g_ArmDisarm_SavedSerial @0xCD5A68.
    u32  armSavedSerial_[3] = {0, 0, 0};
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

    // Corpses (item graphic 0x2006). Unlike the official client where CCorpse is
    // a CMobile subclass (CCorpse_OnRender @0x406560), we keep the corpse-only
    // anim state in a side map keyed by the corpse item serial; the live position
    // still lives in items_. The 0x1A object carries body in the amount field and
    // facing in the direction byte (CObjectManager_HandleMove @0x4C7D6C copies
    // them to stackCount/facingFlags), so the corpse renders the dead body's
    // death-pose frame from anim.mul. deathAction/equip/deadMobile are filled by
    // the 0xAF that preceded the corpse (DeathAnimationQueue_Add @0x4C3B50).
    struct CorpseObj {
        u16 body = 0;        // dead body graphic (0x1A amount / stackCount)
        u8  dir = 0;         // facing it died at (0x1A direction byte)
        u16 hue = 0;
        u8  deathAction = 0; // death anim group that played (0 = not set by 0xAF)
        u32 deadMobile = 0;  // dying mobile serial — corpse is hidden until its
                             // death animation finishes (queue suppression)
        std::vector<EquipObj> equip;  // worn gear snapshot from the dead mobile
    };
    std::unordered_map<u32, CorpseObj> corpses_;
    // Death anim group for a body, mirroring Mobile_PlayDeathAnimation @0x4C65C0.
    static u8 DeathActionForBody(u16 body, bool normalDeath);

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

    // Vendor buy window: the server sends, in order, a 0x3C (stock container
    // contents) then a 0x74 SHOP_DATA (prices + names, SAME order), optionally a
    // second pair for the offered/resale container, then a 0x24 gump 0x30 whose
    // "container" serial is the VENDOR MOBILE. We join 0x3C+0x74 by index into
    // VendorItem rows (carrying the shop-container layer to send back in 0x3B)
    // and accumulate them in pendingVendor_ until the gump 0x30 finalizes the
    // session and emits the `vendor_buy` event. See shopkeeper.c / human.m.
    struct VendorItem { u32 serial; u16 graphic; u16 amount; u32 price; u8 layer; std::string name; };
    std::vector<VendorItem> pendingVendor_;
    int pendingVendorGroups_ = 0;        // 0x74 groups seen this session (0 -> layer 0x1A, else 0x1B)
    // One buy request row (JS Vendor.buy): how many of `serial` to buy from `layer`.
    struct VendorBuyReq { u32 serial; u16 qty; u8 layer; };
    void SendVendorBuy(u32 vendor, const std::vector<VendorBuyReq>& items);  // 0x3B

    // Recent mobiles (players/NPCs) from 0x77/0x78. A reject at a tile that
    // holds a mobile is a moving obstacle (or a stamina-gated shove), never a
    // wall — such tiles are never blacklisted.
    struct MobileObj {
        u32 serial; i32 x; i32 y; i8 z; u8 dir; u16 body; u16 hue; i64 seenMs;
        i64 movedMs = 0;     // when (x,y) last changed (anim: walk vs idle)
        i32 prevX = 0; i32 prevY = 0;  // cell before the current step (slide interp)
        bool running = false; // high bit of the server direction byte
        bool warMode = false; // 0x77/0x78 status flag bit 0x40
        u8 noto = 0;          // 0x78 notoriety: 1 blue,2 green,3/4 gray,5 orange,6 red,7 yellow
        i32 hpCur = -1, hpMax = -1;  // 0xA1/0x2D health (often a 0..max ratio for foreign mobs)
        i64 lastAnimMs = 0;   // 0x6E: when it last played an action animation (0 = never)
        u8 lastAnimAction = 0;// 0x6E action code of that animation (swing/cast/get-hit/...)
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
    // Paperdoll title ("<name> the <job>", e.g. "Aldo the healer") learned from
    // an 0x88 OPEN_PAPERDOLL after we double-click a mobile. The job suffix is the
    // only client-visible way to tell a healer from a tavernkeeper, so the restock
    // bot uses it to pick the right vendor. nullptr until the paperdoll arrives.
    std::unordered_map<u32, std::string> paperdollTitles_;
    const char* PaperdollTitle(u32 serial) const;
    const MobileObj* FindMobileAt(i32 x, i32 y, i8 z) const;
    const MobileObj* FindMobileBySerial(u32 serial) const;

    // Object update/cull radius (Chebyshev tiles), set by 0xC8; default 18.
    // Mirrors g_ObjectUpdateRange in client_2.0.7 (Packet_HandleClientViewRange).
    int viewRange_ = 18;
    // Drop tracked mobiles / open world-containers that have left viewRange_ of
    // the player, like CObjectManager_UpdateMovement @0x4c8b00 (out-of-range
    // entities -> _Destroy_RefCounter). The server re-sends them (0x78/0x1A)
    // when they re-enter range, so the local cache stays bounded to "what's
    // around us" instead of growing stale. Self-gated on player movement via
    // lastPurgeX_/Y_; called once per pump iteration.
    void PurgeOutOfRange();
    i32 lastPurgeX_ = 0x7FFFFFFF, lastPurgeY_ = 0x7FFFFFFF;
    void UpdateMobile(u32 serial, i32 x, i32 y, i8 z, u8 dir, u16 body,
                      u16 hue = 0, bool hasHue = false,
                      u8 statusFlags = 0, bool hasStatusFlags = false,
                      int notoriety = -1);
    void SetMobileEquip(u32 serial, std::vector<EquipObj> equip);
    void SetMobileEquipLayer(u32 mobileSerial, u32 itemSerial, u8 layer,
                             u16 graphic, u16 hue);
    // Serial of our worn item at a layer (0 if nothing equipped there).
    u32 PlayerEquipSerialAt(u8 layer) const;
    // Lowercased tiledata static name for an item graphic ("" if no tiledata).
    std::string ItemNameLower(u16 graphic) const;
    // Equip layer for an item graphic, read from tiledata (StaticTile.quality,
    // as the client does in Gump_HandleMouseOver @0x45486d). 0 = unknown.
    u8 ItemEquipLayer(u16 graphic) const;
    // True if an item graphic matches the query (exact graphic when hasType,
    // else a case-insensitive tiledata-name substring).
    bool ItemMatches(u16 graphic, bool hasType, u16 type,
                     const std::string& lowerName) const;
    // Zones for FindItem (bitmask).
    enum SearchZone : unsigned { kZoneBackpack = 1, kZoneEquip = 2, kZoneWorld = 4 };
    // Resolve an item by type/name across the requested zones (backpack first,
    // then worn equipment, then the nearest world item). Optionally reports the
    // matched graphic, worn layer (0 unless from kZoneEquip) and a where-label.
    u32 FindItem(bool hasType, u16 type, const std::string& lowerName,
                 unsigned zones, u16* graphicOut, u8* layerOut,
                 const char** whereOut) const;
    // Parse a leading item token: quoted 'name'/"name", a 0xserial, a numeric
    // graphic type, or a single bareword name. Sets exactly one of *serialOut,
    // (*hasTypeOut + *typeOut), or *lowerNameOut, and points *restOut past the
    // token. Returns false on a malformed token.
    bool ParseItemToken(const char* arg, u32* serialOut, bool* hasTypeOut,
                        u16* typeOut, std::string* lowerNameOut,
                        const char** restOut) const;
    void HandleUseCommand(const char* arg);
    void HandlePickupCommand(const char* arg);
    void HandleDropCommand(const char* arg);
    void HandleEquipCommand(const char* arg);
    void HandleUnequipCommand(const char* arg);
    // arm/disarm: move the weapon (layer 1) / shield (layer 2) between hand and
    // backpack, remembering what was disarmed so `arm` re-equips it. Mirrors
    // Macro_ActionArmDisarm_Validate @0x4b6c90. `doArm` re-equips the saved
    // item; otherwise the equipped item is dropped into the backpack.
    void ArmDisarmHand(bool doArm, u8 layer);
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

    // Embedded JS scripting engine (QuickJS-NG). Ticks on the main loop after
    // BotTick; `run <file>` (re)starts a script in a fresh runtime. Script
    // errors are caught and printed ([js]) and never crash the client.
    js::JsEngine js_;
};

}
