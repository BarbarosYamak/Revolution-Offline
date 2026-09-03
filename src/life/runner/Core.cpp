#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


Runner::Runner() = default;
Runner::~Runner() = default;

void Runner::LogLine(const char* fmt, ...) const {
    if (!cfg_.verbose) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // One prefix for the whole autonomous layer, so a run can be read with
    // `findstr [life]` and nothing else. Deliberately NOT a packet trace: the
    // M4 brief asks for choices to be explainable without one.
    LogInfo("[life] %s\n", buf);
}

bool Runner::Configure(const RunnerConfig& cfg, std::string* err) {
    cfg_ = cfg;
    store_ = Store(cfg.dataRoot);

    const std::string id = MakeIdentityId(cfg.accountName, cfg.characterName);

    std::string loadErr;
    if (store_.Load(id, &state_, &loadErr)) {
        LogLine("loaded persistent state for %s (%zu places, %zu suppliers, "
                "%zu events, %zu prior sessions)",
                state_.identity.characterName.c_str(),
                state_.memory.Places().size(), state_.memory.Suppliers().size(),
                state_.memory.Events().size(), state_.sessions.size());
    } else if (!loadErr.empty()) {
        // A corrupt file is a hard stop. Silently starting a fresh life on top
        // of an unreadable one would destroy exactly the history M4 exists to
        // keep, and would look like a successful run.
        if (err) *err = loadErr;
        return false;
    } else {
        state_ = PersistentState{};
        state_.identity.identityId    = id;
        state_.identity.accountName   = cfg.accountName;
        state_.identity.characterName = cfg.characterName;
        const prof::Profession* chosen =
            cfg.professionId.empty() ? nullptr : prof::Find(cfg.professionId.c_str());
        if (!chosen) {
            if (err) {
                *err = "unknown profession '" + cfg.professionId +
                       "' -- see uo::prof::All()";
            }
            return false;
        }
        const prof::ProfCheck pc = prof::Validate(rules::Revolution(), *chosen);
        if (!pc.ok) {
            if (err) {
                *err = "profession '" + chosen->id + "' is not a legal life: " +
                       prof::ProfViolationName(pc.violation);
            }
            return false;
        }
        state_.plan = PlanFromProfession(*chosen);
        LogLine("no prior state for %s: a new %s", id.c_str(),
                chosen->label.c_str());
        LogLine("creation request: %s 50.0 + %s 50.0, stats %d/%d/%d = %d",
                rules::SkillName(chosen->startSkillA), rules::SkillName(chosen->startSkillB),
                chosen->startStr, chosen->startDex, chosen->startInt,
                chosen->startStr + chosen->startDex + chosen->startInt);
    }

    // Which life is asking. Resolved from the plan's family so it works the
    // same for a fresh character and for one reloaded from disk. A plan family
    // with no catalogue entry -- the M4 lumberjack, saved before the catalogue
    // existed -- leaves this null and keeps the old lumberjack needs.
    // The rotation memory lives on this Runner and is read by the need model,
    // so the two never disagree about what this life is making today.
    needCfg_.craftFocus = &craftFocus_;
    needCfg_.profession = prof::Find(state_.plan.family.c_str());
    if (!needCfg_.profession) {
        // THE M4 CHARACTER IS NOT A DIFFERENT LIFE, ONLY AN OLDER SPELLING.
        //
        // FrontierLumberjackSwordsman() (Identity.cpp:121) predates the M5
        // catalogue and writes family "frontier_lumberjack_swordsman"; the
        // catalogue registers "lumberjack_swordsman" (Professions.cpp:149).
        // Tarath was created under the old plan, so every reload since has
        // fallen into the branch below and run with a NULL profession -- and
        // a null profession silently disables far more than the comment
        // implies. DoTrainCombat short-circuits on it before WantsToHunt is
        // ever evaluated (Runner.cpp, `!needCfg_.profession`), so the shard's
        // most-run character could never go hunting, and the plan was never
        // rebuilt so it had no viaTrainer flags either. It logged one line
        // about "the original lumberjack needs" and looked fine.
        //
        // Alias the old spelling rather than renaming it: the M4 plan is what
        // that character was actually created with, and rewriting history in
        // Identity.cpp would change what the saved file means.
        if (state_.plan.family == "frontier_lumberjack_swordsman") {
            needCfg_.profession = prof::Find("lumberjack_swordsman");
            if (needCfg_.profession) {
                LogLine("needs: plan family '%s' is the M4 spelling of '%s' "
                        "-- reading the catalogue entry",
                        state_.plan.family.c_str(), needCfg_.profession->id.c_str());
            }
        }
    }
    if (needCfg_.profession) {
        // THE CATALOGUE IS THE INTENTION; the save file only records which
        // life this is. Re-deriving here is not tidiness -- the saved plan
        // carries skills and targets but not the per-target `viaTrainer` and
        // `priority` fields, so a reloaded character silently had nothing it
        // was willing to buy from a trainer, and simply never trained. It
        // logged no error: NextSkillToBuy just returned -1 forever.
        //
        // Anything the character has actually EARNED lives on the server or
        // in Memory, never in the plan, so nothing is lost by rebuilding it.
        state_.plan = PlanFromProfession(*needCfg_.profession);
        LogLine("needs: reading '%s' from the profession catalogue",
                needCfg_.profession->id.c_str());
    } else {
        LogLine("needs: plan family '%s' is not in the catalogue -- using the "
                "original lumberjack needs", state_.plan.family.c_str());
    }

    // Pick a home, once. Deterministic from the identity id rather than random,
    // so the same character always gets the same home even if the state file is
    // lost -- and so a fleet spreads across the map instead of every member
    // rolling the same first entry.
    if (state_.homeCity.empty() && needCfg_.profession) {
        if (needCfg_.profession->homeCities.empty()) {
            // No list at all: Britain, never the index-0 fallback of Yew.
            state_.homeCity = "Britain";
            LogLine("home: %s has no home cities in the catalogue -- Britain",
                    state_.identity.characterName.c_str());
        }
    }
    if (state_.homeCity.empty() && needCfg_.profession &&
        !needCfg_.profession->homeCities.empty()) {
        usize h = 0;
        for (char c : state_.identity.identityId) {
            h = h * 131 + static_cast<unsigned char>(c);
        }
        const std::vector<std::string>& homes = needCfg_.profession->homeCities;
        // A LIFE WHOSE WORK IS GEOGRAPHIC LIVES WHERE THE WORK IS.
        //
        // "miners home city then minoc" and "lumberjacks too" (project owner,
        // 2026-08-29). The hash exists to spread a fleet across the map, which
        // is right for a trade you can practise anywhere -- a scribe, a mage,
        // an alchemist. It is wrong for one you cannot: Minoc is FIRST in the
        // miner's list precisely because that is where the ore is, and Corwyn
        // still rolled Vesper and spent his sessions walking back to it.
        //
        // So a profession that GATHERS something takes the first entry, which
        // the catalogue already orders by where that work actually happens.
        // Everyone else still spreads out.
        const bool workIsPlaceBound = !needCfg_.profession->gathers.empty();
        state_.homeCity = workIsPlaceBound ? homes.front()
                                           : homes[h % homes.size()];
        LogLine("home: %s lives in %s", state_.identity.characterName.c_str(),
                state_.homeCity.c_str());
    }

    // Whatever the source -- a fresh plan or one reloaded from disk -- it has
    // to be a legal Revolution build before the character acts on it.
    const PlanCheck check = ValidatePlan(rules::Revolution(), state_.plan);
    if (!check.ok) {
        if (err) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "build plan is not a legal Revolution build: %s (skill %d)",
                          PlanViolationName(check.violation), check.skillId);
            *err = buf;
        }
        return false;
    }
    LogLine("build plan '%s': %.1f resolved + %.1f unresolved = %.1f/%.1f, "
            "stats %d/%d/%d = %d",
            state_.plan.family.c_str(), check.resolvedTenths / 10.0,
            state_.plan.unresolvedTenths / 10.0, check.plannedTotalTenths / 10.0,
            rules::Revolution().totalSkillCapTenths / 10.0,
            state_.plan.targetStr, state_.plan.targetDex, state_.plan.targetInt,
            check.statTotal);

    // The goal that came back from disk is an INTENTION. Its clock and
    // counters are transient and are rebuilt at reconciliation.
    planner_.Mutable() = state_.goal;
    configured_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Observation -- source of truth C, rebuilt from scratch every tick.
// ---------------------------------------------------------------------------

