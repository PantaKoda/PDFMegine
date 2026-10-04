#include <pdfbookmark/parsing/parsing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pdfbookmark::parsing {
namespace {

constexpr const char* kPolicyId = "s3-toc-parsing-v3";

bool space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' ||
           c == '\n' || c == '\f' || c == '\v';
}
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
char upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}
std::string trim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && space(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && space(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}
std::string lower_ascii(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

std::optional<std::uint32_t> decimal(const std::string& token) {
    if (token.empty() || token.size() > 8) return std::nullopt;
    std::uint32_t value = 0;
    for (const unsigned char c : token) {
        if (!digit(c)) return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

int roman_digit(char c) {
    switch (upper(c)) {
        case 'I': return 1;
        case 'V': return 5;
        case 'X': return 10;
        case 'L': return 50;
        case 'C': return 100;
        case 'D': return 500;
        case 'M': return 1000;
        default: return 0;
    }
}
std::string roman_canonical(std::uint32_t number) {
    constexpr struct Part { std::uint32_t value; const char* token; } parts[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    std::string result;
    for (const auto& part : parts) {
        while (number >= part.value) {
            result += part.token;
            number -= part.value;
        }
    }
    return result;
}
std::optional<std::uint32_t> roman(const std::string& token) {
    if (token.empty() || token.size() > 15) return std::nullopt;
    int value = 0;
    std::string uppercase;
    for (const char c : token) {
        if (!roman_digit(c)) return std::nullopt;
        uppercase += upper(c);
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        const int current = roman_digit(token[i]);
        const int next = i + 1 < token.size() ? roman_digit(token[i + 1]) : 0;
        if (current < next) value -= current;
        else value += current;
    }
    if (value <= 0 || value > 3999 ||
        roman_canonical(static_cast<std::uint32_t>(value)) != uppercase)
        return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

std::optional<PrintedReference> single_reference(const std::string& literal) {
    PrintedReference result;
    result.literal = literal;
    if (const auto value = decimal(literal)) {
        result.numbering = NumberingStyle::Decimal;
        result.ordinal = *value;
        return result;
    }
    if (const auto value = roman(literal)) {
        result.numbering = NumberingStyle::Roman;
        result.ordinal = *value;
        return result;
    }
    const auto dash = literal.find('-');
    if (dash == std::string::npos || dash == 0 || dash > 3 ||
        literal.find('-', dash + 1) != std::string::npos)
        return std::nullopt;
    for (std::size_t i = 0; i < dash; ++i)
        if (!alpha(static_cast<unsigned char>(literal[i]))) return std::nullopt;
    const auto value = decimal(literal.substr(dash + 1));
    if (!value) return std::nullopt;
    result.numbering = NumberingStyle::PrefixedDecimal;
    result.prefix = literal.substr(0, dash);
    result.ordinal = *value;
    return result;
}

std::optional<PrintedReference> reference(const std::string& literal) {
    if (const auto simple = single_reference(literal)) return simple;
    for (std::size_t dash = 0; dash < literal.size(); ++dash) {
        std::size_t width = 0;
        if (literal.compare(dash, 3, "\xE2\x80\x93") == 0) width = 3;
        else if (literal[dash] == '-') width = 1;
        if (width == 0) continue;
        const auto start = single_reference(literal.substr(0, dash));
        const auto end = single_reference(literal.substr(dash + width));
        if (!start || !end || start->numbering != end->numbering ||
            start->prefix != end->prefix)
            continue;
        PrintedReference result = *start;
        result.literal = literal;
        result.is_range = true;
        result.range_end = end->ordinal;
        if (result.ordinal && result.range_end &&
            *result.range_end < *result.ordinal) {
            result.uncertain = true;
            result.reasons.push_back("Range end precedes start");
        }
        return result;
    }
    return std::nullopt;
}

// Text-layer OCR artifacts inside a number ("21 1", "I I9", "33 I"): digits
// with a stray space, or a letter I/l for the digit 1. Returns the digits
// when `literal` is such a number, nothing for a clean or other token. A
// spaced form gives at most 3 digits, so two real numbers ("12 15") are
// never joined.
std::optional<std::string> ocr_number(const std::string& literal) {
    const std::string value = trim(literal);
    std::string digits;
    std::size_t spaces = 0, letters = 0, digit_count = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (digit(static_cast<unsigned char>(c))) { digits += c; ++digit_count; }
        else if (c == 'I' || c == 'l') { digits += '1'; ++letters; }
        else if (c == ' ' && i > 0 && value[i - 1] != ' ') ++spaces;
        else return std::nullopt;
    }
    if (digit_count == 0 || (spaces == 0 && letters == 0) || spaces > 2 ||
        digits.size() > (spaces ? 3u : 4u))
        return std::nullopt;
    return digits;
}

// A leading section number with the same artifacts: "2. I" -> "2.1",
// "8. I3" -> "8.13", "8.1 1" -> "8.11" (the last only when `alone`, i.e. the
// number is its own region, since "2.1 1 Title" could be read either way).
// Returns the corrected number and the length it replaces, or nothing.
std::optional<std::pair<std::string, std::size_t>> ocr_section_number(
    const std::string& text, bool alone) {
    // A leading "I" or "l" for 1, possibly split off: "I8.6", "I 8.6".
    std::size_t start = 0;
    std::string fixed;
    bool repaired = false;
    if (!text.empty() && (text[0] == 'I' || text[0] == 'l')) {
        start = text.size() > 1 && text[1] == ' ' ? 2 : 1;
        if (start >= text.size() || !digit(static_cast<unsigned char>(text[start])))
            return std::nullopt;
        fixed = "1";
        repaired = true;
    }
    std::size_t i = start;
    while (i < text.size() && digit(static_cast<unsigned char>(text[i]))) ++i;
    if (i == start || i - start + fixed.size() > 3 || i >= text.size() ||
        text[i] != '.')
        return std::nullopt;
    fixed += text.substr(start, i + 1 - start);
    std::size_t j = i + 1;
    if (j < text.size() && text[j] == ' ') { ++j; repaired = true; }
    std::size_t part = 0;
    while (j < text.size() && part < 2) {
        const char c = text[j];
        if (digit(static_cast<unsigned char>(c))) fixed += c;
        else if (c == 'I' || c == 'l') { fixed += '1'; repaired = true; }
        else if (c == ' ' && alone && part == 1 && j + 1 < text.size() &&
                 digit(static_cast<unsigned char>(text[j + 1])) &&
                 (j + 2 == text.size() || text[j + 2] == ' ')) {
            repaired = true;  // "8.1 1": one stray space inside the number.
            ++j;
            continue;
        } else break;
        ++part;
        ++j;
    }
    if (part == 0 || !repaired) return std::nullopt;
    if (j < text.size() && text[j] != ' ') return std::nullopt;  // "1.2 3D".
    return std::make_pair(fixed, j);
}

bool leader_near_end(const std::string& prefix) {
    const std::string value = trim(prefix);
    std::size_t dots = 0;
    for (std::size_t pos = value.size(); pos > 0; --pos) {
        const auto c = static_cast<unsigned char>(value[pos - 1]);
        if (c == '.') ++dots;
        else if (!space(c)) break;
    }
    return dots >= 3;
}
bool leader_before_final_token(const std::string& text) {
    const std::string value = trim(text);
    std::size_t begin = value.size();
    while (begin > 0 && !space(static_cast<unsigned char>(value[begin - 1])))
        --begin;
    return begin > 0 && leader_near_end(value.substr(0, begin));
}
std::string clean_title(std::string prefix) {
    prefix = trim(prefix);
    const auto last_non_dot = prefix.find_last_not_of('.');
    if (last_non_dot == std::string::npos) return {};
    const std::size_t trailing = prefix.size() - last_non_dot - 1;
    if (trailing >= 3) prefix.erase(last_non_dot + 1);
    prefix = trim(prefix);
    // A separator may have spaces inside its dot run after native extraction.
    std::size_t cursor = prefix.size();
    std::size_t dots = 0;
    while (cursor > 0 &&
           (prefix[cursor - 1] == '.' ||
            space(static_cast<unsigned char>(prefix[cursor - 1])))) {
        if (prefix[cursor - 1] == '.') ++dots;
        --cursor;
    }
    if (dots >= 3) prefix.erase(cursor);
    return trim(prefix);
}

struct Split {
    std::string title;
    std::optional<PrintedReference> printed;
};

Split split_row(const std::string& raw) {
    const std::string value = trim(raw);
    if (value.empty()) return {};
    std::size_t begin = value.size();
    while (begin > 0 && !space(static_cast<unsigned char>(value[begin - 1])))
        --begin;
    if (begin > 0 && begin < value.size()) {
        const std::string token = value.substr(begin);
        const std::string prefix = value.substr(0, begin);
        if (const auto parsed = reference(token)) {
            const std::string title = clean_title(prefix);
            if (!title.empty()) return {title, parsed};
        }
        if (leader_near_end(prefix)) {
            const std::string title = clean_title(prefix);
            if (!title.empty()) {
                PrintedReference unknown;
                unknown.literal = token;
                unknown.uncertain = true;
                unknown.reasons.push_back("Unsupported printed reference syntax");
                return {title, unknown};
            }
        }
    }
    // Support a leader directly touching a decimal token: Title......12.
    std::size_t tail = value.size();
    while (tail > 0 && digit(static_cast<unsigned char>(value[tail - 1]))) --tail;
    if (tail < value.size() && tail > 0 && value[tail - 1] == '.') {
        const std::string prefix = value.substr(0, tail);
        if (leader_near_end(prefix)) {
            const auto parsed = reference(value.substr(tail));
            const std::string title = clean_title(prefix);
            if (parsed && !title.empty()) return {title, parsed};
        }
    }
    return {value, std::nullopt};
}

bool contents_heading(const std::string& text) {
    const auto lower = lower_ascii(trim(text));
    return lower == "contents" || lower == "table of contents";
}
// Length of a leading "Part "/"Chapter "/"Section "/"Appendix " label in
// lower-case text, or 0.
std::size_t heading_label(const std::string& lower) {
    for (const char* label : {"part ", "chapter ", "section ", "appendix "})
        if (lower.rfind(label, 0) == 0) return std::string(label).size();
    return 0;
}
bool section_heading(const std::string& text) {
    const auto lower = lower_ascii(trim(text));
    if (heading_label(lower)) return true;
    if (text.size() > 38) return false;
    std::size_t letters = 0;
    bool lowercase = false;
    for (const char c : text) {
        if (c >= 'A' && c <= 'Z') ++letters;
        if (c >= 'a' && c <= 'z') lowercase = true;
    }
    return letters >= 3 && !lowercase;
}

struct Fragment {
    std::string text;
    double left = 0, right = 0, top = 0, bottom = 0;
    text::SourceReference source;
};
struct Row {
    std::string text;
    std::vector<text::SourceReference> sources;
    PageIndex page_index = 0;
    std::uint64_t revision = 0;
    int column = 0;
    double left = 0, right = 0, top = 0, bottom = 0;
    double page_height = 0;
    double text_right = 0;  // Right edge of the row's non-reference text.
    // A separate rightmost region read as a number with text-layer artifacts
    // ("21 1"): its literal, its digits and the row text without it.
    std::optional<std::string> tail_literal;
    std::string tail_digits, body;
    std::vector<std::string> repairs;  // Text-layer corrections, for diagnostics.
};
struct Draft {
    TocEntry entry;
    double left = 0;
    int column = 0;
};

bool valid_quad(const Quad& quad) {
    for (const auto& point : quad.points)
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    return quad.points[1].x > quad.points[0].x &&
           quad.points[2].y > quad.points[0].y;
}
std::vector<Fragment> fragments(const text::PageContent& content,
                                std::vector<UnparsedFragment>& unparsed) {
    std::vector<Fragment> result;
    for (const auto& region : content.regions) {
        if (region.text.empty()) continue;
        if (!region.quad || !valid_quad(*region.quad)) {
            unparsed.push_back({
                region.text,
                {{content.page_index, content.revision, region.id,
                  std::nullopt, std::nullopt}},
                "No usable positioned geometry"});
            continue;
        }
        const std::string& text = region.text;
        std::size_t count = 1;
        for (const char c : text) if (c == '\n') ++count;
        std::size_t piece = 0, cursor = 0;
        while (cursor <= text.size()) {
            const auto next = text.find('\n', cursor);
            const auto end = next == std::string::npos ? text.size() : next;
            const std::string line = trim(text.substr(cursor, end - cursor));
            if (!line.empty()) {
                const double total_height =
                    region.quad->points[2].y - region.quad->points[0].y;
                const double top = region.quad->points[0].y +
                    total_height * piece / count;
                const double bottom = region.quad->points[0].y +
                    total_height * (piece + 1) / count;
                result.push_back({
                    line, region.quad->points[0].x, region.quad->points[1].x,
                    top, bottom,
                    {content.page_index, content.revision, region.id,
                     cursor, end}});
            }
            if (next == std::string::npos) break;
            cursor = next + 1;
            ++piece;
        }
    }
    return result;
}

std::vector<Row> assemble_rows(const text::PageContent& content,
                               const ParsingOptions& options,
                               std::vector<UnparsedFragment>& unparsed) {
    auto parts = fragments(content, unparsed);
    const double width = content.geometry.width_points;
    const double height = content.geometry.height_points;
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0) {
        for (const auto& part : parts)
            unparsed.push_back({part.text, {part.source},
                                "Page geometry unavailable"});
        return {};
    }
    std::vector<double> references;
    for (const auto& part : parts) {
        const auto number = ocr_section_number(part.text, true);
        if (number && number->second == part.text.size()) continue;  // "8.1 1".
        if (split_row(part.text).printed || reference(part.text) ||
            ocr_number(part.text))
            references.push_back(part.right);
    }
    std::sort(references.begin(), references.end());
    struct Cluster { double right; std::size_t count; };
    std::vector<Cluster> clusters;
    for (const double right : references) {
        if (!clusters.empty() && right - clusters.back().right <= 18.0) {
            clusters.back().right =
                (clusters.back().right * clusters.back().count + right) /
                (clusters.back().count + 1);
            ++clusters.back().count;
        } else {
            clusters.push_back({right, 1});
        }
    }
    bool two_columns = false;
    double divider = 0;
    if (clusters.size() >= 2 && clusters.front().count >= 2 &&
        clusters.back().count >= 2 &&
        (clusters.back().right - clusters.front().right) / width >=
            options.column_separation_fraction) {
        two_columns = true;
        double right_start = std::numeric_limits<double>::infinity();
        for (const auto& part : parts)
            if (part.left > clusters.front().right + width * 0.02)
                right_start = std::min(right_start, part.left);
        divider = std::isfinite(right_start) ?
            (clusters.front().right + right_start) / 2.0 :
            (clusters.front().right + clusters.back().right) / 2.0;
    }
    std::vector<Row> output;
    for (int column = 0; column < (two_columns ? 2 : 1); ++column) {
        std::vector<Fragment> lane;
        for (const auto& part : parts)
            if (!two_columns || (part.left >= divider) == (column == 1))
                lane.push_back(part);
        std::sort(lane.begin(), lane.end(), [](const Fragment& a, const Fragment& b) {
            const double ay = (a.top + a.bottom) / 2.0;
            const double by = (b.top + b.bottom) / 2.0;
            if (ay != by) return ay < by;
            return a.left < b.left;
        });
        for (std::size_t i = 0; i < lane.size();) {
            std::vector<Fragment> line;
            const double center = (lane[i].top + lane[i].bottom) / 2.0;
            std::size_t next = i;
            while (next < lane.size() &&
                   std::abs((lane[next].top + lane[next].bottom) / 2.0 -
                            center) <= options.row_y_tolerance_points)
                line.push_back(lane[next++]);
            std::sort(line.begin(), line.end(),
                      [](const Fragment& a, const Fragment& b) {
                          return a.left < b.left;
                      });
            Row row;
            row.page_index = content.page_index;
            row.revision = content.revision;
            row.column = column;
            row.page_height = height;
            row.left = line.front().left;
            row.right = line.front().right;
            row.top = line.front().top;
            row.bottom = line.front().bottom;
            // A section number in its own region ("8.1 1", "8. I3").
            if (line.size() >= 2) {
                auto& first = line.front();
                const auto fixed = ocr_section_number(first.text, true);
                if (fixed && fixed->second == first.text.size()) {
                    row.repairs.push_back("Section number '" + first.text +
                                          "' read as '" + fixed->first + "'");
                    first.text = fixed->first;
                }
            }
            // A page number in its own region at the row's end ("21 1").
            std::size_t body_count = line.size();
            if (line.size() >= 2 &&
                line.back().left - line[line.size() - 2].right >= 4.0) {
                if (const auto digits = ocr_number(line.back().text)) {
                    row.tail_literal = line.back().text;
                    row.tail_digits = *digits;
                    body_count = line.size() - 1;
                }
            }
            for (std::size_t k = 0; k < line.size(); ++k) {
                const auto& fragment = line[k];
                if (!row.text.empty()) row.text += ' ';
                row.text += fragment.text;
                if (k < body_count) {
                    if (!row.body.empty()) row.body += ' ';
                    row.body += fragment.text;
                }
                if (!reference(fragment.text) && !ocr_number(fragment.text))
                    row.text_right = std::max(row.text_right, fragment.right);
                row.sources.push_back(fragment.source);
                row.left = std::min(row.left, fragment.left);
                row.right = std::max(row.right, fragment.right);
                row.top = std::min(row.top, fragment.top);
                row.bottom = std::max(row.bottom, fragment.bottom);
            }
            output.push_back(std::move(row));
            i = next;
        }
    }
    return output;
}

std::string section_number(const std::string& title);

// "Chapter 3" alone is a heading; "Chapter 3. White Dwarfs" with a number
// after it is a heading with its page reference.
bool heading_with_reference(const std::string& title) {
    const std::string value = lower_ascii(trim(title));
    std::size_t words = 0;
    std::string first;
    for (std::size_t i = 0; i < value.size();) {
        while (i < value.size() && value[i] == ' ') ++i;
        const std::size_t start = i;
        while (i < value.size() && value[i] != ' ') ++i;
        if (i > start) {
            if (words == 0) first = value.substr(start, i - start);
            ++words;
        }
    }
    return words >= 3 || (words == 2 && !heading_label(first + " "));
}

// Title and printed reference of one row.
Split split_fields(const Row& row) {
    if (row.tail_literal) {
        Split fields{clean_title(row.body), single_reference(row.tail_digits)};
        if (fields.printed && !fields.title.empty()) {
            fields.printed->literal = trim(*row.tail_literal);
            fields.printed->reasons.push_back(
                "Read as " + row.tail_digits +
                ": a space or the letter I/l inside a number in the text layer");
            return fields;
        }
    }
    if (section_heading(row.text) && !leader_before_final_token(row.text)) {
        Split fields = split_row(row.text);
        if (fields.printed && heading_with_reference(fields.title)) return fields;
        return {trim(row.text), std::nullopt};
    }
    return split_row(row.text);
}

// "2.1 ...", "Chapter 3 ...": a row that begins its own entry.
bool starts_entry(const std::string& text) {
    const std::string value = trim(text);
    const std::string lower = lower_ascii(value);
    return heading_label(lower) || !section_number(value).empty();
}

// A row that starts with a label: "Chapter 1.", "4.6", "D.2", "12", "C".
bool labelled(const std::string& text) {
    if (starts_entry(text)) return true;
    const std::string value = trim(text);
    std::string token = value.substr(0, std::min(value.find(' '), value.size()));
    if (token.size() == value.size()) return false;
    if (!token.empty() && token.back() == '.') token.pop_back();
    return (token.size() == 1 && token[0] >= 'A' && token[0] <= 'Z') ||
           (!token.empty() && token.size() <= 3 && decimal(token).has_value());
}

// "xiv Contents", "Contents xv": the TOC page's own running head.
bool running_head(const Row& row) {
    const std::string value = lower_ascii(trim(row.text));
    bool word = false, folio = false;
    for (std::size_t i = 0; i < value.size();) {
        const auto end = std::min(value.find(' ', i), value.size());
        const std::string token = value.substr(i, end - i);
        if (token == "contents") word = true;
        else if (decimal(token) || roman(token)) folio = true;
        else if (!token.empty()) return false;
        i = end + 1;
    }
    const double center = (row.top + row.bottom) / 2.0;
    return word && folio && std::isfinite(row.page_height) &&
           row.page_height > 0 && center <= row.page_height * 0.12;
}

// A title line that cannot end a title: it ends with a comma, colon or
// hyphen, or with a word such as "and" or "of".
bool ends_open(const std::string& text) {
    const std::string value = lower_ascii(trim(text));
    if (value.empty()) return false;
    const char last = value.back();
    if (last == ',' || last == ':' || last == '-' || last == ';') return true;
    const auto space_at = value.rfind(' ');
    const std::string word =
        space_at == std::string::npos ? value : value.substr(space_at + 1);
    static const std::set<std::string> open = {
        "a", "an", "and", "as", "at", "by", "for", "from", "in", "into", "of",
        "on", "or", "the", "to", "via", "with"};
    return open.count(word) != 0;
}

// `measure` is the widest title text in the row's page column.
bool can_wrap(const Row& first, const Row& second,
              const ParsingOptions& options, double measure) {
    if (contents_heading(first.text) || first.text.size() < 18 ||
        starts_entry(second.text))
        return false;
    const Split continuation = split_fields(second);
    if (!continuation.printed) return false;
    const bool same_column = first.page_index == second.page_index &&
                             first.column == second.column;
    const double line_height = std::max(first.bottom - first.top,
                                         second.bottom - second.top);
    const bool close = same_column && second.top >= first.top &&
                       second.top - first.bottom <=
                           options.wrap_line_gap_factor * line_height;
    // Hanging indent: the first line starts with a label, the continuation
    // starts right of it, and its first word would not have fit on the full
    // first line.
    if (close && labelled(first.text) &&
        second.left > first.left + options.root_indent_tolerance_points &&
        !continuation.title.empty() && second.text_right > second.left) {
        const std::string& title = continuation.title;
        const std::size_t word = std::min(title.find(' '), title.size());
        const double word_width = (second.text_right - second.left) *
                                  static_cast<double>(word) /
                                  static_cast<double>(title.size());
        if (first.text_right + word_width > measure || ends_open(first.text))
            return true;
    }
    if (section_heading(first.text)) return false;
    if (std::abs(first.left - second.left) >
        options.child_indent_min_points) return false;
    if (same_column) return close;
    return second.page_index == first.page_index + 1 &&
           first.bottom >= first.page_height * 0.75 &&
           second.top <= second.page_height * 0.25;
}

// A row that is only a page-number token in the top 10% or bottom 15% of
// its page (a running header/footer of the TOC page itself).
bool margin_page_number(const Row& row) {
    const std::string text = trim(row.text);
    const bool token = (!text.empty() && text.size() <= 4 &&
                        std::all_of(text.begin(), text.end(), [](char c) {
                            return digit(static_cast<unsigned char>(c));
                        })) ||
                       roman(text).has_value();
    if (!token || !std::isfinite(row.page_height) || row.page_height <= 0)
        return false;
    const double center = (row.top + row.bottom) / 2.0;
    return center <= row.page_height * 0.10 ||
           center >= row.page_height * 0.85;
}

// "2.1", "A.2.1": the dotted section number starting a title, or empty.
std::string section_number(const std::string& title) {
    const std::string word = title.substr(0, title.find(' '));
    if (word.size() == title.size() || word.find('.') == std::string::npos)
        return {};
    std::size_t i = 0;
    if (alpha(static_cast<unsigned char>(word[0])) && word.size() > 1 &&
        word[1] == '.') {
        i = 1;  // Appendix letter.
    } else {
        while (i < word.size() && digit(static_cast<unsigned char>(word[i]))) ++i;
        if (i == 0) return {};
    }
    while (i < word.size()) {
        if (word[i] != '.' || i + 1 >= word.size() ||
            !digit(static_cast<unsigned char>(word[i + 1])))
            return {};
        ++i;
        while (i < word.size() && digit(static_cast<unsigned char>(word[i]))) ++i;
    }
    return word;
}

// The number a title starts with, as a possible parent of dotted section
// numbers: "2 Basics" and "2. Basics" -> "2", "Chapter 2: Basics" -> "2",
// "A.2 Tables" -> "A.2". Empty when the title has no number of its own.
std::string heading_number(const std::string& title) {
    std::string value = trim(title);
    const std::string lower = lower_ascii(value);
    if (const auto label = heading_label(lower))
        value = trim(value.substr(label));
    const auto space_at = value.find(' ');
    if (space_at == std::string::npos) return {};  // A number with no title.
    std::string token = value.substr(0, space_at);
    while (!token.empty() && (token.back() == '.' || token.back() == ':'))
        token.pop_back();
    return token;
}

// Explicit numbering ("2.1" under "2", "A.2.1" under "A.2") is stronger
// evidence than indentation: it fills unknown parents and overrides a
// conflicting indentation guess, with the reason recorded.
void numbering_hierarchy(std::vector<Draft>& drafts) {
    for (std::size_t i = 0; i < drafts.size(); ++i) {
        const std::string number = section_number(drafts[i].entry.title);
        if (number.empty()) continue;
        const std::string parent = number.substr(0, number.rfind('.'));
        for (std::size_t prev = i; prev > 0; --prev) {
            if (heading_number(drafts[prev - 1].entry.title) != parent) continue;
            auto& hierarchy = drafts[i].entry.hierarchy;
            const auto& parent_id = drafts[prev - 1].entry.id;
            if (hierarchy.kind == HierarchyKind::KnownParent &&
                hierarchy.parent_id == parent_id)
                break;
            if (hierarchy.kind != HierarchyKind::Unknown)
                hierarchy.reasons.push_back(
                    "Indentation suggested otherwise; section numbering wins");
            hierarchy.kind = HierarchyKind::KnownParent;
            hierarchy.parent_id = parent_id;
            hierarchy.reasons.push_back("Section number " + number +
                                        " is under " + parent);
            break;
        }
    }
}

void infer_hierarchy(std::vector<Draft>& drafts,
                     const ParsingOptions& options) {
    std::map<int, double> baseline;
    for (const auto& draft : drafts) {
        const auto found = baseline.find(draft.column);
        if (found == baseline.end() || draft.left < found->second)
            baseline[draft.column] = draft.left;
    }
    std::vector<double> indents;
    for (const auto& draft : drafts)
        indents.push_back(draft.left - baseline[draft.column]);
    for (std::size_t i = 0; i < drafts.size(); ++i) {
        auto& hierarchy = drafts[i].entry.hierarchy;
        if (indents[i] <= options.root_indent_tolerance_points) {
            hierarchy.kind = HierarchyKind::Root;
            hierarchy.reasons.push_back("Aligned to column root margin");
            continue;
        }
        if (indents[i] < options.child_indent_min_points) {
            hierarchy.reasons.push_back("Indent falls between root and child bands");
            continue;
        }
        for (std::size_t prev = i; prev > 0; --prev) {
            if (drafts[prev - 1].column == drafts[i].column &&
                indents[prev - 1] + options.child_indent_min_points <=
                indents[i]) {
                hierarchy.kind = HierarchyKind::KnownParent;
                hierarchy.parent_id = drafts[prev - 1].entry.id;
                hierarchy.reasons.push_back(
                    "Clearly deeper indentation than preceding entry");
                break;
            }
        }
        if (hierarchy.kind == HierarchyKind::Unknown)
            hierarchy.reasons.push_back(
                "No earlier entry at a supported parent indentation");
    }
}

}  // namespace

Result<ParsedToc> parse(
    const detection::TocCandidate& candidate,
    const std::vector<text::PageAcquisition>& supplied_pages,
    const ParsingOptions& options) {
    if (candidate.id.empty() || candidate.pages.empty() ||
        !std::isfinite(options.row_y_tolerance_points) ||
        options.row_y_tolerance_points <= 0 ||
        !std::isfinite(options.column_separation_fraction) ||
        options.column_separation_fraction <= 0 ||
        options.column_separation_fraction >= 1 ||
        !std::isfinite(options.wrap_line_gap_factor) ||
        options.wrap_line_gap_factor <= 0 ||
        !std::isfinite(options.root_indent_tolerance_points) ||
        options.root_indent_tolerance_points < 0 ||
        !std::isfinite(options.child_indent_min_points) ||
        options.child_indent_min_points <=
            options.root_indent_tolerance_points)
        return Error{ErrorCode::InvalidArgument, "Invalid candidate or parsing options"};

    std::map<PageIndex, const text::PageAcquisition*> supplied;
    for (const auto& page : supplied_pages)
        if (page.page_index < 0 ||
            !supplied.emplace(page.page_index, &page).second)
            return Error{ErrorCode::InvalidArgument,
                         "Invalid or duplicate supplied physical page index"};

    ParsedToc result;
    result.candidate_id = candidate.id;
    result.policy_id = kPolicyId;
    result.start = candidate.start;
    result.end = candidate.end;
    bool degraded_evidence = false;
    for (const auto& interruption : candidate.interruptions)
        result.diagnostics.push_back(
            "Candidate interruption at page " +
            std::to_string(interruption.page_index) + ": " +
            interruption.reason);
    for (const auto& limitation : candidate.limitations)
        result.diagnostics.push_back("Candidate limitation: " + limitation);
    std::vector<Row> rows;
    PageIndex previous = -1;
    for (const auto& candidate_page : candidate.pages) {
        if (candidate_page.page_index <= previous ||
            candidate_page.revision == 0)
            return Error{ErrorCode::InvalidArgument,
                         "Candidate pages are unordered or lack revisions"};
        previous = candidate_page.page_index;
        for (const auto& evidence : candidate_page.row_evidence)
            if (evidence.page_index != candidate_page.page_index ||
                evidence.revision != candidate_page.revision)
                return Error{ErrorCode::InvalidArgument,
                             "Candidate row evidence revision mismatch"};
        const auto found = supplied.find(candidate_page.page_index);
        if (found == supplied.end() || !found->second->selected) {
            result.missing_pages.push_back(candidate_page.page_index);
            result.diagnostics.push_back(
                "Candidate page " + std::to_string(candidate_page.page_index) +
                " is unavailable");
            continue;
        }
        const auto& content = *found->second->selected;
        if (candidate_page.degraded ||
            found->second->outcome == text::Outcome::Degraded) {
            degraded_evidence = true;
            result.diagnostics.push_back(
                "Degraded source evidence at page " +
                std::to_string(candidate_page.page_index));
        }
        if (content.page_index != candidate_page.page_index ||
            content.revision != candidate_page.revision)
            return Error{ErrorCode::InvalidArgument,
                         "Stale candidate page revision or index"};
        std::set<std::uint32_t> ids;
        for (const auto& region : content.regions)
            if (!ids.insert(region.id).second)
                return Error{ErrorCode::InvalidArgument,
                             "Duplicate source region ID in page revision"};
        for (const auto& evidence : candidate_page.row_evidence)
            if (!ids.count(evidence.region_id))
                return Error{ErrorCode::InvalidArgument,
                             "Candidate row evidence region is unavailable"};
        auto page_rows = assemble_rows(content, options, result.unparsed);
        rows.insert(rows.end(), std::make_move_iterator(page_rows.begin()),
                    std::make_move_iterator(page_rows.end()));
    }

    // Widest title text per page column: a line this wide was full.
    std::map<std::pair<PageIndex, int>, double> measure;
    for (const auto& row : rows) {
        auto& width = measure[{row.page_index, row.column}];
        width = std::max(width, row.text_right);
    }
    std::vector<Draft> drafts;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (contents_heading(row.text)) continue;
        if (running_head(row)) {
            result.diagnostics.push_back(
                "Ignored the TOC page's running head '" + trim(row.text) +
                "' on page " + std::to_string(row.page_index));
            continue;
        }
        Split fields = split_fields(row);
        std::vector<text::SourceReference> sources = row.sources;
        std::vector<std::string> repairs = row.repairs;
        if (!fields.printed && i + 1 < rows.size() &&
            can_wrap(row, rows[i + 1], options,
                     measure[{row.page_index, row.column}])) {
            const auto continuation = split_fields(rows[i + 1]);
            fields.title = trim(row.text + " " + continuation.title);
            fields.printed = continuation.printed;
            sources.insert(sources.end(), rows[i + 1].sources.begin(),
                           rows[i + 1].sources.end());
            repairs.insert(repairs.end(), rows[i + 1].repairs.begin(),
                           rows[i + 1].repairs.end());
            ++i;
        }
        // "2. I Thermodynamic ...": the letter I for 1 in a section number.
        if (const auto fixed = ocr_section_number(fields.title, false)) {
            repairs.push_back("Section number '" +
                              fields.title.substr(0, fixed->second) +
                              "' read as '" + fixed->first + "'");
            fields.title = fixed->first + fields.title.substr(fixed->second);
        }
        const bool heading = section_heading(fields.title);
        if (!fields.printed && !heading && margin_page_number(row)) {
            result.diagnostics.push_back(
                "Ignored the TOC page's own page number '" + trim(row.text) +
                "' on page " + std::to_string(row.page_index));
            continue;
        }
        if (!fields.printed && !heading) {
            result.unparsed.push_back(
                {row.text, row.sources,
                 "No supported reference or section-heading evidence"});
            continue;
        }
        if (fields.title.empty()) {
            result.unparsed.push_back(
                {row.text, row.sources, "No title could be recovered"});
            continue;
        }
        TocEntry entry;
        entry.id = candidate.id + "/p" + std::to_string(row.page_index) +
                   "r" + std::to_string(row.revision) +
                   "e" + std::to_string(drafts.size());
        entry.title = std::move(fields.title);
        entry.order = drafts.size();
        entry.printed_reference = std::move(fields.printed);
        entry.sources = std::move(sources);
        for (const auto& repair : repairs)
            entry.diagnostics.push_back(repair + " (text-layer artifact)");
        if (!entry.printed_reference)
            entry.diagnostics.push_back(
                "Section heading without printed reference");
        else if (entry.printed_reference->uncertain)
            entry.diagnostics.push_back(
                "Printed reference syntax is uncertain");
        drafts.push_back({std::move(entry), row.left, row.column});
    }
    infer_hierarchy(drafts, options);
    numbering_hierarchy(drafts);
    for (auto& draft : drafts)
        result.entries.push_back(std::move(draft.entry));
    result.completeness =
        result.missing_pages.empty() && result.unparsed.empty() &&
        candidate.interruptions.empty() && !degraded_evidence &&
        result.start == detection::BoundaryState::Closed &&
        result.end == detection::BoundaryState::Closed ?
        ParseCompleteness::Complete : ParseCompleteness::Incomplete;
    for (const auto& entry : result.entries)
        if (entry.hierarchy.kind == HierarchyKind::Unknown ||
            (entry.printed_reference &&
             entry.printed_reference->uncertain))
            result.completeness = ParseCompleteness::Incomplete;
    if (result.start != detection::BoundaryState::Closed ||
        result.end != detection::BoundaryState::Closed)
        result.diagnostics.push_back(
            "Candidate boundary may continue or is unknown");
    if (!result.unparsed.empty())
        result.diagnostics.push_back(
            "Unparsed or unsupported source fragments remain");
    return result;
}

}  // namespace pdfbookmark::parsing
