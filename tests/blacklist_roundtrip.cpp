// Standalone round-trip check for blacklist.mul (verdata format).
// Compile manually: see scripts/build_bltest.bat
#include "bot/Blacklist.h"

#include <cstdio>

static int g_fail = 0;
static void expect(bool cond, const char* name) {
    std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail = 1;
}

int main() {
    const char* path = "test_blacklist.mul";
    std::remove(path);

    // Write some spots, then reload into a fresh instance.
    {
        uo::bot::Blacklist bl;
        bl.Load(path, 512);                 // empty -> remembers path
        bl.AddPersistent(1000, 2000, 5, 0); // exact cell
        bl.AddPersistent(1003, 2000, 7, 2); // range 2 box
        bl.AddPersistent(40, 40, 0, 1);     // different block
    }

    uo::bot::Blacklist bl2;
    bl2.Load(path, 512);

    expect(bl2.PersistentCount() == 3, "reloaded 3 persistent spots");
    expect(bl2.IsBlocked(1000, 2000, 5),  "exact spot blocked");
    expect(!bl2.IsBlocked(999, 2000, 5),  "neighbor of range-0 spot free");
    expect(bl2.IsBlocked(1003, 2000, 7),  "range-2 center blocked");
    expect(bl2.IsBlocked(1005, 2002, 7),  "within range 2 blocked");
    expect(!bl2.IsBlocked(1006, 2000, 7), "outside range 2 free");
    expect(!bl2.IsBlocked(1003, 2000, 80),"z far away not blocked");
    expect(bl2.IsBlocked(40, 40, 0),      "second-block spot blocked");

    std::remove(path);
    std::printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: OK\n");
    return g_fail;
}
