#include "life/runner/RunnerInternal.h"
#include "uo/recall_plan.h"

#include <cstdlib>

// MARKING OUR OWN RUNES (uo/recall_plan.h). A mage with Magery 60 and a blank
// rune in the pack marks it at home, so the travel layer can recall home with
// a loose rune when there is no runebook page for it. The server is the only
// witness: a rune is blank while its label is the stock name, and it counts
// as marked only once the label the server gives back has CHANGED after the
// cast. Blank runes are never bought -- the vendor policy blocks them
// (data/revolution_vendor_policy.tsv, "marked, not bought") -- so this only
// acts on one the character already owns.

namespace uo::life {
using namespace runner_detail;

void Runner::TickRunes(Client& client, const Observation& obs) {
    if (obs.nowMs - runeTickMs_ < 5000) return;
    runeTickMs_ = obs.nowMs;
    const u32 pack = client.BackpackSerial();
    if (!pack) return;

    // What this character marked in earlier sessions, back into the travel
    // layer's memory. The travel layer only recalls with one still in the pack.
    if (!runesRestored_) {
        runesRestored_ = true;
        for (const KnownPlace& p : state_.memory.Places()) {
            if (p.kind != "rune") continue;
            const u32 serial = static_cast<u32>(std::strtoul(p.name.c_str(), nullptr, 16));
            travel::KnownRune r;
            r.serial = serial; r.graphic = recall::kRuneGraphic; r.name = "own rune";
            r.marked = true; r.destinationKnown = true; r.x = p.x; r.y = p.y; r.z = p.z;
            client.Knowledge().NoteRune(r);
        }
    }

    // A cast in flight: read the rune's new name back before believing it.
    if (markRune_) {
        if (obs.nowMs - markCastMs_ < 4000) return;
        const std::string* label = client.ServerItemName(markRune_);
        if (!markLookedMs_) { client.ActionLookAt(markRune_); markLookedMs_ = obs.nowMs; return; }
        if (obs.nowMs - markLookedMs_ < 2000) return;
        if (label && *label != markLabelBefore_ && !recall::LooksBlankRune(*label)) {
            travel::KnownRune r;
            r.serial = markRune_; r.graphic = recall::kRuneGraphic; r.name = *label;
            r.marked = true; r.destinationKnown = true; r.x = markX_; r.y = markY_; r.z = markZ_;
            client.Knowledge().NoteRune(r);
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%08X", markRune_);
            state_.memory.NotePlace("rune", hex, markX_, markY_, markZ_, obs.nowMs);
            LogLine("travel: marked a rune at %d,%d -- the server calls it '%s'", markX_, markY_, label->c_str());
        } else {
            LogLine("travel: the Mark did not take (the rune still reads '%s')", label ? label->c_str() : "?");
            markRestUntilMs_ = obs.nowMs + 120000;
        }
        markRune_ = 0; markLookedMs_ = 0;
        return;
    }
    if (obs.nowMs < markRestUntilMs_) return;

    // Find a rune whose name we have not read, or a blank one.
    u32 blank = 0;
    for (usize i = 0; i < client.ContainerItemCount(pack); ++i) {
        u32 serial = 0; u16 graphic = 0, amount = 0;
        if (!client.ContainerItemAt(pack, i, &serial, &graphic, &amount) || graphic != recall::kRuneGraphic) continue;
        bool known = false;
        for (const auto& r : client.Knowledge().Runes()) known = known || r.serial == serial;
        if (known) continue;
        const std::string* label = client.ServerItemName(serial);
        if (!label) {
            if (obs.nowMs - runeLookMs_ >= 30000) { runeLookMs_ = obs.nowMs; client.ActionLookAt(serial); }
            return;
        }
        if (recall::LooksBlankRune(*label)) { blank = serial; break; }
    }

    const HomeReturn home = ResolveHomeReturn(client.WorldAtlas(), state_.homeCity, obs.x, obs.y);
    recall::MarkSight s;
    s.haveBlankRune = blank != 0;
    s.mageryTenths = obs.SkillTenths(rules::kMagery);
    s.mana = obs.mana;
    s.reagents = client.BackpackItemCount(0x0F7A) > 0 && client.BackpackItemCount(0x0F7B) > 0 &&
                 client.BackpackItemCount(0x0F86) > 0;
    s.atHome = home.resolved && home.inHome;
    s.safe = !obs.dead && !obs.underAttack && obs.attackersOnMe == 0 && obs.hostilesNear == 0;
    s.busy = client.ActionBusy() || client.TravelBusy();
    s.ownRuneNearHome = home.resolved && client.Knowledge().BestRuneFor(home.x, home.y, 60) != nullptr;
    if (!recall::ShouldMark(s)) return;
    markRune_ = blank;
    markLabelBefore_ = *client.ServerItemName(blank);
    markCastMs_ = obs.nowMs;
    markX_ = obs.x; markY_ = obs.y; markZ_ = static_cast<i8>(client.PlayerZ());
    LogLine("travel: marking a blank rune here at home (%d,%d)", obs.x, obs.y);
    client.ActionCastSpell(45, blank);   // Mark: the server checks skill, mana, reagents, fizzle
}

}  // namespace uo::life
