#include "Client.h"

#include <cstdio>

int main(int argc, char** argv) {
    uo::Client::Config cfg;
    cfg.loginHost        = "172.28.160.1";
    cfg.loginPort        = 2593;
    cfg.username         = "xrip";
    cfg.password         = "xrip";
    cfg.version          = "2.0.7";
    cfg.logFile          = "uo-client.log.jsonl";
    cfg.plaintextSeed    = 0xAC1CA001u;  // server IP: 172.28.160.1
    cfg.gamePortOverride = 0;
    cfg.gameHostOverride = nullptr;
    cfg.sendSeed         = true;      // most shards (incl. UO Demo) accept seed prefix
    cfg.tiledataPath     = "E:/uo/tiledata.mul";
    cfg.mapPath          = "E:/uo/map0.mul";
    cfg.staidxPath       = "E:/uo/staidx0.mul";
    cfg.staticsPath      = "E:/uo/statics0.mul";
    cfg.verdataPath      = "E:/uo/verdata.mul";  // patched map/statics overlay (ok if absent)
    cfg.legacyMovePacket = false;       // UO Demo 1997 protocol: 3-byte 0x02
    cfg.enableKeepalive  = false;       // 0x73 0x00 every 20s — stays inside the
                                       // shard's ~60s client-silence kick window
    cfg.acceptDoors      = true;        // path through doors; open them at runtime

    if (argc > 1) cfg.loginHost        = argv[1];
    if (argc > 2) cfg.loginPort        = static_cast<uo::u16>(std::atoi(argv[2]));
    if (argc > 3) cfg.username         = argv[3];
    if (argc > 4) cfg.password         = argv[4];
    if (argc > 5) cfg.gamePortOverride = static_cast<uo::u16>(std::atoi(argv[5]));
    if (argc > 6) cfg.gameHostOverride = argv[6];

    std::printf("uo-client M1 — host=%s:%u user='%s' seed=0x%08X\n",
                cfg.loginHost, cfg.loginPort, cfg.username, cfg.plaintextSeed);

    uo::Client client(cfg);
    return client.Run();
}
