// uo_viewer -- the Revolution Offline observer client.
//
// A native replacement for ClassicUO as the "watch the shard" window. It is a
// REAL client: it logs in over the same TCP socket, with the same packets, as
// the headless bots (uo::Client does all of that; nothing here touches the
// wire). What it adds is a window, a camera that follows the character, and --
// the reason it exists -- a data layer that cannot be killed by a graphic this
// shard's Renaissance-era client data does not have.
//
// ClassicUO died on this shard with IndexOutOfRangeException every launch: a
// unicorn mount item (0x3EB4) is a ship "prow" in Revolution's 2.0.3
// tiledata.mul, so the animId it read there was not a body id, and indexing an
// animation table with it took the process down. Here every tiledata / art /
// hue / anim lookup goes through uo/safe_graphics.h, which is total over the
// whole input domain and falls back to a visible magenta placeholder.
//
//   uo_viewer --user revolutionbot01
//
// Esc quits: it sends 0xD1 and closes the socket, so the character logs out
// cleanly instead of lingering as a link-dead body.

#include "Client.h"

#include "uo/anim.h"
#include "uo/animdata.h"
#include "uo/animinfo.h"
#include "uo/art.h"
#include "uo/hues.h"
#include "uo/log.h"
#include "uo/safe_graphics.h"
#include "uo/texmap.h"
#include "uo/tiledata.h"
#include "win32/MiniFB.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// --------------------------------------------------------------------------
// Project layout discovery. The viewer wants three things from the project
// tree: the credentials file, the client data (MULs) and the generated world
// data. Rather than hardcode an absolute path, walk up from the working
// directory looking for the marker file.
// --------------------------------------------------------------------------
constexpr const char* kCredentialsRel = "local/dev/bot-credentials.env";

bool FileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool DirHasMarker(const std::string& root) {
    return FileExists(root + "/" + kCredentialsRel);
}

std::string FindProjectRoot() {
    if (const char* env = std::getenv("REVOLUTION_ROOT")) {
        if (env[0] && DirHasMarker(env)) return env;
    }
    std::string cur = ".";
    for (int i = 0; i < 8; ++i) {
        if (DirHasMarker(cur)) return cur;
        cur += "/..";
    }
    return std::string();
}

// --------------------------------------------------------------------------
// Credentials.
//
// The file is a plain KEY=VALUE env file. We never print a password, never log
// one and never put one in a window title -- only the account NAME is ever
// shown. uo::Client already redacts the password out of packet dumps.
// --------------------------------------------------------------------------
struct EnvFile {
    std::vector<std::pair<std::string, std::string>> kv;

    bool Load(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::string line;
        int c;
        auto flush = [&] {
            // Strip CR (the file may be CRLF) and surrounding blanks.
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                     line.back() == '\t'))
                line.pop_back();
            std::size_t b = 0;
            while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
            std::string t = line.substr(b);
            line.clear();
            // observer-credentials.env carries a UTF-8 BOM.
            if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF &&
                static_cast<unsigned char>(t[1]) == 0xBB &&
                static_cast<unsigned char>(t[2]) == 0xBF)
                t.erase(0, 3);
            if (t.empty() || t[0] == '#') return;
            const std::size_t eq = t.find('=');
            if (eq == std::string::npos) return;
            kv.emplace_back(t.substr(0, eq), t.substr(eq + 1));
        };
        while ((c = std::fgetc(f)) != EOF) {
            if (c == '\n') flush();
            else line.push_back(static_cast<char>(c));
        }
        flush();
        std::fclose(f);
        return true;
    }

    const char* Get(const std::string& key) const {
        for (const auto& p : kv)
            if (p.first == key) return p.second.c_str();
        return nullptr;
    }

    bool Has(const std::string& key) const { return Get(key) != nullptr; }
};

std::string Upper(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char ch : s)
        o.push_back((ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch);
    return o;
}

