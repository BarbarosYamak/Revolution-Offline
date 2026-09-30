// The shard calendar (uo/era.h): dated Revolution changes, parsing, and the
// dated mechanics a bot plans with.

#include "uo/era.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace uo;
using namespace uo::era;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    std::printf("[table]\n");
    int n = 0;
    const Change* c = Changes(&n);
    bool sorted = true, sourced = true, complete = true;
    std::set<int> features;
    for (int i = 0; i < n; ++i) {
        if (i && c[i].date < c[i - 1].date) sorted = false;
        if (!c[i].source || !std::strstr(c[i].source, ".md:")) sourced = false;
        if (!Valid(c[i].date) || c[i].date / 10000 < kFirstYear || c[i].date / 10000 > kLastYear) complete = false;
        features.insert(static_cast<int>(c[i].feature));
    }
    Check(sorted, "the changes are in date order");
    Check(sourced, "every change cites the docs line that quotes the dated archive entry");
    Check(complete, "every date is valid and inside 2008-2016");
    Check(static_cast<int>(features.size()) == n && n == static_cast<int>(Feature::Count),
          "every feature has exactly one dated change");
    bool none2013to2015 = true;
    for (int i = 0; i < n; ++i) if (c[i].date / 10000 >= 2013 && c[i].date / 10000 <= 2015) none2013to2015 = false;
    Check(none2013to2015, "2013-2015 stays empty: no dated evidence, nothing invented");

    std::printf("[dates]\n");
    Check(Parse("2009-05-14") == 20090514 && Parse("20090514") == 20090514, "both date spellings parse");
    Check(Parse("2009-13-01") == 0 && Parse("banana") == 0 && Parse("") == 0, "nonsense is refused");
    Check(Format(20090514) == "2009-05-14", "format round-trips");
    Check(InProfile(kDefaultDate) && !InProfile(Key(2012, 1, 7)) && !InProfile(Key(2008, 6, 1)),
          "the default lives inside the server's 2009-2010 profile");

    std::printf("[what a character knows]\n");
    Check(!Active(Feature::RunebookCharges, Key(2009, 5, 12)) && Active(Feature::RunebookCharges, Key(2009, 5, 13)),
          "runebook charges start on 13.05.2009, not a day earlier");
    Check(RecallReagentsEach(Key(2009, 1, 1)) == 3 && RecallReagentsEach(Key(2009, 5, 14)) == 1,
          "Recall reagents drop 3 -> 1 on 14.05.2009");
    Check(CookingBatch(Key(2008, 6, 1), 1000) == 40 && CookingBatch(Key(2010, 1, 1), 1000) == 80 &&
          CookingBatch(Key(2011, 6, 1), 1000) == 100 && CookingBatch(Key(2010, 1, 1), 500) == 40,
          "campfire batch at GM: 40, 80, 100 over the years, linear in skill");
    Check(!Active(Feature::GuildRunebooks, kProfileEnd) && Active(Feature::GuildRunebooks, Key(2012, 1, 7)),
          "guild runebooks are 2012, outside the profile");
    Check(!Active(Feature::AntiMacroDisconnect, Key(2015, 12, 31)), "the 2016 revival rules are 2016 only");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