Observation Runner::Observe(Client& client, i64 nowMs) const {
    Observation obs;
    obs.nowMs = nowMs;
    obs.inWorld = client.IsInWorld();
    if (!obs.inWorld) return obs;

    obs.dead    = client.IsDead();
    obs.mounted = client.PlayerIsMounted();
    obs.warMode = client.WarModeOn();

    obs.x = client.PlayerX();
    obs.y = client.PlayerY();
    obs.z = client.PlayerZ();

    obs.hp    = client.PlayerHp();
    obs.hpMax = client.PlayerHpMax();
    obs.mana  = client.PlayerMana();

    obs.str   = client.PlayerStr();
    obs.dex   = client.PlayerDex();
    obs.intel = client.PlayerInt();

    for (const SkillTarget& t : state_.plan.skills) {
        const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
        if (base >= 0) obs.skills.push_back({t.skillId, base});
    }

    obs.gold      = client.PlayerGold();
    obs.goldOnHand = static_cast<i32>(client.BackpackItemCount(kGoldCoin));
    obs.coinWanted = coinWanted_;
    obs.weight    = client.PlayerWeight();
    obs.maxWeight = client.PlayerMaxWeight();
    if (obs.maxWeight <= 0) {
        // The server only sends maxWeight when its status packet is flagged >= 5
        // (Client.cpp OnStats), and this shard sends less -- so it is ALWAYS 0
        // here and WeightFraction() was permanently 0. The character therefore
        // never noticed it was full, never banked, and simply overflowed:
        // "You put the logs at your feet. It is too heavy.."
        //
        // Derive it from the engine's own formula instead of guessing.
        // CResourceCalc.cpp:24 -- 40 + STR * 3.5, plus a flat +60 for humans
        // with the Strong Back racial. Tenths of stones there; PlayerWeight()
        // is in whole stones, so this is the stone figure.
        obs.maxWeight = 40 + (obs.str * 35) / 10;
    }
    // The server's own words are the definitive signal, and they arrive whether
    // or not it ever told us a capacity.
    obs.overloaded = client.JournalSaidSince("it is too heavy", overloadWatchMs_);

    obs.bandages = static_cast<i32>(client.BackpackItemCount(kBandage));
    obs.healPotions = static_cast<i32>(client.BackpackItemCount(kHealPotion));
    // Counted here rather than in the goal, because a need that cannot see
    // the condition cannot select the goal that fixes it.
    for (const ClothingPiece& p : kBasicClothing) {
        // No tiledata, no layer, no opinion. Without it there is no way to
        // ask what is worn, and guessing would report a dressed character
        // naked.
        if (!client.ItemEquipLayer(p.graphic)) continue;
        bool worn = false;
        if (ClothingOnHand(client, p, &worn) || worn) continue;
        ++obs.clothingMissing;
    }
    // A skirt is optional wardrobe, but if a female character already owns one
    // in her backpack it must participate in the "dress from the pack" need.
    // Otherwise an otherwise clothed character never enters DoReplaceEquipment
    // and WearBasicClothing never gets a chance to equip the skirt layer.
    if (client.PlayerIsFemale()) {
        constexpr u16 kSkirts[] = {0x1516, 0x1531, 0x1537}; // long, short, kilt
        for (u16 graphic : kSkirts) {
            const u8 layer = client.ItemEquipLayer(graphic);
            if (layer && !client.EquippedGraphicAt(layer) &&
                client.FindBackpackItemByGraphic(graphic)) {
                ++obs.clothingMissing;
                break;
            }
        }
    }
    obs.logs     = static_cast<i32>(client.BackpackItemCount(kLog));
    obs.food     = CountAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]));
    // HUNGER AS THE SERVER LAST SAID IT. "You are <level>" over the eight
    // levels in core/messages.scp:470-477, AND the food_full_N line every
    // successful meal produces -- both are statements about the same
    // STAT_FOOD (act::HungerStatements carries the table and the arithmetic).
    //
    // THE LAST STATEMENT WINS, and that is the whole point. The old read was
    // "did it ever say hungry since session start", but the shard says "You
    // are hungry" exactly ONCE, at login, and never repeats it. So a character
    // that logged in hungry stayed hungry to the client forever: it ate, the
    // server answered "You are nearly stuffed, but manage to eat the food",
    // and the flag did not move -- so it ate the rest of its pack, walked to
    // the provisioner, bought more, ate that, and did it again for the whole
    // session (2026-09-02 wave: g_Halain.console.txt:39 is the only hunger
    // status line in the run, followed by 351 `food: eating` lines).
    {
        usize rows = 0;
        const act::HungerStatement* table = act::HungerStatements(&rows);
        i64 newestMs = -1;
        int level = -1;
        for (usize i = 0; i < rows; ++i) {
            const i64 saidMs =
                client.JournalLastSaidMs(table[i].text, sessionStartJournalMs_);
            // Strict >: the table is ordered most-specific-first, so on the
            // one line that matches two phrases the specific one keeps it.
            if (saidMs > newestMs) {
                newestMs = saidMs;
                level = static_cast<int>(table[i].level);
            }
        }
        obs.starving = (level >= 0 && level <= act::kHungerLevelStarving);
        obs.hungry   = (level >= 0 && level <= act::kHungerLevelHungry);
    }

    obs.axeInPack = FindAny(client, kHatchet, 2) != 0 || FindAny(client, kAxe, 2) != 0;
    obs.weaponEquipped = HandsBusy(client);
    // Read the worn graphic rather than inferring from a full hand. The first
    // live run swung the newbie katana at a tree for two minutes because a
    // filled weapon hand was taken to mean "the axe is out".
    obs.axeEquipped = AxeInHand(client);

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    // THE SHEEP THIS LIFE IS PUTTING DOWN IS NOT A THREAT. Halain's first
    // wool trip (2026-09-03): the shorn sheep hit back, the scan counted it
    // and the cow beside it as "2 attackers", SURVIVE fled at 100% HP thirty
    // tiles toward the bank, and the sheep was out of range by the time
    // HARVEST_WOOL got the turn back. Nobody runs from a sheep.
    if (clothKillSheep_) {
        hostiles.erase(std::remove_if(hostiles.begin(), hostiles.end(),
                                      [&](const Client::HostileHit& h) {
                                          return h.serial == clothKillSheep_;
                                      }),
                       hostiles.end());
    }
    obs.marketQuiet = obs.nowMs < marketQuietUntilMs_;
    // WHICH items the player market came back empty on, not merely THAT it
    // did. marketQuietUntilMs_ above is a ten-minute damper on the trade goal
    // as a whole; a need that has to decide "may I walk to Yew for this one
    // material" needs the item, and the memory has been carrying it all
    // along.
    obs.noSellerFor.clear();
    for (const LifeEvent& e : state_.memory.Events()) {
        if (e.kind != "no_player_seller") continue;
        if (e.detail.empty()) continue;
        if (obs.nowMs - e.atMs > kPlayerWindowMemoryMs) continue;
        if (!obs.NoSellerFor(e.detail)) obs.noSellerFor.push_back(e.detail);
    }
    // The market TRIP's own budget, exposed to the need model. Same numbers
    // DoTradeWithPlayer refuses on; see Observation::marketTripFitsSession.
    obs.marketTripFitsSession =
        cfg_.sessionLimitMs <= 0 ||
        (cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_)) >=
            kMarketTripBudgetMs;
    // ...and the goal's own stand-down, for the same reason: a need that waits
    // on a market answer must be able to tell "nobody answered" from "the
    // errand that does the asking is not available to be picked".
    obs.marketAskOnCooldown =
        planner_.Cooling(GoalKind::TradeWithPlayer, obs.nowMs);
    obs.hostilesNear = static_cast<i32>(hostiles.size());
    const u32 warTarget = client.WarWatchdog().TargetSerial();
    i32 adjacent = 0;
    for (const Client::HostileHit& h : hostiles) {
        if (TileDist(h.x, h.y, obs.x, obs.y) <= 1) ++adjacent;
    }
    // A FIGHT, not a crowd. Adjacency alone is not being attacked: Session B
    // opened combat with a COW that happened to be standing next to it, and
    // then spent twenty seconds failing to dent it. The war watchdog's target
    // is the client's own record that a fight is actually happening, so that
    // is the gate; adjacent hostiles only scale the pressure once it is.
    obs.underAttack = warTarget != 0 && warTarget != clothKillSheep_;
    obs.attackersOnMe = obs.underAttack ? std::max(1, adjacent) : 0;

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    // The death location is enough to begin a recovery run.  The corpse serial
    // is a transient world-object id and is normally learned only after we
    // return to the death tile, so requiring it here made a logout turn every
    // real corpse into "nowhere to go back to".
    obs.corpseKnown = death.valid;
    obs.corpseX = death.x;
    obs.corpseY = death.y;
    obs.corpseRecoveryAttempts = death.recoveryAttempts;

    // Arrival is a claim about the TILE. `TreeCount` asks the shard's own
    // statics whether there is anything here to chop, which travel success
    // does not answer (see docs/UOOFFLINE_BEHAVIOR_AUDIT.md section 3.6).
    // AM I WHERE MY WORK IS -- for THIS life. TreeCount answered on behalf of
    // every profession, so a miner at its vein, a mage in its tower and a
    // fisher on a dock were all told they were "not at work" and sent hiking
    // toward the nearest forest looking for logs they had no use for.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers == "fish") {
        Client::WaterHit w;
        obs.atWorkSite = client.NearestWater(obs.x, obs.y, 4, &w);
    } else if (gathers.empty()) {
        // A life that gathers nothing is never away from its work: a mage or
        // an alchemist works wherever it happens to be standing.
        obs.atWorkSite = true;
    } else if (gathers == "ore") {
        // A MINER IS AT WORK WHERE THE ORE IS. This fell through to counting
        // TREES -- the comment above says a miner at its vein was the very
        // case this block existed to fix, and ore was still never handled. So
        // a miner standing in Minoc, the mining town, was judged by how many
        // trees happened to be around him. Corwyn trained in Minoc and walked
        // back to Vesper without a single swing.
        const i32 d = client.DistanceToResource(wm::ResourceKind::Mining);
        obs.atWorkSite = d >= 0 && d <= kAtOreDistance;
    } else {
        obs.atWorkSite = client.TreeCount(obs.x, obs.y, cfg_.searchRadius) > 0;
    }
    // NO SKILL ADVANCES INSIDE A REGION_FLAG_SAFE AREA (Source-X
    // Skill_Experience; docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.2 point 1).
    // Twenty-five of them on map 0, and the ones that matter are the
    // SHRINES -- quiet, safe-looking, and exactly where a bot would
    // otherwise choose to stand and meditate for an hour to no effect.
    {
        const wm::Region* here = client.CurrentRegion();
        obs.inNoGainRegion = here && here->flags.safe;
    }
    obs.treeAdjacent = client.TreeCount(obs.x, obs.y, 2) > 0;
    obs.atBank = client.BankContainer() != 0 &&
                 client.ContainerKnown(client.BankContainer());
    // AND WHETHER THIS IS THE MARKET. Geometry only -- the same test the trade
    // handler uses to decide it has arrived -- because the planner needs to
    // know a market trip has been PAID FOR before it lets an ordinary errand
    // walk the character away again. See Observation::atMarket.
    obs.atMarket = AtMarketBank(client);

    // READ THE BOX while it is open, and KEEP what it said.
    //
    // A character that does not remember its own bank cannot sell what it
    // banked -- it has no reason to walk to a box it does not know holds
    // anything, so everything it ever gathered leaves the economy for good.
    // This is not omniscience: it is remembering your own container, which is
    // the most ordinary thing a player does.
    obs.bankOpen = obs.atBank;
    if (obs.atBank) {
        const u32 box = client.BankContainer();
        std::vector<market::Stock> fresh;
        const usize n = client.ContainerItemCount(box);
        for (usize i = 0; i < n; ++i) {
            u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
            if (!client.ContainerItemAt(box, i, &serial, &gfx, &amount, &hue)) continue;
            // Hue first, graphic fallback (S1): ore and the iron ingot are
            // one graphic for every metal, so a coloured deposit in the bank
            // must be read by its hue or it merges into the plain-iron count.
            const char* name = econ::ItemNameForGraphicAndHue(gfx, hue);
            if (!name) continue;          // nothing we have a name for
            if (amount == 0) amount = 1;
            bool merged = false;
            for (market::Stock& k : fresh) {
                if (k.item == name) { k.qty += amount; merged = true; break; }
            }
            if (!merged) fresh.push_back({name, static_cast<i32>(amount)});
        }
        obs.bank = std::move(fresh);
    } else {
        // Away from the box: what the character REMEMBERS is what it has.
        obs.bank = state_.bank;
    }

    // The pack, as the M7 economy layer wants it: quantities keyed by itemdef
    // defname. Built from what THIS life produces and consumes, so the final
    // list is a handful of names rather than every item on the shard.
    //
    // NOT graphic-only anymore (S1, docs/CRAFTER_RUN_2026_08_30.md #20). One
    // name can have several graphics -- iron ingots are 0x1BEF/0x1BF0/0x1BF1
    // by stack size -- but the reverse is also true and is the trap the old
    // per-name GraphicsForItem() sum fell into: ore is ONE graphic for every
    // metal, and so is the iron ingot for its twelve special colours, so
    // summing BackpackItemCount() over "every graphic i_ore_iron uses" also
    // counted every coloured vein in the pack as plain iron. A single
    // hue-resolved pass over the actual pack contents, mirroring the bank
    // box above, is the only way to keep them apart.
    if (needCfg_.profession) {
        std::unordered_map<std::string, i32> packByName;
        {
            const u32 packBox = client.BackpackSerial();
            const usize pn = client.ContainerItemCount(packBox);
            for (usize i = 0; i < pn; ++i) {
                u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
                if (!client.ContainerItemAt(packBox, i, &serial, &gfx, &amount, &hue))
                    continue;
                const char* name = econ::ItemNameForGraphicAndHue(gfx, hue);
                if (!name) continue;          // nothing we have a name for
                packByName[name] += (amount ? amount : 1);
            }
        }
        auto countInto = [&](const std::string& item) {
            for (const market::Stock& have : obs.pack) {
                if (have.item == item) return;   // already counted
            }
            const auto it = packByName.find(item);
            obs.pack.push_back({item, it != packByName.end() ? it->second : 0});
        };
        for (const std::string& it : needCfg_.profession->produces) countInto(it);
        for (const std::string& it : needCfg_.profession->consumes) countInto(it);

        // AND EVERY METAL THE PACK ACTUALLY HOLDS, listed or not.
        //
        // The two loops above only fill names this profession `produces` or
        // `consumes` -- "i_ore_iron" and "i_ingot_iron" for a mining smith.
        // The hue pass, though, now names a coloured vein HONESTLY, so
        // i_ore_rusty / i_ingot_bronze resolve to names that are in neither
        // list and were simply DROPPED from obs.pack. Before S1 they were at
        // least counted (wrongly) as iron; after it they vanished, which is
        // the worse failure: the gather goal cannot see its own haul, the
        // smelt goal has nothing to melt and the bank goal nothing to put
        // away, all while the pack is full.
        //
        // This is not a rare tail. r_default_rock -- the region type every
        // ordinary rock on the map uses -- weights, out of ~100 parts
        // (runtime/scripts/core/regiontypes.scp:19-37):
        //     50.0 mr_iron, 10.0 mr_nothing, 8.0 mr_rusty,
        //      6.0 mr_old_copper, 6.0 mr_dull_copper, 5.0 mr_bronze, ...
        // and every one of those four coloured ores carries SKILL=1.0,30.0,
        // the same band as mr_iron (core/regionresources.scp:259-285). So a
        // 50-skill miner is inside the band for all of them and about a
        // quarter of what it digs up is not iron at all.
        //
        // obs.bank needs no equivalent: its pass above is unfiltered and
        // already keeps every name the hue lookup resolves.
        //
        // AND EVERY REAGENT, for the same reason and a sharper one. A spell
        // consumes what the SPELL lists, not what the profession's `consumes`
        // happens to name: the mage entry names four reagents
        // (Professions.cpp) while Night Sight -- the first spell practice
        // reaches for -- costs spider silk and sulfurous ash, neither of them
        // on that list. Counting only the listed four would report a pouch of
        // 250 ash as EMPTY, and the restock errand would then buy ash forever.
        for (const auto& kv : packByName) {
            if (kv.first.compare(0, 6, "i_ore_") == 0 ||
                kv.first.compare(0, 8, "i_ingot_") == 0 ||
                kv.first.compare(0, 7, "i_reag_") == 0)
                countInto(kv.first);
        }
    }

    // Which of this plan's trainable skills have already been refused. Read
    // from memory, so one wasted walk teaches the character for good -- and
    // so the answer survives a logout.
    for (usize i = 0; i < state_.plan.skills.size(); ++i) {
        if (i >= state_.plan.viaTrainer.size() || !state_.plan.viaTrainer[i]) continue;
        const int id = state_.plan.skills[i].skillId;
        const TrainerFor* tf = TrainerForSkill(id);
        // Either the trade is exhausted (several NPCs of it have said no), or
        // one of them has said the character is already past teaching -- which
        // no other NPC can undo, so it counts on its own.
        if (state_.memory.TrainerSaysMaxed(id) ||
            (tf && state_.memory.TrainerRefused(id, tf->trade))) {
            obs.trainerRefusedSkills.push_back(id);
        }
    }

    // Which of this life's own tools it is actually holding. Checked in the
    // pack AND in both hands: several Sphere gathering skills read SRC.WEAPON,
    // so "carried" and "wielded" are different questions.
    if (needCfg_.profession) {
        for (const prof::ToolNeed& t : needCfg_.profession->tools) {
            for (u16 g : t.graphics) {
                if (client.FindBackpackItemByGraphic(g) ||
                    client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    obs.toolsHeld.push_back(t.name);
                    break;
                }
            }
        }
    }

    // What this life wants to BUY next. A generic tradesman teaches to 30.0
    // (sphere.ini NPCTrainPercent=30 of a GM's 100.0); a guildmaster overrides
    // to 50.0. The ceiling passed here is the lower one, so the character
    // never pays for a skill it has already grown past a plain trainer.
    // Which skill this life should PRACTISE -- do the thing that raises it.
    // Distinct from wantTrainSkill, which is a skill to BUY from a guildmaster
    // and stops at 30.0. Practice is how a skill reaches 100.
    obs.wantPracticeSkill = -1;
    for (const SkillTarget& t : state_.plan.skills) {
        if (obs.SkillTenths(t.skillId) >= t.tenths) continue;
        if (t.skillId != rules::kMeditation &&
            t.skillId != rules::kMagery) continue;   // see DoPracticeSkill
        obs.wantPracticeSkill = t.skillId;
        break;
    }

    // The reagent shopping list PRACTICE_SKILL left behind, if any, minus
    // anything the pack has since acquired. Kept here rather than in the goal
    // so it survives a goal change: the whole point is that a DIFFERENT goal
    // (BUY_SUPPLIES) does the fetching.
    // Observe is const, so the list itself is pruned where it is USED
    // (DoBuySupplies); what the need model sees is only the still-missing part.
    // The Magery table is data (data/revolution_spells.tsv, exported from the
    // shard's own spells_magery.scp by tools/spellgen.py). Load it here, once,
    // because BOTH the practice goal and the food errand ask DefForSpell what
    // a cast costs and neither may run before the table exists.
    spell::LoadSpellTable(client.DataDir());
    obs.practiceReagentsShort.clear();
    for (const std::string& r : reagentWants_) {
        if (market::QtyOf(obs.pack, r) <= 0)
            obs.practiceReagentsShort.push_back(r);
    }
    obs.practiceReagentQty = reagentWantQty_;

    // THE BOOK, AND WHAT IS IN IT.
    //
    // i_spellbook is ITEMDEF 0efa on this shard. The count is what the client
    // has been told is inside it, which is only populated after the book has
    // been opened once -- so 0 here means "no book, or a book we have not
    // looked in yet", and the goal opens it rather than assuming it is empty.
    // A BOOK IN THE HAND IS STILL A BOOK. This looked in the backpack only,
    // while the tools scan a few lines above checks the same graphic in the
    // pack AND both hands -- so every "needs considered" line reported
    // held=[spellbook,] while spellbookSerial stayed 0. DoFillSpellbook trusts
    // this value, so Ilyandra spent a whole life believing she had no book:
    // "opening the book" never fired once in 1,300 log lines, she tried to BUY
    // a second spellbook 32 times, and Magery could never be practised
    // ("Create Food is not in this character's spellbook") because the book she
    // was wearing was invisible to the only code that reads it.
    obs.spellbookSerial = client.FindBackpackItemByGraphic(kSpellbookGraphic);
    if (!obs.spellbookSerial) {
        for (u8 layer : {kLayerHand1, kLayerHand2}) {
            if (client.EquippedGraphicAt(layer) == kSpellbookGraphic) {
                obs.spellbookSerial = client.EquippedAtLayer(layer);
                break;
            }
        }
    }
    obs.spellsKnown =
        obs.spellbookSerial
            ? static_cast<int>(client.ContainerItemCount(obs.spellbookSerial))
            : 0;

    obs.wantTrainSkill = NextSkillToBuy(state_.plan, obs, 300);
    if (obs.wantTrainSkill >= 0) {
        for (const SkillTarget& t : state_.plan.skills) {
            if (t.skillId == obs.wantTrainSkill) { obs.wantTrainTarget = t.tenths; break; }
        }
    }

    return obs;
}

