// Small talk (uo/chatter.h): every line is plain ASCII the client can send,
// none of it can be mistaken for a handshake or a trade, and hearing is
// forgiving about case and punctuation.

#include "uo/chatter.h"
#include "uo/market.h"
#include "uo/social.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace uo;
using namespace uo::chatter;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    std::printf("[lines]\n");
    bool ascii = true, handshake = false, trade = false, sized = true;
    for (int t = 0; t < static_cast<int>(Topic::Count); ++t) {
        const Lines l = LinesFor(static_cast<Topic>(t));
        if (l.count < 3) sized = false;
        for (int i = 0; i < l.count; ++i) {
            const std::string line = l.items[i];
            if (line.empty() || line.size() > 60) sized = false;
            for (unsigned char c : line) if (c < 0x20 || c > 0x7E) ascii = false;
            social::Activity a = social::Activity::None;
            if (social::IsInvitation(line, &a) || social::SparInvitationKit(line) != 0 ||
                line == social::kSparReady || line.find("consent") != std::string::npos ||
                line.find("regroup") != std::string::npos)
                handshake = true;
            market::TradeIntent intent;
            if (market::ParseSellOffer(line, &intent) ||
                market::ClassifyBuyLine(line, &intent) != market::BuyLineKind::NotABuyLine)
                trade = true;
        }
    }
    Check(ascii, "every line is printable ASCII (the client speaks with 0x03)");
    Check(sized, "every topic has at least three lines of sensible length");
    Check(!handshake, "no small-talk line reads as an invitation, consent or spar handshake");
    Check(!trade, "no small-talk line reads as a WTS offer or a WTB line");

    std::printf("[picking]\n");
    Check(std::strcmp(Pick(Topic::Greet, "Ayse", 5), Pick(Topic::Greet, "Ayse", 5)) == 0, "deterministic");
    std::set<std::string> said;
    for (int b = 0; b < 60; ++b) said.insert(Pick(Topic::Idle, "Ayse", b));
    Check(said.size() >= 4, "the same character varies its idle talk over time");
    int chatty = 0, shy = 0;
    for (int b = 0; b < 1000; ++b) {
        chatty += WantsToSpeak(90, "Kaan", b);
        shy += WantsToSpeak(15, "Kaan", b);
    }
    Check(chatty > 800 && shy > 100 && shy < 250, "sociability sets how often a character speaks");
    Check(WantsToSpeak(100, "x", 1) && WantsToSpeak(100, "y", 2), "sociability 100 always speaks");

    std::printf("[hearing]\n");
    Check(Classify("sa") == Heard::Sa && Classify("SA!!") == Heard::Sa && Classify(" Sa. ") == Heard::Sa,
          "\"sa\" in any case and punctuation");
    Check(Classify("selamun aleykum") == Heard::Sa, "the long form is \"sa\" too");
    Check(Classify("Selam") == Heard::Greeting && Classify("hi") == Heard::Greeting &&
          Classify("naber") == Heard::Greeting, "greetings, Turkish and English");
    Check(Classify("iyi oyunlar") == Heard::Farewell && Classify("sagol") == Heard::Thanks, "farewell, thanks");
    Check(Classify("WTS 10 i_bandage 5gp") == Heard::Nothing &&
          Classify("salam") == Heard::Nothing && Classify("sat") == Heard::Nothing,
          "trade lines and near-misses are not greetings");
    Check(AnswerTo(Heard::Sa, false) == Topic::AnswerSa, "\"sa\" is answered \"as\"");
    Check(AnswerTo(Heard::Greeting, true) == Topic::GreetFriend &&
          AnswerTo(Heard::Greeting, false) == Topic::GreetBack, "a friend gets a warmer greeting");
    Check(AnswerTo(Heard::Thanks, false) == Topic::Count, "a thank-you needs no answer");
    bool answerIsSa = false;
    const Lines as = LinesFor(Topic::AnswerSa);
    for (int i = 0; i < as.count; ++i) answerIsSa = answerIsSa || Classify(as.items[i]) != Heard::Nothing;
    Check(!answerIsSa, "\"as\" is not itself heard as a greeting (no echo)");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
