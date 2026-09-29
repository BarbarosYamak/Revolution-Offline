#include "uo/life.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace uo::life {
namespace {
bool Token(const std::string& s) {
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(),
        [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}
}
std::string FormatCraftOrder(const CraftOrder& o, const char* verb, const std::string& to) {
    return (to.empty() ? "" : to + ", ") + "ORDER " + verb + " " + o.id + " " +
        std::to_string(o.terms.qty) + " " + o.terms.item + " " +
        std::to_string(o.terms.pricePerUnit) + "gp";
}

bool ParseCraftOrder(const std::string& text, std::string& verb, CraftOrder& o) {
    if (text.size() > 240) return false;
    const auto comma = text.find(',');
    std::istringstream in(comma == std::string::npos ? text : text.substr(comma + 1));
    std::string tag, price, extra;
    i64 qty = 0;
    CraftOrder parsed;
    std::string action;
    if (!(in >> tag >> action >> parsed.id >> qty >> parsed.terms.item >> price) || in >> extra)
        return false;
    if (tag != "ORDER" || (action != "REQUEST" && action != "ACCEPT" &&
        action != "READY" && action != "CANCEL") || !Token(parsed.id) ||
        !Token(parsed.terms.item) || qty < 1 || qty > 200 || price.size() < 3 ||
        price.substr(price.size() - 2) != "gp") return false;
    i64 value = 0;
    for (usize i = 0; i + 2 < price.size(); ++i) {
        if (price[i] < '0' || price[i] > '9') return false;
        value = value * 10 + price[i] - '0';
        if (value > 100000) return false;
    }
    // Secure trade offers one gold stack through a 16-bit amount field.
    if (value < 1 || value * qty > 65535) return false;
    parsed.terms.qty = static_cast<i32>(qty);
    parsed.terms.pricePerUnit = static_cast<i32>(value);
    verb = action;
    o = parsed;
    return true;
}

}
