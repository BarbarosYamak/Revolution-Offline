// Client persistence layer (M4 step 1) -- the seam between one session's live
// knowledge and the character's persistent life (uo/lifecycle.h).
//
// The record is loaded when the world comes up (0x55) and written back on
// logout, on death, and once a minute in between. What is saved is TARGETS and
// KNOWLEDGE only; skills, stats, gold and items are the server's and are re-read
// from it every session.

#include "Client.h"

#include "uo/vendor_policy.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace uo {

void Client::LifeBegin() {
    lifeActive_ = false;
    if (!cfg_.lifeDir || !cfg_.lifeDir[0]) return;

    std::string name;
    if (selectedChar_ >= 0 && selectedChar_ < charCount_ &&
        charSlots_[selectedChar_].name[0])
        name = charSlots_[selectedChar_].name;
    else if (cfg_.charName)
        name = cfg_.charName;
    if (name.empty()) {
        LogWarn("[life] no character name known; persistence off this session\n");
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(cfg_.lifeDir, ec);
    lifePath_ = (std::filesystem::path(cfg_.lifeDir) / life::RecordFileName(name)).string();

    const life::Clock clock = life::NowClock(NowMs());
    life::CharacterRecord rec;
    const life::LoadResult lr = life::LoadFile(lifePath_, &rec);
    char ev[320];

    switch (lr.status) {
        case life::LoadStatus::Ok:
            // RecordFileName folds every non-alphanumeric to '_', so two names
            // can share a file. Never adopt someone else's life.
            if (rec.name != name) {
                LogWarn("[life] %s holds '%s', not '%s'; persistence off\n",
                        lifePath_.c_str(), rec.name.c_str(), name.c_str());
                std::snprintf(ev, sizeof(ev), "path='%s' holds='%s'",
                              lifePath_.c_str(), rec.name.c_str());
                LogEvent("life_name_mismatch", ev);
                return;
            }
            life::Apply(rec, &knowledge_, &supplies_, clock);
            LogInfo("[life] resumed %s (%s): session %d, objective %s '%s', "
                    "%zu supplier(s), %zu death(s) on record%s%s\n",
                    rec.name.c_str(), life::AspirationName(rec.aspiration),
                    rec.sessions + 1, life::ObjectiveKindName(rec.objective.kind),
                    rec.objective.target.c_str(), rec.suppliers.size(),
                    rec.deaths.size(), lr.detail.empty() ? "" : " -- ",
                    lr.detail.c_str());
            // The rule is "log out somewhere safe". Say so when the last
            // session did not, rather than letting it pass quietly.
            if (rec.lastLogout.valid && !rec.lastLogout.safe)
                LogWarn("[life] last session logged out somewhere UNSAFE at (%d,%d) %s\n",
                        rec.lastLogout.x, rec.lastLogout.y,
                        rec.lastLogout.placeId.c_str());
            break;

        case life::LoadStatus::NotFound:
            // The only archetype built so far. When there is a second, the
            // choice is made here from configuration, not guessed.
            rec = life::NewLumberjackSwordsman(name.c_str(), cfg_.username,
                                               clock.wallMs);
            LogInfo("[life] no record for %s; beginning a new %s life at %s\n",
                    name.c_str(), life::AspirationName(rec.aspiration),
                    lifePath_.c_str());
            break;

        case life::LoadStatus::Corrupt:
        case life::LoadStatus::Invalid:
        default:
            // Leave the file exactly as it is and do not save over it. A
            // fresh life would silently erase whatever this one had learned.
            LogError("[life] %s is %s (%s); persistence OFF, file left untouched\n",
                     lifePath_.c_str(), life::LoadStatusName(lr.status),
                     lr.detail.c_str());
            std::snprintf(ev, sizeof(ev), "path='%s' status=%s detail='%s'",
                          lifePath_.c_str(), life::LoadStatusName(lr.status),
                          lr.detail.c_str());
            LogEvent("life_unreadable", ev);
            return;
    }

    ++rec.sessions;
    rec.lastLoginWallMs = clock.wallMs;
    lifeRecord_ = std::move(rec);
    lifeActive_ = true;

    std::snprintf(ev, sizeof(ev), "name='%s' session=%d status=%s",
                  lifeRecord_.name.c_str(), lifeRecord_.sessions,
                  life::LoadStatusName(lr.status));
    LogEvent("life_begin", ev);
    // Write at once: the session count is how "several consecutive sessions"
    // gets measured, and it must not depend on this session ending cleanly.
    LifeSave("login");
}

void Client::LifeSave(const char* why) {
    if (!lifeActive_) return;
    const life::Clock clock = life::NowClock(NowMs());
    life::Capture(&lifeRecord_, knowledge_, supplies_, clock);

    const bool endOfSession = why && (std::strcmp(why, "logout") == 0 ||
                                      std::strcmp(why, "session_end") == 0);
    if (endOfSession) {
        const wm::Region* r = CurrentRegion();
        life::LogoutPoint& l = lifeRecord_.lastLogout;
        l.valid = true;
        l.x = playerX_; l.y = playerY_; l.z = playerZ_;
        l.placeId = r ? r->id : "";
        // Safe = alive, and inside a region the shard itself guards or marks
        // safe. Anything else is the graveyard case from the plan.
        l.safe = life_ == act::LifeState::Alive && r &&
                 (r->flags.guarded || r->flags.safe);
        l.wallMs = clock.wallMs;
    }

    std::string err;
    lifeLastSaveMs_ = NowMs();
    if (!life::SaveFile(lifePath_, lifeRecord_, &err)) {
        LogError("[life] save (%s) failed: %s\n", why ? why : "?", err.c_str());
        LogEvent("life_save_failed", err.c_str());
        return;
    }
    if (std::strcmp(why ? why : "", "autosave") != 0) {
        char ev[192];
        std::snprintf(ev, sizeof(ev), "why=%s at=(%d,%d,%d)%s", why ? why : "?",
                      playerX_, playerY_, static_cast<int>(playerZ_),
                      endOfSession ? (lifeRecord_.lastLogout.safe ? " safe=1" : " safe=0")
                                   : "");
        LogEvent("life_saved", ev);
    }
}

void Client::LifeTick() {
    if (!lifeActive_ || loggingOut_) return;
    if (NowMs() - lifeLastSaveMs_ >= kLifeAutosaveMs) LifeSave("autosave");
}

void Client::SetLifeObjective(life::ObjectiveKind kind, const char* target) {
    if (!lifeActive_) return;
    life::Objective& o = lifeRecord_.objective;
    const std::string t = target ? target : "";
    if (o.kind == kind && o.target == t) {
        ++o.attempts;
        return;
    }
    o.kind = kind;
    o.target = t;
    o.sinceWallMs = life::NowClock(NowMs()).wallMs;
    o.attempts = 0;
}

// A vendor offer is the one verified observation of stock: we asked, and the
// server listed it. Every row whose graphic maps to a defname is recorded;
// unmapped rows are skipped, since a need is always phrased as a defname and
// a supplier nobody can ask for is noise.
void Client::NoteVendorStock(u32 vendorSerial) {
    if (!vendorSerial) return;
    i32 vx = 0, vy = 0;
    i8  vz = 0;
    if (!MobilePosition(vendorSerial, &vx, &vy, &vz)) {
        // No position, no supplier: "a supplier without a serial or a
        // position is a rumour" (uo/supplier.h).
        return;
    }
    const auto title = paperdollTitles_.find(vendorSerial);
    const char* name = title != paperdollTitles_.end() ? title->second.c_str() : "";
    const i64 now = NowMs();
    for (const VendorItem& v : vendorOffer_) {
        const char* itemdef = econ::ItemNameForGraphic(v.graphic);
        if (!itemdef) continue;
        supplies_.RecordVendorStock(vendorSerial, name, vx, vy, vz, itemdef,
                                    v.amount, static_cast<i32>(v.price), now);
    }
}

}  // namespace uo