bool IEquals(const std::string& a, const std::string& b) {
    return Upper(a) == Upper(b);
}

bool EndsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// --------------------------------------------------------------------------
// Credential resolution across the project's several env files.
//
// There is more than one shape in local/dev/:
//
//   bot-credentials.env       UO_BOT_USER / UO_BOT_PASS / UO_BOT_PASS_<ACCOUNT>
//   observer-credentials.env  OBSERVER_ACCOUNT / OBSERVER_PASSWORD / _HOST / _PORT
//   admin-credentials.env     ADMIN_ACCOUNT    / ADMIN_PASSWORD    / _HOST / _PORT
//
// Rather than special-case each, treat any key ending in _ACCOUNT or _USER as
// naming an account, and look for its sibling <PREFIX>_PASSWORD / <PREFIX>_PASS
// / <PREFIX>_PASS_<ACCOUNT>. That makes a new env file work with no code change.
//
// A password read here goes exactly one place: the 0x80 / 0x91 login packets.
// It is never printed, logged, or put in the window title.
// --------------------------------------------------------------------------
struct Account {
    std::string prefix;   // "", "OBSERVER", "ADMIN", "UO_BOT" ...
    std::string name;
};

// Every account the file names, in file order.
std::vector<Account> AccountsIn(const EnvFile& env) {
    std::vector<Account> out;
    for (const auto& p : env.kv) {
        std::string prefix;
        if (EndsWith(p.first, "_ACCOUNT"))   prefix = p.first.substr(0, p.first.size() - 8);
        else if (EndsWith(p.first, "_USER")) prefix = p.first.substr(0, p.first.size() - 5);
        else continue;
        if (p.second.empty()) continue;
        out.push_back({prefix, p.second});
    }
    return out;
}

// Password for `user`. Tries the sibling keys of whichever account entry names
// it, then the file-wide defaults, then the process environment.
std::string PasswordFor(const EnvFile& env, const std::string& user) {
    const std::string U = Upper(user);
    for (const Account& a : AccountsIn(env)) {
        if (!IEquals(a.name, user)) continue;
        const char* candidates[] = {nullptr, nullptr, nullptr};
        const std::string k1 = a.prefix + "_PASSWORD";
        const std::string k2 = a.prefix + "_PASS_" + U;
        const std::string k3 = a.prefix + "_PASS";
        candidates[0] = env.Get(k2);   // most specific first
        candidates[1] = env.Get(k1);
        candidates[2] = env.Get(k3);
        for (const char* c : candidates)
            if (c && c[0]) return c;
    }
    if (const char* p = env.Get("UO_BOT_PASS_" + U)) if (p[0]) return p;
    if (const char* p = env.Get("UO_BOT_PASS"))      if (p[0]) return p;
    if (const char* p = std::getenv("UO_BOT_PASS"))  if (p[0]) return p;
    return std::string();
}

