#pragma once

// Engine-private strict JSON (RFC 8259) value, parser and deterministic writer.
// Numbers keep their source lexeme so callers perform checked conversions.

#include <pdfbookmark/core/types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::engine::json {

struct Member;

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;  // String contents (UTF-8) or number lexeme.
    std::vector<Value> array;
    std::vector<Member> object;  // Insertion order; keys unique.

    static Value null() { return {}; }
    static Value of(bool value);
    static Value of(std::int64_t value);
    static Value of(std::uint64_t value);
    static Value of(int value) { return of(static_cast<std::int64_t>(value)); }
    // Non-finite values become null. Rounded to `decimals` fractional digits.
    static Value of(double value, int decimals);
    static Value of(std::string value);
    static Value of(const char* value) { return of(std::string(value)); }
    static Value make_array();
    static Value make_object();

    Value& add(std::string key, Value value);  // Object: append member.
    Value& push(Value value);                  // Array: append element.
    const Value* find(const std::string& key) const;

    std::optional<std::int64_t> as_int64() const;  // Exact integer only.
    std::optional<double> as_double() const;
};

struct Member {
    std::string key;
    Value value;
};

// Strict: no comments, trailing commas, duplicate keys, lone surrogates,
// invalid UTF-8, leading zeros, or trailing content. Depth limit 128.
Result<Value> parse(const std::string& text);

// Deterministic output in member insertion order; `indent` 0 is compact.
std::string write(const Value& value, int indent = 2);

}  // namespace pdfbookmark::engine::json
