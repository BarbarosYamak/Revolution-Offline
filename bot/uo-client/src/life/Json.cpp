#include "uo/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace uo::json {

namespace {

const Value& NullValue() {
    static const Value kNull;
    return kNull;
}

const Array& EmptyArray() {
    static const Array kEmpty;
    return kEmpty;
}

const Object& EmptyObject() {
    static const Object kEmpty;
    return kEmpty;
}

void AppendUtf8(std::string& out, u32 cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void EscapeInto(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Bytes >= 0x20 pass through untouched, so already-valid
                    // UTF-8 (a Turkish character name, say) survives a
                    // round trip byte for byte.
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

class Parser {
public:
    Parser(const std::string& text, ParseError* err) : s_(text), err_(err) {}

    Value Run() {
        SkipWs();
        Value v = ParseValue(0);
        if (Failed()) return Value();
        SkipWs();
        if (i_ != s_.size()) {
            Fail("trailing data after the top-level value");
            return Value();
        }
        return v;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool Failed() const { return err_ && err_->failed; }

    void Fail(const char* msg) {
        if (!err_ || err_->failed) return;
        err_->failed = true;
        err_->offset = i_;
        err_->message = msg;
    }

    void SkipWs() {
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    bool Literal(const char* lit) {
        const usize n = std::strlen(lit);
        if (s_.compare(i_, n, lit) != 0) return false;
        i_ += n;
        return true;
    }

    Value ParseValue(int depth) {
        if (depth > kMaxDepth) { Fail("nesting too deep"); return Value(); }
        if (i_ >= s_.size()) { Fail("unexpected end of input"); return Value(); }
        switch (s_[i_]) {
            case '{': return ParseObject(depth);
            case '[': return ParseArray(depth);
            case '"': return Value(ParseString());
            case 't': if (Literal("true"))  return Value(true);  Fail("expected true"); return Value();
            case 'f': if (Literal("false")) return Value(false); Fail("expected false"); return Value();
            case 'n': if (Literal("null"))  return Value();      Fail("expected null"); return Value();
            default:  return ParseNumber();
        }
    }

    Value ParseObject(int depth) {
        ++i_;  // '{'
        Object obj;
        SkipWs();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; return Value(std::move(obj)); }
        for (;;) {
            SkipWs();
            if (i_ >= s_.size() || s_[i_] != '"') { Fail("expected an object key"); return Value(); }
            std::string key = ParseString();
            if (Failed()) return Value();
            SkipWs();
            if (i_ >= s_.size() || s_[i_] != ':') { Fail("expected ':' after an object key"); return Value(); }
            ++i_;
            SkipWs();
            Value v = ParseValue(depth + 1);
            if (Failed()) return Value();
            obj[std::move(key)] = std::move(v);
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
            Fail("expected ',' or '}' in an object");
            return Value();
        }
        return Value(std::move(obj));
    }

    Value ParseArray(int depth) {
        ++i_;  // '['
        Array arr;
        SkipWs();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; return Value(std::move(arr)); }
        for (;;) {
            SkipWs();
            Value v = ParseValue(depth + 1);
            if (Failed()) return Value();
            arr.push_back(std::move(v));
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
            Fail("expected ',' or ']' in an array");
            return Value();
        }
        return Value(std::move(arr));
    }

    // Reads one \uXXXX escape body (the four hex digits), or -1.
    int ParseHex4() {
        if (i_ + 4 > s_.size()) return -1;
        int v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_ + static_cast<usize>(k)];
            int d;
            if (c >= '0' && c <= '9')      d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return -1;
            v = (v << 4) | d;
        }
        i_ += 4;
        return v;
    }

    std::string ParseString() {
        std::string out;
        ++i_;  // opening quote
        for (;;) {
            if (i_ >= s_.size()) { Fail("unterminated string"); return out; }
            const char c = s_[i_++];
            if (c == '"') break;
            if (c != '\\') { out.push_back(c); continue; }
            if (i_ >= s_.size()) { Fail("unterminated escape"); return out; }
            const char e = s_[i_++];
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    const int hi = ParseHex4();
                    if (hi < 0) { Fail("bad \\u escape"); return out; }
                    u32 cp = static_cast<u32>(hi);
                    // A high surrogate must be followed by its low half; a
                    // lone surrogate is written through as U+FFFD rather than
                    // producing invalid UTF-8.
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                            const usize save = i_;
                            i_ += 2;
                            const int lo = ParseHex4();
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000u + ((cp - 0xD800u) << 10) +
                                     (static_cast<u32>(lo) - 0xDC00u);
                            } else {
                                i_ = save;
                                cp = 0xFFFD;
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    Fail("unknown escape");
                    return out;
            }
        }
        return out;
    }

