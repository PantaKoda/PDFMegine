#include "json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace pdfbookmark::engine::json {

Value Value::of(bool value) {
    Value v;
    v.kind = Kind::Bool;
    v.boolean = value;
    return v;
}
Value Value::of(std::int64_t value) {
    Value v;
    v.kind = Kind::Number;
    v.text = std::to_string(value);
    return v;
}
Value Value::of(std::uint64_t value) {
    Value v;
    v.kind = Kind::Number;
    v.text = std::to_string(value);
    return v;
}
Value Value::of(double value, int decimals) {
    if (!std::isfinite(value)) return null();
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    Value v;
    v.kind = Kind::Number;
    v.text = std::move(text);
    return v;
}
Value Value::of(std::string value) {
    Value v;
    v.kind = Kind::String;
    v.text = std::move(value);
    return v;
}
Value Value::make_array() {
    Value v;
    v.kind = Kind::Array;
    return v;
}
Value Value::make_object() {
    Value v;
    v.kind = Kind::Object;
    return v;
}
Value& Value::add(std::string key, Value value) {
    object.push_back({std::move(key), std::move(value)});
    return object.back().value;
}
Value& Value::push(Value value) {
    array.push_back(std::move(value));
    return array.back();
}
const Value* Value::find(const std::string& key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& member : object)
        if (member.key == key) return &member.value;
    return nullptr;
}
std::optional<std::int64_t> Value::as_int64() const {
    if (kind != Kind::Number || text.empty() ||
        text.find_first_of(".eE") != std::string::npos)
        return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || end != text.c_str() + text.size())
        return std::nullopt;
    return static_cast<std::int64_t>(value);
}
std::optional<double> Value::as_double() const {
    if (kind != Kind::Number) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size() ||
        !std::isfinite(value))
        return std::nullopt;
    return value;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    Result<Value> run() {
        Value value;
        if (!value_at(value, 0)) return fail();
        skip_space();
        if (pos_ != s_.size()) {
            error_ = "Trailing content after JSON value";
            return fail();
        }
        return value;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;
    std::string error_;

    Error fail() const {
        return Error{ErrorCode::InvalidArgument,
                     "Invalid JSON at byte " + std::to_string(pos_) + ": " +
                         (error_.empty() ? "syntax error" : error_)};
    }
    void skip_space() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' ||
                s_[pos_] == '\r'))
            ++pos_;
    }
    bool literal(const char* word) {
        const std::string w = word;
        if (s_.compare(pos_, w.size(), w) != 0) return false;
        pos_ += w.size();
        return true;
    }
    bool value_at(Value& out, int depth) {
        if (depth > 128) {
            error_ = "Nesting too deep";
            return false;
        }
        skip_space();
        if (pos_ >= s_.size()) {
            error_ = "Unexpected end of input";
            return false;
        }
        const char c = s_[pos_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.kind = Value::Kind::String;
            return string(out.text);
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        if (literal("true")) { out = Value::of(true); return true; }
        if (literal("false")) { out = Value::of(false); return true; }
        if (literal("null")) { out = Value::null(); return true; }
        error_ = "Unexpected character";
        return false;
    }
    bool object(Value& out, int depth) {
        out = Value::make_object();
        ++pos_;
        skip_space();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skip_space();
            if (pos_ >= s_.size() || s_[pos_] != '"') {
                error_ = "Expected member name";
                return false;
            }
            std::string key;
            if (!string(key)) return false;
            if (out.find(key)) {
                error_ = "Duplicate member '" + key + "'";
                return false;
            }
            skip_space();
            if (pos_ >= s_.size() || s_[pos_] != ':') {
                error_ = "Expected ':'";
                return false;
            }
            ++pos_;
            Value member;
            if (!value_at(member, depth + 1)) return false;
            out.add(std::move(key), std::move(member));
            skip_space();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
            error_ = "Expected ',' or '}'";
            return false;
        }
    }
    bool array(Value& out, int depth) {
        out = Value::make_array();
        ++pos_;
        skip_space();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            Value element;
            if (!value_at(element, depth + 1)) return false;
            out.push(std::move(element));
            skip_space();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
            error_ = "Expected ',' or ']'";
            return false;
        }
    }
    bool digits() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        return pos_ > start;
    }
    bool number(Value& out) {
        const std::size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '0') {
            ++pos_;
        } else if (!digits()) {
            error_ = "Invalid number";
            return false;
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (!digits()) { error_ = "Invalid fraction"; return false; }
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (!digits()) { error_ = "Invalid exponent"; return false; }
        }
        out.kind = Value::Kind::Number;
        out.text = s_.substr(start, pos_ - start);
        return true;
    }
    static void append_utf8(std::string& out, std::uint32_t c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    bool hex4(std::uint32_t& value) {
        if (s_.size() - pos_ < 4) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    // Validates one raw UTF-8 sequence starting at pos_ and copies it.
    bool raw_utf8(std::string& out) {
        const auto c = static_cast<unsigned char>(s_[pos_]);
        std::size_t length = 0;
        std::uint32_t scalar = 0, minimum = 0;
        if ((c & 0xE0) == 0xC0) { length = 2; scalar = c & 0x1F; minimum = 0x80; }
        else if ((c & 0xF0) == 0xE0) { length = 3; scalar = c & 0x0F; minimum = 0x800; }
        else if ((c & 0xF8) == 0xF0) { length = 4; scalar = c & 0x07; minimum = 0x10000; }
        else return false;
        if (s_.size() - pos_ < length) return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(s_[pos_ + k]);
            if ((next & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF ||
            (scalar >= 0xD800 && scalar <= 0xDFFF))
            return false;
        out.append(s_, pos_, length);
        pos_ += length;
        return true;
    }
    bool string(std::string& out) {
        ++pos_;  // Opening quote.
        out.clear();
        while (true) {
            if (pos_ >= s_.size()) {
                error_ = "Unterminated string";
                return false;
            }
            const auto c = static_cast<unsigned char>(s_[pos_]);
            if (c == '"') { ++pos_; return true; }
            if (c < 0x20) {
                error_ = "Unescaped control character in string";
                return false;
            }
            if (c >= 0x80) {
                if (!raw_utf8(out)) {
                    error_ = "Invalid UTF-8 in string";
                    return false;
                }
                continue;
            }
            ++pos_;
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ >= s_.size()) { error_ = "Bad escape"; return false; }
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t unit = 0;
                    if (!hex4(unit)) { error_ = "Bad \\u escape"; return false; }
                    if (unit >= 0xDC00 && unit <= 0xDFFF) {
                        error_ = "Lone low surrogate";
                        return false;
                    }
                    if (unit >= 0xD800 && unit <= 0xDBFF) {
                        std::uint32_t low = 0;
                        if (s_.compare(pos_, 2, "\\u") != 0) {
                            error_ = "Lone high surrogate";
                            return false;
                        }
                        pos_ += 2;
                        if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            error_ = "Invalid surrogate pair";
                            return false;
                        }
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    }
                    append_utf8(out, unit);
                    break;
                }
                default:
                    error_ = "Bad escape";
                    return false;
            }
        }
    }
};

