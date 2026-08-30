#pragma once

#include <string>

#include "uo/types.h"

// Layer 1 of docs/APPEARANCE_DESIGN.md ("Creation packet (legal, immediate)"):
// a deterministic appearance roll for the 0x00 CreateCharacter packet. Layer
// 2 (earned wardrobe, M13/M15) is out of scope here.

namespace uo {

// Everything the 0x00 create-character packet sends about how a character
// looks (build::CreateCharacterParams's appearance fields, minus name/slot/
// stats/skills/city, which are not appearance).
struct Appearance {
    bool female   = false;
    u16  skinHue  = 0;
    u16  hairId   = 0;
    u16  hairHue  = 0;
    u16  beardId  = 0;  // 0 = no beard; always 0 for female
    u16  beardHue = 0;  // 0 when beardId == 0
    u16  shirtHue = 0;
    u16  pantsHue = 0;
};

// Rolls a stable appearance for `identityId`, which must already be the
// sanitized "account.charname" identity string -- the same string
// Client::SendCreateCharacter and main.cpp's home-city pick build from
// cfg_.username/cfg_.charName (lowercased, alnum plus '-'/'_' kept, anything
// else folded to '_'). This function does not sanitize its input itself; it
// only appends its own already-clean per-field suffixes ("skin", "hair_hue",
// ...) before hashing, so callers must sanitize once, the same way, rather
// than each rolling their own escaping.
//
// Pure and deterministic: the same identityId always yields the same
// Appearance (so a character looks the same on every login/recreate), and
// different identityIds are overwhelmingly likely to differ in at least one
// field (the fields are hashed under independent suffixes, so they do not
// move in lockstep). Every hue/style field is drawn from the exact range
// Source-X's CChar::InitPlayer accepts for a human character created through
// the 0x00 packet -- see the citations in the .cpp -- so nothing rolled here
// is silently clamped or discarded server-side.
Appearance AppearanceForIdentity(const std::string& identityId);

} // namespace uo
