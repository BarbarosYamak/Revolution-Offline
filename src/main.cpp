#include "Client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void PrintUsage() {
    std::printf(
        "uo-client - headless UO protocol client\n"
        "\n"
        "Usage: uo_client [options]\n"
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
        "\n"
        "Diagnostics:\n"
        "  --log <file>       session log path (default uo-client.log)\n"
        "  --log-packets      write per-packet hex lines to the log file\n"
        "  --headless         no render window\n"
        "  --mul-dir <dir>    directory holding the MUL files (pathfinding/render)\n"
        "  -h, --help         this text\n"
        "\n"
        "Passwords are read from UO_BOT_PASS when --pass is omitted and are\n"
        "never written to the packet log.\n");
}

// Join dir + name into a static-lifetime buffer (one per call site).
const char* MulPath(const char* dir, const char* name) {
    if (!dir || !dir[0]) return nullptr;
    static char pool[16][512];
    static int next = 0;
    char* out = pool[next % 16];
    ++next;
    std::snprintf(out, sizeof(pool[0]), "%s/%s", dir, name);
    return out;
}

bool ArgIs(const char* a, const char* name) { return std::strcmp(a, name) == 0; }

}  // namespace

int main(int argc, char** argv) {
    uo::Client::Config cfg{};

    // --- defaults: a local Sphere/Source-X development shard ---------------
    cfg.loginHost        = "127.0.0.1";
    cfg.loginPort        = 2593;
    cfg.username         = nullptr;
    cfg.password         = nullptr;
    cfg.version          = "2.0.7";
    cfg.logFile          = "uo-client.log";
    // Any non-zero dword is accepted; Source-X only rejects seed 0 and treats
    // 0xFFFFFFFF as a UO:KR probe (src/network/CNetworkInput.cpp:636-655).
    cfg.plaintextSeed    = 0x7F000001u;
    cfg.gamePortOverride = 0;
    cfg.gameHostOverride = nullptr;
    cfg.sendSeed         = true;
    cfg.legacyMovePacket = false;   // 7-byte 0x02 (Source-X reads dir+seq, ignores the key)
    cfg.enableKeepalive  = true;    // Source-X drops silent sockets (DeadSocketTime)
    cfg.acceptDoors      = true;
    cfg.enableRenderer   = false;   // headless by default; --render to enable
    cfg.charSlot         = 0;
    cfg.charName         = nullptr;
    cfg.createCharIfMissing = false;
    cfg.runWhenWalking   = false;   // walking never trips the walk-buffer check
    cfg.scenarioPath     = nullptr;
    cfg.logPackets       = false;
    cfg.keepaliveIntervalMs = 0;    // 0 = built-in default
    cfg.renderWidth      = 960;
    cfg.renderHeight     = 540;
    cfg.renderScale      = 2;

    const char* mulDir = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (ArgIs(a, "-h") || ArgIs(a, "--help")) { PrintUsage(); return 0; }
        else if (ArgIs(a, "--headless"))     cfg.enableRenderer = false;
        else if (ArgIs(a, "--render"))       cfg.enableRenderer = true;
        else if (ArgIs(a, "--run"))          cfg.runWhenWalking = true;
        else if (ArgIs(a, "--no-keepalive")) cfg.enableKeepalive = false;
        else if (ArgIs(a, "--log-packets"))  cfg.logPackets = true;
        else if (ArgIs(a, "--create-char"))  cfg.createCharIfMissing = true;
        else if (!next) {
            std::fprintf(stderr, "missing value for %s\n", a);
            return 64;
        }
        else if (ArgIs(a, "--host"))      { cfg.loginHost = next; ++i; }
        else if (ArgIs(a, "--port"))      { cfg.loginPort = static_cast<uo::u16>(std::atoi(next)); ++i; }
        else if (ArgIs(a, "--user"))      { cfg.username = next; ++i; }
        else if (ArgIs(a, "--pass"))      { cfg.password = next; ++i; }
        else if (ArgIs(a, "--version"))   { cfg.version = next; ++i; }
        else if (ArgIs(a, "--seed"))      { cfg.plaintextSeed = static_cast<uo::u32>(std::strtoul(next, nullptr, 0)); ++i; }
        else if (ArgIs(a, "--char-slot")) { cfg.charSlot = std::atoi(next); ++i; }
        else if (ArgIs(a, "--char-name")) { cfg.charName = next; ++i; }
        else if (ArgIs(a, "--scenario"))  { cfg.scenarioPath = next; ++i; }
        else if (ArgIs(a, "--keepalive")) { cfg.keepaliveIntervalMs = static_cast<uo::u32>(std::atoi(next)); ++i; }
        else if (ArgIs(a, "--log"))       { cfg.logFile = next; ++i; }
        else if (ArgIs(a, "--mul-dir"))   { mulDir = next; ++i; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", a);
            PrintUsage();
            return 64;
        }
    }

    // Credentials: CLI first, then environment. Never hard-coded.
    if (!cfg.username || !cfg.username[0]) cfg.username = std::getenv("UO_BOT_USER");
    if (!cfg.password || !cfg.password[0]) cfg.password = std::getenv("UO_BOT_PASS");
    if (!cfg.username || !cfg.username[0] || !cfg.password || !cfg.password[0]) {
        std::fprintf(stderr,
            "error: no credentials. Pass --user/--pass or set "
            "UO_BOT_USER / UO_BOT_PASS.\n");
        return 64;
    }

    // MUL files are only needed for A* pathfinding and the renderer; the M1
    // action set (walk / say / backpack / logout) does not touch them.
    cfg.tiledataPath = MulPath(mulDir, "tiledata.mul");
    cfg.mapPath      = MulPath(mulDir, "map0.mul");
    cfg.staidxPath   = MulPath(mulDir, "staidx0.mul");
    cfg.staticsPath  = MulPath(mulDir, "statics0.mul");
    cfg.verdataPath  = MulPath(mulDir, "verdata.mul");
    cfg.artIdxPath   = MulPath(mulDir, "artidx.mul");
    cfg.artPath      = MulPath(mulDir, "art.mul");
    cfg.texIdxPath   = MulPath(mulDir, "texidx.mul");
    cfg.texPath      = MulPath(mulDir, "texmaps.mul");
    cfg.animIdxPath  = MulPath(mulDir, "anim.idx");
    cfg.animPath     = MulPath(mulDir, "anim.mul");
    cfg.animDataPath = MulPath(mulDir, "animdata.mul");
    cfg.animInfoPath = MulPath(mulDir, "animinfo.mul");
    cfg.huesPath     = MulPath(mulDir, "hues.mul");
    cfg.radarcolPath = MulPath(mulDir, "radarcol.mul");

    uo::LogInfo("uo-client -> %s:%u  user='%s' char=%s seed=0x%08X%s\n",
                cfg.loginHost, cfg.loginPort, cfg.username,
                cfg.charName ? cfg.charName : "<by slot>",
                cfg.plaintextSeed,
                cfg.scenarioPath ? " (scenario)" : "");

    uo::Client client(cfg);
    return client.Run();
}