// ---------------------------------------------------------------------------
// Learning. Only from things actually observed.
// ---------------------------------------------------------------------------

// Take the axe in hand, unequipping whatever is there first.
//
// A hand already holding something cannot take a second weapon: Sphere answers
// the lift-and-wear with "You put the hatchet in your pack" and the character
// keeps swinging the newbie katana at trees. So this is two actions, and the
// first live run is the reason it is not one.
//
// Returns true while the arming is still in progress (the caller should wait),
// false when the axe is in hand or there is no axe to arm.
bool Runner::ArmAxe(Client& client, const Observation& obs) {
    if (AxeInHand(client)) return false;
    if (client.ActionBusy()) return true;

    const u32 hatchet = FindAny(client, kHatchet, 2);
    const u32 axe = hatchet ? hatchet : FindAny(client, kAxe, 2);
    if (!axe) return false;   // nothing to arm; the caller decides what that means

    // Clear BOTH hands. Which one the axe wants is the shard's decision, so
    // the only reliable way to make room is to empty the pair.
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u32 worn = client.EquippedAtLayer(layer);
        if (!worn) continue;
        LogLine("arming the axe: taking off 0x%04X from layer %u first",
                client.EquippedGraphicAt(layer), static_cast<unsigned>(layer));
        client.ActionUnequip(worn);
        nextActionMs_ = obs.nowMs + 1400;
        return true;
    }

    // Layer 0 = "wherever this belongs". Naming a layer here is how the first
    // live run earned `worn on a different layer` and never armed its axe.
    LogLine("arming the axe");
    client.ActionEquip(axe, kLayerServerChooses);
    nextActionMs_ = obs.nowMs + 1600;
    return true;
}