    Value ParseNumber() {
        const usize start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        bool anyDigit = false;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; anyDigit = true; }
        bool isInt = true;
        if (i_ < s_.size() && s_[i_] == '.') {
            isInt = false;
            ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; anyDigit = true; }
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            isInt = false;
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        if (!anyDigit) { Fail("expected a number"); return Value(); }
        const std::string text = s_.substr(start, i_ - start);
        if (isInt) {
            return Value(static_cast<i64>(std::strtoll(text.c_str(), nullptr, 10)));
        }
        return Value(std::strtod(text.c_str(), nullptr));
    }

    const std::string& s_;
    usize       i_ = 0;
    ParseError* err_ = nullptr;
};

}  // namespace

const Array&  Value::AsArray()  const { return isArray()  ? arr_ : EmptyArray(); }
const Object& Value::AsObject() const { return isObject() ? obj_ : EmptyObject(); }

const Value& Value::operator[](const char* key) const {
    if (!isObject() || !key) return NullValue();
    const auto it = obj_.find(key);
    return it == obj_.end() ? NullValue() : it->second;
}

const Value& Value::At(usize index) const {
    if (!isArray() || index >= arr_.size()) return NullValue();
    return arr_[index];
}

usize Value::Size() const {
    if (isArray())  return arr_.size();
    if (isObject()) return obj_.size();
    return 0;
}

bool Value::Has(const char* key) const {
    return isObject() && key && obj_.find(key) != obj_.end();
}

void Value::Set(const char* key, Value v) {
    if (!isObject()) { type_ = Type::Object; obj_.clear(); }
    if (key) obj_[key] = std::move(v);
}

void Value::Push(Value v) {
    if (!isArray()) { type_ = Type::Array; arr_.clear(); }
    arr_.push_back(std::move(v));
}

void Value::Write(std::string& out, int indent, int depth) const {
    const bool pretty = indent > 0;
    const std::string pad    = pretty ? std::string(static_cast<usize>(indent * (depth + 1)), ' ') : std::string();
    const std::string padEnd = pretty ? std::string(static_cast<usize>(indent * depth), ' ') : std::string();

    switch (type_) {
        case Type::Null:   out += "null"; return;
        case Type::Bool:   out += bool_ ? "true" : "false"; return;
        case Type::Number: {
            char buf[40];
            if (isInt_) {
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(int_));
            } else if (!std::isfinite(dbl_)) {
                // JSON has no infinity or NaN. Writing 0 keeps the file
                // parseable; the alternative is an unreadable state file.
                std::snprintf(buf, sizeof(buf), "0");
            } else {
                std::snprintf(buf, sizeof(buf), "%.10g", dbl_);
            }
            out += buf;
            return;
        }
        case Type::String: EscapeInto(out, str_); return;
        case Type::Array: {
            if (arr_.empty()) { out += "[]"; return; }
            out += '[';
            bool first = true;
            for (const Value& v : arr_) {
                if (!first) out += ',';
                first = false;
                if (pretty) { out += '\n'; out += pad; }
                v.Write(out, indent, depth + 1);
            }
            if (pretty) { out += '\n'; out += padEnd; }
            out += ']';
            return;
        }
        case Type::Object: {
            if (obj_.empty()) { out += "{}"; return; }
            out += '{';
            bool first = true;
            for (const auto& kv : obj_) {
                if (!first) out += ',';
                first = false;
                if (pretty) { out += '\n'; out += pad; }
                EscapeInto(out, kv.first);
                out += pretty ? ": " : ":";
                kv.second.Write(out, indent, depth + 1);
            }
            if (pretty) { out += '\n'; out += padEnd; }
            out += '}';
            return;
        }
    }
}

std::string Value::Serialize(int indent) const {
    std::string out;
    Write(out, indent, 0);
    if (indent > 0) out.push_back('\n');
    return out;
}

Value Parse(const std::string& text, ParseError* err) {
    ParseError local;
    Parser p(text, err ? err : &local);
    return p.Run();
}

bool ReadFile(const char* path, std::string* out) {
    if (!path || !out) return false;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out->clear();
    char buf[4096];
    usize n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

bool WriteFileAtomic(const char* path, const std::string& text) {
    if (!path) return false;
    std::string tmp = path;
    tmp += ".tmp";

    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool wrote = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool flushed = std::fflush(f) == 0;
    std::fclose(f);
    if (!wrote || !flushed) {
        std::remove(tmp.c_str());
        return false;
    }

    // Windows' rename() refuses to clobber an existing file, so the old copy
    // goes first. That opens a window where neither file exists; the .tmp is
    // already complete on disk at that point, so a crash inside the window
    // leaves recoverable data rather than a truncated state.json.
    std::remove(path);
    if (std::rename(tmp.c_str(), path) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace uo::json
