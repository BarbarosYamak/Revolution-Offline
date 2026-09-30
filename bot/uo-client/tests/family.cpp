// Families (uo/family.h), pure.
#include "uo/family.h"

#include <cstdio>
#include <set>

using namespace uo;
using namespace uo::family;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    int n = 0;
    const char* const* names = Surnames(&n);
    bool ascii = true, letters = true;
    for (int i = 0; i < n; ++i)
        for (const char* c = names[i]; *c; ++c) {
            if (static_cast<unsigned char>(*c) > 0x7E) ascii = false;
            if (!std::isalpha(static_cast<unsigned char>(*c))) letters = false;
        }
    Check(n >= 30 && ascii && letters, "a list of Turkish surnames, ASCII letters only (the server takes 2-16 letters)");
    Check(ChooseSurname("acc.ayse", 0) == ChooseSurname("acc.ayse", 0), "the same founder picks the same first name");
    std::set<std::string> tries;
    for (int a = 0; a < 5; ++a) tries.insert(ChooseSurname("acc.ayse", a));
    Check(tries.size() == 5, "a refused name leads to a different next choice");

    FounderSight s; s.totalGold = 80000; s.sociability = 70; s.closeFriends = 2;
    Check(WantsToFound(s), "rich, sociable, with friends: founds a family");
    FounderSight poor = s; poor.totalGold = 60000;
    Check(!WantsToFound(poor), "the deed (50k) plus one invitation (15k) plus a reserve must be affordable");
    FounderSight shy = s; shy.sociability = 30;
    Check(!WantsToFound(shy), "a loner does not found a family");
    FounderSight friendless = s; friendless.closeFriends = 0;
    Check(!WantsToFound(friendless), "nobody to invite, no family");
    FounderSight member = s; member.inFamily = true;
    Check(!WantsToFound(member), "one family per character");

    Check(CanInvite(true, 2, 30000) && !CanInvite(false, 2, 30000) && !CanInvite(true, kMaxMembers, 90000) &&
          !CanInvite(true, 2, 20000), "only the head invites, while it can pay and the family has room");
    Check(AcceptInvite(false, 3, false) && !AcceptInvite(false, 1, false) && !AcceptInvite(true, 5, false) &&
          !AcceptInvite(false, 5, true), "accept a trusted friend only, once, never a foe");

    Check(IsFamilyName("Ayse Yilmaz", "Yilmaz") && IsFamilyName("ayse yilmaz", "Yilmaz"), "a member is known by the last name");
    Check(!IsFamilyName("Yilmaz", "Yilmaz") && !IsFamilyName("Ayse Yilmazoglu", "Yilmaz") &&
          !IsFamilyName("AyseYilmaz", "Yilmaz"), "only a whole last word counts");
    Check(IsInviteGump({"Aile daveti", "Kemal Yilmaz", "Yilmaz"}) && !IsInviteGump({"Runebook", "x", "y"}),
          "the server's invitation gump is recognised");
    std::string city;
    Check(ParseHomeCall(HomeCall("Minoc"), &city) && city == "Minoc" && !ParseHomeCall("selam", &city),
          "the head's 'aile evi' call round-trips");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
