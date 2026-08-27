// viewer_safety -- the regression that says "the observer client cannot be
// killed by a graphic this shard's client data does not have".
//
// ClassicUO died on Revolution with IndexOutOfRangeException on every launch:
// it read a mount item's animId straight out of tiledata and indexed its
// animation tables with it. In Revolution's Renaissance-era (2.0.3) tiledata
// the unicorn mount graphic 0x3EB4 is a ship "prow", so that animId is not a
// body id at all -- it is whatever bytes happen to live at that offset.
//
// This test drives the full 32-bit input domain through uo::safegfx, the layer
// uo_viewer routes every tiledata / art / hue / anim lookup through. It runs in
// two configurations:
//
//   1. UNBOUND -- every loader pointer null. This is the "client data missing
//      or truncated" case, and is what the test always runs, on any machine,
//      with no assets. A null table must degrade to a placeholder, never to a
//      dereference.
//   2. LOADED  -- the real MULs, when UO_MUL_DIR points at them. Skipped
//      silently otherwise, so the test needs no assets in CI.
//
// The test "passes" by completing: every call below is a call that, done
// unguarded, is an out-of-range index. If any guard is missing the process
// faults here rather than in front of an operator.

#include "uo/safe_graphics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace uo;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// Every accessor, over an input domain that runs past every table bound.
void SweepEverything(safegfx::SafeTables& safe, const char* label) {
    safe.audit().Reset();
    std::printf("  [%s] tiledata sweep\n", label); std::fflush(stdout);

    // --- tiledata: the whole u16 domain plus ids past the 0x4000 tables ----
    for (u32 g = 0; g <= 0x10001u; ++g) {
        const tiledata::StaticTile& st = safe.StaticTile(g);
        const tiledata::LandTile&   lt = safe.LandTile(g);
        // Touch the fields the renderer touches, so a bad reference faults here.
        volatile u32 sink = st.flags ^ lt.flags ^ st.animId ^ lt.textureId;
        (void)sink;
        (void)safe.WornAnimFor(g);
        (void)safe.MountBodyFor(g);
        (void)safe.AnimFrameOffset(g, g & 0x7Fu);
    }

    std::printf("  hue sweep\n"); std::fflush(stdout);
    // --- hues: including the 0x8000 partial-hue flag and ids past 3000 -----
    for (u32 h = 0; h <= 0xFFFFu; ++h) (void)safe.Remap(0x7C1F, h);

    std::printf("  anim sweep\n"); std::fflush(stdout);
    // --- anim: bodies far past the 999 this era's anim.idx can address -----
    for (u32 body = 0; body <= 0x1200u; ++body) {
        for (u32 dir = 0; dir < 8; ++dir) {
            (void)safe.BodyFrameCount(body, dir, 0);
            (void)safe.BodyFrameCount(body, dir, 255);
            (void)safe.BodyFrame(body, dir, 255, 0xFFFFu);
            (void)safe.BodyFrame(body, dir, 0, 0);
        }
        (void)safe.MoveFrameCount(body, body & 1u);
    }
    // Facings past 7 must fold, not index.
    for (u32 dir = 0; dir < 64; ++dir) (void)safe.BodyFrameCount(400, dir, 0);

    std::printf("  art sweep\n"); std::fflush(stdout);
    // --- art: never null, always drawable ---------------------------------
    for (u32 g = 0; g <= 0xFFFFu; ++g) {
        const art::Sprite& s = safe.StaticArt(g);
        if (!s.width || !s.height || s.px.size() != static_cast<usize>(s.width) * s.height) {
            std::printf("FAIL[%s]: static art %u is not a drawable sprite\n", label, g);
            ++g_failures;
            break;
        }
    }
    ++g_checks;
    for (u32 g = 0; g <= 0x4001u; ++g) {
        const art::Sprite& s = safe.LandArt(g);
        if (!s.width || !s.height || s.px.size() != static_cast<usize>(s.width) * s.height) {
            std::printf("FAIL[%s]: land art %u is not a drawable sprite\n", label, g);
            ++g_failures;
            break;
        }
    }
    ++g_checks;
    // Past u16 too -- the renderer adds a graphic-increment byte to an item id.
    (void)safe.StaticArt(0x10000u);
    (void)safe.LandArt(0xFFFFFFFFu);

    std::printf("  texmap sweep\n"); std::fflush(stdout);
    // --- texmaps ----------------------------------------------------------
    for (u32 t = 0; t <= 0x10001u; ++t) (void)safe.LandTexture(t);

    const safegfx::Audit& a = safe.audit();
    std::printf("[%s] %llu lookups, %llu guarded "
                "(tile %llu art %llu body %llu action %llu mount %llu worn %llu "
                "hue %llu absent %llu)\n",
                label,
                static_cast<unsigned long long>(a.lookups),
                static_cast<unsigned long long>(a.Guarded()),
                static_cast<unsigned long long>(a.tileClamped),
                static_cast<unsigned long long>(a.artMissing),
                static_cast<unsigned long long>(a.bodyRejected),
                static_cast<unsigned long long>(a.actionClamped),
                static_cast<unsigned long long>(a.mountRejected),
                static_cast<unsigned long long>(a.wornRejected),
                static_cast<unsigned long long>(a.hueRejected),
                static_cast<unsigned long long>(a.nullTable));

    Check(a.lookups > 300000, "the sweep actually exercised the tables");
    Check(a.tileClamped > 0, "ids past the tiledata tables were clamped");
    Check(a.bodyRejected > 0, "bodies past anim.idx were refused");
}

}  // namespace

