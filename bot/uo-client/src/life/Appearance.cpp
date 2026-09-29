// Layer 1 appearance roll (docs/APPEARANCE_DESIGN.md). Extracted out of
// Client::SendCreateCharacter so it is a pure, unit-testable function rather
// than logic inline in the packet builder -- the packet builder now just
// calls AppearanceForIdentity and copies the result into
// build::CreateCharacterParams.
//
// Every bot used to be sent the same fixed skinHue/hairId/hairHue/shirtHue/
// pantsHue (the CreateCharacterParams defaults in include/uo/builders.h), so
// every character came out with the same face and the same shirt and pants
// color -- "new characters always having same face male/female shirt or
// pants or shoes same color we need diversity" (project owner, 2026-08-29).
// Appearance must still be STABLE for a given character (the same character
// looks the same on every login/recreate), so it is derived from identity
// rather than randomised per run, the same way main.cpp:411-427 derives a
// character's home city: h = h*131 + c over the lowercased
// "account.charname". Each attribute below reuses that exact hash shape with
// its own suffix appended to the identity string, so hair color, shirt
// color, etc. vary independently of each other instead of moving in
// lockstep.

#include "uo/appearance.h"

namespace uo {

namespace {

usize HashIdentity(const std::string& identityId, const char* suffix) {
    u64 h = 0;
    for (char c : identityId) h = h * 131 + static_cast<unsigned char>(c);
    h = h * 131 + static_cast<unsigned char>('.');
    for (const char* c = suffix; c && *c; ++c) h = h * 131 + static_cast<unsigned char>(*c);
    // Finalizer (splitmix64-style avalanche). h*131+c only ever multiplies
    // by an ODD constant, so h*131 preserves h's parity exactly -- bit 0 of
    // the raw accumulator is just the XOR-sum of every input byte, with no
    // mixing at all. Two equal digits (e.g. "account7.char7") then cancel
    // exactly and the low bit stops depending on which id was hashed. That
    // bit is exactly what `% 2` (the sex roll) reads, so without this step
    // sex could silently collapse to a constant for a whole class of
    // identity ids. The finalizer runs AFTER the h*131+c accumulation so it
    // does not change that idiom (main.cpp's home-city hash is untouched);
    // it only ensures every output bit here depends on the whole input
    // before any field reads a slice of it with `%`.
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return static_cast<usize>(h);
}

} // namespace

Appearance AppearanceForIdentity(const std::string& identityId) {
    Appearance a;

    // Sex. Builders.cpp offset 70 is a single byte, even=male/odd=female,
    // written from CreateCharacterParams::female.
    a.female = (HashIdentity(identityId, "sex") % 2) != 0;

    // Skin hue. Range is runtime/scripts/core/defs_hues.scp:52
    // "colors_skin {1002 1058}" (decimal), which is exactly Source-X's own
    // clamp: HUE_SKIN_LOW=0x3EA (1002) / HUE_SKIN_HIGH=0x422 (1058) in
    // server/Source-X/src/game/chars/CChar.cpp (CChar::InitPlayer, "Check
    // skin hue"). The old default 0x83EA (33770) was nowhere near this --
    // its low byte 0x3EA is the valid range's own low bound, so it reads
    // like a stray 0x8000 bit tacked onto an otherwise-correct value.
    {
        constexpr u16 kSkinLow = 1002, kSkinHigh = 1058;
        a.skinHue = static_cast<u16>(kSkinLow + (HashIdentity(identityId, "skin") % (kSkinHigh - kSkinLow + 1)));
    }

    // Hair style. Valid ids and the male/female exclusions come straight
    // from CChar::InitPlayer's human case (CChar.cpp, "Create hair"):
    // ITEMID_HAIR_SHORT..PONYTAIL (0x203B-0x203D) plus
    // ITEMID_HAIR_MOHAWK..TOPKNOT (0x2044-0x204A), with BUNS (0x2046)
    // female-only and RECEDING (0x2048) male-only -- ids and the exclusion
    // rule are both from server/Source-X/src/game/uo_files/
    // uofiles_enums_itemid.h:842-851 and CChar.cpp's InitPlayer. Anything
    // outside this set is silently discarded server-side (idHair =
    // ITEMID_NOTHING), so picking from it is what makes the request stick.
    {
        static const u16 kHairStyles[] = {
            0x203B, // short
            0x203C, // long
            0x203D, // ponytail
            0x2044, // mohawk
            0x2045, // pageboy
            0x2046, // buns       -- female only
            0x2047, // afro
            0x2048, // receding   -- male only
            0x2049, // 2 pigtails
            0x204A, // topknot
        };
        u16 candidates[10];
        usize nCand = 0;
        for (u16 id : kHairStyles) {
            if (id == 0x2046 && !a.female) continue;
            if (id == 0x2048 && a.female) continue;
            candidates[nCand++] = id;
        }
        a.hairId = candidates[HashIdentity(identityId, "hair_style") % nCand];
    }

    // Hair hue. runtime/scripts/core/defs_hues.scp:55 "colors_hair {1102
    // 1149}" (decimal). Source-X's own clamp (HUE_HAIR_LOW=0x44E/1102,
    // HUE_HAIR_HIGH=0x4AD/1197, same file as skin) is wider, but 1102-1149
    // is the shard's own curated range -- it is also exactly the "Normal
    // Hair Dye" NPC service range in runtime/scripts/items/
    // i_profession_barber.scp, with 1150-1197 reserved for the 500000gp
    // "Bright Hair Dye" upsell, so it is the range a starting character
    // plausibly has rather than one only a barber's premium service grants.
    {
        constexpr u16 kHairHueLow = 1102, kHairHueHigh = 1149;
        a.hairHue = static_cast<u16>(kHairHueLow + (HashIdentity(identityId, "hair_hue") % (kHairHueHigh - kHairHueLow + 1)));
    }

    // Beard: males only (CChar::InitPlayer forces idBeard to ITEMID_NOTHING
    // for fFemale before it even looks at race, so a female bot sending one
    // would just have it discarded -- but there is no reason to send bogus
    // data). Valid ids are ITEMID_BEARD_LONG..MOUSTACHE (0x203E-0x2041) and
    // ITEMID_BEARD_SH_M..GO_M (0x204B-0x204D), again from CChar.cpp's human
    // case. 0 (no beard) is included as its own candidate so male bots
    // aren't all bearded -- OSI character creation itself offers "no facial
    // hair" as a choice.
    if (!a.female) {
        static const u16 kBeardStyles[] = {
            0,      // no beard
            0x203E, // long
            0x203F, // short
            0x2040, // goatee
            0x2041, // moustache
            0x204B, // short (medium)
            0x204C, // long (medium)
            0x204D, // goatee (vandyke)
        };
        a.beardId = kBeardStyles[HashIdentity(identityId, "beard_style") % (sizeof(kBeardStyles) / sizeof(kBeardStyles[0]))];
        if (a.beardId != 0) {
            // Beard hue shares the human clamp with hair (HUE_HAIR_LOW/HIGH
            // in CChar::InitPlayer's beard case), but is hashed under its
            // own suffix so a bearded bot doesn't always match its own hair
            // color -- salt-and-pepper is as plausible as a perfect match.
            constexpr u16 kHairHueLow = 1102, kHairHueHigh = 1149;
            a.beardHue = static_cast<u16>(kHairHueLow + (HashIdentity(identityId, "beard_hue") % (kHairHueHigh - kHairHueLow + 1)));
        }
    }

    // Shirt/pants hues. The project owner suggested "the colours from
    // leather dye" as nice hues -- runtime/scripts/revolution/
    // revolution_rare_dyes.scp:20 defines exactly that pool,
    // colors_revo_rare_dye (27 hues, includes the four special-robe
    // elemental hues Fire 0x080A, Earth 0x07AD, Energy 0x0796, Ice 0x0800).
    // It CANNOT be used here, though: CChar::InitPlayer clamps this packet's
    // wShirtHue/wPantsHue to [HUE_BLUE_LOW, HUE_DYE_HIGH] = [0x0002, 0x03E9]
    // (CChar.cpp ~2145-2163; uofiles_enums.h:42,62) before ever equipping
    // the shirt/pants layer -- that is decimal {2 1001}, i.e. exactly
    // runtime/scripts/core/defs_hues.scp:51 "colors_all {2 1001}". Every
    // hue in the rare-dye/robe pool is >1001 (lowest is 0x0453=1107), so
    // sending any of them here would silently clamp to 1001 for every
    // character -- LESS diverse than the unpatched default, not more. The
    // rare/robe hues stay reachable for actual clothing post-creation (a
    // real dye tub, or an ITEMDEF's own COLOR=, is not clamped this way);
    // for the create packet itself the only correct source is colors_all,
    // which is also the exact range VENDOR_S_TAILOR/VENDOR_S_WEAVER already
    // roll clothing colors from (COLOR=colors_all, tm_vend.scp) -- so this
    // reuses the shard's own idiom for clothing color, not a new one.
    {
        constexpr u16 kDyeLow = 2, kDyeHigh = 1001;
        a.shirtHue = static_cast<u16>(kDyeLow + (HashIdentity(identityId, "shirt") % (kDyeHigh - kDyeLow + 1)));
        a.pantsHue = static_cast<u16>(kDyeLow + (HashIdentity(identityId, "pants") % (kDyeHigh - kDyeLow + 1)));
    }

    return a;
}

} // namespace uo