// ---------------------------------------------------------------------------
// Holding the build to its caps.
//
// A build plan that is never enforced is a wish. Revolution's caps are 700.0
// skill and 225 stat, and both are reached by ACCUMULATION -- so a character
// that never locks anything eventually spends its budget on whatever it
// happened to use most, not on what it planned.
//
// The policy is deliberately narrow, because the plan is:
//   * a PLANNED skill at or past its target  -> LOCK  (stop spending budget)
//   * a PLANNED skill below its target        -> UP    (keep training it)
//   * an UNPLANNED skill                      -> left alone. The 200 unresolved
//     points are unspent ON PURPOSE, and setting them DOWN would quietly
//     decide the rest of the build.
//   * a stat at or past its target            -> LOCK
//   * a stat below its target                 -> UP
//
// Nothing here raises a skill or a stat. It moves the arrow beside it, which
// is what a player does, and the server remains free to refuse.
void Runner::MaintainBuildLocks(Client& client, const Observation& obs) {
    if (obs.nowMs < nextLockCheckMs_) return;
    nextLockCheckMs_ = obs.nowMs + 30000;

    const rules::Profile& p = rules::Revolution();

    // --- DO NOT LOCK EARLY. -----------------------------------------------
    //
    // A lock is an END-OF-BUILD instrument. Its only job is to stop a finished
    // part of the build from eating budget the unfinished parts still need,
    // and that budget is only scarce near the cap. A young character has
    // hundreds of points of headroom, so locking anything then just freezes
    // growth for no gain -- and a locked stat cannot fall either, which is
    // exactly the redistribution a build near the cap depends on.
    //
    // So: nothing is locked until the character is actually approaching the
    // cap. Below the gate, everything trains up and no lock packet is sent at
    // all. The gates are the last 100 skill points and the last 25 stat
    // points -- the region where the caps start to bind.
    const i32 skillSum = obs.SkillSumTenths();
    const i32 statSum  = obs.str + obs.dex + obs.intel;
    const bool skillsNearCap = skillSum >= p.totalSkillCapTenths - 1000;   // 600.0 of 700.0
    const bool statsNearCap  = statSum  >= p.totalStatCap - 25;            // 200 of 225

    if (!lockGateLogged_ && (skillsNearCap || statsNearCap)) {
        lockGateLogged_ = true;
        LogLine("build: approaching the caps (skills %.1f/%.1f, stats %d/%d) -- "
                "lock management starts now",
                skillSum / 10.0, p.totalSkillCapTenths / 10.0, statSum,
                p.totalStatCap);
    }

    if (skillsNearCap) {
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 have = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (have < 0) continue;   // the server has not told us yet
            const u8 want = (have >= t.tenths) ? build::kLockLocked : build::kLockUp;
            const i32 now = client.PlayerSkillLock(static_cast<u16>(t.skillId));
            if (now == static_cast<i32>(want)) continue;
            LogLine("build: %s at %.1f/%.1f -> %s", rules::SkillName(t.skillId),
                    have / 10.0, t.tenths / 10.0,
                    want == build::kLockLocked ? "LOCK" : "train up");
            client.ActionSetSkillLock(static_cast<u16>(t.skillId), want);
        }
    }

    if (statsNearCap) {
        // The client is never told a stat's lock state, so each transition is
        // sent once rather than reconciled against the server.
        const struct { u8 code; i32 have; i32 target; const char* name; } kStats[3] = {
            {0, obs.str,   state_.plan.targetStr, "STR"},
            {1, obs.dex,   state_.plan.targetDex, "DEX"},
            {2, obs.intel, state_.plan.targetInt, "INT"},
        };
        for (const auto& s : kStats) {
            const u8 want = (s.have >= s.target) ? build::kLockLocked : build::kLockUp;
            if (statLockSent_[s.code] == want + 1) continue;
            statLockSent_[s.code] = static_cast<u8>(want + 1);
            LogLine("build: %s at %d/%d -> %s", s.name, s.have, s.target,
                    want == build::kLockLocked ? "LOCK" : "train up");
            client.ActionSetStatLock(s.code, want);
        }
    }
}

// ---------------------------------------------------------------------------
// WHAT EVERY NEW PLAYER ALREADY KNOWS (docs/LIFE_GATE_WAVE1.md theme 1).
//
// The M4 brief's Phase 15 rule still holds: "Seed only what the character
// would reasonably know at creation ... Everything else should be learned."
// It used to mean only "a lumberjack knows Yew has woods" -- but wave-1's
// evidence was two failures of the SAME shape, not one:
//
//   Vorar (lumberjack): GATHER_LOGS spun on "no known source of that
//   resource" with an empty places/resources memory.
//   Draver / Lyra (smith / scribe): the very first BANK goal failed "no
//   banker in sight" at 00:32-00:33 -- DoBank only ever scans mobiles
//   already in view, and nothing had ever told either of them where a
//   counter was.
//
// So this now delegates to the pure uo::life::SeedNewbieKnowledge
// (newbie_knowledge.h, unit-tested directly against the shipped atlas in
// tests/newbie_knowledge.cpp), which seeds the home bank and healer and
// provisioner alongside the resource lead this always seeded. This wrapper's
// only job is what a live session alone can supply: the atlas itself, the
// moment "world knowledge is ready", and the once-per-life guard.
//
// ANCHORED ON state_.homeCity, NOT ON WHEREVER THE CHARACTER IS STANDING.
// The shard's own chargen spawn point (map0_starts.scp) is not driven by
// Profession::homeCities, so the two can differ -- and what a lumberjack
// knows on day one is "Yew has woods", not "wherever I happened to spawn has
// woods nearby".
//
// A DIFFERENT EVENT NAME ON PURPOSE. A character already carrying the older
// "common_knowledge_seeded" mark from a prior run is not "already seeded" by
// today's fuller definition -- it never got a bank, healer or provisioner --
// so re-seeding under a new name picks it up on its next load rather than
// silently skipping it forever.
void Runner::SeedNewbieKnowledge(Client& client, i64 nowMs) {
    // Older lives carry the first, resource-only seed marker.  That marker
    // predates common_knowledge_bank, so treating it as complete leaves a
    // miner with a BANK goal but no counter it can route to.  The seed is
    // idempotent; only skip when this life has the complete current version.
    if (state_.memory.HasEvent("newbie_knowledge_seeded") &&
        state_.memory.BestPlace("common_knowledge_bank")) return;
    if (!client.WorldKnowledgeReady()) return;

    const world_atlas::Atlas* atlas = client.WorldAtlas();
    if (!atlas) return;

    const usize placesBefore = state_.memory.Places().size();
    const usize resourcesBefore = state_.memory.Resources().size();

    life::SeedNewbieKnowledge(state_, needCfg_.profession, state_.homeCity,
                              *atlas, nowMs);

    state_.memory.NoteEvent("newbie_knowledge_seeded", state_.homeCity.c_str(),
                            "", client.PlayerX(), client.PlayerY(), nowMs);
    LogLine("newbie knowledge: %zu place(s) and %zu resource hint(s) seeded "
            "near home (%s) -- everything else is earned",
            state_.memory.Places().size() - placesBefore,
            state_.memory.Resources().size() - resourcesBefore,
            state_.homeCity.empty() ? "nowhere yet" : state_.homeCity.c_str());
}

void Runner::LearnFromObservation(Client& client, const Observation& obs) {
    // Keep what the open box said. Observe() is const by design -- it is the
    // ephemeral half of the truth split -- so the remembering happens here,
    // which is the function whose whole job is "what did I learn".
    if (obs.bankOpen) {
        state_.bank = obs.bank;
        state_.bankSeenMs = obs.nowMs;
    }
    // NOTHING is written here. Standing where trees are visible is not
    // knowledge worth keeping: doing so gave the character 64 imaginary
    // "stands" after one session, which it then preferred over asking the
    // atlas, and it spent four sessions working scrub 210 tiles short of the
    // real Yew woods. A stand is recorded only where a chop YIELDED (see
    // DoGatherLogs), and leads come from HintResource.
    if (obs.atBank) {
        // Pack emptied: stop reacting to the overflow message that got us here.
        // (The `+ 1` that used to be here is now inside JournalNowMs, which
        // returns an exclusive mark for every caller rather than only this one.)
        overloadWatchMs_ = client.JournalNowMs();
        // (The place itself was recorded when the box was opened, from the
        // banker's own position -- see above.)
        if (!state_.memory.HasEvent("bank_learned")) {
            state_.memory.NoteEvent("bank_learned", "opened a bank box", "bank",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("memory_learned=PLACE kind=bank at %d,%d", obs.x, obs.y);
        }
    }

    // A healer we have actually seen. The world model records the sighting;
    // this copies it into the character's own memory so it survives logout.
    const travel::ServiceSighting* healer =
        client.Knowledge().RecentService(wm::Service::Healer, obs.nowMs,
                                         60 * 60 * 1000);
    if (healer) {
        state_.memory.NotePlace("healer", healer->title.c_str(), healer->x,
                                healer->y, healer->z, obs.nowMs);
    }

    state_.memory.ExpireDanger(obs.nowMs);
}

// ---------------------------------------------------------------------------
// Unreachable-foe memory (audit section 3.7). Lives on the RUNNER, not on the
// goal, so abandoning a fight and coming back does not forget it.
// ---------------------------------------------------------------------------

// A destination we already travelled to this session and found treeless. The
// memory failure count reorders things eventually, but only a hard skip stops
// the same no-op trip repeating within one session.
bool Runner::IsDeadTarget(i32 x, i32 y) const {
    for (const auto& d : deadTargets_) {
        if (TileDist(d.first, d.second, x, y) <= 8) return true;
    }
    return false;
}

bool Runner::IsUnreachable(u32 serial, i64 nowMs) const {
    for (const auto& e : unreachable_) {
        if (e.first == serial && nowMs < e.second) return true;
    }
    return false;
}

void Runner::MarkUnreachable(u32 serial, i64 nowMs) {
    const i64 until = nowMs + 30 * 1000;
    for (auto& e : unreachable_) {
        if (e.first == serial) { e.second = until; return; }
    }
    unreachable_.emplace_back(serial, until);
    if (unreachable_.size() > 32) unreachable_.erase(unreachable_.begin());
}

// ---------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------

bool Runner::Checkpoint(Client& client, i64 nowMs, const char* why) {
    if (!configured_) return false;
    state_.checkpointMs = nowMs;
    state_.goal = planner_.Current();
    if (client.IsInWorld()) {
        state_.lastKnownGold = client.PlayerGold();
        state_.lastKnownStr  = client.PlayerStr();
        state_.lastKnownDex  = client.PlayerDex();
        state_.lastKnownInt  = client.PlayerInt();
        state_.lastKnownX    = client.PlayerX();
        state_.lastKnownY    = client.PlayerY();
        state_.lastKnownDead = client.IsDead();
        state_.lastKnownSkills.clear();
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (base >= 0) state_.lastKnownSkills.push_back({t.skillId, base});
        }
    }
    std::string err;
    if (!store_.Save(state_, &err)) {
        LogError("[life] checkpoint failed (%s): %s\n", why, err.c_str());
        return false;
    }
    lastCheckpointMs_ = nowMs;
    LogLine("checkpoint (%s) -> %s", why,
            store_.PathFor(state_.identity.identityId).c_str());
    // The end-of-session verdict is also the only one a run gets if it never
    // reaches a clean WindDown -- a crash, a disconnect, an operator kill.
    // Checkpoint fires far more often than that (periodic + several action
    // sites), so this is gated to once per kHistogramIntervalMs rather than
    // reprinting on every save; same "session_goals" prefix as the WindDown
    // call, so one grep catches both (S2.8).
    if (nowMs - lastHistogramMs_ >= kHistogramIntervalMs) {
        LogGoalHistogram();
        lastHistogramMs_ = nowMs;
    }
    return true;
}

