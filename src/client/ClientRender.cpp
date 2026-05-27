#include "Client.h"

#include "bot/Pathfinding.h"
#include "uo/builders.h"
#include "uo/art.h"
#include "uo/tiledata.h"
#include "uo/texmap.h"
#include "uo/map.h"
#include "uo/anim.h"
#include "uo/animdata.h"
#include "uo/animinfo.h"
#include "uo/hues.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "render/Minimap.h"
#include "render/RadarColors.h"
#include "win32/MiniFB.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

namespace uo {

namespace {

constexpr int   kHudFontHeight = 14;
constexpr int   kSysLogLines = 8;
constexpr usize kChatInputMax = 120;
constexpr i64   kOverheadMs = 6000;
constexpr int   kHeadOffset = 44;
constexpr int   kStatusBarWidth = 96;
constexpr int   kStatusBarHeight = 8;
constexpr int   kStatusPanelWidth = 196;
constexpr int   kStatusPanelHeight = 66;
constexpr i64   kOverheadNameProbeMs = 10000;
constexpr i64   kStatusProbeMs = 3000;
// Mouse-over highlight hue, verbatim from the client's draw-hue chain
// (Mobile_OnRender @0x406FE0: serial == g_ContextActionTargetSerial -> hue 53).
constexpr u16   kHighlightHue = 53;
// How long a single-click item name label floats over the item.
constexpr i64   kItemLabelMs = 4000;

// Inverse of bot::DirToDelta: a unit (dx,dy) world step -> UO facing 0..7
// (0=N(0,-1), 2=E(+1,0), 4=S(0,+1), 6=W(-1,0); odds are the diagonals).
u8 DeltaToDir(int dx, int dy) {
    if (dx == 0 && dy < 0)  return 0;  // N
    if (dx > 0 && dy < 0)   return 1;  // NE
    if (dx > 0 && dy == 0)  return 2;  // E
    if (dx > 0 && dy > 0)   return 3;  // SE
    if (dx == 0 && dy > 0)  return 4;  // S
    if (dx < 0 && dy > 0)   return 5;  // SW
    if (dx < 0 && dy == 0)  return 6;  // W
    return 7;                          // NW
}

u16 HudColor(int r, int g, int b) {
    return static_cast<u16>(0x8000 |
                            ((std::clamp(r, 0, 255) >> 3) << 10) |
                            ((std::clamp(g, 0, 255) >> 3) << 5) |
                             (std::clamp(b, 0, 255) >> 3));
}

u16 JournalColor(u16 hue, u16 fallback) {
    if (hue == 0x0025 || hue == 0x0026) return HudColor(255, 96, 96);
    if (hue == 0x0044 || hue == 0x0059) return HudColor(128, 180, 255);
    if (hue == 0x03B2) return HudColor(225, 225, 225);
    return fallback;
}

u16 ScaleHudColor(u16 c, int scale255) {
    scale255 = std::clamp(scale255, 0, 255);
    const int r = static_cast<int>((c >> 10) & 31) * 255 / 31;
    const int g = static_cast<int>((c >> 5) & 31) * 255 / 31;
    const int b = static_cast<int>(c & 31) * 255 / 31;
    return HudColor(r * scale255 / 255, g * scale255 / 255, b * scale255 / 255);
}

// Body-kind thresholds mirror AnimLoader::IndexFor (the anim.mul index layout):
// monsters (high detail) < 200, animals (low detail) < 400, people >= 400.
enum class BodyKind { Monster, Animal, People };
BodyKind KindOf(u16 body) {
    if (body < 200) return BodyKind::Monster;
    if (body < 400) return BodyKind::Animal;
    return BodyKind::People;
}

bool MonsterHasRunAction(u16 body) {
    switch (body) {
        case 4: case 5: case 6: case 9: case 10: case 12:
        case 30: case 39: case 59: case 60: case 61:
            return true;
        default:
            return false;
    }
}

// Map motion/war state -> animation group id (UO per-kind group enums).
// Remote mobile packets preserve the run bit in the direction byte and carry
// war mode in the status flag byte (bit 0x40).
u8 PickAction(u16 body, bool moving, bool running, bool warMode) {
    const BodyKind k = KindOf(body);
    switch (k) {
        case BodyKind::Monster:
            return moving ? (running && MonsterHasRunAction(body) ? 19u : 0u) : 1u;
        case BodyKind::Animal:  return moving ? (running ? 1u : 0u) : 2u; // run/walk / stand
        case BodyKind::People:
            if (!moving && warMode) return 7u; // ready/attack stance.
            return moving ? (running ? 2u : 0u) : 4u; // run/walk(unarmed) / stand
    }
    return 0u;
}

constexpr i64 kAnimIntervalIdleMs = 200;
constexpr i64 kIdleFidgetDelayMs  = 15000; // Mobile_TryPlayIdleAnimation gate.
constexpr i64 kAnimListTickMs = 76;        // GameLoop_Update object/anim gate.

// Chair/sit table, ported verbatim from the 2.0.7 g_SittingChairTable
// (@0x55DB68; Mobile_FindSittingChair). A humanoid standing on one of these
// tiles is drawn SEATED. dirMask is a bitmask of the chair's facing(s):
// bit0->0(N), bit1->2(E), bit2->4(S), bit3->6(W); bit4 (0x10) = special. The
// seated body faces the chair (overriding its walk facing). offElse/off06 are
// the seat Y shift (off06 when facing N/W, offElse otherwise) — signed.
struct ChairSit { uo::u16 graphic; uo::u8 dirMask; int offElse; int off06; };
constexpr ChairSit kChairSit[] = {
    {0x0B2E,4,6,6},{0x0B2F,2,6,6},{0x0B4E,2,0,0},{0x0B4F,4,0,0},
    {0x0B52,2,0,0},{0x0B53,4,0,0},{0x0B56,2,4,4},{0x0B57,4,4,4},
    {0x0B5A,2,8,8},{0x0B5B,4,8,8},{0x1218,4,4,4},{0x1219,2,4,4},
    {0x0B50,1,0,0},{0x0B51,8,0,0},{0x0B54,1,0,0},{0x0B55,8,0,0},
    {0x0B58,8,0,8},{0x0B59,1,0,8},{0x0B5C,1,0,8},{0x0B5D,8,0,8},
    {0x121A,1,0,8},{0x121B,8,0,8},{0x0B32,4,0,0},{0x0B33,2,0,0},
    {0x1527,2,0,0},{0x0459,5,2,2},{0x045A,10,2,2},{0x045B,5,2,2},
    {0x045C,10,2,2},{0x0B2C,10,2,2},{0x0B2D,5,2,2},{0x3DFF,5,2,2},
    {0x3E00,10,2,2},{0x0B5F,10,3,14},{0x0B60,10,3,14},{0x0B61,10,3,14},
    {0x0B62,10,3,10},{0x0B63,10,3,10},{0x0B64,10,3,10},{0x0B65,5,3,10},
    {0x0B66,5,3,10},{0x0B67,5,3,10},{0x0B68,5,3,10},{0x0B69,5,3,10},
    {0x0B6A,5,3,10},{0x0B91,4,6,6},{0x0B92,4,6,6},{0x0B93,2,6,6},
    {0x0B94,2,6,6},{0x1DC7,10,3,10},{0x1DC8,10,3,10},{0x1DC9,10,3,10},
    {0x1DCA,5,3,10},{0x1DCB,5,3,10},{0x1DCC,5,3,10},{0x1DCD,10,3,10},
    {0x1DCE,10,3,10},{0x1DCF,10,3,10},{0x1DD0,5,3,10},{0x1DD1,5,3,10},
    {0x1DD2,10,3,10},{0x11FC,15,2,7},{0x0A2A,15,-4,-4},{0x0A2B,15,-8,-8},
    {0x0CF3,26,2,8},{0x0CF4,26,2,8},{0x0CF6,21,2,8},{0x0CF7,21,2,8},
    {0x1771,31,0,0},
};

const ChairSit* FindChairSit(uo::u16 graphic) {
    for (const ChairSit& c : kChairSit)
        if (c.graphic == graphic) return &c;
    return nullptr;
}

// Pick the seated facing from the chair's dirMask. Single-bit -> that cardinal
// facing; multi-bit (stools/thrones) -> the allowed facing nearest the mob's
// current facing. bit4 (0x10) is special and ignored for facing.
uo::u8 SitFacing(uo::u8 dirMask, uo::u8 curDir) {
    int best = -1, bestDist = 99;
    const int cur = curDir & 7;
    for (int b = 0; b < 4; ++b) {
        if (!(dirMask & (1 << b))) continue;
        const int f = b * 2;                 // 0=N,2=E,4=S,6=W
        int d = std::abs(f - cur);
        if (d > 4) d = 8 - d;                 // circular over 8 facings
        if (d < bestDist) { bestDist = d; best = f; }
    }
    return best >= 0 ? static_cast<uo::u8>(best) : static_cast<uo::u8>(cur);
}
} // namespace

void Client::RenderTick() {
    if (!cfg_.enableRenderer) return;

    if (!renderInit_) {
        renderInit_ = true;
        if (!EnsureWorldLoaded()) {
            LogWarn( "[render] world data unavailable; renderer off\n");
            cfg_.enableRenderer = false;
            return;
        }
        art_ = std::make_unique<art::ArtLoader>();
        if (!cfg_.artIdxPath || !cfg_.artPath ||
            !art_->Open(cfg_.artIdxPath, cfg_.artPath)) {
            LogWarn( "[render] failed to open art MULs; renderer off\n");
            art_.reset();
            cfg_.enableRenderer = false;
            return;
        }
        texmaps_ = std::make_unique<texmap::TexmapLoader>();
        if (!cfg_.texIdxPath || !cfg_.texPath ||
            !texmaps_->Open(cfg_.texIdxPath, cfg_.texPath)) {
            LogWarn( "[render] failed to open texmaps; renderer off\n");
            art_.reset();
            texmaps_.reset();
            cfg_.enableRenderer = false;
            return;
        }
        // Body animations are optional — without them mobiles just aren't drawn.
        anim_ = std::make_unique<anim::AnimLoader>();
        if (!cfg_.animIdxPath || !cfg_.animPath ||
            !anim_->Open(cfg_.animIdxPath, cfg_.animPath)) {
            LogWarn( "[render] anim MULs unavailable; mobiles won't draw\n");
            anim_.reset();
        }
        animData_ = std::make_unique<animdata::AnimDataLoader>();
        if (!cfg_.animDataPath || !animData_->Load(cfg_.animDataPath)) {
            LogWarn( "[render] animdata.mul unavailable; static item animation disabled\n");
            animData_.reset();
        }
        animInfo_ = std::make_unique<animinfo::AnimInfoLoader>();
        if (!cfg_.animInfoPath || !animInfo_->Load(cfg_.animInfoPath)) {
            LogWarn( "[render] animinfo.mul unavailable; using default mobile timing\n");
        }
        hues_ = std::make_unique<hues::HuesLoader>();
        if (!cfg_.huesPath || !hues_->Load(cfg_.huesPath)) {
            LogWarn( "[render] hues.mul unavailable; object/mobile hue disabled\n");
            hues_.reset();
        }
        // radarcol.mul drives the minimap colours (real-client radar palette).
        // Optional: without it the minimap panel is simply not drawn.
        radarColors_ = std::make_unique<render::RadarColors>();
        if (!cfg_.radarcolPath || !radarColors_->Load(cfg_.radarcolPath)) {
            LogWarn( "[render] radarcol.mul unavailable; minimap disabled\n");
            radarColors_.reset();
        }
        const int rw = cfg_.renderWidth  > 0 ? cfg_.renderWidth  : 800;
        const int rh = cfg_.renderHeight > 0 ? cfg_.renderHeight : 600;
        const int sc = cfg_.renderScale  > 0 ? cfg_.renderScale  : 1;
        if (!mfb_open("uo-client world", rw, rh, sc, 15)) {
            LogWarn( "[render] mfb_open failed; renderer off\n");
            art_.reset();
            cfg_.enableRenderer = false;
            return;
        }
        renderer_ = std::make_unique<render::Renderer>(rw, rh);
        text_ = std::make_unique<render::TextRenderer>();
        if (!text_->Init(kHudFontHeight)) {
            LogWarn("[render] Arial text unavailable; HUD text disabled\n");
            text_.reset();
        } else {
            LogInfo("[render] HUD text initialized (Arial %dpx)\n", kHudFontHeight);
        }
        renderWindowOpen_ = true;
        mfb_set_hide_cursor(1);   // we draw our own UO cursor from art_
        LogInfo("[render] world window opened (%dx%d)\n", rw, rh);
    }

    if (!renderWindowOpen_ || !renderer_ || !worldMap_ || !tileData_) return;

    HandleRenderChatInput();
    if (chatInputActive_) {
        HandleWorldClick();
        if (const char* keys = mfb_keystatus()) {
            minimapKeyDown_ = keys[0x4D] != 0;
            spaceKeyDown_ = keys[0x20] != 0;
            tabKeyDown_ = keys[0x09] != 0;
        }
    } else {
        HandleManualWalk();
        HandleWorldClick();
    }

    std::vector<render::DynItem> dyn;
    dyn.reserve(items_.size());
    for (const auto& kv : items_) {
        render::DynItem di{kv.second.itemId, kv.second.x, kv.second.y,
                           kv.second.z, kv.second.gfxOffset, kv.second.hue};
        di.serial = kv.first;
        if (kv.first == hoverSerial_) di.hue = kHighlightHue;  // light up under cursor
        dyn.push_back(di);
    }

    // Per-direction worn-item draw order (layer numbers, back-to-front),
    // verbatim from g_DrawLayerOrder @0x5144C0 with the mount slot (layer 25)
    // omitted here — the mount is handled separately (drawn as a body under the
    // rider, see applyMount below), not as a worn overlay. Index by facing 0..7.
    static constexpr uo::u8 kLayerDrawOrder[8][24] = {
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,2,21,20,6},
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,21,20,2,6},
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,21,20,2,6},
        {20,5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,6,1,2,21},
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,21,20,6,2},
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,21,20,6,2},
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,2,21,20,6},
        {5,4,3,24,19,13,8,9,14,15,7,23,17,22,12,10,11,16,18,1,2,21,20,6},
    };

    // Resolve worn items to anim ids in the client's per-facing draw order. Only
    // graphics with a valid worn anim (0x190..0x3E7) are drawable.
    auto resolveEquip = [&](uo::u8 dir, const std::vector<EquipObj>& equip,
                            std::vector<render::EquipAnim>& out) {
        if (!tileData_ || equip.empty()) return;
        for (uo::u8 slot : kLayerDrawOrder[dir & 7]) {
            for (const auto& e : equip) {
                if (e.layer != slot) continue;
                const uo::u16 a = tileData_->ItemAnimId(e.graphic);
                if (a >= 0x190 && a < 0x3E8) out.push_back({a, e.hue});
                break;
            }
        }
    };

    // Pick the animation group from motion state and the frame from the render
    // clock. Falls back to group 0 (walk) when the chosen group is unavailable,
    // so bodies without a stand group still draw.
    const i64 nowAnim = NowMs();
    auto moveDurationMs = [&](uo::u16 body, bool running) -> i64 {
        const u8 ticks = animInfo_ ? animInfo_->MoveFrameCount(body, running)
                                   : static_cast<u8>(running ? 2 : 4);
        return static_cast<i64>(ticks ? ticks : 1) * kAnimListTickMs;
    };

    auto resolveAnim = [&](uo::u16 body, uo::u8 dir, bool moving, bool running,
                           bool warMode,
                           uo::u32 animCounter, uo::u8& action, uo::u16& frame) {
        action = PickAction(body, moving, running, warMode);
        uo::u32 fc = anim_ ? anim_->FrameCount(body, dir, action) : 0u;
        if (fc == 0u && action != 0u) {
            action = 0u;
            fc = anim_ ? anim_->FrameCount(body, dir, 0u) : 0u;
        }
        frame = 0;
        if (fc == 0u) return;
        if (moving && fc > 1u) --fc; // Client uses frameCounter % (frameCount-1).
        if (moving) {
            frame = static_cast<uo::u16>(animCounter % fc);
            return;
        }
        frame = static_cast<uo::u16>((nowAnim / kAnimIntervalIdleMs) % fc);
    };

    auto tickMoveAnim = [&](i64& lastTickMs, u32& counter) {
        if (lastTickMs == 0) lastTickMs = nowAnim;
        while (nowAnim - lastTickMs >= kAnimListTickMs) {
            lastTickMs += kAnimListTickMs;
            ++counter;
        }
    };

    auto stopServerAnim = [](ServerAnimState& a) {
        a.active = false;
        a.hasRenderedFrame = false;
    };

    auto renderServerAnimFrame = [](ServerAnimState& a) {
        a.renderedFrame = a.currentFrame;
        a.hasRenderedFrame = true;
    };

    auto advanceServerAnimFrame = [&](ServerAnimState& a) {
        if (a.reverse) {
            if (a.currentFrame != 0u && a.currentFrame <= a.maxFrames) {
                renderServerAnimFrame(a);
                --a.currentFrame;
                a.pad = 0;
                return;
            }
            if (a.currentDuration != 0u) {
                renderServerAnimFrame(a);
                --a.currentDuration;
                a.currentFrame = static_cast<u16>(a.maxFrames - 1u);
                a.pad = 0;
                return;
            }
            if (a.bounce) {
                a.reverse = false;
                a.bounce = false;
                renderServerAnimFrame(a);
                ++a.currentFrame;
                a.pad = 0;
                return;
            }
            stopServerAnim(a);
            return;
        }

        if (a.currentFrame < static_cast<u16>(a.maxFrames - 1u)) {
            renderServerAnimFrame(a);
            ++a.currentFrame;
            a.pad = 0;
            return;
        }
        if (a.currentDuration == static_cast<u16>(a.maxDuration - 1u)) {
            if (a.bounce) {
                a.reverse = true;
                a.bounce = false;
                renderServerAnimFrame(a);
                --a.currentFrame;
                a.pad = 0;
                return;
            }
            stopServerAnim(a);
            return;
        }
        renderServerAnimFrame(a);
        ++a.currentDuration;
        a.currentFrame = 0;
        a.pad = 0;
    };

    auto resolveServerAnim = [&](uo::u16 body, uo::u8 dir, ServerAnimState& a,
                                 uo::u8& action, uo::u16& frame) -> bool {
        if (!a.active || !anim_) return false;
        const uo::u32 fullCount = anim_->FrameCount(body, dir, a.action);
        if (fullCount == 0u) {
            stopServerAnim(a);
            return false;
        }
        a.maxFrames = static_cast<u16>(
            std::min<uo::u32>(a.maxFrames ? a.maxFrames : fullCount, fullCount));
        if (a.maxFrames == 0u) { stopServerAnim(a); return false; }
        if (a.currentFrame >= a.maxFrames)
            a.currentFrame = a.reverse ? static_cast<u16>(a.maxFrames - 1u) : 0u;

        while (a.active && nowAnim - a.lastTickMs >= kAnimListTickMs) {
            a.lastTickMs += kAnimListTickMs;
            const u16 oldPad = a.pad;
            ++a.pad;
            if (oldPad <= a.delayPerFrame) continue;
            advanceServerAnimFrame(a);
        }
        if (!a.active) return false;

        action = a.action;
        frame = a.hasRenderedFrame ? a.renderedFrame : a.currentFrame;
        return true;
    };

    auto pickIdleFidget = [](uo::u16 body, bool secondChoice, uo::u8& action,
                             uo::u16& maxFrames, uo::u16& repeatCount) {
        repeatCount = 0;
        const BodyKind k = KindOf(body);
        if (k == BodyKind::People) {
            action = secondChoice ? 5u : 6u; // people fidgets from Mobile_TryPlayIdleAnimation.
            maxFrames = 5;
            repeatCount = secondChoice ? 1u : 0u;
        } else if (k == BodyKind::Animal) {
            action = secondChoice ? 9u : 10u;
            maxFrames = secondChoice ? 5u : 3u;
        } else if (body >= 150u) {
            action = secondChoice ? 3u : 4u; // sea/anim2 monster class.
            maxFrames = secondChoice ? 15u : 20u;
        } else {
            action = secondChoice ? 17u : 18u;
            maxFrames = 5;
            repeatCount = (body == 21u && secondChoice) ? 1u : 0u;
        }
    };

    auto stopIdleAnim = [&](IdleAnimState& idle) {
        idle.active = false;
        idle.hasRenderedFrame = false;
        idle.nextProbeMs = nowAnim + kIdleFidgetDelayMs;
    };

    auto renderIdleFrame = [](IdleAnimState& idle) {
        idle.renderedAction = static_cast<uo::u8>(idle.action + idle.currentDuration);
        idle.renderedFrame = idle.currentFrame;
        idle.hasRenderedFrame = true;
    };

    auto advanceIdleFrame = [&](IdleAnimState& idle) {
        if (idle.reverse) {
            if (idle.currentFrame != 0u && idle.currentFrame <= idle.maxFrames) {
                renderIdleFrame(idle);
                --idle.currentFrame;
                idle.pad = 0;
                return;
            }
            if (idle.currentDuration != 0u) {
                renderIdleFrame(idle);
                --idle.currentDuration;
                idle.currentFrame = static_cast<uo::u16>(idle.maxFrames - 1u);
                idle.pad = 0;
                return;
            }
            if (idle.repeatCount != 0u) {
                idle.reverse = false;
                idle.repeatCount = 0;
                renderIdleFrame(idle);
                ++idle.currentFrame;
                idle.pad = 0;
                return;
            }
            stopIdleAnim(idle);
            return;
        }

        if (idle.currentFrame < idle.maxFrames - 1u) {
            renderIdleFrame(idle);
            ++idle.currentFrame;
            idle.pad = 0;
            return;
        }
        if (idle.currentDuration == idle.maxDuration - 1u) {
            if (idle.repeatCount != 0u) {
                idle.reverse = true;
                idle.repeatCount = 0;
                renderIdleFrame(idle);
                --idle.currentFrame;
                idle.pad = 0;
                return;
            }
            stopIdleAnim(idle);
            return;
        }

        renderIdleFrame(idle);
        ++idle.currentDuration;
        idle.currentFrame = 0;
        idle.pad = 0;
    };

    auto tickIdleAnim = [&](IdleAnimState& idle) {
        while (idle.active && nowAnim - idle.lastTickMs >= kAnimListTickMs) {
            idle.lastTickMs += kAnimListTickMs;
            const u16 oldPad = idle.pad;
            ++idle.pad;
            if (oldPad <= idle.delayPerFrame) continue;
            advanceIdleFrame(idle);
        }
    };

    auto resolveIdleAnim = [&](uo::u16 body, uo::u8 dir, bool warMode, IdleAnimState& idle,
                               uo::u8& action, uo::u16& frame) {
        action = PickAction(body, false, false, warMode);
        uo::u32 fc = anim_ ? anim_->FrameCount(body, dir, action) : 0u;
        if (fc == 0u && action != 0u) {
            action = 0u;
            fc = anim_ ? anim_->FrameCount(body, dir, 0u) : 0u;
        }
        frame = fc == 0u ? 0u : static_cast<uo::u16>(std::min<uo::u32>(3u, fc - 1u));

        if (!anim_) return;
        if (idle.active) {
            tickIdleAnim(idle);
            if (idle.active && idle.hasRenderedFrame) {
                action = idle.renderedAction;
                frame = idle.renderedFrame;
                return;
            }
            if (idle.active) return;
        }

        if (idle.nextProbeMs == 0) idle.nextProbeMs = nowAnim + kIdleFidgetDelayMs;
        if (nowAnim < idle.nextProbeMs) return;

        // The real client tests rand() % (4*mobileCount + 120) < 2 each
        // object update after 15s. We keep the same low-probability timer.
        const int nearby = std::max<int>(1, static_cast<int>(mobileCache_.size() + 1));
        std::uniform_int_distribution<int> chance(0, 4 * nearby + 119);
        if (chance(nav_.rng) >= 2) {
            idle.nextProbeMs = nowAnim + 76;
            return;
        }

        uo::u8 fidgetAction = 0;
        uo::u16 maxFrames = 0;
        uo::u16 repeatCount = 0;
        std::uniform_int_distribution<int> choice(0, 1);
        pickIdleFidget(body, choice(nav_.rng) != 0, fidgetAction, maxFrames, repeatCount);
        fc = anim_->FrameCount(body, dir, fidgetAction);
        if (fc == 0u) {
            idle.nextProbeMs = nowAnim + 76;
            return;
        }
        maxFrames = static_cast<uo::u16>(std::min<uo::u32>(maxFrames, fc));
        if (maxFrames == 0u) return;
        idle.active = true;
        idle.lastTickMs = nowAnim;
        idle.action = fidgetAction;
        idle.maxFrames = maxFrames;
        idle.delayPerFrame = 0;
        idle.maxDuration = 1;
        idle.currentFrame = 0;
        idle.currentDuration = 0;
        idle.pad = 0;
        idle.repeatCount = repeatCount;
        idle.reverse = false;
        idle.hasRenderedFrame = false;
    };

    // Slide between previous and current cell over the step, so the sprite moves
    // in sync with the walk cycle instead of teleporting. dd = (t-1)*(cur-prev):
    // prev-cur at t=0, easing to 0 at t=1 (sprite reaches its current cell).
    auto slideDelta = [&](i64 movedMs, i64 durMs, uo::i32 cur, uo::i32 prev) -> float {
        if (movedMs == 0 || durMs <= 0) return 0.0f;
        const i64 el = nowAnim - movedMs;
        if (el < 0 || el >= durMs) return 0.0f;
        const double t = static_cast<double>(el) / static_cast<double>(durMs);
        return static_cast<float>((t - 1.0) * (cur - prev));
    };

    // A held/worn light source (torch, lantern): the item graphic of the first
    // equipped item flagged kFlagLightSource (the renderer classifies its
    // color/radius). -1 if none.
    auto equipLightGraphic = [&](const std::vector<EquipObj>& eq) -> int {
        if (!tileData_) return -1;
        for (const EquipObj& e : eq) {
            if (tileData_->Static(e.graphic).flags & tiledata::kFlagLightSource)
                return e.graphic;
        }
        return -1;
    };

    // Mount/seat handling: a mobile with an item on the mount layer (25) is
    // "mounted" — the client renders it seated (the ride anim group) and draws
    // the mount body under it. This is also how a chair seats a player: the
    // server occupies layer 25; a chair has no body anim, so only the seated
    // pose shows (the chair itself is a world static). Mirrors the original
    // (Mobile_OnEquip: isMounted = pEquipped[25]; ride groups 23/24).
    auto mountGraphic = [](const std::vector<EquipObj>& equip) -> uo::u16 {
        for (const EquipObj& e : equip)
            if (e.layer == 25) return e.graphic;
        return 0;
    };

    auto applyMountUnderlay = [&](const std::vector<EquipObj>& equip, uo::u8 dir,
                                  bool moving, bool running, int counter,
                                  render::Mob& mob) -> bool {
        const uo::u16 mountG = mountGraphic(equip);
        if (!mountG || mob.body < 0x190) return false;   // only people ride/sit
        const uo::u16 mb = tileData_ ? tileData_->ItemAnimId(mountG) : 0;
        if (mb) {                                         // real mount: draw its body
            mob.mountBody = mb;
            mob.mountAction = PickAction(mb, moving, running, false);
            const uo::u32 mfc = anim_ ? anim_->FrameCount(mb, dir, mob.mountAction) : 0;
            mob.mountFrame = (moving && mfc) ? static_cast<uo::u16>(counter % mfc) : 0;
            return true;
        }
        return false;
    };

    auto applyMount = [&](const std::vector<EquipObj>& equip, uo::u8 dir,
                          bool moving, bool running, int counter,
                          render::Mob& mob) -> bool {
        const uo::u16 mountG = mountGraphic(equip);
        if (!mountG || mob.body < 0x190) return false;   // only people ride/sit
        mob.action = (moving && running) ? 24u : 23u;    // ride run / ride idle+walk
        const uo::u32 fc = anim_ ? anim_->FrameCount(mob.body, dir, mob.action) : 0;
        mob.frame = (moving && fc) ? static_cast<uo::u16>(counter % fc) : 0;
        applyMountUnderlay(equip, dir, moving, running, counter, mob);
        return true;
    };

    // Seated-on-chair: a HUMANOID mob standing on a chair tile (a DynItem in
    // `dyn` whose graphic is in kChairSit) is drawn seated, facing the chair
    // (overriding its walk facing), with a seat Y shift. Mirrors the 2.0.7
    // Mobile_FindSittingChair (humanoid body, stationary). Returns true if seated
    // (caller then skips the normal walk/idle + mount selection).
    std::vector<map::StaticItem> sitStatics(256);
    auto applySit = [&](uo::i32 x, uo::i32 y, uo::i8 z, bool moving,
                        render::Mob& mob) -> bool {
        if (moving || mob.body < 0x190 || mob.body >= 0x3E8) return false;
        const ChairSit* c = nullptr;
        // (1) dynamic 0x1A items on the tile
        for (const render::DynItem& d : dyn) {
            if (d.x == x && d.y == y && std::abs(int(d.z) - int(z)) <= 4) {
                c = FindChairSit(d.itemId);
                if (c) break;
            }
        }
        // (2) map statics on the tile (tavern furniture etc. live in statics.mul)
        if (!c && worldMap_) {
            uo::u32 n = 0;
            if (worldMap_->ReadStatics(static_cast<uo::u32>(x) / 8, static_cast<uo::u32>(y) / 8,
                                       sitStatics.data(),
                                       static_cast<uo::u32>(sitStatics.size()), &n)) {
                const uo::u8 lx = static_cast<uo::u8>(x & 7), ly = static_cast<uo::u8>(y & 7);
                for (uo::u32 i = 0; i < n; ++i) {
                    const map::StaticItem& s = sitStatics[i];
                    if (s.cellX == lx && s.cellY == ly && std::abs(int(s.z) - int(z)) <= 4) {
                        c = FindChairSit(s.itemId);
                        if (c) break;
                    }
                }
            }
        }
        if (!c) return false;
        mob.dir = SitFacing(c->dirMask, mob.dir);
        mob.sitting = true;
        // off06/offElse is the per-chair fine seat height; kSitBaseY corrects our
        // anchor baseline vs the client's render-cache origin (TUNABLE).
        constexpr int kSitBaseY = 20;
        mob.sitOffsetY = kSitBaseY + ((mob.dir == 0 || mob.dir == 6) ? c->off06 : c->offElse);
        // Seated anim group, exactly as the 2.0.7 Mobile_RenderSeatedOnChair:
        // facing N/W (0/6) -> group 25, else -> group 4. Our AnimLoader index
        // matches the client's people layout, so this is the real sit pose.
        mob.action = (mob.dir == 0 || mob.dir == 6) ? 25u : 4u;
        mob.frame = 0;
        return true;
    };

    // Mobiles: nearby NPCs/players from the cache plus the local player. The
    // player carries isPlayer=true so the renderer can compute the roof cutoff.
    std::vector<render::Mob> mobs;
    mobs.reserve(mobileCache_.size() + 1);
    for (auto it = mobileCache_.begin(); it != mobileCache_.end(); ) {
        if (it->deadRemoveMs != 0 && !it->serverAnim.active && nowAnim >= it->deadRemoveMs) {
            mobileNames_.erase(it->serial);
            it = mobileCache_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& m : mobileCache_) {
        if (!m.body) continue;
        render::Mob mob{m.body, m.x, m.y, m.z, m.dir, false, m.hue, {}};
        mob.serial = m.serial;
        if (m.serial == hoverSerial_) mob.hue = kHighlightHue;  // light up under cursor
        mob.light = equipLightGraphic(m.equip);
        const i64 mobMoveMs = moveDurationMs(m.body, m.running);
        const bool moving = m.movedMs != 0 && (nowAnim - m.movedMs) < mobMoveMs;
        // Sitting overrides facing, so resolve worn gear AFTER it (gear must
        // follow the seated facing, not the walk facing).
        const bool seated = applySit(m.x, m.y, m.z, moving, mob);
        resolveEquip(mob.dir, m.equip, mob.equipAnims);
        if (!seated) {
            const bool serverAnim = resolveServerAnim(m.body, mob.dir, m.serverAnim,
                                                      mob.action, mob.frame);
            if (serverAnim) {
                m.idleAnim.active = false;
                if (moving) tickMoveAnim(m.moveAnimTickMs, m.moveAnimCounter);
                else m.moveAnimTickMs = 0;
                applyMountUnderlay(m.equip, mob.dir, moving, m.running,
                                   m.moveAnimCounter, mob);
            } else if (moving) {
                m.idleAnim.active = false;
                m.idleAnim.nextProbeMs = nowAnim + kIdleFidgetDelayMs;
                tickMoveAnim(m.moveAnimTickMs, m.moveAnimCounter);
                if (!applyMount(m.equip, m.dir, moving, m.running, m.moveAnimCounter, mob))
                    resolveAnim(m.body, m.dir, true, m.running, m.warMode, m.moveAnimCounter,
                                mob.action, mob.frame);
            } else {
                m.moveAnimTickMs = 0;
                if (!applyMount(m.equip, m.dir, moving, m.running, m.moveAnimCounter, mob))
                    resolveIdleAnim(m.body, m.dir, m.warMode, m.idleAnim, mob.action, mob.frame);
            }
        }
        mob.ddx = slideDelta(m.movedMs, mobMoveMs, m.x, m.prevX);
        mob.ddy = slideDelta(m.movedMs, mobMoveMs, m.y, m.prevY);
        mobs.push_back(std::move(mob));
    }
    render::Mob self{playerBody_, playerX_, playerY_, playerZ_, playerFacing_, true, playerHue_, {}};
    self.serial = playerSerial_;
    if (playerSerial_ != 0 && playerSerial_ == hoverSerial_) self.hue = kHighlightHue;
    self.light = equipLightGraphic(playerEquip_);
    const i64 selfMoveMs = moveDurationMs(playerBody_, player_.running);
    const bool selfMidStep = lastStepMs_ != 0 &&
                             (nowAnim - lastStepMs_) < selfMoveMs;
    const bool selfMovementChainActive =
        nav_.bot.active || !nav_.bot.path.empty() || !nav_.movement.pending.empty() ||
        (lastManualMoveMs_ != 0 &&
         nowAnim - lastManualMoveMs_ <
             static_cast<i64>(BotMoveGapMs() + kAnimListTickMs));
    const bool selfMoving = selfMidStep ||
                            (selfMovementChainActive && lastStepMs_ != 0);
    const bool selfRunning = selfMoving && player_.running;
    const bool selfSeated = applySit(playerX_, playerY_, playerZ_, selfMoving, self);
    resolveEquip(self.dir, playerEquip_, self.equipAnims);
    if (!selfSeated) {
        const bool serverAnim = resolveServerAnim(playerBody_, self.dir, playerServerAnim_,
                                                  self.action, self.frame);
        if (serverAnim) {
            playerIdleAnim_.active = false;
            if (selfMoving) tickMoveAnim(playerMoveAnimTickMs_, playerMoveAnimCounter_);
            else playerMoveAnimTickMs_ = 0;
            applyMountUnderlay(playerEquip_, self.dir, selfMoving, selfRunning,
                               playerMoveAnimCounter_, self);
        } else if (selfMoving) {
            playerIdleAnim_.active = false;
            playerIdleAnim_.nextProbeMs = nowAnim + kIdleFidgetDelayMs;
            tickMoveAnim(playerMoveAnimTickMs_, playerMoveAnimCounter_);
            if (!applyMount(playerEquip_, playerFacing_, selfMoving, selfRunning,
                            playerMoveAnimCounter_, self))
                resolveAnim(playerBody_, playerFacing_, true, selfRunning, playerWarMode_,
                            playerMoveAnimCounter_,
                            self.action, self.frame);
        } else {
            playerMoveAnimTickMs_ = 0;
            if (!selfMovementChainActive) playerMoveAnimCounter_ = 3;
            if (!applyMount(playerEquip_, playerFacing_, selfMoving, selfRunning,
                            playerMoveAnimCounter_, self))
                resolveIdleAnim(playerBody_, playerFacing_, playerWarMode_, playerIdleAnim_,
                                self.action, self.frame);
        }
    }
    self.ddx = slideDelta(lastStepMs_, selfMoveMs, playerX_, prevPlayerX_);
    self.ddy = slideDelta(lastStepMs_, selfMoveMs, playerY_, prevPlayerY_);
    mobs.push_back(std::move(self));

    // Ambient night/cave darkness (2.0.7 light pass). Forced off while
    // alwaysDay_. darkness = clamp(overall - personal, 0..31); RenderWorld then
    // carves lit pools around light.mul emitters and composites — world only,
    // before the minimap/HUD overlays below, which stay at full brightness.
    int darkness = 0;
    if (!alwaysDay_) {
        darkness = static_cast<int>(overallLightLevel_) -
                   static_cast<int>(personalLightLevel_);
        if (darkness < 0) darkness = 0;
        if (darkness > 31) darkness = 31;
    }
    renderer_->RenderWorld(*worldMap_, *art_, *tileData_, *texmaps_,
                           playerX_, playerY_, playerZ_, dyn.data(), dyn.size(),
                           animData_.get(), static_cast<uo::u32>(nowAnim / kAnimListTickMs),
                           hues_.get(),
                           anim_.get(), mobs.data(), mobs.size(),
                           darkness);

    // Object interaction: pick the object under the cursor (for next frame's
    // highlight) and dispatch left/double clicks. Done after RenderWorld so the
    // pick list reflects the frame just drawn.
    {
        int mx = 0, my = 0;
        hoverSerial_ = mfb_mousepos(&mx, &my) ? renderer_->PickObject(mx, my) : 0;
    }
    HandleItemClicks();

    // Window hotkeys: 'M' toggles the minimap, SPACE sends OpenDoor, TAB
    // requests war/peace mode.
    if (!chatInputActive_) {
        if (const char* keys = mfb_keystatus()) {
            const bool mDown = keys[0x4D] != 0;   // VK 'M'
            if (mDown && !minimapKeyDown_) minimapVisible_ = !minimapVisible_;
            minimapKeyDown_ = mDown;

            const bool spaceDown = keys[0x20] != 0;   // VK_SPACE
            if (spaceDown && !spaceKeyDown_) {
                u8 ob[8];
                Send(ob, build::OpenDoor(ob), "0x12 OpenDoor (SPACE)");
                LogInfo("[door] OpenDoor (SPACE)\n");
            }
            spaceKeyDown_ = spaceDown;

            const bool tabDown = keys[0x09] != 0; // VK_TAB
            if (tabDown && !tabKeyDown_) {
                u8 pkt[8];
                const bool wantWar = !playerWarMode_;
                Send(pkt, build::WarMode(pkt, wantWar, warModeArg1_,
                                         warModeArg2_, warModeArg3_),
                     wantWar ? "0x72 WarMode on (TAB)" : "0x72 WarMode off (TAB)");
                LogInfo("[war] request %s (TAB)\n", wantWar ? "on" : "off");
            }
            tabKeyDown_ = tabDown;
        }
    }
    if (minimapVisible_ && worldMap_ && radarColors_) {
        if (!minimap_) {
            int ms = std::min(200, std::min(renderer_->Width(), renderer_->Height()) - 16);
            if (ms < 64) ms = 64;
            minimap_ = std::make_unique<render::Minimap>(ms);
        }
        // Replay the remaining directions from the player to get route cells.
        std::vector<i32> px, py;
        px.reserve(nav_.bot.path.size() + 1);
        py.reserve(nav_.bot.path.size() + 1);
        i32 cx = playerX_, cy = playerY_;
        px.push_back(cx); py.push_back(cy);
        for (u8 d : nav_.bot.path) {
            i32 ddx = 0, ddy = 0;
            bot::DirToDelta(static_cast<u8>(d & 7), &ddx, &ddy);
            cx += ddx; cy += ddy;
            px.push_back(cx); py.push_back(cy);
        }
        minimap_->Render(*worldMap_, *radarColors_, playerX_, playerY_,
                         px.data(), py.data(), px.size());
        const int mw = minimap_->Size();
        renderer_->Overlay(minimap_->Frame(), mw, mw, renderer_->Width() - mw - 8, 8);
    }

    DrawStatusBars();
    DrawContainers();
    DrawSystemLog();
    DrawChatInput();
    DrawOverheadText();
    DrawCursorOverlay();

    char title[64];
    if (nav_.bot.active || !nav_.bot.path.empty()) {
        std::snprintf(title, sizeof(title), "uo-client [%d,%d,%d] path=%zu",
                      playerX_, playerY_, static_cast<int>(playerZ_),
                      nav_.bot.path.size());
    } else {
        std::snprintf(title, sizeof(title), "uo-client [%d,%d,%d]",
                      playerX_, playerY_, static_cast<int>(playerZ_));
    }
    mfb_set_title(title);
    if (!mfb_update(renderer_->Frame(), 0)) {
        // User closed the window — stop drawing, keep the bot running.
        mfb_close();
        renderWindowOpen_ = false;
        cfg_.enableRenderer = false;
        LogInfo("[render] window closed; rendering disabled\n");
    }
}