// --------------------------------------------------------------------------
// Startup self-audit.
//
// This is the explicit verification the observer client is FOR. Before a single
// frame is drawn we push the entire 16-bit graphic domain, plus every action
// group and facing of a wide body range, plus deliberately absurd ids, through
// the same guards the renderer uses. If any of them could fault, this faults
// first -- at startup, deterministically, with the offending id on screen --
// rather than an hour into watching the shard.
// --------------------------------------------------------------------------
void RunGraphicsSelfAudit(uo::safegfx::SafeTables& safe, bool verbose) {
    using namespace uo;

    safe.audit().Reset();

    // 1. Every possible graphic id through every tiledata/art/animdata path.
    //    0x10000 is deliberately past u16 so the > 0xFFFF branches are taken.
    for (u32 g = 0; g <= 0x10001u; ++g) {
        (void)safe.StaticTile(g);
        (void)safe.LandTile(g);
        (void)safe.WornAnimFor(g);
        (void)safe.MountBodyFor(g);
        (void)safe.AnimFrameOffset(g, g & 0xFFu);
    }

    // 2. Every hue the wire can carry, including the 0x8000 partial-hue flag
    //    and values far past hues.mul's 3000 entries.
    for (u32 h = 0; h <= 0xFFFFu; ++h) (void)safe.Remap(0x7C1F, h);

    // 3. Bodies. Sweep well past the 999 this era's anim.idx can address, at
    //    every facing, with action groups past every kind's real range.
    for (u32 body = 0; body <= 0x1000u; ++body) {
        for (u32 dir = 0; dir < 8; ++dir) {
            (void)safe.BodyFrameCount(body, dir, 0);
            (void)safe.BodyFrameCount(body, dir, 255);
            (void)safe.BodyFrame(body, dir, 255, 0xFFFF);
        }
        (void)safe.MoveFrameCount(body, true);
    }

    // 4. The named killers, each asserted to resolve to something drawable.
    for (const auto& k : safegfx::kKnownHostileGraphics) {
        const art::Sprite& s = safe.StaticArt(k.graphic);
        const u16 mount = safe.MountBodyFor(k.graphic);
        if (verbose) {
            LogInfo("[safe] 0x%04X -> art %ux%u, mount body %u  (%s)\n",
                    k.graphic, s.width, s.height, mount, k.note);
        }
        if (mount != 0 && !safegfx::BodyInRange(mount)) {
            LogError("[safe] FATAL: 0x%04X produced un-drawable mount body %u\n",
                     k.graphic, mount);
            std::abort();
        }
    }

    // 5. Art for every id, so no index into art.mul/artidx.mul is unguarded.
    //    Land art is only 0..0x3FFF; statics span the whole u16 domain.
    for (u32 g = 0; g <= 0xFFFFu; ++g) {
        const art::Sprite& s = safe.StaticArt(g);
        if (!s.width || s.px.empty()) {
            LogError("[safe] FATAL: static art %u returned an empty sprite\n", g);
            std::abort();
        }
    }
    for (u32 g = 0; g <= 0x4001u; ++g) {
        const art::Sprite& s = safe.LandArt(g);
        if (!s.width || s.px.empty()) {
            LogError("[safe] FATAL: land art %u returned an empty sprite\n", g);
            std::abort();
        }
    }

    const safegfx::Audit& a = safe.audit();
    uo::LogInfo("[safe] self-audit passed: %llu lookups, %llu guarded "
                "(tile %llu, art %llu, body %llu, action %llu, mount %llu, "
                "worn %llu, hue %llu, absent-table %llu)\n",
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
}

void PrintUsage() {
    std::printf(
        "uo_viewer - Revolution Offline observer client\n"
        "\n"
        "Watches the shard through a real logged-in character. Same protocol as\n"
        "the headless bots; adds a window, a following camera, and a data layer\n"
        "that cannot be killed by out-of-era graphics.\n"
        "\n"
        "Usage: uo_viewer [options]\n"
        "\n"
        "Connection:\n"
        "  --host <h>        server host (default 127.0.0.1)\n"
        "  --port <p>        server port (default 2593)\n"
        "  --user <u>        account name; default: the first account the\n"
        "                    credentials file names\n"
        "  --char-name <s>   character to play (default: the account's slot 0)\n"
        "  --create-char     create the character if the account has none\n"
        "  --version <v>     client version reported in 0xBD (default 2.0.3)\n"
        "\n"
        "Data:\n"
        "  --root <dir>      project root (default: walk up for %s)\n"
        "  --mul-dir <dir>   client data directory (default $UO_MUL_DIR, else\n"
        "                    <root>/local/revolution-client)\n"
        "  --creds <file>    credentials env file (default <root>/%s).\n"
        "                    Also accepts local\\dev\\observer-credentials.env and\n"
        "                    admin-credentials.env: any <PREFIX>_ACCOUNT /\n"
        "                    <PREFIX>_USER key names an account and its\n"
        "                    <PREFIX>_PASSWORD / _HOST / _PORT are used.\n"
        "\n"
        "Window:\n"
        "  --width <n>       framebuffer width  (default 1024)\n"
        "  --height <n>      framebuffer height (default 768)\n"
        "  --scale <n>       integer upscale    (default 1)\n"
        "  --no-placeholders do NOT draw magenta markers for graphics this\n"
        "                    client data cannot resolve (default: draw them)\n"
        "\n"
        "Diagnostics:\n"
        "  --audit-only      run the graphics self-audit and exit; no network\n"
        "  --skip-audit      skip the startup self-audit\n"
        "  --dump-png <f>    write the frame to <f> once in world, then keep going\n"
        "  --dump-after <s>  seconds in world before the dump (default 6)\n"
        "  --quit-after <s>  log out and exit after <s> seconds (0 = never)\n"
        "  --log <file>      session log path (default uo-viewer.log)\n"
        "  -h, --help        this text\n"
        "\n"
        "Esc quits: sends 0xD1 and closes the socket, so the character logs out.\n",
        kCredentialsRel, kCredentialsRel);
}

bool ArgIs(const char* a, const char* n) { return std::strcmp(a, n) == 0; }

}  // namespace