void Runner::EndSession(const char* why) {
    if (phase_ == Phase::WindDown || phase_ == Phase::LoggingOut ||
        phase_ == Phase::Done) {
        return;
    }
    LogLine("session_end_requested reason=\"%s\"", why ? why : "");
    phase_ = Phase::WindDown;
    windDownStartedMs_ = lastTickMs_;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void Runner::Tick(Client& client, i64 nowMs) {
    if (!configured_ || finished_) return;
    lastTickMs_ = nowMs;

    switch (phase_) {
        case Phase::AwaitWorld: {
            if (!client.IsInWorld()) return;
            if (sessionStartMs_ == 0) {
                sessionStartMs_ = nowMs;
                // RestTick's blockedForMs is obs.nowMs - lastRealErrandMs_;
                // left at its 0 sentinel, the very first idle tick of a
                // session reads as "blocked" since session start (S2_WIRING
                // _PLAN.md S2.2), a false Stagnant before any real errand has
                // even had a chance to run. Stamping it here gives it the
                // same origin as the session clock it is measured against.
                lastRealErrandMs_ = nowMs;
                sessionStartJournalMs_ = client.JournalNowMs();
                // The server has to tell us the skills before anything can be
                // reconciled against them.
                client.ActionRequestSkills();
                LogLine("in world; asked the server for the skill list");
                nextActionMs_ = nowMs + 2500;
                return;
            }
            if (nowMs < nextActionMs_) return;
            phase_ = Phase::Reconcile;
            return;
        }

        case Phase::Reconcile: {
            Observation obs = Observe(client, nowMs);
            // Rehydrate a pending corpse run after a reconnect.  Object
            // serials are deliberately not persisted, but the recorded death
            // tile is sufficient to travel back and discover the current
            // corpse object there.
            // A reconnect may happen after resurrection but before the corpse
            // run.  The fresh client has no session-local DeathRecord in that
            // case, yet the durable corpse_pending event is still authoritative.
            if (!client.Knowledge().LastDeath().valid) {
                for (auto it = state_.memory.Events().rbegin();
                     it != state_.memory.Events().rend(); ++it) {
                    if (it->kind == "corpse_recovered" || it->kind == "corpse_abandoned")
                        break;
                    if (it->kind == "corpse_pending") {
                        client.Knowledge().NoteDeath(it->x, it->y, 0, "", nowMs);
                        obs = Observe(client, nowMs);
                        LogLine("corpse run: restored pending death at %d,%d", it->x, it->y);
                        break;
                    }
                }
            }
            SeedNewbieKnowledge(client, nowMs);
            const ReconcileReport rep = Reconcile(&state_, obs);

            LogLine("reconciliation: %s, %d field(s) differed",
                    rep.firstEverLogin ? "first ever login" : "resuming a life",
                    rep.driftFields);
            for (const ReconcileLine& l : rep.lines) {
                LogLine("  %-12s persisted=%-10s server=%-10s -> %s",
                        l.field.c_str(), l.persisted.c_str(), l.server.c_str(),
                        l.result.c_str());
            }
            if (rep.goalDropped) {
                LogLine("  restored objective DROPPED: %s", rep.goalDropReason.c_str());
            } else if (state_.goal.active) {
                LogLine("  restored objective KEPT: %s (progress %d)",
                        GoalKindName(state_.goal.kind), state_.goal.progress);
            }
            planner_.Mutable() = state_.goal;

            // Transient observations from the previous life are gone by
            // construction -- they were never persisted -- but the runtime
            // caches that DO exist in this process are cleared explicitly so
            // nothing carries over between sessions in one host.
            unreachable_.clear();
            chopTargetValid_ = false;
            chopCursorPending_ = false;
            travelInFlight_ = false;

            session_ = SessionSummary{};
            session_.startedMs = nowMs;
            session_.goldStart = obs.gold;
            session_.skillTenthsStart = obs.SkillSumTenths();
            logsAtSessionStart_ = obs.logs;
            // Fresh gate for this session's histogram, so the "login
            // reconciled" Checkpoint two lines down does not immediately
            // fire LogGoalHistogram on an all-zero goalPicks[].
            lastHistogramMs_ = nowMs;

            state_.identity.sessions++;

            // Survival on, and left on. M3.9.1 proved this path live: a
            // character disengaged at ~32%, bandaged, and survived.
            client.SetSurvivalEnabled(true);
            client.EnsurePeaceMode();

            Checkpoint(client, nowMs, "login reconciled");
            phase_ = Phase::Live;
            return;
        }

        case Phase::Live: {
            if (!client.IsInWorld()) return;
            const Observation obs = Observe(client, nowMs);

            // WHEN DID THIS LIFE COME BACK? The robe the server hands out at a
            // resurrection is only identifiable by the moment it appears (see
            // CutResurrectionRobe), so the dead->alive transition has to be
            // noticed as it happens rather than inferred later from a robe
            // that might be anyone's.
            if (wasDead_ && !obs.dead) resurrectedAtMs_ = nowMs;
            wasDead_ = obs.dead;

            LearnFromObservation(client, obs);
            MaintainBuildLocks(client, obs);

            // THE REAGENT LIST IS SETTLED WHERE IT CAN BE SEEN, not only where
            // it is spent. BUY_SUPPLIES prunes it too, but once the last
            // reagent is bought its own need disappears and it is never
            // entered again -- so the last entry would never come off and
            // PRACTICE_SKILL would sit out its whole stand-down holding a full
            // pouch (run_gates/g_Aurelius.console.txt:717-731).
            if (!reagentWants_.empty() && obs.practiceReagentsShort.empty()) {
                LogLine("practice: the pouch is stocked again -- practising is "
                        "back on the table");
                reagentWants_.clear();
                planner_.ClearCooldown(GoalKind::PracticeSkill);
            }

            // --- session limits -------------------------------------------
            const i64 elapsed = nowMs - sessionStartMs_;
            if (cfg_.sessionLimitMs > 0 && elapsed >= cfg_.sessionLimitMs) {
                // Deferral rules from the audit (section 3.13): never end a
                // session on top of a corpse run, and never while dead.
                //
                // BUT A DEFERRAL WITHOUT A BOUND IS NOT A DEADLINE. A corpse
                // run stuck in a retry loop held both conditions true
                // forever, and Hector was still connected 5 minutes past a
                // 30-minute window (artifacts/wave_2026-09-02_verdict.md).
                // The grace buys the corpse run a chance to finish; after
                // that THE CLOCK WINS -- the in-flight goal and trip are
                // cancelled and the ordinary wind-down/logout path runs, the
                // same one a player uses. No process kill.
                const bool defer =
                    obs.dead || planner_.Current().kind == GoalKind::RecoverCorpse;
                if (!defer) {
                    EndSession("session time limit reached");
                    return;
                }
                if (elapsed >= cfg_.sessionLimitMs + kSessionOverrunGraceMs) {
                    LogLine("session_overrun goal=%s dead=%d over=%llds -- the "
                            "clock wins",
                            GoalKindName(planner_.Current().kind),
                            obs.dead ? 1 : 0,
                            static_cast<long long>(
                                (elapsed - cfg_.sessionLimitMs) / 1000));
                    if (client.TravelBusy())
                        client.TravelAbort("session time limit reached");
                    travelInFlight_ = false;
                    if (planner_.Current().active)
                        planner_.Finish(false, "session time limit reached", nowMs);
                    EndSession("session time limit reached (grace spent)");
                    return;
                }
            }
            if (cfg_.goalLimit > 0 && session_.goalsCompleted >= cfg_.goalLimit &&
                !obs.dead) {
                EndSession("goal limit reached");
                return;
            }

            // --- decide ----------------------------------------------------
            const std::vector<Need> needs =
                AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
            std::string why;
            const GoalKind previous = planner_.Current().kind;
            const bool wasActive = planner_.Current().active;
            if (planner_.Select(needs, obs, state_.memory, nowMs, &why)) {
                session_.goalsAttempted++;
                {
                    const int gi = static_cast<int>(planner_.Current().kind);
                    if (gi >= 0 && gi < static_cast<int>(GoalKind::Count))
                        session_.goalPicks[gi]++;
                }
                // S2.2: DecideRest's `blockedForMs` needs to know when a REAL
                // errand -- anything outside the Wander family -- was last
                // picked. One line at one site (S2_WIRING_PLAN.md S2.2).
                if (FamilyOf(planner_.Current().kind) != GoalFamily::Wander) {
                    lastRealErrandMs_ = obs.nowMs;
                }
                if (wasActive) {
                    // SELF-SUPERSESSION: "goal_changed=X from=X" -- the
                    // planner cleared the goal (Exhausted, or a completion)
                    // and re-picked the identical kind. Greppable before
                    // S2.8; now totalled for the session_goals verdict line.
                    if (previous == planner_.Current().kind) {
                        session_.selfSupersessions++;
                    }
                    LogLine("goal_changed=%s from=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind),
                            GoalKindName(previous), why.c_str());
                } else {
                    LogLine("goal=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind), why.c_str());
                }
                LogGoalChange(obs, why);
                // A new goal starts from a clean transient slate -- but only
                // if it is genuinely a NEW goal.
                //
                // Re-picking the SAME kind must not wipe the journey already
                // under way. Corran walked from Vesper to the Minoc tinker;
                // partway there TRAIN_AT_NPC hit its 300-second limit and was
                // re-picked as TRAIN_AT_NPC, which cleared travelInFlight_. He
                // then ARRIVED beside the tinker with the flag false, took the
                // "start another trip" branch instead of the "arrived, look
                // around" one, and set off for Britain 856 tiles away 61
                // milliseconds after getting there. He never scanned Minoc at
                // all -- one scan in the whole session, back in Vesper.
                const bool sameErrand = wasActive && previous == planner_.Current().kind;
                if (!sameErrand) {
                    chopTargetValid_ = false;
                    chopCursorPending_ = false;
                    travelInFlight_ = false;
                    travelAttempts_ = 0;
                    // Each DoXxx handler's lastXxxPlan_ exists only so
                    // LogPlan fires on a plan transition, not every tick
                    // (S2_WIRING_PLAN.md S2.0). Left across a goal change, a
                    // plan whose name happens to match the last one logged
                    // this session -- e.g. plan=disengage picked up again
                    // several goals later -- reads as "no change" and never
                    // logs, even though it is a brand new goal's first tick.
                    // Reset every one of them to its sentinel here, with the
                    // rest of the transient slate this guard already wipes
                    // (review finding 6).
                    lastCombatMove_ = life::CombatMove::Wait;
                    lastHealPlan_ = HealStep::None;
                    lastRestPlan_ = static_cast<RestStep>(0xFF);
                    lastRecoveryPlan_ = static_cast<RecoveryStep>(0xFF);
                    lastTrainPlan_ = TrainStep::Done;
                    lastCraftPlan_ = static_cast<CraftStep>(0xFF);
                    lastBandageAcquirePlan_ = AcquireStep::Done;
                    lastPotionAcquirePlan_ = AcquireStep::Done;
                    lastGarmentAcquirePlan_ = AcquireStep::Done;
                    lastToolAcquirePlanByItem_.clear();
                }
                // Per-errand counters belong to the errand. vendorChases_
                // bounds how long a wandering shopkeeper may be followed, and
                // a fresh goal deserves a fresh allowance -- otherwise one
                // restless vendor early in a session silences every purchase
                // made after it.
                vendorChases_ = 0;
                logsAtGoalStart_ = obs.logs;
            }
            // Select itself can end a goal -- an attempts-exhausted one goes
            // through Finish, so it can trip the noop-spin backstop without
            // ever reaching the completion path below.
            LogSpinIfDetected();

            // --- act -------------------------------------------------------
            // Point the trainer machinery at whatever the plan wants next.
            // Doing it here, from data, is what keeps `if (miner) ...` out of
            // the goal bodies.
            if (obs.wantTrainSkill >= 0) {
                if (const TrainerFor* tf = TrainerForSkill(obs.wantTrainSkill)) {
                    if (trainerTrade_ != tf->trade) {
                        trainerTrade_ = tf->trade;
                        trainerService_ = tf->service;
                        trainTrips_ = 0;
                        trainAsked_ = false;
                    }
                }
            }

            RunGoal(client, obs);

            // --- checkpoint ------------------------------------------------
            if (cfg_.checkpointIntervalMs > 0 &&
                nowMs - lastCheckpointMs_ >= cfg_.checkpointIntervalMs) {
                Checkpoint(client, nowMs, "periodic");
            }
            return;
        }

        case Phase::WindDown: {
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; return; }
            // Logging out somewhere hostile is how this project lost three
            // characters -- Source-X does not drop a combat-flagged connection
            // immediately, and one died in the gap AFTER logout_ack. So the
            // wind-down walks to a known bank first and only then logs out.
            client.EnsurePeaceMode();

            // The deadline is checked BEFORE the travel guard, and it ABORTS
            // the trip. Session B put this the other way round and a trip that
            // never arrived held the wind-down open for fourteen minutes --
            // the timeout was unreachable while travel was busy, which is not
            // a bound at all.
            // LOGGING OUT LATE IS CHEAP. LOGGING OUT IN THE WILD IS FATAL.
            //
            // Source-X leaves the body standing in the world after the
            // disconnect, so whatever is wandering past finishes the job:
            // Corwyn was killed by a Gazer a minute after one logout and by a
            // Wudgh in the same minute as the next, losing his tools, his
            // ingots and six shields. Four wild logouts in a row, each one
            // starting the next ghost walk.
            //
            // So the abort is bounded twice over rather than once. The plain
            // deadline still applies when the character is nowhere near
            // safety -- an unreachable target must not hold the session open
            // for fourteen minutes, which is what happened before it existed.
            // But when a safe spot is CLOSE and the trip is still moving, the
            // last stretch is worth another minute; giving up thirty tiles
            // out is how a 3-minute session ends in open country.
            // IS IT STILL WALKING, or is it stuck? That is the question the
            // original deadline was really asking. A trip that cannot arrive
            // must not hold the session open -- but one that is visibly
            // covering ground deserves to finish, and the old fixed bound
            // could not tell the two apart. It cut Corwyn off mid-stride in
            // Britain with the bank in sight.
            const i64 windDownMs = nowMs - windDownStartedMs_;
            if (client.PlayerX() != windDownLastX_ ||
                client.PlayerY() != windDownLastY_) {
                windDownLastX_ = client.PlayerX();
                windDownLastY_ = client.PlayerY();
                windDownMovedMs_ = nowMs;
            }
            const bool stillMoving =
                windDownMovedMs_ != 0 &&
                nowMs - windDownMovedMs_ < kWindDownStalledMs;
            const i64 budgetMs =
                stillMoving ? kWindDownGraceMs : kWindDownBudgetMs;

            const bool outOfTime = windDownMs > budgetMs;
            if (outOfTime && client.TravelBusy()) {
                LogLine("wind-down: the trip has run past its deadline (%llds, "
                        "%s); abandoning it and logging out where I stand",
                        static_cast<long long>(windDownMs / 1000),
                        stillMoving ? "still moving, but far too long"
                                    : "not moving");
                client.TravelAbort("wind-down deadline");
                travelInFlight_ = false;
                return;
            }
            if (client.TravelBusy()) return;

            const KnownPlace* bank = state_.memory.NearestPlace(
                "bank", client.PlayerX(), client.PlayerY());
            // A REACHED GUARDED SPOT IS SAFE ALREADY -- do not walk PAST it
            // looking for a bank. Without this a character standing in the
            // middle of a guarded town, but more than 6 tiles from any bank
            // this life has personally learned, read as unsafe and set off
            // on "asking the world for one" (below), which is a fresh
            // cross-world walk exactly like the one that just stranded
            // Dorvar: the abort at the top of this block only fires once a
            // trip is ALREADY committed and stalled, so the fix is to not
            // start an avoidable one in the first place when where we are
            // standing already answers to a guard.
            const wm::Region* hereRegion = client.CurrentRegion();
            const bool safeHere = client.BankContainer() != 0 ||
                                  windDownArrived_ ||
                                  (hereRegion && hereRegion->flags.guarded) ||
                                  (bank && TileDist(bank->x, bank->y, client.PlayerX(),
                                                    client.PlayerY()) <= 6);

            if (travelInFlight_) {
                travelInFlight_ = false;
                if (client.TravelSucceeded()) {
                    // Arriving IS the safe state. Requiring an open bank box
                    // here made the first live run re-plan the same
                    // already-arrived trip every tick for four minutes.
                    windDownArrived_ = true;
                    state_.memory.NotePlace("safe", "logout point", client.PlayerX(),
                                            client.PlayerY(), client.PlayerZ(), nowMs);
                    LogLine("wind-down: arrived somewhere safe at %d,%d",
                            client.PlayerX(), client.PlayerY());
                } else {
                    LogLine("wind-down: the trip did not arrive (%s)",
                            client.TravelFailureText());
                }
                return;
            }

            // Bounded, and it has to be: the alternative to "log out here" is
            // never logging out, and a character that never ends its session
            // never proves anything about resuming one.
            if (!safeHere && !outOfTime && windDownTrips_ < 2) {
                windDownTrips_++;

                // THE NEAREST SAFE PLACE, NOT THE NEAREST *REMEMBERED* ONE.
                //
                // NearestPlace already sorts by distance -- but only over what
                // this character has personally learned, and Corwyn had learned
                // nothing but Minoc. So standing in Britain, twenty tiles from
                // a bank, "nearest" meant Minoc: 1,752 tiles away.
                //
                // That produced a self-sustaining death loop. Four logouts in
                // the wild, every one of them in the Britain region --
                // (1422,1555), (1330,1978), (1618,1442), (1701,1367) -- and
                // Source-X leaves the body standing in the world after the
                // disconnect, so each one was killed where it stopped:
                //
                //   16:17 disconnected   16:18 'Corwyn' was killed by N'Gazer'
                //   16:25 disconnected   16:25 'Corwyn' was killed by N'Wudgh'
                //
                // He then woke as a ghost, walked to the Britain healer,
                // resurrected, was ordered home to Minoc, ran out of clock in
                // open country, and did it all again. Full loot took his tools,
                // his ingots and six heater shields on the way round.
                //
                // "That should be nearest, or near work place" (project owner,
                // 2026-08-30). The atlas knows every city's bank, so when the
                // remembered one is a journey, ask the world for a closer one.
                // Britain was a perfectly good place to log out; going home was
                // the whole mistake.
                const i32 known = bank
                                      ? TileDist(bank->x, bank->y, client.PlayerX(),
                                                 client.PlayerY())
                                      : -1;
                if (bank && known <= kWindDownPreferKnownWithin) {
                    LogLine("wind-down: travelling to a known bank at %d,%d, "
                            "%d tiles off (attempt %d)",
                            bank->x, bank->y, known, windDownTrips_);
                    travelInFlight_ =
                        client.TravelToPoint(bank->x, bank->y, 3, "logout_safe");
                } else {
                    if (bank)
                        LogLine("wind-down: the nearest bank this life has "
                                "learned is %d tiles away -- asking the world "
                                "for a closer one (attempt %d)",
                                known, windDownTrips_);
                    else
                        LogLine("wind-down: no bank learned yet; asking the "
                                "world for one (attempt %d)", windDownTrips_);
                    travelInFlight_ = client.TravelToService(wm::Service::Banker, nullptr);
                }
                if (!travelInFlight_) {
                    LogLine("wind-down: could not start the trip (%s); logging out here",
                            client.TravelFailureText());
                }
                return;
            }
            if (!safeHere) {
                LogLine("wind-down: giving up on reaching a safe spot after %d "
                        "attempt(s); logging out at %d,%d and recording it",
                        windDownTrips_, client.PlayerX(), client.PlayerY());
                state_.memory.NoteEvent("logout_unsafe",
                                        "could not reach a known-safe spot", "",
                                        client.PlayerX(), client.PlayerY(), nowMs);
            }

            session_.endedMs = nowMs;
            session_.goldEnd = client.PlayerGold();
            {
                i32 sum = 0;
                for (const SkillTarget& t : state_.plan.skills) {
                    const i32 b = client.PlayerSkillBase(static_cast<u16>(t.skillId));
                    if (b >= 0) sum += b;
                }
                session_.skillTenthsEnd = sum;
            }
            session_.logsGathered =
                logsAtSessionStart_ >= 0
                    ? std::max(0, static_cast<i32>(client.BackpackItemCount(kLog)) -
                                      logsAtSessionStart_)
                    : 0;
            session_.placesLearned = static_cast<i32>(state_.memory.Places().size());
            session_.suppliersLearned = static_cast<i32>(state_.memory.Suppliers().size());
            session_.cleanLogout = true;

            state_.identity.totalPlayTimeMs += (nowMs - sessionStartMs_);
            state_.sessions.push_back(session_);
            if (state_.sessions.size() > kMaxSessions) {
                state_.sessions.erase(state_.sessions.begin());
            }

            LogLine("session_summary duration=%llds goals=%d/%d gold=%d->%d "
                    "skills=%.1f->%.1f logs=+%d kills=%d deaths=%d places=%d "
                    "suppliers=%d",
                    static_cast<long long>((nowMs - sessionStartMs_) / 1000),
                    session_.goalsCompleted, session_.goalsAttempted,
                    session_.goldStart, session_.goldEnd,
                    session_.skillTenthsStart / 10.0, session_.skillTenthsEnd / 10.0,
                    session_.logsGathered, session_.kills, session_.deaths,
                    session_.placesLearned, session_.suppliersLearned);

            // HOW THE DAY WAS SPENT, as one greppable line. Extracted to
            // LogGoalHistogram (S2.8) so a crash or a killed session -- not
            // just a clean logout -- can still leave a verdict; see the
            // gated call inside Checkpoint. Unconditional and stamped here:
            // the clean-logout verdict always prints, and the periodic
            // Checkpoint call two lines down must not immediately repeat it.
            LogGoalHistogram();
            lastHistogramMs_ = nowMs;

            Checkpoint(client, nowMs, "clean logout");
            LogLine("logging out");
            client.ActionLogout();
            phase_ = Phase::LoggingOut;
            return;
        }

        case Phase::LoggingOut:
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; }
            return;

        case Phase::Done:
            finished_ = true;
            return;
    }
}

