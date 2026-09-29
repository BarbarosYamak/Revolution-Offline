#include "Client.h"
#include "uo/endian.h"
#include "uo/life.h"
#include "uo/vendor_errand.h"
#include <cstdio>
#include <cstring>
#include <memory>

using namespace uo;
namespace {
int failures = 0;
void Check(bool ok, const char* why) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", why); }
}
void Mobile(Client& client, u32 serial, const char* title) {
    u8 mobile[23]{}; mobile[0] = 0x78;
    StoreBE16(mobile + 1, sizeof(mobile)); StoreBE32(mobile + 3, serial);
    StoreBE16(mobile + 7, 0x190); StoreBE16(mobile + 9, 101); StoreBE16(mobile + 11, 100);
    mobile[18] = 1;
    client.DispatchPacketForTest(mobile, sizeof(mobile));
    u8 doll[66]{}; doll[0] = 0x88; StoreBE32(doll + 1, serial);
    std::memcpy(doll + 5, title, std::strlen(title));
    client.DispatchPacketForTest(doll, sizeof(doll));
}
void Offer(Client& client, u32 vendor, u16 graphic, u16 amount) {
    const u32 box = 0x40001000 + vendor;
    if (amount) {
        u8 item[20]{}; item[0] = 0x25;
        StoreBE32(item + 1, box + 100); StoreBE16(item + 5, graphic);
        StoreBE16(item + 8, amount); StoreBE32(item + 14, box);
        client.DispatchPacketForTest(item, sizeof(item));
        const char* label = "tinker tools";
        std::vector<u8> shop(13 + std::strlen(label)); shop[0] = 0x74;
        StoreBE16(shop.data() + 1, static_cast<u16>(shop.size()));
        StoreBE32(shop.data() + 3, box); shop[7] = 1;
        StoreBE32(shop.data() + 8, 34); shop[12] = static_cast<u8>(std::strlen(label));
        std::memcpy(shop.data() + 13, label, std::strlen(label));
        client.DispatchPacketForTest(shop.data(), shop.size());
    }
    u8 draw[7]{}; draw[0] = 0x24;
    StoreBE32(draw + 1, vendor); StoreBE16(draw + 5, 0x30);
    client.DispatchPacketForTest(draw, sizeof(draw));
}
}
int main() {
    Client::Config cfg{};
    auto client = std::make_unique<Client>(cfg);
    client->SetOfflineForTest(true); client->SetInWorldForTest(); client->SetClockForTest(1000000);
    u8 login[37]{}; login[0] = 0x1B; StoreBE32(login + 1, 1);
    StoreBE16(login + 9, 0x190); StoreBE16(login + 11, 100); StoreBE16(login + 13, 100);
    client->DispatchPacketForTest(login, sizeof(login));
    Mobile(*client, 2, "Bob the tinker");
    Mobile(*client, 3, "Alice the provisioner");
    const auto graphics = econ::GraphicsForItem("i_tinker_tools");
    Check(!graphics.empty(), "tinker tools have a known graphic");
    if (graphics.empty()) return 1;
    supply::Need need{supply::NeedKind::Item, "i_tinker_tools", 1};
    Check(!client->ObservedSuppliers().Best(need, 100, 100, 1000000).usable,
          "titles alone are not verified inventory");
    Offer(*client, 3, graphics.front(), 4);
    Check(client->ObservedSuppliers().Best(need, 100, 100, 1000000).supplier.serial == 3,
          "real vendor packets populate the supplier registry");

    life::Observation obs; obs.inWorld = true; obs.nowMs = 1000000;
    obs.x = obs.y = 100; obs.gold = 1000;
    life::VendorErrandSpec spec; spec.Sell("tinker", wm::Service::Tinker);
    spec.graphic = graphics.front(); spec.qty = 10; spec.what = "tinker tools";
    life::VendorErrand errand; errand.Begin(spec);
    errand.Tick(*client, obs); // choose the verified supplier, despite its title
    errand.Tick(*client, obs); // already adjacent
    errand.Tick(*client, obs); // request a fresh list
    Check(client->ActionBusy(), "remembered stock is reopened before buying");
    Offer(*client, 3, graphics.front(), 0); // empty now; the old snapshot must disappear
    Check(!client->ObservedSuppliers().Best(need, 100, 100, obs.nowMs).usable,
          "an empty replacement list removes formerly available stock");
    auto refused = errand.Tick(*client, obs);
    Check(errand.Running() && refused.why.find("trying another shop") != std::string::npos,
          "an unavailable shop rotates within the errand");
    errand.Tick(*client, obs); // Bob is the untried fallback
    errand.Tick(*client, obs);
    errand.Tick(*client, obs);
    Check(client->ActionBusy(), "previous vendor refusal does not reject the next shop");
    Offer(*client, 2, graphics.front(), 2);
    Check(client->ActionResult() == act::Result::Success,
          "fallback vendor receives the new shop request");
    errand.Tick(*client, obs);
    const auto purchase = errand.Tick(*client, obs);
    Check(purchase.keeper == 2 && purchase.acted && purchase.why.find("buying 2") != std::string::npos,
          "fallback purchase uses fresh partial stock, not the old requested quantity");
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
