#include <pdfbookmark/mapping/mapping.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace pdfbookmark::mapping {
namespace {
constexpr const char* kPolicyId = "s4-page-mapping-v3";
bool space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' ||
           c == '\n' || c == '\f' || c == '\v';
}
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool letter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
char upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}
std::string trim(const std::string& value) {
    std::size_t first = 0, end = value.size();
    while (first < end && space(static_cast<unsigned char>(value[first])))
        ++first;
    while (end > first && space(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(first, end - first);
}
std::string heading_key(const std::string& text) {
    std::string out;
    bool pending_space = false;
    for (const unsigned char c : trim(text)) {
        if (space(c)) pending_space = !out.empty();
        else {
            if (pending_space) out += ' ';
            pending_space = false;
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
    }
    return out;
}
std::optional<std::uint32_t> decimal(const std::string& text) {
    if (text.empty() || text.size() > 8) return std::nullopt;
    std::uint32_t value = 0;
    for (const unsigned char c : text) {
        if (!digit(c)) return std::nullopt;
        value = value * 10 + c - '0';
    }
    return value;
}
int roman_value(char c) {
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
std::string roman_canonical(unsigned int value) {
    constexpr struct Part { unsigned int value; const char* text; } parts[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    std::string result;
    for (const auto& part : parts)
        while (value >= part.value) {
            result += part.text;
            value -= part.value;
        }
    return result;
}
std::optional<std::uint32_t> roman(const std::string& text) {
    if (text.empty() || text.size() > 15) return std::nullopt;
    std::string uppercase;
    int value = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int current = roman_value(text[i]);
        if (!current) return std::nullopt;
        uppercase += upper(text[i]);
        const int next = i + 1 < text.size() ? roman_value(text[i + 1]) : 0;
        value += current < next ? -current : current;
    }
    if (value <= 0 || value > 3999 ||
        roman_canonical(static_cast<unsigned int>(value)) != uppercase)
        return std::nullopt;
    return static_cast<std::uint32_t>(value);
}
struct Token {
    parsing::NumberingStyle style = parsing::NumberingStyle::Unknown;
    std::uint32_t ordinal = 0;
    std::string prefix;
};
std::optional<Token> token(const std::string& raw) {
    const std::string text = trim(raw);
    if (const auto value = decimal(text))
        return Token{parsing::NumberingStyle::Decimal, *value, {}};
    if (const auto value = roman(text))
        return Token{parsing::NumberingStyle::Roman, *value, {}};
    const auto hyphen = text.find('-');
    if (hyphen == std::string::npos || hyphen == 0 || hyphen > 3 ||
        text.find('-', hyphen + 1) != std::string::npos)
        return std::nullopt;
    for (std::size_t i = 0; i < hyphen; ++i)
        if (!letter(static_cast<unsigned char>(text[i])))
            return std::nullopt;
    const auto value = decimal(text.substr(hyphen + 1));
    if (!value) return std::nullopt;
    std::string prefix = text.substr(0, hyphen);
    for (char& c : prefix) c = upper(c);
    return Token{parsing::NumberingStyle::PrefixedDecimal, *value,
                 std::move(prefix)};
}
Token entry_token(const parsing::PrintedReference& ref) {
    Token result{ref.numbering, ref.ordinal.value_or(0), ref.prefix};
    for (char& c : result.prefix) c = upper(c);
    return result;
}
bool same_token(const Token& lhs, const Token& rhs) {
    return lhs.style == rhs.style && lhs.ordinal == rhs.ordinal &&
           lhs.prefix == rhs.prefix;
}
bool matches_section(const Token& value, const NumberingSection& section) {
    std::string prefix = section.prefix;
    for (char& c : prefix) c = upper(c);
    return value.style == section.style && value.prefix == prefix;
}
bool valid_page(PageIndex page, PageCount count) {
    return page >= 0 && page < count;
}
std::optional<PageIndex> shifted(std::uint32_t ordinal, std::int64_t offset,
                                 PageCount count) {
    if (offset > std::numeric_limits<std::int64_t>::max() -
                     static_cast<std::int64_t>(ordinal))
        return std::nullopt;
    const auto index = static_cast<std::int64_t>(ordinal) + offset;
    if (index < 0 || index >= count) return std::nullopt;
    return static_cast<PageIndex>(index);
}
bool in_section(PageIndex index, const NumberingSection& section) {
    return index >= section.first && index < section.end;
}
bool source_equal(const text::SourceReference& lhs,
                  const text::SourceReference& rhs) {
    return lhs.page_index == rhs.page_index &&
           lhs.revision == rhs.revision && lhs.region_id == rhs.region_id;
}
using Pages = std::map<PageIndex, const text::PageAcquisition*>;
using Facts = std::map<PageIndex, const text::PdfPageFacts*>;
bool page_supplied(PageIndex index, const Pages& pages) {
    const auto found = pages.find(index);
    return found != pages.end() && found->second->selected.has_value();
}
bool labels_complete(const NumberingSection& section, const Facts& facts) {
    for (PageIndex page = section.first; page < section.end; ++page) {
        const auto found = facts.find(page);
        if (found == facts.end() ||
            found->second->viewer_label.availability ==
                text::FactAvailability::Unsupported ||
            found->second->viewer_label.availability ==
                text::FactAvailability::Failed)
            return false;
    }
    return true;
}
void add_page(std::vector<PageIndex>& pages, PageIndex index) {
    if (std::find(pages.begin(), pages.end(), index) == pages.end())
        pages.push_back(index);
}
void request_page(MappingResult& result, std::set<std::string>& ids,
                  PageIndex index, PageCount count, const std::string& purpose,
                  int priority, const MappingOptions& options,
                  const Pages& pages) {
    if (!valid_page(index, count) || page_supplied(index, pages) ||
        options.max_request_pages == 0) return;
    const std::string id = "s4-p" + std::to_string(index) + "-" + purpose;
    if (ids.insert(id).second)
        result.requests.push_back(
            {id, index, static_cast<PageIndex>(index + 1), purpose, priority});
}
void resolve(EntryMapping& result, const DestinationAlternative& choice) {
    result.status = MappingStatus::Resolved;
    result.pdf_page_index = choice.pdf_page_index;
    result.method = choice.method;
    result.supporting_sources = choice.sources;
    add_page(result.supporting_pages, choice.pdf_page_index);
    result.reasons.push_back(choice.reason);
}

struct Anchors {
    std::map<PageIndex, std::set<std::int64_t>> by_page;
    std::map<std::int64_t, std::set<PageIndex>> by_offset;
    bool contradictory = false;
    std::optional<std::int64_t> strong;
    std::vector<PageIndex> outliers;  // Ignored minority anchors (S4-10).
};
Anchors anchors_for(
    const NumberingSection& section,
    const std::vector<MappingObservation>& observations,
    const std::map<std::string, const parsing::TocEntry*>& entries,
    const std::map<std::string, std::string>& associations,
    const std::vector<NumberingSection>& sections, bool allow_dominance) {
    Anchors result;
    for (const auto& observation : observations) {
        if (!in_section(observation.page_index, section) ||
            !observation.ordinal) continue;
        Token value{observation.style, *observation.ordinal,
                    observation.prefix};
        if (!matches_section(value, section)) continue;
        // Viewer labels are uncorroborated hints, never content anchors.
        if (observation.kind == ObservationKind::ViewerLabel) continue;
        if (observation.kind == ObservationKind::HeadingMatch) {
            if (!observation.entry_id ||
                !entries.count(*observation.entry_id)) continue;
            const auto associated = associations.find(*observation.entry_id);
            if (associated != associations.end()) {
                if (associated->second != section.id) continue;
            } else {
                std::size_t compatible = 0;
                for (const auto& other : sections)
                    if (matches_section(value, other)) ++compatible;
                if (compatible != 1) continue;
            }
        }
        const auto offset =
            static_cast<std::int64_t>(observation.page_index) -
            static_cast<std::int64_t>(*observation.ordinal);
        result.by_page[observation.page_index].insert(offset);
        result.by_offset[offset].insert(observation.page_index);
    }
    for (const auto& page : result.by_page)
        if (page.second.size() > 1) result.contradictory = true;
    if (result.by_offset.size() > 1) result.contradictory = true;
    if (!result.contradictory && result.by_offset.size() == 1 &&
        result.by_offset.begin()->second.size() >= 2)
        result.strong = result.by_offset.begin()->first;
    // S4-10: stray numbers (code listings, footnote marks) must not veto an
    // offset that nearly every anchor page supports. Only with per-entry
    // target confirmation, which independently checks each destination.
    if (result.contradictory && allow_dominance) {
        std::map<std::int64_t, std::size_t> support;  // Pages with one offset.
        for (const auto& page : result.by_page)
            if (page.second.size() == 1) ++support[*page.second.begin()];
        const auto best = std::max_element(
            support.begin(), support.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        const std::size_t total = result.by_page.size();
        if (best != support.end() && best->second >= 3 &&
            best->second * 5 >= total * 4) {
            result.strong = best->first;
            result.contradictory = false;
            for (const auto& page : result.by_page)
                if (page.second.size() != 1 || *page.second.begin() != best->first)
                    result.outliers.push_back(page.first);
        }
    }
    return result;
}

struct Validated {
    std::map<std::string, const parsing::TocEntry*> entries;
    Pages pages;
    Facts facts;
    std::map<std::string, const NumberingSection*> sections;
    std::map<std::string, std::string> associations;
    std::map<std::string, PageIndex> manual_entries;
    std::map<std::string, std::int64_t> manual_offsets;
    std::map<std::string, std::vector<AssociatedLocalLink>> links;
};
std::optional<Error> validate(
    const std::vector<parsing::TocEntry>& entries,
    const DocumentEvidence& evidence,
    const std::vector<MappingOverride>& overrides,
    const MappingOptions& options, Validated& out) {
    const PageCount count = evidence.input.page_count;
    if (count <= 0 || !std::isfinite(options.footer_band_fraction) ||
        options.footer_band_fraction <= 0 ||
        options.footer_band_fraction >= 0.5 ||
        !std::isfinite(options.heading_band_fraction) ||
        options.heading_band_fraction <= 0 ||
        options.heading_band_fraction >= 0.5 ||
        !std::isfinite(options.header_band_fraction) ||
        options.header_band_fraction <= 0 ||
        options.header_band_fraction >= 0.5 ||
        options.max_request_pages == 0)
        return Error{ErrorCode::InvalidArgument,
                     "Invalid document identity or mapping options"};
    for (const auto& entry : entries)
        if (entry.id.empty() || !out.entries.emplace(entry.id, &entry).second)
            return Error{ErrorCode::InvalidArgument,
                         "Entry IDs must be nonempty and unique"};
    for (const auto& page : evidence.pages)
        if (!valid_page(page.page_index, count) ||
            !out.pages.emplace(page.page_index, &page).second ||
            (page.selected && page.selected->page_index != page.page_index))
            return Error{ErrorCode::InvalidArgument,
                         "Invalid or duplicate supplied page"};
    for (const auto& entry : entries)
        for (const auto& source : entry.sources) {
            const auto found = out.pages.find(source.page_index);
            if (found != out.pages.end() && found->second->selected &&
                found->second->selected->revision != source.revision)
                return Error{ErrorCode::InvalidArgument,
                             "Entry source refers to a stale page revision"};
        }
    for (const auto& fact : evidence.facts)
        if (!valid_page(fact.page_index, count) ||
            !out.facts.emplace(fact.page_index, &fact).second)
            return Error{ErrorCode::InvalidArgument,
                         "Invalid or duplicate page fact"};
    for (const auto& section : evidence.sections) {
        if (section.id.empty() || section.origin.empty() ||
            section.first < 0 || section.end <= section.first ||
            section.end > count ||
            section.style == parsing::NumberingStyle::Unknown ||
            !out.sections.emplace(section.id, &section).second)
            return Error{ErrorCode::InvalidArgument,
                         "Invalid numbering section"};
        if ((section.style == parsing::NumberingStyle::PrefixedDecimal) !=
                !section.prefix.empty() ||
            section.prefix.size() > 3)
            return Error{ErrorCode::InvalidArgument,
                         "Section prefix does not match numbering style"};
        for (const unsigned char c : section.prefix)
            if (!letter(c))
                return Error{ErrorCode::InvalidArgument,
                             "Section prefix contains unsupported characters"};
    }
    for (std::size_t i = 0; i < evidence.sections.size(); ++i)
        for (std::size_t j = i + 1; j < evidence.sections.size(); ++j)
            if (evidence.sections[i].first < evidence.sections[j].end &&
                evidence.sections[j].first < evidence.sections[i].end)
                return Error{ErrorCode::InvalidArgument,
                             "Numbering section physical ranges overlap"};
    for (const auto& association : evidence.entry_sections)
        if (association.origin.empty() ||
            !out.entries.count(association.entry_id) ||
            !out.sections.count(association.section_id) ||
            !out.associations.emplace(association.entry_id,
                                      association.section_id).second)
            return Error{ErrorCode::InvalidArgument,
                         "Invalid or duplicate entry-section association"};
    for (const auto& override_value : overrides) {
        if (override_value.origin.empty())
            return Error{ErrorCode::InvalidArgument,
                         "Manual override must identify its origin"};
        if (override_value.kind == OverrideKind::EntryDestination) {
            if (!out.entries.count(override_value.target_id) ||
                !override_value.destination || override_value.offset ||
                !valid_page(*override_value.destination, count) ||
                !out.manual_entries.emplace(override_value.target_id,
                                            *override_value.destination).second)
                return Error{ErrorCode::InvalidArgument,
                             "Invalid or duplicate entry override"};
        } else {
            if (!out.sections.count(override_value.target_id) ||
                !override_value.offset || override_value.destination ||
                !out.manual_offsets.emplace(override_value.target_id,
                                            *override_value.offset).second)
                return Error{ErrorCode::InvalidArgument,
                             "Invalid or duplicate section offset override"};
        }
    }
    for (const auto& link : evidence.associated_links) {
        const auto found = out.entries.find(link.entry_id);
        const auto fact = out.facts.find(link.source.page_index);
        if (found == out.entries.end() ||
            !valid_page(link.destination, count) ||
            std::none_of(found->second->sources.begin(),
                         found->second->sources.end(),
                         [&](const auto& source) {
                             return source_equal(source, link.source);
                         }) ||
            fact == out.facts.end() ||
            fact->second->local_link_destinations.availability !=
                text::FactAvailability::Present ||
            !fact->second->local_link_destinations.value ||
            std::find(
                fact->second->local_link_destinations.value->begin(),
                fact->second->local_link_destinations.value->end(),
                link.destination) ==
                fact->second->local_link_destinations.value->end())
            return Error{ErrorCode::InvalidArgument,
                         "Local link lacks valid source association or fact"};
        out.links[link.entry_id].push_back(link);
    }
    return std::nullopt;
}

struct MarginNumber {
    Token value;
    std::string literal;
    std::uint32_t region_id = 0;
};

bool title_like(const std::string& text) {
    std::size_t letters = 0;
    for (const unsigned char c : text)
        if (letter(c) || c >= 0xC0) ++letters;
    return letters >= 2;
}

// "Chapter 5", "Part 2", "Figure 3": the number labels something else and is
// not a page number, so a running header ending this way is not evidence.
bool ends_with_label(const std::string& text) {
    static const char* const labels[] = {
        "chapter", "part", "section", "unit", "lesson", "lecture", "appendix",
        "volume", "vol", "book", "figure", "fig", "table", "page", "step",
        "exercise", "problem", "example", "no", "number"};
    std::string word;
    for (auto it = text.rbegin(); it != text.rend(); ++it) {
        const unsigned char c = static_cast<unsigned char>(*it);
        if (letter(c)) word.insert(word.begin(), static_cast<char>(
            c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        else if (!word.empty()) break;
        else if (!space(c) && c != '.' && c != ':' && c != '#') break;
    }
    for (const char* label : labels)
        if (word == label) return true;
    return false;
}

// Printed page numbers in one margin band of a page. A candidate is a region
// that is only a page-number token, or - for the outermost region of the band
// only - a short running header/footer whose first or last word is the token
// and whose remainder is text ("Modern processors 3", "4 Introduction").
// Ordinal 0 is never a printed page number.
std::vector<MarginNumber> margin_numbers(const text::PageContent& content,
                                         double height,
                                         const MappingOptions& options,
                                         bool header) {
    struct InBand {
        const text::TextRegion* region;
        double top, bottom;
    };
    std::vector<InBand> band;
    for (const auto& region : content.regions) {
        if (!region.quad || region.text.empty()) continue;
        const double top = region.quad->points[0].y;
        const double bottom = region.quad->points[2].y;
        if (!std::isfinite(top) || !std::isfinite(bottom)) continue;
        const double center = (top + bottom) / 2.0;
        const bool inside =
            header ? center <= height * options.header_band_fraction
                   : center >= height * (1.0 - options.footer_band_fraction);
        if (inside) band.push_back({&region, top, bottom});
    }
    const InBand* outermost = nullptr;
    for (const auto& item : band)
        if (!outermost || (header ? item.top < outermost->top
                                  : item.bottom > outermost->bottom))
            outermost = &item;
    std::vector<MarginNumber> result;
    for (const auto& item : band) {
        const std::string text = trim(item.region->text);
        std::optional<Token> value = token(text);
        std::string literal = text;
        if (!value && &item == outermost && text.size() <= 80 &&
            text.find('\n') == std::string::npos) {
            const auto last = text.find_last_of(' ');
            const auto first = text.find(' ');
            if (last != std::string::npos &&
                title_like(text.substr(0, last)) &&
                !ends_with_label(text.substr(0, last)))
                if (auto t = token(text.substr(last + 1))) {
                    value = t;
                    literal = text.substr(last + 1);
                }
            if (!value && first != std::string::npos &&
                title_like(text.substr(first + 1)))
                if (auto t = token(text.substr(0, first))) {
                    value = t;
                    literal = text.substr(0, first);
                }
        }
        if (value && value->ordinal > 0) {
            if (&item == outermost && literal != text)
                return {{*value, literal, item.region->id}};  // Running form.
            result.push_back({*value, literal, item.region->id});
        }
    }
    return result;
}

std::vector<MappingObservation> observe(
    const std::vector<parsing::TocEntry>& entries,
    const DocumentEvidence& evidence, const MappingOptions& options,
    const Facts& facts) {
    std::vector<MappingObservation> observations;
    std::set<PageIndex> toc_pages;
    for (const auto& entry : entries)
        for (const auto& source : entry.sources)
            toc_pages.insert(source.page_index);
    for (const auto& page : evidence.pages) {
        if (!page.selected) continue;
        const auto& content = *page.selected;
        const double height = content.geometry.height_points;
        if (!std::isfinite(height) || height <= 0) continue;
        // Printed page numbers in the footer and header bands (S4-09).
        for (const bool header : {false, true}) {
            const auto candidates =
                margin_numbers(content, height, options, header);
            // Several numbers in one band (figure axes, code line numbers)
            // are not page-number evidence.
            if (candidates.size() != 1) continue;
            const auto& found = candidates.front();
            observations.push_back({
                header ? ObservationKind::PrintedHeader
                       : ObservationKind::PrintedFooter,
                page.page_index, found.literal, found.value.style,
                found.value.ordinal, found.value.prefix,
                text::SourceReference{page.page_index, content.revision,
                                      found.region_id, std::nullopt,
                                      std::nullopt},
                std::nullopt});
        }
        // Heading matches (S4-02, S4-11). Lines are regions in top-to-bottom
        // order; a heading may wrap over up to 3 nearby lines and a numbered
        // title may follow a "Chapter" label ("Chapter 9" / "Distributed-
        // memory parallel programming" / "with MPI"). Un-numbered titles only
        // match in the top band; titles starting with a number ("9.1 Message
        // passing") may match on any line, as exact numbered headings.
        if (toc_pages.count(page.page_index)) continue;
        struct Line {
            const text::TextRegion* region;
            double top, bottom;
            std::string key;
        };
        std::vector<Line> lines;
        for (const auto& region : content.regions) {
            if (!region.quad || region.text.empty()) continue;
            const double top = region.quad->points[0].y;
            const double bottom = region.quad->points[2].y;
            if (!std::isfinite(top) || !std::isfinite(bottom)) continue;
            auto key = heading_key(region.text);
            if (!key.empty()) lines.push_back({&region, top, bottom, std::move(key)});
        }
        std::stable_sort(lines.begin(), lines.end(),
                         [](const Line& a, const Line& b) { return a.top < b.top; });
        for (const auto& entry : entries) {
            const std::string title = heading_key(entry.title);
            if (title.empty()) continue;
            const bool numbered = digit(static_cast<unsigned char>(title[0]));
            const std::string labelled = "chapter " + title;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const double center = (lines[i].top + lines[i].bottom) / 2.0;
                if (!numbered && center > height * options.heading_band_fraction)
                    break;  // Sorted by top: no later line is in the band.
                std::string joined;
                bool matched = false;
                for (std::size_t j = i; j < lines.size() && j < i + 3; ++j) {
                    if (j > i) {
                        const double gap = lines[j].top - lines[j - 1].bottom;
                        const double size = std::max(
                            lines[j].bottom - lines[j].top,
                            lines[j - 1].bottom - lines[j - 1].top);
                        if (gap < -size || gap > 1.5 * size) break;
                        joined += ' ';
                    }
                    joined += lines[j].key;
                    if (joined == title || (numbered && joined == labelled)) {
                        matched = true;
                        break;
                    }
                    if (joined.size() > labelled.size()) break;
                }
                if (!matched) continue;
                MappingObservation observed;
                observed.kind = ObservationKind::HeadingMatch;
                observed.page_index = page.page_index;
                observed.literal = lines[i].region->text;
                observed.source = text::SourceReference{
                    page.page_index, content.revision, lines[i].region->id,
                    std::nullopt, std::nullopt};
                observed.entry_id = entry.id;
                if (entry.printed_reference && entry.printed_reference->ordinal) {
                    const auto ref = entry_token(*entry.printed_reference);
                    observed.style = ref.style;
                    observed.ordinal = ref.ordinal;
                    observed.prefix = ref.prefix;
                }
                observations.push_back(std::move(observed));
                break;  // One heading observation per entry per page.
            }
        }
    }
    for (const auto& fact_pair : facts) {
        const auto& label = fact_pair.second->viewer_label;
        if (label.availability != text::FactAvailability::Present ||
            !label.value) continue;
        const auto parsed = token(*label.value);
        MappingObservation observed;
        observed.kind = ObservationKind::ViewerLabel;
        observed.page_index = fact_pair.first;
        observed.literal = *label.value;
        if (parsed) {
            observed.style = parsed->style;
            observed.ordinal = parsed->ordinal;
            observed.prefix = parsed->prefix;
        }
        observations.push_back(std::move(observed));
    }
    std::sort(observations.begin(), observations.end(),
              [](const auto& lhs, const auto& rhs) {
                  return std::tie(lhs.page_index, lhs.kind, lhs.entry_id,
                                  lhs.literal) <
                         std::tie(rhs.page_index, rhs.kind, rhs.entry_id,
                                  rhs.literal);
              });
    return observations;
}

bool range_fits(const parsing::PrintedReference& ref,
                std::int64_t offset, PageCount count,
                const NumberingSection& section) {
    if (!ref.ordinal) return false;
    const auto first = shifted(*ref.ordinal, offset, count);
    if (!first || !in_section(*first, section)) return false;
    if (!ref.is_range) return true;
    if (!ref.range_end || *ref.range_end < *ref.ordinal) return false;
    const auto last = shifted(*ref.range_end, offset, count);
    return last && in_section(*last, section);
}
// Only acquired page content confirms a target: an exact heading for this
// entry, or a printed footer equal to its reference. Labels and links do not.
// Heading and footer observations always carry their source region.
std::vector<text::SourceReference> confirming_sources(
    const parsing::TocEntry& entry, PageIndex target,
    const std::vector<MappingObservation>& observations) {
    std::vector<text::SourceReference> result;
    const bool has_ref = entry.printed_reference &&
                         entry.printed_reference->ordinal &&
                         !entry.printed_reference->uncertain;
    for (const auto& observation : observations) {
        if (observation.page_index != target || !observation.source) continue;
        const bool heading =
            observation.kind == ObservationKind::HeadingMatch &&
            observation.entry_id && *observation.entry_id == entry.id;
        const bool footer =
            has_ref && (observation.kind == ObservationKind::PrintedFooter ||
                        observation.kind == ObservationKind::PrintedHeader) &&
            observation.ordinal &&
            same_token(Token{observation.style, *observation.ordinal,
                             observation.prefix},
                       entry_token(*entry.printed_reference));
        if (heading || footer) result.push_back(*observation.source);
    }
    return result;
}
bool pages_complete(const NumberingSection& section, const Pages& pages) {
    for (PageIndex page = section.first; page < section.end; ++page)
        if (!page_supplied(page, pages)) return false;
    return true;
}
std::vector<const NumberingSection*> compatible_sections(
    const parsing::TocEntry& entry, const DocumentEvidence& evidence,
    const Validated& valid) {
    std::vector<const NumberingSection*> result;
    const auto association = valid.associations.find(entry.id);
    if (!entry.printed_reference || !entry.printed_reference->ordinal ||
        entry.printed_reference->uncertain ||
        entry.printed_reference->numbering ==
            parsing::NumberingStyle::Unknown) {
        if (association != valid.associations.end())
            result.push_back(valid.sections.at(association->second));
        return result;
    }
    const auto value = entry_token(*entry.printed_reference);
    for (const auto& section : evidence.sections)
        if (matches_section(value, section) &&
            (association == valid.associations.end() ||
             association->second == section.id))
            result.push_back(&section);
    return result;
}
std::vector<PageIndex> label_pages(
    const parsing::PrintedReference& ref, const NumberingSection& section,
    const std::vector<MappingObservation>& observations,
    const Facts& facts) {
    std::vector<PageIndex> result;
    if (!section.viewer_labels_match_printed ||
        !labels_complete(section, facts)) return result;
    const auto expected = entry_token(ref);
    for (const auto& observation : observations)
        if (observation.kind == ObservationKind::ViewerLabel &&
            in_section(observation.page_index, section) &&
            observation.ordinal &&
            same_token(Token{observation.style, *observation.ordinal,
                             observation.prefix}, expected))
            result.push_back(observation.page_index);
    return result;
}
void append_anchor_sources(
    DestinationAlternative& choice, EntryMapping& mapped,
    const Anchors& anchors, std::int64_t offset,
    const NumberingSection& section,
    const std::vector<MappingObservation>& observations) {
    const auto found = anchors.by_offset.find(offset);
    if (found == anchors.by_offset.end()) return;
    for (const PageIndex page : found->second) {
        add_page(mapped.supporting_pages, page);
        for (const auto& observation : observations) {
            if (observation.page_index != page || !observation.ordinal ||
                !observation.source) continue;
            const auto observed_offset =
                static_cast<std::int64_t>(page) -
                static_cast<std::int64_t>(*observation.ordinal);
            if (observed_offset == offset &&
                matches_section(
                    Token{observation.style, *observation.ordinal,
                          observation.prefix}, section) &&
                std::none_of(choice.sources.begin(), choice.sources.end(),
                             [&](const auto& source) {
                                 return source_equal(source,
                                                     *observation.source);
                             }))
                choice.sources.push_back(*observation.source);
        }
    }
}

}  // namespace

Result<MappingResult> map(
    const std::vector<parsing::TocEntry>& entries,
    const DocumentEvidence& evidence,
    const std::vector<MappingOverride>& overrides,
    const MappingOptions& options) {
    Validated valid;
    if (const auto error = validate(entries, evidence, overrides,
                                    options, valid))
        return *error;
    MappingResult result;
    result.policy_id = kPolicyId;
    result.diagnostics = evidence.limitations;
    result.observations = observe(entries, evidence, options, valid.facts);
    std::map<std::string, Anchors> anchor_map;
    for (const auto& section : evidence.sections)
        anchor_map.emplace(section.id,
            anchors_for(section, result.observations, valid.entries,
                        valid.associations, evidence.sections,
                        options.require_target_confirmation));
    std::set<std::string> request_ids;
    for (const auto& entry : entries) {
        EntryMapping mapped;
        mapped.entry_id = entry.id;
        const auto compatible = compatible_sections(entry, evidence, valid);
        if (compatible.size() == 1)
            mapped.section_id = compatible.front()->id;
        else if (compatible.size() > 1)
            mapped.reasons.push_back(
                "Multiple compatible numbering sections; association required");
        const auto associated = valid.associations.find(entry.id);
        if (associated != valid.associations.end() && compatible.empty())
            mapped.reasons.push_back(
                "Explicit entry section conflicts with reference syntax");

        const auto manual_entry = valid.manual_entries.find(entry.id);
        if (manual_entry != valid.manual_entries.end()) {
            resolve(mapped, {manual_entry->second,
                             ResolutionMethod::ManualEntry,
                             "Explicit entry destination override", {}});
            for (const auto& link : valid.links[entry.id])
                if (link.destination != manual_entry->second)
                    mapped.reasons.push_back(
                        "Manual destination contradicts associated local link");
            if (compatible.size() == 1 && entry.printed_reference &&
                entry.printed_reference->ordinal) {
                const auto& observed = anchor_map.at(compatible.front()->id);
                const auto implied =
                    static_cast<std::int64_t>(manual_entry->second) -
                    static_cast<std::int64_t>(
                        *entry.printed_reference->ordinal);
                for (const auto& anchor : observed.by_offset)
                    if (anchor.first != implied) {
                        mapped.reasons.push_back(
                            "Manual entry destination contradicts observed anchors");
                        break;
                    }
                const auto offset =
                    valid.manual_offsets.find(compatible.front()->id);
                if (offset != valid.manual_offsets.end()) {
                    const auto predicted = shifted(
                        *entry.printed_reference->ordinal, offset->second,
                        evidence.input.page_count);
                    if (predicted && *predicted != manual_entry->second)
                        mapped.reasons.push_back(
                            "Manual entry destination contradicts section offset");
                }
            }
            result.entries.push_back(std::move(mapped));
            continue;
        }

        std::vector<DestinationAlternative> choices;
        // Labels and local links are untrusted hints. They become choices only
        // when acquired target content corroborates them.
        std::vector<DestinationAlternative> unverified;
        for (const auto& link : valid.links[entry.id]) {
            auto confirming = confirming_sources(entry, link.destination,
                                                 result.observations);
            if (!confirming.empty()) {
                confirming.insert(confirming.begin(), link.source);
                choices.push_back({
                    link.destination, ResolutionMethod::AssociatedLocalLink,
                    "Entry-associated local link corroborated by target content",
                    std::move(confirming)});
                continue;
            }
            unverified.push_back({
                link.destination, ResolutionMethod::AssociatedLocalLink,
                "Entry-associated local link lacks target content corroboration",
                {link.source}});
            request_page(result, request_ids, link.destination,
                         evidence.input.page_count,
                         "corroborate-link-" + entry.id, 3, options,
                         valid.pages);
        }
        bool contradictory_anchors = false;
        for (const auto* section : compatible) {
            const auto manual_offset =
                valid.manual_offsets.find(section->id);
            const auto& anchors = anchor_map.at(section->id);
            if (manual_offset != valid.manual_offsets.end() &&
                entry.printed_reference &&
                entry.printed_reference->ordinal) {
                if (!range_fits(*entry.printed_reference,
                                manual_offset->second,
                                evidence.input.page_count, *section))
                    return Error{ErrorCode::InvalidArgument,
                                 "Section offset override maps entry outside its section"};
                const auto destination =
                    shifted(*entry.printed_reference->ordinal,
                            manual_offset->second,
                            evidence.input.page_count);
                DestinationAlternative choice{
                    *destination, ResolutionMethod::ManualOffset,
                    "Explicit section offset override", {}};
                if (compatible.size() == 1) {
                    resolve(mapped, choice);
                    for (const auto& anchor : anchors.by_offset)
                        if (anchor.first != manual_offset->second) {
                            mapped.reasons.push_back(
                                "Manual offset contradicts observed anchors");
                            break;
                        }
                    for (const auto& link : valid.links[entry.id])
                        if (link.destination != *destination)
                            mapped.reasons.push_back(
                                "Manual offset contradicts associated local link");
                    break;
                }
                choices.push_back(std::move(choice));
                continue;
            }
            if (!entry.printed_reference ||
                !entry.printed_reference->ordinal ||
                entry.printed_reference->uncertain) {
                if (pages_complete(*section, valid.pages)) {
                    for (const auto& observation : result.observations)
                        if (observation.kind ==
                                ObservationKind::HeadingMatch &&
                            observation.entry_id &&
                            *observation.entry_id == entry.id &&
                            in_section(observation.page_index, *section))
                            choices.push_back({
                                observation.page_index,
                                ResolutionMethod::HeadingMatch,
                                "Exact heading in fully observed section",
                                observation.source ?
                                    std::vector<text::SourceReference>{
                                        *observation.source} :
                                    std::vector<text::SourceReference>{}});
                }
                continue;
            }
            for (const PageIndex labeled :
                 label_pages(*entry.printed_reference, *section,
                             result.observations, valid.facts)) {
                auto confirming = confirming_sources(entry, labeled,
                                                     result.observations);
                if (!confirming.empty()) {
                    choices.push_back({
                        labeled, ResolutionMethod::ViewerLabel,
                        "Viewer label corroborated by target content",
                        std::move(confirming)});
                    continue;
                }
                unverified.push_back({
                    labeled, ResolutionMethod::ViewerLabel,
                    "Viewer label lacks target content corroboration", {}});
                request_page(result, request_ids, labeled,
                             evidence.input.page_count,
                             "corroborate-label-" + entry.id, 2, options,
                             valid.pages);
            }
            if (anchors.contradictory) {
                contradictory_anchors = true;
                mapped.reasons.push_back(
                    "Checked anchors imply conflicting offsets in section " +
                    section->id);
                for (const auto& offset : anchors.by_offset) {
                    if (!range_fits(*entry.printed_reference, offset.first,
                                    evidence.input.page_count, *section))
                        continue;
                    const auto destination =
                        shifted(*entry.printed_reference->ordinal,
                                offset.first, evidence.input.page_count);
                    DestinationAlternative choice{
                        *destination, ResolutionMethod::InferredOffset,
                        "Conflicting observed offset " +
                            std::to_string(offset.first), {}};
                    append_anchor_sources(choice, mapped, anchors,
                                          offset.first, *section,
                                          result.observations);
                    choices.push_back(std::move(choice));
                }
                continue;
            }
            if (!anchors.strong) {
                mapped.reasons.push_back(
                    "Fewer than two independent agreeing anchors in section " +
                    section->id);
                if (anchors.by_offset.size() == 1) {
                    const auto destination =
                        shifted(*entry.printed_reference->ordinal,
                                anchors.by_offset.begin()->first,
                                evidence.input.page_count);
                    if (destination && in_section(*destination, *section))
                        request_page(result, request_ids, *destination,
                                     evidence.input.page_count,
                                     "check-target-" + entry.id, 2,
                                     options, valid.pages);
                }
                for (const PageIndex sample : {
                         section->first,
                         static_cast<PageIndex>(section->end - 1)})
                    request_page(result, request_ids, sample,
                                 evidence.input.page_count,
                                 "numbering-anchor-" + section->id, 1,
                                 options, valid.pages);
                continue;
            }
            if (!range_fits(*entry.printed_reference, *anchors.strong,
                            evidence.input.page_count, *section)) {
                mapped.reasons.push_back(
                    "Inferred destination or range is outside section");
                continue;
            }
            const auto destination =
                shifted(*entry.printed_reference->ordinal,
                        *anchors.strong, evidence.input.page_count);
            if (options.require_target_confirmation &&
                confirming_sources(entry, *destination,
                                   result.observations).empty()) {
                mapped.reasons.push_back(
                    "Offset has two anchors but target lacks confirmation");
                request_page(result, request_ids, *destination,
                             evidence.input.page_count,
                             "confirm-target-" + entry.id, 3,
                             options, valid.pages);
                continue;
            }
            DestinationAlternative choice{
                *destination, ResolutionMethod::InferredOffset,
                options.require_target_confirmation ?
                    "Two independent agreeing anchors and target confirmation" :
                    "Two independent agreeing anchors; caller waived target confirmation",
                {}};
            append_anchor_sources(choice, mapped, anchors,
                                  *anchors.strong, *section,
                                  result.observations);
            if (!anchors.outliers.empty()) {
                std::string pages;
                for (std::size_t i = 0; i < anchors.outliers.size() && i < 5; ++i)
                    pages += (i ? ", " : "") + std::to_string(anchors.outliers[i]);
                mapped.reasons.push_back(
                    "Ignored " + std::to_string(anchors.outliers.size()) +
                    " outlier anchor page(s) (physical " + pages +
                    (anchors.outliers.size() > 5 ? ", ..." : "") +
                    ") against the dominant offset");
            }
            choices.push_back(std::move(choice));
        }
        const auto note_contradicting_hints = [&]() {
            for (const auto& hint : unverified)
                if (hint.pdf_page_index != *mapped.pdf_page_index)
                    mapped.reasons.push_back(
                        "Uncorroborated hint suggests another destination: " +
                        hint.reason + " (index " +
                        std::to_string(hint.pdf_page_index) + ")");
        };
        if (mapped.status == MappingStatus::Resolved) {
            note_contradicting_hints();
            result.entries.push_back(std::move(mapped));
            continue;
        }
        std::sort(choices.begin(), choices.end(),
                  [](const auto& a, const auto& b) {
                      return std::tie(a.pdf_page_index, a.method) <
                             std::tie(b.pdf_page_index, b.method);
                  });
        choices.erase(std::unique(choices.begin(), choices.end(),
            [](const auto& a, const auto& b) {
                return a.pdf_page_index == b.pdf_page_index &&
                       a.method == b.method;
            }), choices.end());
        std::set<PageIndex> destinations;
        for (const auto& choice : choices)
            destinations.insert(choice.pdf_page_index);
        if (destinations.size() > 1) {
            mapped.status = MappingStatus::Ambiguous;
            mapped.alternatives = std::move(choices);
            mapped.reasons.push_back(
                "Plausible destination evidence is not unique");
        } else if (destinations.size() == 1 &&
                   !contradictory_anchors && compatible.size() <= 1) {
            resolve(mapped, choices.front());
            if (choices.size() > 1)
                mapped.reasons.push_back(
                    "Independent methods agree on destination");
            note_contradicting_hints();
        } else {
            if (contradictory_anchors || compatible.size() > 1) {
                mapped.alternatives = std::move(choices);
                mapped.reasons.push_back(
                    "Evidence or section scope remains ambiguous");
            }
            if (mapped.alternatives.empty()) {
                mapped.alternatives = unverified;
                if (entry.printed_reference &&
                    entry.printed_reference->ordinal &&
                    !entry.printed_reference->uncertain) {
                    const auto expected =
                        entry_token(*entry.printed_reference);
                    for (const auto& observation : result.observations)
                        if (observation.kind ==
                                ObservationKind::ViewerLabel &&
                            observation.ordinal &&
                            same_token(
                                Token{observation.style,
                                      *observation.ordinal,
                                      observation.prefix}, expected) &&
                            std::none_of(
                                unverified.begin(), unverified.end(),
                                [&](const auto& hint) {
                                    return hint.pdf_page_index ==
                                               observation.page_index &&
                                           hint.method ==
                                               ResolutionMethod::ViewerLabel;
                                }))
                            mapped.alternatives.push_back({
                                observation.page_index,
                                ResolutionMethod::ViewerLabel,
                                "Viewer label without trusted complete section evidence",
                                {}});
                }
                std::set<PageIndex> hinted;
                for (const auto& hint : mapped.alternatives)
                    hinted.insert(hint.pdf_page_index);
                if (hinted.size() > 1) {
                    mapped.status = MappingStatus::Ambiguous;
                    mapped.reasons.push_back(
                        "Multiple uncorroborated label/link alternatives");
                } else if (!hinted.empty()) {
                    mapped.reasons.push_back(
                        "Label/link hint awaits target content corroboration");
                }
            }
            if (!entry.printed_reference)
                mapped.reasons.push_back(
                    "Entry has no printed reference or corroborated local link");
            else if (entry.printed_reference->uncertain ||
                     !entry.printed_reference->ordinal)
                mapped.reasons.push_back(
                    "Printed reference syntax is unavailable or uncertain");
            else if (compatible.empty())
                mapped.reasons.push_back(
                    "No matching known numbering section");
            mapped.reasons.push_back("No justified destination");
        }
        result.entries.push_back(std::move(mapped));
    }
    std::sort(result.requests.begin(), result.requests.end(),
              [](const auto& lhs, const auto& rhs) {
                  if (lhs.priority != rhs.priority)
                      return lhs.priority > rhs.priority;
                  return std::tie(lhs.first, lhs.id) <
                         std::tie(rhs.first, rhs.id);
              });
    if (result.requests.size() > options.max_requests)
        result.requests.resize(options.max_requests);
    return result;
}

}  // namespace pdfbookmark::mapping
