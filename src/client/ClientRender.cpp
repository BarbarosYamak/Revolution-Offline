#include "Client.h"

#include "bot/Pathfinding.h"
#include "uo/builders.h"
#include "uo/art.h"
#include "uo/tiledata.h"
#include "uo/texmap.h"
#include "uo/map.h"
#include "uo/anim.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "render/Minimap.h"
#include "render/RadarColors.h"
#include "win32/MiniFB.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace uo {

namespace {

constexpr int   kHudFontHeight = 13;
constexpr int   kSysLogLines = 8;
constexpr i64   kOverheadMs = 6000;
constexpr int   kHeadOffset = 44;
constexpr int   kStatusBarWidth = 96;
constexpr int   kStatusBarHeight = 8;
constexpr int   kStatusPanelWidth = 196;
constexpr int   kStatusPanelHeight = 66;
constexpr i64   kOverheadNameProbeMs = 10000;
constexpr i64   kStatusProbeMs = 3000;

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

    HandleManualWalk();
    HandleWorldClick();

    std::vector<render::DynItem> dyn;
    dyn.reserve(items_.size());
    for (const auto& kv : items_)
        dyn.push_back({kv.second.itemId, kv.second.x, kv.second.y,
                       kv.second.z, kv.second.gfxOffset});

    // Mobiles: nearby NPCs/players from the cache plus the local player. The
    // player carries isPlayer=true so the renderer can compute the roof cutoff.
    std::vector<render::Mob> mobs;
    mobs.reserve(mobileCache_.size() + 1);
    for (const auto& m : mobileCache_) {
        if (!m.body) continue;
        mobs.push_back({m.body, m.x, m.y, m.z, m.dir, false});
    }
    mobs.push_back({playerBody_, playerX_, playerY_, playerZ_, playerFacing_, true});

    renderer_->RenderWorld(*worldMap_, *art_, *tileData_, *texmaps_,
                           playerX_, playerY_, playerZ_, dyn.data(), dyn.size(),
                           anim_.get(), mobs.data(), mobs.size());

    // Window hotkeys: 'M' toggles the minimap, SPACE sends OpenDoor.
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
    DrawSystemLog();
    DrawOverheadText();
    DrawCursorOverlay();

    char title[64];
    std::snprintf(title, sizeof(title), "uo-client [%d,%d,%d]", playerX_, playerY_, static_cast<int>(playerZ_));
    mfb_set_title(title);
    if (!mfb_update(renderer_->Frame(), 0)) {
        // User closed the window — stop drawing, keep the bot running.
        mfb_close();
        renderWindowOpen_ = false;
        cfg_.enableRenderer = false;
        LogInfo("[render] window closed; rendering disabled\n");
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

void Client::DrawSystemLog() {
    if (!renderer_ || !text_) return;

    const u16 fallback = HudColor(220, 220, 220);
    const int lh = text_->LineHeight();
    int drawn = 0;
    for (auto it = journal_.rbegin(); it != journal_.rend() && drawn < kSysLogLines; ++it) {
        if (it->ownerKind != JournalOwnerKind::System) continue;
        const int y = renderer_->Height() - 10 - lh * (drawn + 1);
        text_->Draw(*renderer_, it->text, 10, y, JournalColor(it->hue, fallback),
                    render::TextRenderer::Align::Left);
        ++drawn;
    }
}

void Client::DrawOverheadText() {
    if (!renderer_ || !text_) return;

    struct LabelTarget { u32 serial; i32 x; i32 y; i8 z; u16 body; u8 dir; std::string name; };
    const i64 now = NowMs();
    std::vector<LabelTarget> targets;
    if (!player_.name.empty()) {
        targets.push_back({playerSerial_, playerX_, playerY_, playerZ_,
                           playerBody_, playerFacing_, player_.name});
    }
    for (const auto& m : mobileCache_) {
        auto it = mobileNames_.find(m.serial);
        if (it == mobileNames_.end() || it->second.empty()) {
            int sx = 0, sy = 0;
            renderer_->WorldToScreen(m.x, m.y, m.z, playerX_, playerY_, playerZ_, &sx, &sy);
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
        targets.push_back({m.serial, m.x, m.y, m.z, m.body, m.dir, it->second});
    }

    const u16 nameColor = HudColor(135, 210, 255);
    const u16 speechColor = HudColor(255, 255, 255);
    const int lh = text_->LineHeight();
    auto textTopOffset = [&](u16 body, u8 dir) {
        int offset = kHeadOffset;
        if (anim_) {
            if (const anim::Frame* fr = anim_->Body(body, dir)) {
                int minY = anim::Frame::kH;
                for (int row = 0; row < anim::Frame::kH; ++row) {
                    const u16* src = &fr->px[static_cast<usize>(row) * anim::Frame::kW];
                    for (int col = 0; col < anim::Frame::kW; ++col) {
                        if (src[col]) {
                            minY = std::min(minY, row);
                            break;
                        }
                    }
                }
                if (minY < anim::Frame::kH)
                    offset = std::clamp(anim::Frame::kAnchorY - minY - 4, 32, 96);
            }
        }
        return offset;
    };

    for (const auto& t : targets) {
        int sx = 0, sy = 0;
        renderer_->WorldToScreen(t.x, t.y, t.z, playerX_, playerY_, playerZ_, &sx, &sy);
        if (sx < -120 || sx > renderer_->Width() + 120 ||
            sy < -120 || sy > renderer_->Height() + 80) {
            continue;
        }

        int y = sy - textTopOffset(t.body, t.dir) - lh;
        text_->Draw(*renderer_, t.name, sx, y, nameColor, render::TextRenderer::Align::Center);

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
    if (nav_.bot.active || !nav_.bot.path.empty()) {
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
        player_.running = nav_.movement.run;
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
    const art::Sprite* sp = art_->Static(static_cast<u16>(0x206A + (dir & 7)));
    if (!sp || sp->px.empty()) return;

    // Cursor art fills its background with a chroma key (0x001F blue); key it
    // out via the corner pixel. Centre the arrow on the cursor position.
    const u16 key = sp->px[0];
    renderer_->BlitSpriteKeyed(sp->px.data(), sp->width, sp->height,
                               mx - sp->width / 2, my - sp->height / 2, key);
}

} // namespace uo
