#pragma once

// ---------------------------------------------------------------------------
// A minimal JSON value, parser and writer (M4).
//
// WHY A HAND-ROLLED ONE
//
// The persistent bot state has to be inspectable by a human -- the M4 brief
// asks for "JSON, SQLite, or another simple inspectable local format" -- and
// this project takes no third-party runtime dependencies. The subset here is
// exactly what `bot_data/<identity>/state.json` needs and nothing more:
//
//   object, array, string, number, bool, null
//
// Numbers are held as a double AND as an i64, because every number this
// schema stores is either an integer (tenths of a skill point, gold, epoch
// milliseconds) or a small ratio, and silently rounding a millisecond stamp
// through a float is the kind of drift that only shows up a week later.
//
// Escapes handled on both sides: \" \\ \/ \b \f \n \r \t and \uXXXX (BMP,
// re-encoded to UTF-8; surrogate pairs are combined). Anything the parser
// cannot make sense of is a hard error with a byte offset, never a silent
// default -- a state file that half-parses is worse than one that fails.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <map>
#include <string>
#include <vector>

namespace uo::json {

class Value;

using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

enum class Type : u8 { Null = 0, Bool, Number, String, Array, Object };

class Value {
public:
    Value() = default;
    Value(bool b)               : type_(Type::Bool),   bool_(b) {}
    Value(i64 n)                : type_(Type::Number), int_(n), dbl_(static_cast<double>(n)), isInt_(true) {}
    Value(int n)                : Value(static_cast<i64>(n)) {}
    Value(double d)             : type_(Type::Number), int_(static_cast<i64>(d)), dbl_(d), isInt_(false) {}
    Value(const char* s)        : type_(Type::String), str_(s ? s : "") {}
    Value(std::string s)        : type_(Type::String), str_(std::move(s)) {}
    Value(Array a)              : type_(Type::Array),  arr_(std::move(a)) {}
    Value(Object o)             : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Type::Null; }
    bool isBool()   const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray()  const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Typed reads with an explicit fallback. A missing or wrong-typed field
    // never throws and never guesses -- the caller states the default, which
    // is what schema migration needs.
    bool        AsBool(bool def = false) const   { return isBool() ? bool_ : def; }
    i64         AsInt(i64 def = 0) const         { return isNumber() ? int_ : def; }
    double      AsDouble(double def = 0.0) const { return isNumber() ? dbl_ : def; }
    const std::string& AsString(const std::string& def) const { return isString() ? str_ : def; }
    std::string AsString() const                 { return isString() ? str_ : std::string(); }

    const Array&  AsArray() const;
    const Object& AsObject() const;

    // Object field lookup. Returns a static null Value when absent, so
    // `v["a"]["b"].AsInt(0)` is safe on any shape.
    const Value& operator[](const char* key) const;
    const Value& At(usize index) const;   // array element, null when out of range
    usize Size() const;                   // array/object element count, else 0
    bool  Has(const char* key) const;

    // Mutation, for building a document.
    void Set(const char* key, Value v);
    void Push(Value v);
    static Value MakeObject() { return Value(Object{}); }
    static Value MakeArray()  { return Value(Array{}); }

    std::string Serialize(int indent = 2) const;

private:
    void Write(std::string& out, int indent, int depth) const;

    Type        type_ = Type::Null;
    bool        bool_ = false;
    i64         int_  = 0;
    double      dbl_  = 0.0;
    bool        isInt_ = true;
    std::string str_;
    Array       arr_;
    Object      obj_;
};

struct ParseError {
    bool        failed = false;
    usize       offset = 0;
    std::string message;
};

// Parses `text`. On failure returns a null Value and fills `err`.
Value Parse(const std::string& text, ParseError* err);

// Read/write a whole file. WriteFile is ATOMIC where the platform allows it:
// it writes `<path>.tmp`, flushes, then renames over `path`, so a crash can
// never leave a half-written state file behind -- the M4 crash-safety proof
// depends on that being true.
bool ReadFile(const char* path, std::string* out);
bool WriteFileAtomic(const char* path, const std::string& text);

}  // namespace uo::json