// HOW THE DAY WAS SPENT, as one greppable line.
//
// R1's exit proof is "at least four goal families, none above half the
// picks", and that has to be checkable without reading fifty thousand
// lines by eye. Printing the shape of the day is also the only way the
// monotony ever became visible: p0gate10 looked like a healthy session
// until its goals were counted and turned out to be CRAFT / BUY_SUPPLIES /
// EARN_GOLD in a ring and nothing else.
//
// S2.8: extracted out of the WindDown case so it is also reachable from
// Checkpoint (gated, kHistogramIntervalMs) -- a crash, a disconnect, or a
// session killed by the operator used to print no verdict at all. The
// arithmetic itself (families/picks/top/topFrac/varied) is
// uo::life::SummariseGoalPicks (Goals.cpp), a pure function reachable by
// ctest; this method is formatting only.
void Runner::LogGoalHistogram() const {
    const GoalHistogram h = SummariseGoalPicks(session_.goalPicks);

    // Counted by FAMILY, not by goal kind, for the summary numbers above --
    // but the breakdown text below still wants per-family and per-kind
    // counts, which is display detail, not the tested arithmetic.
    i32 famCount[static_cast<int>(GoalFamily::Count)] = {};
    for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
        const i32 n = session_.goalPicks[i];
        if (n <= 0) continue;
        famCount[static_cast<int>(FamilyOf(static_cast<GoalKind>(i)))] += n;
    }
    std::string hist;
    for (int f = 0; f < static_cast<int>(GoalFamily::Count); ++f) {
        if (famCount[f] <= 0) continue;
        if (!hist.empty()) hist += " ";
        char fc[64];
        std::snprintf(fc, sizeof(fc), "%s=%d(%.0f%%)",
                      GoalFamilyName(static_cast<GoalFamily>(f)), famCount[f],
                      h.picks ? (100.0 * famCount[f] / h.picks) : 0.0);
        hist += fc;
    }
    hist += " |";
    for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
        const i32 n = session_.goalPicks[i];
        if (n <= 0) continue;
        hist += " ";
        char cell[64];
        std::snprintf(cell, sizeof(cell), "%s=%d(%.0f%%)",
                      GoalKindName(static_cast<GoalKind>(i)), n,
                      h.picks ? (100.0 * n / h.picks) : 0.0);
        hist += cell;
    }
    LogLine("session_goals families=%d picks=%d top=%.0f%% varied=%d "
            "self_superseded=%d | %s",
            h.families, h.picks, h.topFrac * 100.0, h.varied ? 1 : 0,
            session_.selfSupersessions,
            hist.empty() ? "(none)" : hist.c_str());
}