void Client::HandleRenderChatInput() {
    uint32_t ch = 0;
    while (mfb_poll_char(&ch)) {
        if (!chatInputActive_) {
            if (ch == '\r' || ch == '\n') {
                chatInputActive_ = true;
                chatInputLine_.clear();
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (!chatInputLine_.empty())
                HandleStdinLine(chatInputLine_.c_str());
            chatInputLine_.clear();
            chatInputActive_ = false;
            continue;
        }
        if (ch == 27) {
            chatInputLine_.clear();
            chatInputActive_ = false;
            continue;
        }
        if (ch == '\b') {
            if (!chatInputLine_.empty())
                chatInputLine_.pop_back();
            continue;
        }
        if (ch == '\t') ch = ' ';
        if (ch >= 32 && ch < 127 && chatInputLine_.size() < kChatInputMax)
            chatInputLine_.push_back(static_cast<char>(ch));
    }
}

void Client::DrawStatusBars() {
    if (!renderer_) return;
    if (playerSerial_ != 0 &&
        player_.hpMax <= 0 && player_.manaMax <= 0 && player_.stamMax <= 0) {
        const i64 now = NowMs();
        if (now - lastStatusProbeMs_ >= kStatusProbeMs) {
            u8 pkt[16];
            const usize n = build::GetPlayerStatus(pkt, 0x04, playerSerial_);
            Send(pkt, n, "0x34 GetPlayerStatus (HUD basic status)");
            lastStatusProbeMs_ = now;
        }
    }

    struct Bar { const char* name; i32 cur; i32 max; u16 fill; };
    const Bar bars[] = {
        {"HP", player_.hpCur, player_.hpMax, HudColor(190, 42, 42)},
        {"Mana", player_.manaCur, player_.manaMax, HudColor(50, 86, 190)},
        {"Stam", player_.stamCur, player_.stamMax, HudColor(205, 172, 45)},
    };

    constexpr int panelX = 8;
    constexpr int panelY = 8;
    constexpr int labelX = panelX + 8;
    constexpr int x = panelX + 48;
    int y = panelY + 9;
    const u16 border = HudColor(215, 215, 215);
    const u16 panel = HudColor(5, 5, 7);
    const u16 bg = HudColor(18, 18, 20);
    const u16 label = HudColor(245, 245, 245);

    std::vector<u16> fb(static_cast<usize>(kStatusPanelWidth) * kStatusPanelHeight, panel);
    for (int px = 0; px < kStatusPanelWidth; ++px) {
        fb[px] = border;
        fb[static_cast<usize>(kStatusPanelHeight - 1) * kStatusPanelWidth + px] = border;
    }
    for (int py = 0; py < kStatusPanelHeight; ++py) {
        fb[static_cast<usize>(py) * kStatusPanelWidth] = border;
        fb[static_cast<usize>(py) * kStatusPanelWidth + (kStatusPanelWidth - 1)] = border;
    }
    renderer_->Overlay(fb.data(), kStatusPanelWidth, kStatusPanelHeight, panelX, panelY);

    for (const Bar& b : bars) {
        renderer_->FillRect(x - 1, y - 1, kStatusBarWidth + 2, kStatusBarHeight + 2, border);
        renderer_->FillRect(x, y, kStatusBarWidth, kStatusBarHeight, bg);
        if (b.max > 0 && b.cur >= 0) {
            const int fillW = std::clamp(b.cur, 0, b.max) * kStatusBarWidth / b.max;
            renderer_->FillRect(x, y, fillW, kStatusBarHeight, b.fill);
        }

        if (text_) {
            char buf[64];
            if (b.max > 0 && b.cur >= 0)
                std::snprintf(buf, sizeof(buf), "%d/%d", b.cur, b.max);
            else
                std::snprintf(buf, sizeof(buf), "--/--");
            text_->Draw(*renderer_, b.name, labelX,
                        y - 4, label, render::TextRenderer::Align::Left);
            text_->Draw(*renderer_, buf, x + kStatusBarWidth + 6,
                        y - 3, label, render::TextRenderer::Align::Left);
        }
        y += kStatusBarHeight + 11;
    }
}

void Client::DrawContainers() {
    if (!renderer_ || !text_ || openContainers_.empty()) return;

    const int lh = text_->LineHeight();
    const int pad = 6;
    const int rowH = lh + 2;
    const int panelW = 280;
    const int maxRows = 14;
    const u16 border = HudColor(215, 215, 215);
    const u16 bg = HudColor(12, 12, 16);
    const u16 titleColor = HudColor(255, 220, 120);
    const u16 itemColor = HudColor(225, 225, 225);

    // tiledata.mul static-tile name (loaded lazily with the MULs; may be null
    // before the first goto). NUL-padded to 20 bytes, not always terminated.
    auto tileName = [&](u16 graphic) -> std::string {
        if (!tileData_) return std::string();
        const char* n = tileData_->Static(graphic).name;
        usize len = 0;
        while (len < 20 && n[len]) ++len;
        return std::string(n, len);
    };
    auto itemGraphic = [&](u32 serial) -> u16 {
        auto i = items_.find(serial);
        return (i != items_.end()) ? i->second.itemId : 0;
    };

    const int panelX = renderer_->Width() - panelW - 8;
    int y = 8;
    if (minimapVisible_ && minimap_) y = 8 + minimap_->Size() + 12;

    for (const auto& oc : openContainers_) {
        auto it = containerItems_.find(oc.serial);
        const std::vector<ContainerItem>* items =
            (it != containerItems_.end()) ? &it->second : nullptr;
        const int nItems = items ? static_cast<int>(items->size()) : 0;
        const int shown = std::min(nItems, maxRows);
        const int extra = nItems - shown;
        const int rows = 1 + std::max(shown, 1) + (extra > 0 ? 1 : 0);
        const int panelH = pad * 2 + rows * rowH;
        if (y + panelH > renderer_->Height() - 8) break;  // out of room

        renderer_->FillRect(panelX - 1, y - 1, panelW + 2, panelH + 2, border);
        renderer_->FillRect(panelX, y, panelW, panelH, bg);

        const char* kind = "Container";
        if (oc.gumpId == 500 || oc.gumpId == 501) kind = "Bank";
        else if (oc.gumpId == 10 || oc.gumpId == 48) kind = "Paperdoll";

        int ty = y + pad;
        char buf[96];
        const std::string contName = tileName(itemGraphic(oc.serial));
        if (!contName.empty())
            std::snprintf(buf, sizeof(buf), "%s: %s (0x%08X) [%d]",
                          kind, contName.c_str(), oc.serial, nItems);
        else
            std::snprintf(buf, sizeof(buf), "%s (0x%08X) [%d]", kind, oc.serial, nItems);
        text_->Draw(*renderer_, buf, panelX + pad, ty, titleColor,
                    render::TextRenderer::Align::Left);
        ty += rowH;

        if (nItems == 0) {
            text_->Draw(*renderer_, "(empty)", panelX + pad, ty, itemColor,
                        render::TextRenderer::Align::Left);
        } else {
            for (int i = 0; i < shown; ++i) {
                const ContainerItem& ci = (*items)[i];
                const std::string nm = tileName(ci.graphic);
                std::snprintf(buf, sizeof(buf), "0x%04X x%-3u %s",
                              ci.graphic, ci.amount, nm.c_str());
                text_->Draw(*renderer_, buf, panelX + pad, ty, itemColor,
                            render::TextRenderer::Align::Left);
                ty += rowH;
            }
            if (extra > 0) {
                std::snprintf(buf, sizeof(buf), "... +%d more", extra);
                text_->Draw(*renderer_, buf, panelX + pad, ty, itemColor,
                            render::TextRenderer::Align::Left);
            }
        }
        y += panelH + 8;
    }
}

void Client::DrawSystemLog() {
    if (!renderer_ || !text_) return;

    const u16 fallback = HudColor(220, 220, 220);
    const int lh = text_->LineHeight();
    const int bottomReserve = lh + 8;
    int drawn = 0;
    for (auto it = journal_.rbegin(); it != journal_.rend() && drawn < kSysLogLines; ++it) {
        if (it->ownerKind != JournalOwnerKind::System) continue;
        const int y = renderer_->Height() - 10 - bottomReserve - lh * (drawn + 1);
        text_->Draw(*renderer_, it->text, 10, y, JournalColor(it->hue, fallback),
                    render::TextRenderer::Align::Left);
        ++drawn;
    }
}

void Client::DrawChatInput() {
    if (!renderer_ || !text_ || !chatInputActive_) return;

    const int lh = text_->LineHeight();
    const bool showCursor = ((NowMs() / 500) & 1) == 0;
    std::string line = "> ";
    line += chatInputLine_;
    if (showCursor) line += '_';
    text_->Draw(*renderer_, line, 10, renderer_->Height() - 10 - lh,
                HudColor(245, 245, 245), render::TextRenderer::Align::Left);
}

void Client::DrawOverheadText() {
    if (!renderer_ || !text_) return;

    struct LabelTarget {
        u32 serial;
        i32 x;
        i32 y;
        i8 z;
        u16 body;
        u8 dir;
        float ddx;
        float ddy;
        std::string name;
    };
    const i64 now = NowMs();
    auto moveDurationMs = [&](uo::u16 body, bool running) -> i64 {
        const u8 ticks = animInfo_ ? animInfo_->MoveFrameCount(body, running)
                                   : static_cast<u8>(running ? 2 : 4);
        return static_cast<i64>(ticks ? ticks : 1) * kAnimListTickMs;
    };
    auto slideDelta = [&](i64 movedMs, i64 durMs, uo::i32 cur, uo::i32 prev) -> float {
        if (movedMs == 0 || durMs <= 0) return 0.0f;
        const i64 el = now - movedMs;
        if (el < 0 || el >= durMs) return 0.0f;
        const double t = static_cast<double>(el) / static_cast<double>(durMs);
        return static_cast<float>((t - 1.0) * (cur - prev));
    };

    const i64 playerMoveMs = moveDurationMs(playerBody_, player_.running);
    const float playerDdx = slideDelta(lastStepMs_, playerMoveMs, playerX_, prevPlayerX_);
    const float playerDdy = slideDelta(lastStepMs_, playerMoveMs, playerY_, prevPlayerY_);
    auto projectLabel = [&](i32 x, i32 y, i8 z, float ddx, float ddy, int* outSx, int* outSy) {
        renderer_->WorldToScreen(x, y, z, playerX_, playerY_, playerZ_, outSx, outSy);
        constexpr int kHalfTile = 22;
        const int camOffX = static_cast<int>(std::lround(-(playerDdx - playerDdy) * kHalfTile));
        const int camOffY = static_cast<int>(std::lround(-(playerDdx + playerDdy) * kHalfTile));
        const int mobOffX = static_cast<int>(std::lround((ddx - ddy) * kHalfTile));
        const int mobOffY = static_cast<int>(std::lround((ddx + ddy) * kHalfTile));
        if (outSx) *outSx += camOffX + mobOffX;
        if (outSy) *outSy += camOffY + mobOffY;
    };

    std::vector<LabelTarget> targets;
    if (!player_.name.empty()) {
        targets.push_back({playerSerial_, playerX_, playerY_, playerZ_,
                           playerBody_, playerFacing_, playerDdx, playerDdy,
                           player_.name});
    }
    for (const auto& m : mobileCache_) {
        const i64 mobMoveMs = moveDurationMs(m.body, m.running);
        const float mobDdx = slideDelta(m.movedMs, mobMoveMs, m.x, m.prevX);
        const float mobDdy = slideDelta(m.movedMs, mobMoveMs, m.y, m.prevY);
        auto it = mobileNames_.find(m.serial);
        if (it == mobileNames_.end() || it->second.empty()) {
            int sx = 0, sy = 0;
            projectLabel(m.x, m.y, m.z, mobDdx, mobDdy, &sx, &sy);
            if (sx >= -120 && sx <= renderer_->Width() + 120 &&
                sy >= -120 && sy <= renderer_->Height() + 80) {
                const auto p = overheadNameProbeMs_.find(m.serial);
                if (p == overheadNameProbeMs_.end() || now - p->second >= kOverheadNameProbeMs) {
                    u8 pkt[8];
                    const usize n = build::MobNameQuery(pkt, m.serial);
                    Send(pkt, n, "0x98 AllNames (HUD name query)");
                    overheadNameProbeMs_[m.serial] = now;
                }
            }
            continue;
        }
        targets.push_back({m.serial, m.x, m.y, m.z, m.body, m.dir,
                           mobDdx, mobDdy, it->second});
    }

    const u16 nameColor = HudColor(135, 210, 255);
    const u16 speechColor = HudColor(255, 255, 255);
    const int lh = text_->LineHeight();
    auto textTopOffset = [&](u16 body, u8 dir) {
        int offset = kHeadOffset;
        if (anim_) {
            if (const anim::Frame* fr = anim_->Body(body, dir)) {
                int minY = fr->height;
                for (int row = 0; row < fr->height; ++row) {
                    const u16* src = &fr->px[static_cast<usize>(row) * fr->width];
                    for (int col = 0; col < fr->width; ++col) {
                        if (src[col]) {
                            minY = std::min(minY, row);
                            break;
                        }
                    }
                }
                if (minY < fr->height)
                    offset = std::clamp(fr->anchorY - minY - 4, 32, 96);
            }
        }
        return offset;
    };

    for (const auto& t : targets) {
        int sx = 0, sy = 0;
        projectLabel(t.x, t.y, t.z, t.ddx, t.ddy, &sx, &sy);
        if (sx < -120 || sx > renderer_->Width() + 120 ||
            sy < -120 || sy > renderer_->Height() + 80) {
            continue;
        }

        int y = sy - textTopOffset(t.body, t.dir) - lh;
        text_->Draw(*renderer_, t.name, sx, y, nameColor,
                    render::TextRenderer::Align::Center);

        int speechLines = 0;
        for (auto it = journal_.rbegin(); it != journal_.rend() && speechLines < 2; ++it) {
            const u32 serial = it->sourceSerial & 0x7FFFFFFFu;
            if (serial != t.serial) continue;
            if (it->ownerKind != JournalOwnerKind::Player &&
                it->ownerKind != JournalOwnerKind::Mobile) {
                continue;
            }
            if (now - it->timeMs > kOverheadMs) continue;
            const i64 age = now - it->timeMs;
            const int fade = age < 4000 ? 255 : static_cast<int>((kOverheadMs - age) * 255 / 2000);
            y -= lh;
            text_->Draw(*renderer_, it->text, sx, y,
                        ScaleHudColor(JournalColor(it->hue, speechColor), fade),
                        render::TextRenderer::Align::Center);
            ++speechLines;
        }
    }

    // Single-click item name labels, anchored to each item's live position so
    // they track if the item is moved/updated. Expired or vanished items drop.
    constexpr int kHalfTile = 22;
    const u16 itemColor = HudColor(255, 255, 255);
    for (auto li = itemLabels_.begin(); li != itemLabels_.end(); ) {
        auto obj = items_.find(li->serial);
        if (now >= li->expireMs || obj == items_.end()) {
            li = itemLabels_.erase(li);
            continue;
        }
        const Client::ItemObj& o = obj->second;
        int sx = 0, sy = 0;
        projectLabel(o.x, o.y, o.z, 0.0f, 0.0f, &sx, &sy);
        if (sx >= -120 && sx <= renderer_->Width() + 120 &&
            sy >= -120 && sy <= renderer_->Height() + 80) {
            int spriteH = 44;   // fall back to a tile if the art is unavailable
            if (art_) {
                const art::Sprite* sp = art_->Static(static_cast<u16>(o.itemId + o.gfxOffset));
                if (sp && sp->height) spriteH = sp->height;
            }
            // Sprite top sits at sy + kHalfTile - spriteH (item is bottom-anchored
            // a tile below the WorldToScreen cell point); float the label above it.
            const i64 age = now - (li->expireMs - kItemLabelMs);
            const int fade = age < kItemLabelMs - 1000
                                 ? 255
                                 : static_cast<int>((kItemLabelMs - age) * 255 / 1000);
            const int y = sy + kHalfTile - spriteH - lh;
            text_->Draw(*renderer_, li->text, sx, y,
                        ScaleHudColor(itemColor, std::clamp(fade, 0, 255)),
                        render::TextRenderer::Align::Center);
        }
        ++li;
    }
}

// Arrow keys in the render window steer the player on foot. Screen-aligned to
// the iso axes (up = north-west on screen = the (-1,-1) world step), with
// diagonals when two keys are held. Manual input cancels any active bot path
// so we don't fight the autopilot. Throttled to the normal walk cadence.
void Client::HandleManualWalk() {
    const char* keys = mfb_keystatus();
    if (!keys) return;

    constexpr int kVkLeft = 0x25, kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28;
    const bool up = keys[kVkUp], down = keys[kVkDown];
    const bool left = keys[kVkLeft], right = keys[kVkRight];
    if (!(up || down || left || right)) return;

    // Arrows map to world CARDINAL directions (Up=North): a single press is a
    // straight N/E/S/W world step (not a diagonal). Because the view is iso,
    // a cardinal world step looks diagonal on screen — that's expected. Two
    // keys combine into the 4 diagonal facings.
    int dx = 0, dy = 0;
    if (up)    { dy -= 1; }   // North
    if (down)  { dy += 1; }   // South
    if (left)  { dx -= 1; }   // West
    if (right) { dx += 1; }   // East
    if (dx == 0 && dy == 0) return;   // opposing keys cancel

    // Take over from the bot.
    if (nav_.bot.active || nav_.bot.planning || !nav_.bot.path.empty()) {
        BotAbortPath("manual walk");
        nav_.follow.active = false;
    }
    if (!nav_.movement.pending.empty()) return;   // wait for the prior step to ack

    const i64 now = NowMs();
    const u32 gap = BotMoveGapMs();
    if (lastManualMoveMs_ != 0 && now - lastManualMoveMs_ < static_cast<i64>(gap))
        return;

    const u8 dir = DeltaToDir(dx, dy);
    const bool wasStep = (dir == playerFacing_);   // turn first, then step
    const u8 seq  = NextSeq();
    const u8 wire = nav_.movement.run ? static_cast<u8>(dir | 0x80) : dir;
    u8 buf[16];
    usize n = build::MoveRequest(buf, wire, seq, 0u, cfg_.legacyMovePacket);
    if (!Send(buf, n, "0x02 Move (manual arrow)")) return;
    nav_.movement.pending.push_back({seq, dir, wasStep, now});
    nav_.movement.lastMoveSentMs = now;
    lastManualMoveMs_ = now;
    if (wasStep) BotPredictStep(dir);
    else {
        playerFacing_ = dir;
        player_.facing = dir;
        player_.running = false;
        lastStepMs_ = 0;
    }
}

// Right-click in the render window retargets the bot: invert the screen point
// to a world cell, cancel whatever path/follow is running, and goto there.
void Client::HandleWorldClick() {
    int mx, my;
    if (!mfb_poll_rclick(&mx, &my)) return;
    if (!renderer_) return;

    i32 wx = 0, wy = 0;
    renderer_->ScreenToWorld(mx, my, playerX_, playerY_, &wx, &wy);
    if (wx < 0 || wy < 0) return;

    // Cancel the current task cleanly (mirrors BotInterruptForThreat) so
    // BotStartGoto's busy-guard lets the new destination through.
    if (nav_.follow.active) BotStopFollow("retargeted by right-click");
    BotAbortPath("retargeted by right-click");
    BotResetMovement();

    LogInfo("[bot] right-click goto (%d,%d) [screen %d,%d]\n", wx, wy, mx, my);
    BotStartGoto(wx, wy);
}

// Left-click and double-click on world objects, mirroring the client's
// WorldGump click state machine. A double-click sends 0x06 (use/open) — the
// gesture that opens a container (server replies 0x24+0x3C, drawn by the HUD).
// A single click is DEFERRED until the double-click window elapses, then sends
// 0x09 (single-click look), so a double-click never also fires the single.
void Client::HandleItemClicks() {
    if (!renderer_) return;
    u8 buf[16];

    // A pending single-click commits once the double-click window passes with no
    // second press (WorldGump_OnHoverTick @0x47A910: frameCount==1 && elapsed >
    // GetDoubleClickTime()).
    if (pendingLClick_ &&
        NowMs() - pendingLClickMs_ >= static_cast<i64>(mfb_double_click_ms())) {
        if (pendingLClickSerial_) {
            // Item: show its name locally from tiledata (no packet), exactly like
            // the client. Mobile/other: send the 0x09 look request.
            if (items_.count(pendingLClickSerial_)) {
                ShowItemLabel(pendingLClickSerial_);
            } else {
                const usize n = build::SingleClick(buf, pendingLClickSerial_);
                Send(buf, n, "0x09 SingleClick (look)");
                LogInfo("[click] single-click 0x%08X\n", pendingLClickSerial_);
            }
        }
        pendingLClick_ = false;
    }

    int mx = 0, my = 0;
    const bool dbl = mfb_poll_ldblclick(&mx, &my);
    int lx = 0, ly = 0;
    const bool single = mfb_poll_lclick(&lx, &ly);

    if (dbl) {
        // Two presses inside the window: the use/open action wins; drop any
        // deferred single from the first press.
        pendingLClick_ = false;
        const u32 s = renderer_->PickObject(mx, my);
        if (s) {
            const usize n = build::DoubleClick(buf, s);
            Send(buf, n, "0x06 DoubleClick (use/open)");
            LogInfo("[click] double-click use 0x%08X\n", s);
        }
    } else if (single) {
        // First press: pick now (the draw list is current), defer the look.
        const u32 s = renderer_->PickObject(lx, ly);
        pendingLClick_ = (s != 0);
        pendingLClickSerial_ = s;
        pendingLClickMs_ = NowMs();
    }
}

// Tiledata_FormatItemName @0x4C4870: article prefix from the flag bits 0xC000
// (0x4000="a ", 0x8000="an ", 0xC000="the "), then the name with its
// %singular/plural% markup resolved to the singular form (text before '/').
std::string Client::FormatItemName(u16 graphic) const {
    if (!tileData_) return std::string();
    const tiledata::StaticTile& st = tileData_->Static(graphic);
    std::string out;
    switch (st.flags & 0xC000u) {
        case 0x4000u: out = "a ";   break;
        case 0x8000u: out = "an ";  break;
        case 0xC000u: out = "the "; break;
        default: break;
    }
    const char* p = st.name;
    for (usize n = 0; n < 20 && p[n]; ) {
        if (p[n] != '%') { out += p[n++]; continue; }
        ++n;                                            // enter %...% markup
        while (n < 20 && p[n] && p[n] != '/' && p[n] != '%') out += p[n++];  // singular
        while (n < 20 && p[n] && p[n] != '%') ++n;      // skip plural part
        if (n < 20 && p[n] == '%') ++n;                 // consume closing %
    }
    // Trim trailing spaces some names carry past the 20-byte field.
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void Client::ShowItemLabel(u32 serial) {
    auto it = items_.find(serial);
    if (it == items_.end()) return;
    std::string name = FormatItemName(static_cast<u16>(it->second.itemId + it->second.gfxOffset));
    if (name.empty()) return;
    const i64 expire = NowMs() + kItemLabelMs;
    for (auto& lbl : itemLabels_) {                     // refresh an existing label
        if (lbl.serial != serial) continue;
        lbl.text = std::move(name);
        lbl.expireMs = expire;
        return;
    }
    itemLabels_.push_back({serial, expire, std::move(name)});
}

namespace {
// 8-way screen sector (0..7) from the window centre, ported verbatim from the
// client's Cursor_GetDirectionFromCenter @0x4B9FD0. The 2:1 comparison ratios
// match the isometric view, so the arrow points the way the player would walk.
int CursorDirFromCenter(int x, int y, int w, int h) {
    const int v6 = x - w / 2;
    const int v7 = y - h / 2;
    if (v6 <= 0) {
        if (v7 <= -2 * v6) {
            if (2 * v7 <= -v6) {
                if (2 * v7 <= v6) return v7 <= 2 * v6 ? 0 : 7;
                return 6;
            }
            return 5;
        }
        return 4;
    }
    if (v7 >= -2 * v6) {
        if (2 * v7 >= -v6) {
            if (2 * v7 >= v6) return v7 >= 2 * v6 ? 4 : 3;
            return 2;
        }
        return 1;
    }
    return 0;
}
}  // namespace

// Software mouse cursor: the UO directional walk cursor matching the mouse's
// screen sector. The cursors are STATIC ART (not gumps): g_CursorArtId in the
// client is an art index 0x4000+itemId — peace-mode walk = itemId 0x206A+dir
// (war set 0x2053+dir), per MouseManager_Startup @0x4B9530. The OS cursor is
// hidden over the client area, so this is the only cursor the user sees.
void Client::DrawCursorOverlay() {
    if (!art_ || !renderer_) return;
    int mx, my;
    if (!mfb_mousepos(&mx, &my)) return;   // cursor left the window

    const int dir = CursorDirFromCenter(mx, my, renderer_->Width(), renderer_->Height());
    const u16 base = playerWarMode_ ? 0x2053u : 0x206Au;
    const art::Sprite* sp = art_->Static(static_cast<u16>(base + (dir & 7)));
    if (!sp || sp->px.empty()) return;

    // The click hotspot is encoded IN the cursor art as two pure-green marker
    // pixels (0x03E0), one on the top row giving hotspot X and one on the left
    // column giving hotspot Y — ported from Cursor_LoadSpritesAndHotspots
    // @0x4B9A90, which scans the art for value 992 (0x03E0) to fill
    // g_CursorHotspotX/YTable. The shape (and thus the hotspot) changes per
    // screen sector, so each direction points from its own marker. We draw the
    // sprite so that marker lands on the mouse, then pick at the mouse — matching
    // the client (Cursor_DrawOverlay @0x4BBF60 blits top-left at
    // g_CursorPos - hotspot; MouseManager_OnLButtonDown @0x4BA200 hit-tests at
    // g_CursorPos + hotspot = the marker).
    int hx = 0, hy = 0;
    for (int x = 0; x < sp->width; ++x)
        if ((sp->px[x] & 0x7FFF) == 0x03E0) { hx = x; break; }            // top row
    for (int y = 0; y < sp->height; ++y)
        if ((sp->px[static_cast<usize>(y) * sp->width] & 0x7FFF) == 0x03E0) { hy = y; break; }  // left col

    // Cursor art fills its background with a chroma key (0x001F blue); key it
    // out via the corner pixel, and drop the two green hotspot markers too.
    const u16 key = sp->px[0];
    renderer_->BlitSpriteKeyed(sp->px.data(), sp->width, sp->height,
                               mx - hx, my - hy, key, /*skipHotspotMarker=*/true);
}

} // namespace uo
