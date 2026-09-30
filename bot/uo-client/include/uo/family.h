#pragma once
#include "uo/types.h"

#include <cctype>
#include <string>
#include <vector>

// FAMILIES (last names). Owner testimony 2026-09-30: a family deed (~50k)
// made its user the head, who chose a last name; each invitation cost a deed
// (10-20k) and the invited player carried the same last name. A family was
// social, shared a home/house, and stood together in PvP. The server side is
// runtime/scripts/revolution/revolution_family.scp.
//
// Pure: who founds a family, which name, whom to invite, whether to accept,
// and how a passer-by is recognised as family -- by the last name the server
// put in their NAME, which is the only thing any player could see.
namespace uo::family {

inline constexpr i32 kFoundDeedGold = 50000;
inline constexpr i32 kInviteDeedGold = 15000;
inline constexpr i32 kKeepGold = 10000;        // never spend below this for a family
inline constexpr int kMaxMembers = 6;          // head + 5 (UNKNOWN; a bot-side restraint)
inline constexpr i32 kMinTrustToInvite = 3;
inline constexpr i32 kMinTrustToAccept = 2;

// Common Turkish surnames, written without Turkish letters (ASCII speech and
// names). A family picks one; the server refuses a name already taken.
inline const char* const* Surnames(int* n) {
    static const char* const k[] = {
        "Yilmaz", "Kaya", "Demir", "Sahin", "Celik", "Yildiz", "Yildirim", "Ozturk", "Aydin", "Ozdemir",
        "Arslan", "Dogan", "Kilic", "Aslan", "Cetin", "Kara", "Kurt", "Koc", "Ozkan", "Simsek",
        "Polat", "Korkmaz", "Erdogan", "Altun", "Karaca", "Tekin", "Bulut", "Toprak", "Tas", "Akin",
        "Keskin", "Guler", "Aksoy", "Uysal", "Balci", "Coskun", "Bozkurt", "Tunc", "Ates", "Duman"};
    *n = static_cast<int>(sizeof(k) / sizeof(k[0]));
    return k;
}

inline u32 Hash(const std::string& s) {
    u32 h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

// The try-th choice for this character (the server refuses a taken name, so
// a head walks down its own list).
inline std::string ChooseSurname(const std::string& identityId, int attempt) {
    int n = 0;
    const char* const* names = Surnames(&n);
    return names[(Hash(identityId) + static_cast<u32>(attempt) * 7u) % static_cast<u32>(n)];
}

struct FounderSight {
    bool inFamily = false;
    i32  totalGold = 0;
    i32  sociability = 0;
    int  closeFriends = 0;     // relationships at kMinTrustToInvite or above
};

// Only a rich, sociable character with friends to invite founds a family.
inline bool WantsToFound(const FounderSight& s) {
    return !s.inFamily && s.sociability >= 60 && s.closeFriends >= 1 &&
           s.totalGold >= kFoundDeedGold + kInviteDeedGold + kKeepGold;
}

// Can the head afford one more invitation?
inline bool CanInvite(bool isHead, int members, i32 totalGold) {
    return isHead && members < kMaxMembers && totalGold >= kInviteDeedGold + kKeepGold;
}

// Accept an invitation from someone we trust, if not already in a family.
inline bool AcceptInvite(bool inFamily, i32 trustInInviter, bool inviterIsFoe) {
    return !inFamily && !inviterIsFoe && trustInInviter >= kMinTrustToAccept;
}

// "Ayse Yilmaz" is of the Yilmaz family. Case-insensitive, whole last word.
inline bool IsFamilyName(const std::string& name, const std::string& surname) {
    if (surname.empty() || name.size() <= surname.size() + 1) return false;
    const std::string tail = name.substr(name.size() - surname.size());
    if (name[name.size() - surname.size() - 1] != ' ') return false;
    for (usize i = 0; i < tail.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(tail[i])) != std::tolower(static_cast<unsigned char>(surname[i])))
            return false;
    return true;
}

// The server's invitation gump (revolution_family.scp d_family_invite):
// text 0 "Aile daveti", text 1 the inviter, text 2 the last name.
inline bool IsInviteGump(const std::vector<std::string>& texts) {
    return texts.size() >= 3 && texts[0] == "Aile daveti";
}

// What the head says so members know the family home: "aile evi: Minoc".
inline std::string HomeCall(const std::string& city) { return "aile evi: " + city; }
inline bool ParseHomeCall(const std::string& text, std::string* city) {
    static const std::string p = "aile evi: ";
    if (text.size() <= p.size() || text.compare(0, p.size(), p) != 0) return false;
    *city = text.substr(p.size());
    return true;
}

}  // namespace uo::family