// The one place a plan's step is logged. Callers emit this once per plan
// change (a `lastPlan*_` member compared against the new step), not once per
// tick -- per-tick emission is what produced the 311-line forge spam this
// slice exists to end.
void Runner::LogSpinIfDetected() {
    const GoalKind spun = planner_.TakeSpinDetected();
    if (spun == GoalKind::Count) return;
    LogLine("goal_spinning=%s reason=\"completed %d times in a row with "
            "progress 0 -- cooled off for a minute; this is a bug in that "
            "goal, not pacing\"",
            GoalKindName(spun), 5);
}

void Runner::LogPlan(const char* kind, const char* reason) const {
    LogLine("plan=%s reason=\"%s\"", kind, reason);
}

// AN ERRAND'S REASON, ON CHANGE OR ONCE A MINUTE -- never once per tick.
//
// The errands answer with a reason every tick on purpose: an unexplained
// stand-down is the defect that whole layer exists to end. But printing every
// one of them prints the TICK RATE, not the errand. Measured: 214 "potions:"
// lines in run_r4/w_Bruin.console.txt, 209 of them the identical "an action
// is already in flight" while a single 8-second vendor ask was outstanding.
//
// Same sentinel rule LogPlan already uses for plan steps, over text instead of
// an enum, and per tag so one errand's chatter cannot hide another's.
void Runner::LogErrandReason(const char* tag, const char* reason,
                             i64 nowMs) const {
    if (!tag || !reason || !reason[0]) return;
    ErrandLogSentinel& seen = errandLogSeen_[tag];
    if (seen.atMs != 0 && seen.reason == reason &&
        nowMs - seen.atMs < kErrandReasonRepeatMs)
        return;
    seen.reason = reason;
    seen.atMs = nowMs;
    LogLine("%s: %s", tag, reason);
}

// The ONLY legal way a plan hands the turn to another goal (S2_WIRING_PLAN.md
// S2.0). `to` is advisory only -- it is logged, never dispatched; the
// receiving goal is chosen by Planner::Select on the next tick from whatever
// need AssessNeeds already produces. The cooldown is load-bearing: without it
// Planner::Score treats `from` as still feasible and it can simply win again.
bool Runner::HandOff(GoalKind from, GoalKind to, i64 restMs, const char* why,
                     i64 nowMs) {
    planner_.Cooldown(from, nowMs + restMs);
    planner_.Finish(false, why, nowMs);
    LogLine("handoff=%s->%s reason=\"%s\"", GoalKindName(from), GoalKindName(to),
            why);
    nextActionMs_ = nowMs + 2000;
    return false;
}

bool Runner::VetoTripOverSessionBudget(Client& client, const Observation& obs,
                                       GoalKind goal, const char* goalName,
                                       i64 cooldownMs) {
    if (cfg_.sessionLimitMs <= 0) return true;   // no session clock to run out
    const i32 tiles = client.TravelLastPlannedTiles();
    // 0 means no plan has landed yet (this trip's TravelPlanRoute runs on a
    // later Client tick than the TravelToXxx() call that started it) or the
    // plan failed outright -- either way there is nothing here yet to judge,
    // and the ordinary travel-failure/replan machinery will be heard from on
    // its own. Vetoing on a stale zero would only ever wave a real trip
    // through, never wrongly block one, so that side is safe to skip too.
    if (tiles <= 0) return true;
    const i64 remainingMs = cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_);
    if (TripFitsSessionBudget(remainingMs, tiles, kWindDownBudgetMs)) return true;

    LogLine("goal_blocked=%s reason=\"not enough session left for the trip\" "
            "tiles=%d left=%llds need=%llds", goalName, tiles,
            static_cast<long long>(remainingMs / 1000),
            static_cast<long long>(
                (EstimateTripTimeMs(tiles) + kWindDownBudgetMs) / 1000));
    client.TravelAbort("not enough session left for the trip");
    travelInFlight_ = false;
    planner_.Cooldown(goal, obs.nowMs + cooldownMs);
    planner_.Finish(false, "not enough session left for the trip", obs.nowMs);
    return false;
}

void Runner::LogGoalChange(const Observation& obs, const std::string& why) {
    // Every need considered, not just the winner and the blocked ones. "Why
    // did it not do X" is only answerable if X appears somewhere.
    {
        const std::vector<Need> all =
            AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
        std::string line;
        for (const Need& n : all) {
            line += Fmt2("%s(%s%s %.2f) ", NeedKindName(n.kind), n.what.c_str(),
                         n.blocked ? " BLOCKED" : "", n.urgency);
        }
        std::string held;
        for (const std::string& t : obs.toolsHeld) { held += t; held += ","; }
        // SAY WHAT THE CHARACTER WANTS TO BUY, even when the answer is
        // "nothing". NeedSkillTraining is absent from this line whenever
        // NextSkillToBuy returns -1, and an ABSENT need is indistinguishable
        // from a need that was never asked about: run_m5/pair2 and p0gate1
        // both show a scribe with 317 gold and Meditation at 21.9 -- squarely
        // inside the 30.0 trainer ceiling -- producing no training need at
        // all, and the log gave no way to tell whether the planner had
        // decided against it or never considered it. "Why didn't it do X" has
        // to be answerable from the log.
        LogLine("needs considered: %s | tools defined=%zu held=[%s] "
                "want_train=%s (target %.1f)",
                line.empty() ? "(none)" : line.c_str(),
                needCfg_.profession ? needCfg_.profession->tools.size() : 0,
                held.c_str(),
                obs.wantTrainSkill >= 0 ? rules::SkillName(obs.wantTrainSkill)
                                        : "nothing",
                obs.wantTrainTarget / 10.0);
    }

    const std::vector<Need> needs =
        AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
    const std::vector<ScoredGoal> scored = planner_.Score(needs, obs, state_.memory);
    for (const ScoredGoal& g : scored) {
        if (g.kind != planner_.Current().kind) continue;
        LogLine("  score=%.1f", g.score);
        for (const std::string& r : g.reasons) LogLine("  reason: %s", r.c_str());
        break;
    }
    // Blocked goals are reported at every change, so "why didn't it do X"
    // never needs a packet trace to answer.
    for (const ScoredGoal& g : scored) {
        if (g.feasible) continue;
        LogLine("  BLOCKED_NEED %s: %s", GoalKindName(g.kind), g.blockedWhy.c_str());
    }
    (void)why;
}

// ---------------------------------------------------------------------------
// Goal execution
// ---------------------------------------------------------------------------