int main() {
    std::printf("viewer_safety: start\n"); std::fflush(stdout);
    // ---- the sanitizers, stated as facts ---------------------------------
    Check(safegfx::SanitizeDir(9) == 1, "facing folds into 0..7");
    Check(safegfx::SanitizeDir(0xFFFFFFFFu) == 7, "huge facing folds");
    Check(safegfx::SanitizeHue(0x8000u | 33u) == 33, "partial-hue flag is stripped");
    Check(safegfx::SanitizeHue(0x2FFFu) == 0, "hue past hues.mul is refused");
    Check(safegfx::SanitizeHue(2999) == 2999, "last real hue survives");
    Check(safegfx::SanitizeAction(400, 255) == 0, "action past the people range resets");
    Check(safegfx::SanitizeAction(400, 34) == 34, "last people action survives");
    Check(safegfx::SanitizeAction(100, 30) == 0, "people action on a monster resets");
    Check(safegfx::SanitizeWornAnim(0x18F) == 0, "worn anim below 0x190 refused");
    Check(safegfx::SanitizeWornAnim(0x3E8) == 0, "worn anim above 0x3E7 refused");
    Check(safegfx::SanitizeWornAnim(0x190) == 0x190, "first worn anim survives");
    Check(!safegfx::BodyInRange(0), "body 0 is not a body");
    Check(!safegfx::BodyInRange(1000), "body 1000 is past this era's anim.idx");
    Check(!safegfx::BodyInRange(0x3EB4), "the unicorn graphic is not a body id");
    Check(safegfx::BodyInRange(400), "human body 400 is addressable");

    // The named killer: whatever tiledata says at 0x3EB4, it must not become a
    // body index. This is the exact step ClassicUO got wrong.
    Check(safegfx::SanitizeMountBody(0x3EB4) == 0,
          "0x3EB4 as an animId is refused as a mount body");
    Check(safegfx::SanitizeMountBody(0xFFFF) == 0, "0xFFFF is refused as a mount body");
    Check(safegfx::SanitizeMountBody(200) == 200, "a real mount body survives");

    // The placeholder must be a real, visible bitmap -- not an empty sprite
    // that would silently draw nothing.
    const art::Sprite& ph = safegfx::Placeholder();
    Check(ph.width == 20 && ph.height == 20, "placeholder is 20x20");
    Check(ph.px.size() == 400, "placeholder has a full pixel buffer");
    bool anyInk = false, allOpaque = true;
    for (u16 p : ph.px) {
        if (p == safegfx::kPlaceholderInk) anyInk = true;
        if (!(p & 0x8000u)) allOpaque = false;
    }
    Check(anyInk, "placeholder actually contains its marker colour");
    Check(allOpaque, "every placeholder pixel is opaque (nothing shows through)");

    // ---- configuration 1: nothing bound ----------------------------------
    {
        safegfx::SafeTables unbound;
        unbound.Bind(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        SweepEverything(unbound, "unbound");
        Check(unbound.audit().nullTable > 0, "absent tables were detected, not dereferenced");
        // An unloaded TileDataLoader must also be safe when used directly --
        // this is the null array that used to be indexed unconditionally.
        tiledata::TileDataLoader td;   // never Load()ed
        Check(td.Static(0x3EB4).animId == 0, "unloaded tiledata Static() is safe");
        Check(td.Land(0xFFFF).flags == 0, "unloaded tiledata Land() is safe");
        Check(td.ItemAnimId(0xFFFF) == 0, "unloaded tiledata ItemAnimId() is safe");
    }

    // ---- configuration 2: the real client data, when available -----------
    const char* mulDir = std::getenv("UO_MUL_DIR");
    if (mulDir && mulDir[0]) {
        const std::string d = mulDir;
        // HEAP, not stack. AnimDataLoader alone is a 16384-entry array (~1.1 MB)
        // and AnimInfoLoader adds more -- together they blow the default 1 MB
        // thread stack before main's first instruction. (Found by this test
        // crashing with STATUS_STACK_OVERFLOW; uo::Client already heap-allocates
        // all of these, and uo_viewer now does too.)
        auto td    = std::make_unique<tiledata::TileDataLoader>();
        auto artL  = std::make_unique<art::ArtLoader>();
        auto huesL = std::make_unique<hues::HuesLoader>();
        auto animL = std::make_unique<anim::AnimLoader>();
        auto adL   = std::make_unique<animdata::AnimDataLoader>();
        auto aiL   = std::make_unique<animinfo::AnimInfoLoader>();
        auto texL  = std::make_unique<texmap::TexmapLoader>();

        artL->SetPlaceholders(true);
        const bool okTd  = td->Load((d + "/tiledata.mul").c_str());
        const bool okArt = artL->Open((d + "/artidx.mul").c_str(), (d + "/art.mul").c_str());
        huesL->Load((d + "/hues.mul").c_str());
        animL->Open((d + "/anim.idx").c_str(), (d + "/anim.mul").c_str());
        adL->Load((d + "/animdata.mul").c_str());
        aiL->Load((d + "/animinfo.mul").c_str());
        texL->Open((d + "/texidx.mul").c_str(), (d + "/texmaps.mul").c_str());

        std::printf("[loaded] UO_MUL_DIR=%s tiledata=%d art=%d\n",
                    d.c_str(), okTd ? 1 : 0, okArt ? 1 : 0);

        safegfx::SafeTables loaded;
        loaded.Bind(td.get(), artL.get(), huesL.get(), animL.get(),
                    adL.get(), aiL.get(), texL.get());
        SweepEverything(loaded, "loaded");

        if (okTd) {
            // Report what this shard's tiledata actually says about the named
            // killers, and assert none of them can become a body index.
            for (const auto& k : safegfx::kKnownHostileGraphics) {
                const u16 raw = td->ItemAnimId(k.graphic);
                const u16 mount = loaded.MountBodyFor(k.graphic);
                std::printf("  0x%04X: tiledata animId=%u -> mount body=%u  (%s)\n",
                            k.graphic, raw, mount, k.note);
                Check(mount == 0 || safegfx::BodyInRange(mount),
                      "a hostile graphic never yields an un-addressable body");
            }
        }
        if (okArt) {
            // With placeholders on, EVERY graphic must produce a bitmap.
            for (u32 g = 0; g <= 0xFFFFu; ++g) {
                const art::Sprite* s = artL->Static(static_cast<u16>(g));
                if (!s || !s->width) {
                    std::printf("FAIL: art placeholder missing for graphic %u\n", g);
                    ++g_failures;
                    break;
                }
            }
            ++g_checks;
        }
    } else {
        std::printf("[loaded] skipped (UO_MUL_DIR not set)\n");
    }

    std::printf("viewer_safety: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