int main(int argc, char** argv) {
    std::string root, mulDir, credsPath, user, charName, host = "127.0.0.1";
    std::string version = "2.0.3";
    std::string logFile = "uo-viewer.log";
    std::string dumpPng;
    int port = 2593;
    int width = 1024, height = 768, scale = 1;
    int dumpAfterSec = 6;
    int quitAfterSec = 0;
    bool placeholders = true;
    bool auditOnly = false, skipAudit = false;
    bool createChar = false;
    bool hostGiven = false, portGiven = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (ArgIs(a, "-h") || ArgIs(a, "--help")) { PrintUsage(); return 0; }
        else if (ArgIs(a, "--no-placeholders")) placeholders = false;
        else if (ArgIs(a, "--audit-only"))      auditOnly = true;
        else if (ArgIs(a, "--skip-audit"))      skipAudit = true;
        else if (ArgIs(a, "--create-char"))     createChar = true;
        else if (!next) { std::fprintf(stderr, "missing value for %s\n", a); return 64; }
        else if (ArgIs(a, "--host"))       { host = next; hostGiven = true; ++i; }
        else if (ArgIs(a, "--port"))       { port = std::atoi(next); portGiven = true; ++i; }
        else if (ArgIs(a, "--user"))       { user = next; ++i; }
        else if (ArgIs(a, "--char-name"))  { charName = next; ++i; }
        else if (ArgIs(a, "--version"))    { version = next; ++i; }
        else if (ArgIs(a, "--root"))       { root = next; ++i; }
        else if (ArgIs(a, "--mul-dir"))    { mulDir = next; ++i; }
        else if (ArgIs(a, "--creds"))      { credsPath = next; ++i; }
        else if (ArgIs(a, "--width"))      { width = std::atoi(next); ++i; }
        else if (ArgIs(a, "--height"))     { height = std::atoi(next); ++i; }
        else if (ArgIs(a, "--scale"))      { scale = std::atoi(next); ++i; }
        else if (ArgIs(a, "--dump-png"))   { dumpPng = next; ++i; }
        else if (ArgIs(a, "--dump-after")) { dumpAfterSec = std::atoi(next); ++i; }
        else if (ArgIs(a, "--quit-after")) { quitAfterSec = std::atoi(next); ++i; }
        else if (ArgIs(a, "--log"))        { logFile = next; ++i; }
        else { std::fprintf(stderr, "unknown option: %s\n", a); PrintUsage(); return 64; }
    }

    if (root.empty()) root = FindProjectRoot();
    if (root.empty() && credsPath.empty() && mulDir.empty()) {
        std::fprintf(stderr,
            "error: cannot find the project root (looked for %s above the\n"
            "       working directory). Pass --root, or --creds and --mul-dir.\n",
            kCredentialsRel);
        return 64;
    }
    if (credsPath.empty() && !root.empty()) credsPath = root + "/" + kCredentialsRel;
    if (mulDir.empty()) {
        if (const char* env = std::getenv("UO_MUL_DIR")) mulDir = env;
    }
    if (mulDir.empty() && !root.empty()) mulDir = root + "/local/revolution-client";

    // ---- client data ------------------------------------------------------
    // The MULs are loaded here, up front, purely so the self-audit can hammer
    // the REAL tables (not just the empty-loader paths) before we open a
    // socket. uo::Client opens its own copies for the renderer.
    //
    // HEAP, not stack: AnimDataLoader carries a 16384-entry array (~1.1 MB)
    // and would overflow the default 1 MB thread stack on its own, before
    // main's first instruction. uo::Client heap-allocates these for the same
    // reason.
    auto tdp       = std::make_unique<uo::tiledata::TileDataLoader>();
    auto artp      = std::make_unique<uo::art::ArtLoader>();
    auto huesp     = std::make_unique<uo::hues::HuesLoader>();
    auto animp     = std::make_unique<uo::anim::AnimLoader>();
    auto animDatap = std::make_unique<uo::animdata::AnimDataLoader>();
    auto animInfop = std::make_unique<uo::animinfo::AnimInfoLoader>();
    auto texp      = std::make_unique<uo::texmap::TexmapLoader>();
    auto& td       = *tdp;
    auto& art      = *artp;
    auto& hues     = *huesp;
    auto& anim     = *animp;
    auto& animData = *animDatap;
    auto& animInfo = *animInfop;
    auto& tex      = *texp;

    const std::string pTiledata = mulDir + "/tiledata.mul";
    const std::string pArtIdx   = mulDir + "/artidx.mul";
    const std::string pArt      = mulDir + "/art.mul";
    const std::string pHues     = mulDir + "/hues.mul";
    const std::string pAnimIdx  = mulDir + "/anim.idx";
    const std::string pAnim     = mulDir + "/anim.mul";
    const std::string pAnimData = mulDir + "/animdata.mul";
    const std::string pAnimInfo = mulDir + "/animinfo.mul";
    const std::string pTexIdx   = mulDir + "/texidx.mul";
    const std::string pTex      = mulDir + "/texmaps.mul";

    art.SetPlaceholders(placeholders);
    const bool okTd   = td.Load(pTiledata.c_str());
    const bool okArt  = art.Open(pArtIdx.c_str(), pArt.c_str());
    const bool okHue  = hues.Load(pHues.c_str());
    const bool okAnim = anim.Open(pAnimIdx.c_str(), pAnim.c_str());
    const bool okAd   = animData.Load(pAnimData.c_str());
    const bool okAi   = animInfo.Load(pAnimInfo.c_str());
    const bool okTex  = tex.Open(pTexIdx.c_str(), pTex.c_str());
    uo::LogInfo("[data] %s  tiledata=%d art=%d hues=%d anim=%d animdata=%d "
                "animinfo=%d texmaps=%d\n",
                mulDir.c_str(), okTd, okArt, okHue, okAnim, okAd, okAi, okTex);

    uo::safegfx::SafeTables safe;
    safe.Bind(&td, &art, &hues, &anim, &animData, &animInfo, &tex);

    if (!skipAudit) {
        uo::LogInfo("[safe] running graphics self-audit "
                    "(this is what ClassicUO could not survive)...\n");
        RunGraphicsSelfAudit(safe, /*verbose=*/true);

        // Second pass with NOTHING bound: a missing or truncated client data
        // set must degrade to placeholders, not to a null dereference.
        uo::safegfx::SafeTables empty;
        empty.Bind(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        RunGraphicsSelfAudit(empty, /*verbose=*/false);
        uo::LogInfo("[safe] unbound-table pass passed too\n");
    }
    if (auditOnly) return 0;

    // ---- credentials ------------------------------------------------------
    EnvFile env;
    if (!credsPath.empty() && !env.Load(credsPath)) {
        std::fprintf(stderr, "error: cannot read credentials file '%s'\n",
                     credsPath.c_str());
        return 66;
    }
    // No --user: take the first account the file names. A single-account file
    // (observer-credentials.env, admin-credentials.env) then needs no --user at
    // all -- pointing --creds at it is enough.
    const std::vector<Account> accounts = AccountsIn(env);
    if (user.empty() && !accounts.empty()) user = accounts.front().name;
    if (user.empty()) {
        std::fprintf(stderr,
            "error: no account. Pass --user, or give a credentials file with a\n"
            "       <PREFIX>_ACCOUNT / <PREFIX>_USER entry (%s).\n",
            credsPath.c_str());
        return 64;
    }

    const std::string pass = PasswordFor(env, user);
    if (pass.empty()) {
        std::fprintf(stderr,
            "error: no password for account '%s' in %s.\n"
            "       Looked for <PREFIX>_PASSWORD / <PREFIX>_PASS_%s next to its\n"
            "       account entry, then UO_BOT_PASS_%s, then UO_BOT_PASS.\n",
            user.c_str(), credsPath.c_str(), Upper(user).c_str(),
            Upper(user).c_str());
        return 64;
    }

    // Host/port may also come from the credentials file (OBSERVER_HOST etc.),
    // unless the command line already overrode them.
    for (const Account& a : accounts) {
        if (!IEquals(a.name, user)) continue;
        if (!hostGiven) {
            if (const char* h = env.Get(a.prefix + "_HOST")) if (h[0]) host = h;
        }
        if (!portGiven) {
            if (const char* p = env.Get(a.prefix + "_PORT")) if (p[0]) port = std::atoi(p);
        }
        break;
    }

    // The password is now in memory and goes exactly one place: the 0x80/0x91
    // login packets. It is never printed, logged or titled.
    uo::LogInfo("[viewer] account '%s' @ %s:%d (password loaded from %s, %zu chars)\n",
                user.c_str(), host.c_str(), port, credsPath.c_str(), pass.size());

    // ---- session ----------------------------------------------------------
    std::vector<std::string> owned;
    auto mul = [&](const char* name) -> const char* {
        owned.push_back(mulDir + "/" + name);
        return owned.back().c_str();
    };

    uo::Client::Config cfg{};
    cfg.loginHost = host.c_str();
    cfg.loginPort = static_cast<uo::u16>(port);
    cfg.username  = user.c_str();
    cfg.password  = pass.c_str();
    cfg.version   = version.c_str();
    cfg.logFile   = logFile.c_str();
    cfg.plaintextSeed = 0x7F000001u;   // no encryption; UseNoCrypt=1
    cfg.sendSeed  = true;
    cfg.legacyMovePacket = false;
    cfg.enableKeepalive  = true;
    cfg.acceptDoors      = true;
    cfg.defaultGait      = uo::sphere::Gait::Auto;
    cfg.sessionTag       = "viewer";
    cfg.charSlot         = 0;
    if (!charName.empty()) cfg.charName = charName.c_str();
    // First run on a brand-new account (the Observer account ships with zero
    // characters). 0x00 CreateCharacter is an ordinary player packet -- Sphere
    // clamps the stats and skills and runs its own f_onchar_create, so this
    // grants nothing. It also removes the manual GUI step that ClassicUO forced
    // (docs/OBSERVER_CLIENT.md section 8).
    cfg.createCharIfMissing = createChar;

    cfg.tiledataPath = mul("tiledata.mul");
    cfg.mapPath      = mul("map0.mul");
    cfg.staidxPath   = mul("staidx0.mul");
    cfg.staticsPath  = mul("statics0.mul");
    cfg.verdataPath  = mul("verdata.mul");
    cfg.artIdxPath   = mul("artidx.mul");
    cfg.artPath      = mul("art.mul");
    cfg.texIdxPath   = mul("texidx.mul");
    cfg.texPath      = mul("texmaps.mul");
    cfg.animIdxPath  = mul("anim.idx");
    cfg.animPath     = mul("anim.mul");
    cfg.animDataPath = mul("animdata.mul");
    cfg.animInfoPath = mul("animinfo.mul");
    cfg.huesPath     = mul("hues.mul");
    cfg.radarcolPath = mul("radarcol.mul");

    std::string atlasPath, navgridPath;
    if (!root.empty()) {
        atlasPath   = root + "/bot/uo-client/data/revolution_atlas.txt";
        navgridPath = root + "/bot/uo-client/data/revolution_navgrid.bin";
        if (FileExists(atlasPath))   cfg.atlasPath   = atlasPath.c_str();
        if (FileExists(navgridPath)) cfg.navgridPath = navgridPath.c_str();
    }

    cfg.enableRenderer     = true;
    cfg.renderPlaceholders = placeholders;
    cfg.renderWidth  = width  > 0 ? width  : 1024;
    cfg.renderHeight = height > 0 ? height : 768;
    cfg.renderScale  = scale  > 0 ? scale  : 1;

    if (!uo::net::Socket::WSAStart()) {
        std::fprintf(stderr, "error: winsock init failed\n");
        return 1;
    }

    int rc = 0;
    {
        uo::Client client(cfg);
        if (!client.Start()) {
            rc = client.ExitCode() ? client.ExitCode() : 2;
        } else {
            uo::LogInfo("[viewer] connected; Esc quits (logs out cleanly)\n");

            const auto t0 = std::chrono::steady_clock::now();
            auto elapsedSec = [&] {
                return std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - t0).count();
            };
            long long inWorldAt = -1;
            bool dumped = dumpPng.empty();
            bool quitting = false;
            bool escLatch = false;

            while (!client.Finished()) {
                client.Tick(16);   // ~60 Hz; RenderTick runs inside

                if (client.IsInWorld() && inWorldAt < 0) {
                    inWorldAt = elapsedSec();
                    uo::LogInfo("[viewer] in world at %d,%d,%d\n",
                                client.PlayerX(), client.PlayerY(),
                                static_cast<int>(client.PlayerZ()));
                }

                // Esc -> clean logout. Only meaningful while the window is up;
                // mfb_keystatus is null-safe either way.
                if (!quitting && client.RenderWindowOpen()) {
                    const char* keys = mfb_keystatus();
                    const bool esc = keys && keys[0x1B] != 0;   // VK_ESCAPE
                    if (esc && !escLatch) {
                        uo::LogInfo("[viewer] Esc -> logging out\n");
                        client.ActionLogout();
                        quitting = true;
                    }
                    escLatch = esc;
                }

                // Prove a frame was actually produced.
                if (!dumped && inWorldAt >= 0 &&
                    elapsedSec() - inWorldAt >= dumpAfterSec &&
                    client.RenderFrame()) {
                    if (client.SaveRenderFramePng(dumpPng.c_str())) {
                        uo::LogInfo("[viewer] frame dumped to %s (%dx%d)\n",
                                    dumpPng.c_str(), client.RenderWidth(),
                                    client.RenderHeight());
                    } else {
                        uo::LogWarn("[viewer] could not write %s\n", dumpPng.c_str());
                    }
                    dumped = true;
                }

                if (!quitting && quitAfterSec > 0 && elapsedSec() >= quitAfterSec) {
                    uo::LogInfo("[viewer] --quit-after %ds -> logging out\n",
                                quitAfterSec);
                    client.ActionLogout();
                    quitting = true;
                }
            }
            rc = client.ExitCode();
        }
    }

    uo::net::Socket::WSACleanupOnce();
    uo::LogInfo("[viewer] exit %d\n", rc);
    return rc;
}
