#pragma once
#include "uo/types.h"

#include <algorithm>
#include <cctype>
#include <string>

// SMALL TALK. What a character says that is not a handshake.
//
// RevolutionUO was a Turkish shard; its players greeted each other with "sa"
// and answered "as", wished a miner "kolay gelsin", and said "gecmis olsun"
// to someone who had just died. Until now every bot spoke only the English
// handshake phrases in uo/social.h, which read like a protocol, because that
// is exactly what they are.
//
// This file is FLAVOUR ONLY. The machine-readable phrases (invitations,
// consent, "Ready to spar.", WTB/WTS) stay byte-for-byte what social.h and
// market.h parse, so nothing here can start a party, a spar or a trade. A
// test (tests/chatter.cpp) checks that no line here parses as any of them.
//
// Deterministic: a line is chosen by hashing who speaks, the topic and a
// caller-supplied clock bucket, never by a random generator.
//
// EVIDENCE. Revolution-specific chat logs are UNKNOWN. The phrases are
// everyday Turkish online-game chat of the period (DERIVED), written without
// Turkish letters: the client sends ASCII speech (Client::SayAscii), and
// typing "gecmis olsun" rather than "geçmiş olsun" is itself how most players
// typed on an English keyboard layout.
namespace uo::chatter {

enum class Topic : u8 {
    Greet = 0,     // to a passer-by
    GreetBack,     // answer to a greeting
    AnswerSa,      // the only answer to "sa"
    GreetFriend,   // someone we trust
    WorkerGreet,   // to someone working nearby: "kolay gelsin"
    Thanks,        // after help (a bandage, a trade)
    Farewell,      // leaving a group or logging off
    AfterKill,     // a companion or bystander after a kill
    AfterDeath,    // back on our feet after dying
    Condolence,    // to someone else who died
    Idle,          // town small talk
    Rival,         // to a foe, from a safe distance
    GreetFamily,   // someone carrying our last name
    Count
};

inline const char* TopicName(Topic t) {
    static const char* kNames[] = {"greet", "greet_back", "answer_sa", "greet_friend", "worker_greet",
                                   "thanks", "farewell", "after_kill", "after_death", "condolence",
                                   "idle", "rival", "greet_family"};
    return static_cast<int>(t) < static_cast<int>(Topic::Count) ? kNames[static_cast<int>(t)] : "?";
}

struct Lines { const char* const* items; int count; };

inline Lines LinesFor(Topic t) {
    static const char* const greet[] = {"sa", "slm", "selam", "merhaba", "selamlar", "hi", "slm millet"};
    static const char* const greetBack[] = {"selam", "slm", "merhaba", "selam hosgeldin", "hi"};
    static const char* const answerSa[] = {"as", "as hosgeldin", "aleykumselam"};
    static const char* const greetFriend[] = {"naber kanka", "o0 kimler gelmis", "selam kardesim, nasil gidiyor",
                                              "naber, iyi misin", "hosgeldin abi"};
    static const char* const worker[] = {"kolay gelsin", "kolay gelsin usta", "bereketli olsun", "kolay gelsin, bol kazanclar"};
    static const char* const thanks[] = {"sagol", "eyvallah", "tesekkurler", "sagolasin", "ty"};
    static const char* const farewell[] = {"iyi oyunlar", "gorusuruz", "bye", "hadi kactim ben", "iyi aksamlar"};
    static const char* const afterKill[] = {"gg", "tamamdir", "bir tane daha", "kolaydi", "nice"};
    static const char* const afterDeath[] = {"yine oldum ya", "offf", "cok kalabaliktilar", "bir dahakine",
                                             "lag yedim galiba"};
    static const char* const condolence[] = {"gecmis olsun", "gecmis olsun kanka", "vah vah", "gecmis olsun, rez lazim mi"};
    static const char* const idle[] = {"banka onu yine dolu", "server bu aksam kalabalik", "kimse mining yapmiyor mu",
                                       "reagent fiyatlari artmis", "yarin guild war var mi", "bu aksam dungeon'a giden var mi",
                                       "cok sikildim ya", "britain her zamanki gibi"};
    static const char* const rival[] = {"yine mi sen", "bakariz", "seni gordum", "uzak dur", "sonra konusuruz"};
    static const char* const greetFamily[] = {"selam kuzen", "naber kardesim", "hosgeldin, aile toplaniyor",
                                              "aileden biri geldi", "selam abi, nasilsin"};
    switch (t) {
        case Topic::Greet:       return {greet, 7};
        case Topic::GreetBack:   return {greetBack, 5};
        case Topic::AnswerSa:    return {answerSa, 3};
        case Topic::GreetFriend: return {greetFriend, 5};
        case Topic::WorkerGreet: return {worker, 4};
        case Topic::Thanks:      return {thanks, 5};
        case Topic::Farewell:    return {farewell, 5};
        case Topic::AfterKill:   return {afterKill, 5};
        case Topic::AfterDeath:  return {afterDeath, 5};
        case Topic::Condolence:  return {condolence, 4};
        case Topic::Idle:        return {idle, 8};
        case Topic::Rival:       return {rival, 5};
        case Topic::GreetFamily: return {greetFamily, 5};
        default:                 return {nullptr, 0};
    }
}

inline u32 Hash(const std::string& s, u32 h = 2166136261u) {
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

// One line for this speaker, topic and moment. `bucket` is the caller's
// clock divided into whatever step it likes (a minute, a goal count): the
// same speaker says different things over time and the same thing on replay.
inline const char* Pick(Topic t, const std::string& speaker, i64 bucket) {
    const Lines l = LinesFor(t);
    if (!l.count) return "";
    u32 h = Hash(speaker);
    h = Hash(std::to_string(static_cast<int>(t)) + ":" + std::to_string(bucket), h);
    return l.items[h % static_cast<u32>(l.count)];
}

// Should a character with this sociability (0..100) speak on this occasion?
// Deterministic per speaker and bucket; a shy character still greets now and
// then, a chatty one not every single time.
inline bool WantsToSpeak(i32 sociability, const std::string& speaker, i64 bucket, i32 floor = 10) {
    const i32 chance = std::max(floor, std::min(100, sociability));
    return static_cast<i32>(Hash(speaker + "#" + std::to_string(bucket)) % 100u) < chance;
}

// ---- hearing -----------------------------------------------------------------
// Lower-case, trim spaces and trailing punctuation: "Sa!!" -> "sa".
inline std::string Normalise(const std::string& text) {
    std::string out;
    for (unsigned char c : text) out.push_back(static_cast<char>(std::tolower(c)));
    while (!out.empty() && (out.back() == ' ' || out.back() == '.' || out.back() == '!' || out.back() == '?'))
        out.pop_back();
    usize i = 0;
    while (i < out.size() && out[i] == ' ') ++i;
    return out.substr(i);
}

enum class Heard : u8 { Nothing = 0, Sa, Greeting, Farewell, Thanks };

// What kind of line this is, if it is small talk addressed to anyone nearby.
// "sa" is its own case because it has its own answer.
inline Heard Classify(const std::string& text) {
    const std::string t = Normalise(text);
    if (t == "sa" || t == "s.a" || t == "selamun aleykum" || t == "selamunaleykum") return Heard::Sa;
    static const char* const greetings[] = {"slm", "selam", "selamlar", "merhaba", "mrb", "hi", "hello", "hey",
                                            "naber", "slm millet", "selam millet", "hi all", "hello all"};
    for (const char* g : greetings) if (t == g) return Heard::Greeting;
    static const char* const farewells[] = {"iyi oyunlar", "gorusuruz", "bye", "bb", "iyi aksamlar", "gn"};
    for (const char* g : farewells) if (t == g) return Heard::Farewell;
    static const char* const thanks[] = {"sagol", "eyvallah", "tesekkurler", "ty", "thanks", "thx"};
    for (const char* g : thanks) if (t == g) return Heard::Thanks;
    return Heard::Nothing;
}

// The topic to answer a heard line with, or Count for "say nothing".
inline Topic AnswerTo(Heard h, bool friendOfOurs) {
    switch (h) {
        case Heard::Sa:       return Topic::AnswerSa;
        case Heard::Greeting: return friendOfOurs ? Topic::GreetFriend : Topic::GreetBack;
        case Heard::Farewell: return Topic::Farewell;
        default:              return Topic::Count;   // a thank-you needs no answer
    }
}

}  // namespace uo::chatter
