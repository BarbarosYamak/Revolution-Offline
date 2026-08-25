#include "Client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

void PrintUsage() {
    std::printf(
        "uo-client - headless UO protocol client\n"
        "\n"
        "Usage: uo_client [options]                       (one session)\n"
        "       uo_client [common options] --session <spec> [--session <spec>]...\n"
        "\n"
        "Connection:\n"
        "  --host <h>         server host (default 127.0.0.1)\n"
        "  --port <p>         server port (default 2593)\n"
        "  --user <u>         account name   (or env UO_BOT_USER)\n"
        "  --pass <p>         account password (or env UO_BOT_PASS)\n"
        "  --version <v>      client version reported to 0xBD (default 2.0.7)\n"
        "  --seed <hex>       plaintext seed dword (default 0x7F000001)\n"
        "\n"
        "Character:\n"
        "  --char-slot <n>    character slot to play (default 0)\n"
        "  --char-name <s>    play the slot with this name (overrides --char-slot)\n"
        "  --create-char      create the character if the account has none\n"
        "\n"
        "Behaviour:\n"
        "  --scenario <file>  run a scripted action list once in world\n"
        "  --run              use the running cadence (default: walk)\n"
        "  --no-keepalive     do not send the 0x73 keepalive\n"
        "  --keepalive <ms>   keepalive interval (default 20000)\n"
        "  --stdin            read console commands (one session only)\n"
        "\n"
        "Diagnostics:\n"
        "  --log <file>       session log path (default uo-client.log)\n"
        "  --log-packets      write per-packet hex lines to the log file\n"
        "  --tag <s>          session id shown in logs (default: the account name)\n"
        "  --headless         no render window (default)\n"
        "  --render           open the world window (single session only)\n"
        "  --mul-dir <dir>    directory holding the MUL files (pathfinding/render)\n"
        "  -h, --help         this text\n"
        "\n"
        "Multiple sessions in one process:\n"
        "  --session user:pass:char[:scenario[:tag]]\n"
        "     Repeatable. Each session gets its own socket, parser, world state,\n"
        "     movement sequence, keepalive and log file. Fields left empty fall\n"
        "     back to the common options above. Example:\n"
        "       uo_client --host 127.0.0.1 --mul-dir C:/uo --create-char \\\n"
        "                 --session revolutionbot01::RevolutionBot01:walk.txt \\\n"
        "                 --session revolutionbot02::RevolutionBot02:walk.txt\n"
        "\n"
        "Passwords are read from UO_BOT_PASS (and UO_BOT_PASS_<TAG>) when not\n"
        "given, and are never written to the packet log.\n");
}

bool ArgIs(const char* a, const char* name) { return std::strcmp(a, name) == 0; }

// One session's paths and strings, owned for the process lifetime so the
// Config's raw pointers stay valid. (The M1 build used a rotating static
// buffer for MUL paths, which aliased as soon as a second config existed --
// M1.5 state audit item 10.)
struct SessionStrings {
    std::string user, pass, charName, scenario, tag, logFile;
    std::vector<std::string> mulPaths;

    const char* Mul(const std::string& dir, const char* name) {
        if (dir.empty()) return nullptr;
        mulPaths.push_back(dir + "/" + name);
        return mulPaths.back().c_str();
    }
};

// "user:pass:char:scenario:tag" -- empty fields inherit the common options.
std::vector<std::string> SplitSpec(const char* spec) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = spec; ; ++p) {
        if (*p == ':' || *p == '\0') {
            out.push_back(cur);
            cur.clear();
            if (*p == '\0') break;
        } else {
            cur.push_back(*p);
        }
    }
    return out;
}

