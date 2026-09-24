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

constexpr const char* kPolicyId = "s3-toc-parsing-v2";

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
bool section_heading(const std::string& text) {
    const auto lower = lower_ascii(trim(text));
    if (lower.rfind("part ", 0) == 0 ||
        lower.rfind("chapter ", 0) == 0 ||
        lower.rfind("section ", 0) == 0)
        return true;
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
        if (split_row(part.text).printed || reference(part.text))
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
            for (const auto& fragment : line) {
                if (!row.text.empty()) row.text += ' ';
                row.text += fragment.text;
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

bool can_wrap(const Row& first, const Row& second,
              const ParsingOptions& options) {
    if (contents_heading(first.text) || section_heading(first.text) ||
        first.text.size() < 18 || !split_row(second.text).printed)
        return false;
    if (std::abs(first.left - second.left) >
        options.child_indent_min_points) return false;
    if (first.page_index == second.page_index &&
        first.column == second.column) {
        const double line_height = std::max(first.bottom - first.top,
                                             second.bottom - second.top);
        return second.top >= first.top &&
               second.top - first.bottom <=
                   options.wrap_line_gap_factor * line_height;
    }
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

// Explicit numbering ("2.1" under "2", "A.2.1" under "A.2") is stronger
// evidence than indentation: it fills unknown parents and overrides a
// conflicting indentation guess, with the reason recorded.
void numbering_hierarchy(std::vector<Draft>& drafts) {
    for (std::size_t i = 0; i < drafts.size(); ++i) {
        const std::string number = section_number(drafts[i].entry.title);
        if (number.empty()) continue;
        const std::string parent = number.substr(0, number.rfind('.'));
        for (std::size_t prev = i; prev > 0; --prev) {
            const std::string& title = drafts[prev - 1].entry.title;
            if (title.size() <= parent.size() ||
                title.compare(0, parent.size(), parent) != 0 ||
                title[parent.size()] != ' ')
                continue;
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

    std::vector<Draft> drafts;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (contents_heading(row.text)) continue;
        Split fields = section_heading(row.text) &&
            !leader_before_final_token(row.text) ?
            Split{trim(row.text), std::nullopt} : split_row(row.text);
        std::vector<text::SourceReference> sources = row.sources;
        if (!fields.printed && i + 1 < rows.size() &&
            can_wrap(row, rows[i + 1], options)) {
            const auto continuation = split_row(rows[i + 1].text);
            fields.title = trim(row.text + " " + continuation.title);
            fields.printed = continuation.printed;
            sources.insert(sources.end(), rows[i + 1].sources.begin(),
                           rows[i + 1].sources.end());
            ++i;
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