void write_string(std::string& out, const std::string& text) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += ch;  // UTF-8 passes through unchanged.
                }
        }
    }
    out += '"';
}

void write_value(std::string& out, const Value& value, int indent, int level) {
    const auto newline = [&](int depth) {
        if (indent <= 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent * depth), ' ');
    };
    switch (value.kind) {
        case Value::Kind::Null: out += "null"; return;
        case Value::Kind::Bool: out += value.boolean ? "true" : "false"; return;
        case Value::Kind::Number: out += value.text; return;
        case Value::Kind::String: write_string(out, value.text); return;
        case Value::Kind::Array:
            if (value.array.empty()) { out += "[]"; return; }
            out += '[';
            for (std::size_t i = 0; i < value.array.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                write_value(out, value.array[i], indent, level + 1);
            }
            newline(level);
            out += ']';
            return;
        case Value::Kind::Object:
            if (value.object.empty()) { out += "{}"; return; }
            out += '{';
            for (std::size_t i = 0; i < value.object.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                write_string(out, value.object[i].key);
                out += indent > 0 ? ": " : ":";
                write_value(out, value.object[i].value, indent, level + 1);
            }
            newline(level);
            out += '}';
            return;
    }
}

}  // namespace

Result<Value> parse(const std::string& text) { return Parser(text).run(); }

std::string write(const Value& value, int indent) {
    std::string out;
    write_value(out, value, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

}  // namespace pdfbookmark::engine::json