void Runner::RunGoal(Client& client, const Observation& obs) {
    if (obs.nowMs < nextActionMs_) return;

    // Bounded failure, checked before the body runs so a wedged goal cannot
    // keep acting after it has been abandoned.
    std::string exhaustedWhy;
    if (planner_.Exhausted(obs.nowMs, &exhaustedWhy)) {
        // AND REST IT. Running the full time limit without finishing is the
        // strongest possible evidence that this errand is not working right
        // now, and it was the one failure path with no cooldown -- so the very
        // next Select() saw the same unchanged need and started the identical
        // goal again with a fresh five minutes. Brannoc spent an entire
        // session on it:
        //
        //   goal_changed=TRAIN_AT_NPC from=TRAIN_AT_NPC
        //       reason="previous goal abandoned: ran 300s without finishing"
        //
        // five times over, walking Vesper to Minoc to Magincia looking for a
        // tinker, and finishing the session with goals=0/5, gold unchanged and
        // not one tenth of skill gained.
        //
        // Note this is NOT the spin the backstop catches. That one ends
        // instantly and repeatedly; this one never ends at all until the timer
        // kills it. Opposite symptoms, same cost -- one goal owning every
        // decision a character makes.
        const GoalKind spent = planner_.Current().kind;
        LogLine("goal_failed=%s reason=\"%s\"", GoalKindName(spent),
                exhaustedWhy.c_str());
        session_.goalsFailed++;
        planner_.Finish(false, exhaustedWhy.c_str(), obs.nowMs);
        planner_.Cooldown(spent, obs.nowMs + kExhaustedCooldownMs);
        return;
    }

    bool done = false;
    switch (planner_.Current().kind) {
        case GoalKind::Survive:               done = DoSurvive(client, obs); break;
        case GoalKind::Heal:                  done = DoHeal(client, obs); break;
        case GoalKind::RecoverCorpse:         done = DoRecoverCorpse(client, obs); break;
        case GoalKind::GetTool:               done = DoGetTool(client, obs); break;
        case GoalKind::ReplaceEquipment:      done = DoReplaceEquipment(client, obs); break;
        case GoalKind::Bank:                  done = DoBank(client, obs); break;
        case GoalKind::GatherLogs:            done = DoGatherLogs(client, obs); break;
        case GoalKind::TrainCombat:           done = DoTrainCombat(client, obs); break;
        case GoalKind::EarnGold:              done = DoEarnGold(client, obs); break;
        case GoalKind::TravelToRequiredPlace: done = DoTravel(client, obs); break;
        case GoalKind::TrainAtNpc:            done = DoTrainAtNpc(client, obs); break;
        case GoalKind::TradeWithPlayer:       done = DoTradeWithPlayer(client, obs); break;
        case GoalKind::Fish:                  done = DoFish(client, obs); break;
        case GoalKind::BuySupplies:           done = DoBuySupplies(client, obs); break;
        case GoalKind::Craft:                 done = DoCraft(client, obs); break;
        case GoalKind::GetFood:               done = DoGetFood(client, obs); break;
        case GoalKind::PracticeSkill:         done = DoPracticeSkill(client, obs); break;
        case GoalKind::FillSpellbook:         done = DoFillSpellbook(client, obs); break;
        case GoalKind::MakeBandages:         done = DoMakeBandages(client, obs); break;
        case GoalKind::MakeCloth:            done = DoMakeCloth(client, obs); break;
        case GoalKind::HarvestWool:          done = DoMakeCloth(client, obs); break;
        case GoalKind::BuyMount:             done = DoBuyMount(client, obs); break;
        case GoalKind::Explore:              done = DoExplore(client, obs); break;
        case GoalKind::Mine:                 done = DoMine(client, obs); break;
        case GoalKind::Smelt:                done = DoSmelt(client, obs); break;
        case GoalKind::TameAnimal:           done = DoTameAnimal(client, obs); break;
        case GoalKind::UpgradeGear:          done = DoUpgradeGear(client, obs); break;
        case GoalKind::IdleBriefly:           done = DoIdle(client, obs); break;
        case GoalKind::Count:                 break;
    }

    if (done) {
        LogLine("goal_completed=%s progress=%d",
                GoalKindName(planner_.Current().kind), planner_.Current().progress);
        session_.goalsCompleted++;
        // Count it toward satiation so the same errand does not own the whole
        // session. A life is train, earn, sell, hunt and company in turn, not
        // one loop repeated -- and goods that found no buyer simply going into
        // the bank is a fine end to an errand, not a failure to retry.
        planner_.NoteRan(planner_.Current().kind, obs.nowMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        // The anti-spin backstop, said out loud. A goal cooled off for
        // repeatedly succeeding at nothing is a BUG REPORT, not routine
        // pacing, and it must not be silent -- the three that got through so
        // far were each found by noticing a goal count in the thousands.
        LogSpinIfDetected();
        Checkpoint(client, obs.nowMs, "goal completed");
    }
}

bool Runner::DoTravel(Client& client, const Observation& obs) {
    if (obs.atWorkSite || obs.atBank) return true;
    // WHERE THIS LIFE'S WORK IS. This body walked every character toward a
    // forest -- a miner, a mage and a fisher included -- because the need it
    // answers had no profession gate and the destination was Lumber.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers.empty()) return true;   // nothing to travel to; work is here
    if (client.TravelBusy()) return false;
    if (travelAttempts_ >= 3) {
        LogLine("goal_failed=TRAVEL_TO_REQUIRED_PLACE reason=\"three trips did not arrive\"");
        planner_.Finish(false, "three trips did not arrive", obs.nowMs);
        return false;
    }
    if (!travelInFlight_) {
        travelAttempts_++;
        const KnownResourceSource* stand =
            state_.memory.BestResource(gathers.c_str(), obs.x, obs.y,
                                       obs.nowMs);
        if (stand) {
            travelInFlight_ =
                client.TravelToPoint(stand->x, stand->y, 4, gathers.c_str());
        } else {
            travelInFlight_ = client.TravelToResource(ResourceKindFor(gathers));
        }
        if (!travelInFlight_) {
            LogLine("travel: could not start (%s)", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 8000;
        }
        return false;
    }
    travelInFlight_ = false;
    if (client.TravelSucceeded()) {
        planner_.NoteProgress();
        return true;
    }
    LogLine("travel: did not arrive (%s)", client.TravelFailureText());
    planner_.NoteAttempt(obs.nowMs);
    return false;
}

// ---------------------------------------------------------------------------
// REST AND ROAM -- what a life does when nothing is pressing
// (include/uo/activities/rest.h, S2_WIRING_PLAN.md S2.2). DoExplore and
// DoIdle are both two-line forwarders into RestTick: DecideRest is the one
// place that turns "idle" into EXPLORE (go and learn something), REST (stand
// still and mean it), SETTLE (the session is ending, go somewhere safe) or
// STAGNANT (a fault to report, not a rest to take).
//
// "bots shouldnt be idle unless its state specifically" (project owner). This
// is what a character does instead of standing still, and it is not filler.
//
// Nearly every blocked need in this project is blocked for want of knowing
// WHERE something is: "no known supplier of a tongs", "carrying its own output
// with nobody known to buy it", "no 'tinker' reachable". Bruin finished a
// 25-minute session with session_summary places=1 -- he had seen one location
// all day, which is precisely why he knew no supplier for any of the three
// tools he was short of, and why he idled through 85% of his picks.
//
// So the EXPLORE step goes and looks at an unvisited shop, and reads the
// paperdolls of whoever is standing in it. That is how NearestMobileWithTrade
// and the supplier memory get anything to work with.
bool Runner::RestTick(Client& client, const Observation& obs, GoalKind owner) {
    RestSight see;
    see.sessionEnding = cfg_.sessionLimitMs > 0 &&
        (obs.nowMs - sessionStartMs_) >= cfg_.sessionLimitMs - kRestSettleLeadMs;
    // The same guarded-region read as MayWear's caller (Runner.cpp, around
    // inGuardedRegion) -- NOT flags.safe, which is the no-skill-gain flag, a
    // different fact. An open bank box counts too, matching wind-down's
    // safeHere.
    const wm::Region* here = client.CurrentRegion();
    see.somewhereSafe = (here && here->flags.guarded) || client.BankContainer() != 0;
    see.worthExploring = !exploredEverything_;
    // No direct regen signal exists on this shard; hunger stopping HP
    // regeneration is the fact include/uo/activities/heal.h is written
    // around, so "not hungry and not full" is the cheapest honest proxy.
    see.regenerating = !obs.hungry && obs.hp < obs.hpMax;
    see.hpFraction = obs.HpFraction();
    see.blockedForMs = obs.nowMs - lastRealErrandMs_;

    RestTuning tune;
    tune.restWhileBelowHp = needCfg_.healHpFraction;   // agrees with DecideHeal

    const RestPlan plan = DecideRest(see, tune);
    if (plan.step != lastRestPlan_) {
        LogPlan(RestStepName(plan.step), plan.reason);
        lastRestPlan_ = plan.step;
    }

    switch (plan.step) {
        case RestStep::Explore: {
            if (client.ActionBusy()) return false;

            // Arrived somewhere: LOOK. A place walked to and not looked at
            // teaches nothing, and the scan is the entire point of the errand.
            if (travelInFlight_ && !client.TravelBusy()) {
                travelInFlight_ = false;
                LogLine("explore: arrived at %d,%d -- reading who is here",
                        obs.x, obs.y);
                client.ActionScanMobiles();
                // RECORD IT BY ID, which is what TravelToUnexploredPlace
                // matches against. Storing an empty name would leave the
                // place forever unvisited and send the character back to it
                // on the next tick.
                state_.memory.NotePlace("explored", exploreTarget_.c_str(),
                                        obs.x, obs.y, obs.z, obs.nowMs);
                // AND REMEMBER THE NAME SEPARATELY, because the place record
                // cannot.
                //
                // NotePlace matches on kind AND position (Memory.cpp:34-40),
                // so two atlas entries that resolve to the SAME tile collapse
                // into one record -- and the later one OVERWRITES the name.
                // Minoc's cobbler and provisioner both sit on 2453,430, so
                // the single record's name flipped between them, `seen`
                // never contained both at once, and the pair was
                // re-nominated forever:
                //
                //   going to 'minoc_cobbler' -- somewhere new (15 place(s) known)
                //   arrived at 2453,430 -- reading who is here
                //   going to 'minoc_provisioner' -- somewhere new (15 place(s) known)
                //   arrived at 2453,430 -- reading who is here
                //
                // Eleven picks in a three-minute session, half of everything
                // the character did, and the place count never moved off 15.
                // Keeping the visited IDs here means an id is spent once
                // whatever tile it shares.
                if (!exploreTarget_.empty()) {
                    bool already = false;
                    for (const std::string& id : exploredIds_)
                        if (id == exploreTarget_) { already = true; break; }
                    if (!already) exploredIds_.push_back(exploreTarget_);
                }
                exploreTarget_.clear();
                // A newly-learned place may reveal more still unexplored;
                // only "nowhere new to go" below is allowed to latch this.
                exploredEverything_ = false;
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 3000;
                return true;   // one place per outing; the next tick re-decides
            }
            if (client.TravelBusy()) return false;

            // Somewhere with a service, that this character has not been to.
            // The places it already knows come from its own memory, so two
            // characters explore differently and a character never re-walks
            // its own ground.
            std::vector<std::string> seen;
            for (const KnownPlace& p : state_.memory.Places()) {
                if (!p.name.empty()) seen.push_back(p.name);
            }
            // Plus every id already walked to this session -- see the
            // arrival branch above for why the place records alone cannot
            // answer this.
            for (const std::string& id : exploredIds_) {
                bool dup = false;
                for (const std::string& s : seen) if (s == id) { dup = true; break; }
                if (!dup) seen.push_back(id);
            }
            // HOME, DELIBERATELY -- the one errand where HomeOrNearest's
            // "a shop is a shop wherever you stand" does not apply. Explore
            // with no fence walked Odessa Britain -> Cove -> Minoc, each hop
            // "the nearest unknown place" from wherever the last one left
            // her. A player idles around their own town.
            if (!client.TravelToUnexploredPlace(seen, &exploreTarget_,
                                                state_.homeCity.c_str())) {
                LogLine("explore: nowhere new to go (%s) -- standing down",
                        client.TravelFailureText());
                exploredEverything_ = true;
                return HandOff(GoalKind::Explore, GoalKind::IdleBriefly,
                               kExploredAllCooldownMs, "nowhere unexplored",
                               obs.nowMs);
            }
            travelInFlight_ = true;
            LogLine("explore: nothing else to do, so going to '%s' -- somewhere "
                    "new (%zu place(s) known so far)", exploreTarget_.c_str(),
                    seen.size());
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        case RestStep::Rest: {
            // A bounded no-op. It exists so a tick with nothing to do SAYS so
            // rather than spinning, and so the planner is never in a "no
            // goal" state.
            nextActionMs_ = obs.nowMs + 5000;
            return obs.nowMs - planner_.Current().startedAtMs > 15000;
        }
        case RestStep::Settle: {
            // Phase::WindDown owns the walk to safety and already refuses to
            // log out unsafe -- nothing new is built here.
            EndSession(plan.reason);
            return false;
        }
        case RestStep::Stagnant: {
            LogLine("goal_stagnant=%s reason=\"%s\"", GoalKindName(owner),
                    plan.reason);
            // The third Wander kind, cooled alongside the handoff so it does
            // not simply win the very next Select.
            planner_.Cooldown(GoalKind::TravelToRequiredPlace,
                              obs.nowMs + kStagnantCooldownMs);
            // The advisory `to` is only ever logged, never dispatched -- but
            // when the owner IS Explore, HandOff(Explore, Explore, ...)
            // still reads as a goal advising itself, which is nonsense on
            // its face. IdleBriefly is the honest advisory here: Explore
            // itself is what just went stagnant.
            const GoalKind to = owner == GoalKind::Explore
                                     ? GoalKind::IdleBriefly
                                     : GoalKind::Explore;
            return HandOff(owner, to, kStagnantCooldownMs, plan.reason,
                          obs.nowMs);
        }
    }
    return false;
}

bool Runner::DoExplore(Client& client, const Observation& obs) {
    return RestTick(client, obs, GoalKind::Explore);
}

bool Runner::DoIdle(Client& client, const Observation& obs) {
    return RestTick(client, obs, GoalKind::IdleBriefly);
}

}  // namespace uo::life