// Per-session password override: UO_BOT_PASS_<TAG>, upper-cased.
const char* PasswordForTag(const std::string& tag) {
    if (tag.empty()) return nullptr;
    std::string key = "UO_BOT_PASS_";
    for (char c : tag) {
        key.push_back(static_cast<char>(
            (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c));
    }
    return std::getenv(key.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    // ---- common defaults: a local Sphere/Source-X development shard -------
    uo::Client::Config base{};
    base.loginHost        = "127.0.0.1";
    base.loginPort        = 2593;
    base.username         = nullptr;
    base.password         = nullptr;
    base.version          = "2.0.7";
    base.logFile          = "uo-client.log";
    // Any non-zero dword is accepted; Source-X only rejects seed 0 and treats
    // 0xFFFFFFFF as a UO:KR probe (src/network/CNetworkInput.cpp:636-655).
    base.plaintextSeed    = 0x7F000001u;
    base.gamePortOverride = 0;
    base.gameHostOverride = nullptr;
    base.sendSeed         = true;
    base.legacyMovePacket = false;
    base.enableKeepalive  = true;
    base.acceptDoors      = true;
    base.enableRenderer   = false;
    base.charSlot         = 0;
    base.charName         = nullptr;
    base.createCharIfMissing = false;
    base.runWhenWalking   = false;
    base.sessionTag       = nullptr;
    base.enableStdin      = false;
    base.scenarioPath     = nullptr;
    base.logPackets       = false;
    base.keepaliveIntervalMs = 0;
    base.renderWidth      = 960;
    base.renderHeight     = 540;
    base.renderScale      = 2;

    std::string mulDir;
    std::string baseLog = "uo-client.log";
    std::vector<std::string> sessionSpecs;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (ArgIs(a, "-h") || ArgIs(a, "--help")) { PrintUsage(); return 0; }
        else if (ArgIs(a, "--headless"))     base.enableRenderer = false;
        else if (ArgIs(a, "--render"))       base.enableRenderer = true;
        else if (ArgIs(a, "--run"))          base.runWhenWalking = true;
        else if (ArgIs(a, "--no-keepalive")) base.enableKeepalive = false;
        else if (ArgIs(a, "--log-packets"))  base.logPackets = true;
        else if (ArgIs(a, "--create-char"))  base.createCharIfMissing = true;
        else if (ArgIs(a, "--stdin"))        base.enableStdin = true;
        else if (!next) {
            std::fprintf(stderr, "missing value for %s\n", a);
            return 64;
        }
        else if (ArgIs(a, "--host"))      { base.loginHost = next; ++i; }
        else if (ArgIs(a, "--port"))      { base.loginPort = static_cast<uo::u16>(std::atoi(next)); ++i; }
        else if (ArgIs(a, "--user"))      { base.username = next; ++i; }
        else if (ArgIs(a, "--pass"))      { base.password = next; ++i; }
        else if (ArgIs(a, "--version"))   { base.version = next; ++i; }
        else if (ArgIs(a, "--seed"))      { base.plaintextSeed = static_cast<uo::u32>(std::strtoul(next, nullptr, 0)); ++i; }
        else if (ArgIs(a, "--char-slot")) { base.charSlot = std::atoi(next); ++i; }
        else if (ArgIs(a, "--char-name")) { base.charName = next; ++i; }
        else if (ArgIs(a, "--scenario"))  { base.scenarioPath = next; ++i; }
        else if (ArgIs(a, "--keepalive")) { base.keepaliveIntervalMs = static_cast<uo::u32>(std::atoi(next)); ++i; }
        else if (ArgIs(a, "--tag"))       { base.sessionTag = next; ++i; }
        else if (ArgIs(a, "--log"))       { baseLog = next; ++i; }
        else if (ArgIs(a, "--mul-dir"))   { mulDir = next; ++i; }
        else if (ArgIs(a, "--session"))   { sessionSpecs.push_back(next); ++i; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", a);
            PrintUsage();
            return 64;
        }
    }

    // A bare invocation is just the one-session case of the same code path.
    if (sessionSpecs.empty()) sessionSpecs.push_back("");

    if (sessionSpecs.size() > 1 && base.enableRenderer) {
        std::fprintf(stderr,
            "error: --render supports a single session (one window per process).\n");
        return 64;
    }
    if (sessionSpecs.size() > 1 && base.enableStdin) {
        std::fprintf(stderr,
            "error: --stdin supports a single session (one reader per process).\n");
        return 64;
    }

    // Winsock is process-wide: start it once here, tear it down once at exit.
    // No session may do either (M1.5 state audit item 8).
    if (!uo::net::Socket::WSAStart()) {
        std::fprintf(stderr, "error: winsock init failed\n");
        return 1;
    }

    std::vector<std::unique_ptr<uo::Client>> clients;
    std::vector<std::unique_ptr<SessionStrings>> owned;

    for (std::size_t si = 0; si < sessionSpecs.size(); ++si) {
        const std::vector<std::string> f = SplitSpec(sessionSpecs[si].c_str());
        auto st = std::make_unique<SessionStrings>();

        auto field = [&](std::size_t idx) -> std::string {
            return (idx < f.size()) ? f[idx] : std::string();
        };

        st->user     = field(0);
        st->pass     = field(1);
        st->charName = field(2);
        st->scenario = field(3);
        st->tag      = field(4);

        uo::Client::Config cfg = base;

        if (!st->user.empty()) cfg.username = st->user.c_str();
        if (!st->charName.empty()) cfg.charName = st->charName.c_str();
        if (!st->scenario.empty()) cfg.scenarioPath = st->scenario.c_str();

        // Tag defaults to the account name, so logs are attributable even
        // when --tag is not given.
        if (st->tag.empty()) {
            if (!st->user.empty()) st->tag = st->user;
            else if (base.sessionTag) st->tag = base.sessionTag;
            else if (base.username) st->tag = base.username;
        }
        if (!st->tag.empty()) cfg.sessionTag = st->tag.c_str();

        // Credentials: session spec, then per-session env, then common.
        if (!st->pass.empty()) {
            cfg.password = st->pass.c_str();
        } else {
            const char* envPass = PasswordForTag(st->tag);
            if (!envPass && cfg.username && (!base.username ||
                std::strcmp(cfg.username, base.username) != 0)) {
                // A session-specific account with no password of its own.
                envPass = nullptr;
            }
            if (envPass) { st->pass = envPass; cfg.password = st->pass.c_str(); }
        }
        if (!cfg.username || !cfg.username[0]) cfg.username = std::getenv("UO_BOT_USER");
        if (!cfg.password || !cfg.password[0]) cfg.password = std::getenv("UO_BOT_PASS");

        if (!cfg.username || !cfg.username[0] || !cfg.password || !cfg.password[0]) {
            std::fprintf(stderr,
                "error: session %zu has no credentials. Use --user/--pass, "
                "--session user:pass:..., or UO_BOT_USER / UO_BOT_PASS "
                "(per session: UO_BOT_PASS_<TAG>).\n", si);
            return 64;
        }

        // One log file per session so two sessions never interleave.
        if (sessionSpecs.size() == 1) {
            st->logFile = baseLog;
        } else {
            const std::string stem =
                baseLog.size() > 4 && baseLog.substr(baseLog.size() - 4) == ".log"
                    ? baseLog.substr(0, baseLog.size() - 4)
                    : baseLog;
            st->logFile = stem + "." + (st->tag.empty()
                              ? ("s" + std::to_string(si)) : st->tag) + ".log";
        }
        cfg.logFile = st->logFile.c_str();

        // MUL paths are only needed for pathfinding and the renderer. Each
        // session owns its own strings.
        cfg.tiledataPath = st->Mul(mulDir, "tiledata.mul");
        cfg.mapPath      = st->Mul(mulDir, "map0.mul");
        cfg.staidxPath   = st->Mul(mulDir, "staidx0.mul");
        cfg.staticsPath  = st->Mul(mulDir, "statics0.mul");
        cfg.verdataPath  = st->Mul(mulDir, "verdata.mul");
        cfg.artIdxPath   = st->Mul(mulDir, "artidx.mul");
        cfg.artPath      = st->Mul(mulDir, "art.mul");
        cfg.texIdxPath   = st->Mul(mulDir, "texidx.mul");
        cfg.texPath      = st->Mul(mulDir, "texmaps.mul");
        cfg.animIdxPath  = st->Mul(mulDir, "anim.idx");
        cfg.animPath     = st->Mul(mulDir, "anim.mul");
        cfg.animDataPath = st->Mul(mulDir, "animdata.mul");
        cfg.animInfoPath = st->Mul(mulDir, "animinfo.mul");
        cfg.huesPath     = st->Mul(mulDir, "hues.mul");
        cfg.radarcolPath = st->Mul(mulDir, "radarcol.mul");

        uo::LogInfo("session %zu: %s:%u user='%s' char=%s tag='%s'%s\n",
                    si, cfg.loginHost, cfg.loginPort, cfg.username,
                    cfg.charName ? cfg.charName : "<by slot>",
                    cfg.sessionTag ? cfg.sessionTag : "",
                    cfg.scenarioPath ? " (scenario)" : "");

        owned.push_back(std::move(st));
        clients.push_back(std::make_unique<uo::Client>(cfg));
    }

    // ---- start every session, then drive them round-robin in this thread --
    int failed = 0;
    for (auto& c : clients) {
        if (!c->Start()) ++failed;
    }

    // Each session waits a slice of the tick budget on its own socket, so N
    // sessions still poll at a sane rate. Sessions never share buffers, so
    // the only thing they share here is the thread.
    const int slice = (clients.size() > 1)
                          ? (50 / static_cast<int>(clients.size()))
                          : 50;
    for (;;) {
        bool anyLive = false;
        for (auto& c : clients) {
            if (c->Finished()) continue;
            c->Tick(slice < 1 ? 1 : slice);
            if (!c->Finished()) anyLive = true;
        }
        if (!anyLive) break;
    }

    int rc = 0;
    for (auto& c : clients) {
        const int code = c->ExitCode();
        if (code != 0) {
            uo::LogWarn("session '%s' exited with code %d\n",
                        c->SessionTag(), code);
            rc = code;
        }
    }
    if (failed && rc == 0) rc = 2;

    // Sessions are destroyed before winsock goes away.
    clients.clear();
    uo::net::Socket::WSACleanupOnce();
    return rc;
}
