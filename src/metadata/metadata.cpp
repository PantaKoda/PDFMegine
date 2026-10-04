#include <pdfbookmark/metadata/metadata.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pdfbookmark::metadata {
namespace {

constexpr const char* kPolicyId = "s6-document-metadata-v3";

// ---------------------------------------------------------------- text utils

bool ascii_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
bool ascii_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }
bool ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }

std::string trim(const std::string& text) {
    std::size_t first = 0, end = text.size();
    while (first < end && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (end > first && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(first, end - first);
}

// Collapses whitespace runs to one space.
std::string squeeze(const std::string& text) {
    std::string out;
    bool space = false;
    for (const unsigned char c : trim(text)) {
        if (std::isspace(c)) { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += static_cast<char>(c);
    }
    return out;
}

// ASCII lower-case; other bytes unchanged (keeps UTF-8 byte offsets).
std::string lower(std::string text) {
    for (char& c : text)
        if (ascii_upper(static_cast<unsigned char>(c)))
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// Comparison key: lower-case letters/digits (and non-ASCII bytes), single
// spaces between words, punctuation dropped.
std::string key(const std::string& text) {
    std::string out;
    bool gap = false;
    for (const unsigned char c : text) {
        const bool word = ascii_upper(c) || ascii_lower(c) || ascii_digit(c) || c >= 0x80;
        if (!word) { gap = true; continue; }
        if (gap && !out.empty()) out += ' ';
        gap = false;
        out += static_cast<char>(ascii_upper(c) ? c - 'A' + 'a' : c);
    }
    return out;
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// Has letters and no lower-case ASCII letter.
bool all_caps(const std::string& text) {
    bool letters = false;
    for (const unsigned char c : text) {
        if (ascii_lower(c)) return false;
        if (ascii_upper(c)) letters = true;
    }
    return letters;
}

// A line that cannot end a title or subtitle: it ends with a comma, colon
// or hyphen, or with a word such as "of", "and" or "the".
bool open_ending(const std::string& text) {
    const std::string value = lower(trim(text));
    if (value.empty()) return false;
    const char last = value.back();
    if (last == ',' || last == ':' || last == '-' || last == ';') return true;
    const auto space = value.rfind(' ');
    const std::string word = space == std::string::npos ? value : value.substr(space + 1);
    static const std::set<std::string> open = {
        "a", "an", "and", "as", "at", "by", "for", "from", "in", "into", "of",
        "on", "or", "the", "to", "via", "with"};
    return open.count(word) != 0;
}

// ---------------------------------------------------------------- lines

struct Line {
    std::string text;       // Squeezed, reading order within the line.
    std::string lower;      // lower(text).
    double top = 0, bottom = 0, left = 0, right = 0;
    double height = 0;      // Largest fragment height (font size proxy).
    text::SourceReference source;  // First fragment's region.
};

struct Page {
    const text::PageAcquisition* acquisition = nullptr;
    const text::PageContent* content = nullptr;
    std::vector<Line> lines;  // Top to bottom.
    PageRole role = PageRole::Unknown;
    std::vector<std::string> reasons;
    // Title block found on this page, if any (indices into lines).
    std::vector<std::size_t> title_block;
    std::vector<std::size_t> subtitle_lines;  // Lines read as the subtitle.
    // A "Copyright ..." running head or foot on a page that is not a
    // copyright page: weaker evidence, used only when no copyright or title
    // page states that kind of year (S6-10).
    std::optional<std::size_t> running_copyright;
    double prominence = 0;
};

std::vector<Line> build_lines(const text::PageContent& content) {
    struct Fragment {
        std::string text;
        double top, bottom, left, right;
        std::uint32_t region;
    };
    std::vector<Fragment> fragments;
    for (const auto& region : content.regions) {
        if (!region.quad || region.text.empty()) continue;
        const auto& q = region.quad->points;
        const double top = std::min(q[0].y, q[1].y), bottom = std::max(q[2].y, q[3].y);
        const double left = std::min(q[0].x, q[3].x), right = std::max(q[1].x, q[2].x);
        if (!(bottom > top) || !std::isfinite(top) || !std::isfinite(bottom)) continue;
        // A region holding several text lines is divided evenly (S2-03 style).
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            const auto next = region.text.find('\n', start);
            parts.push_back(region.text.substr(start, next == std::string::npos
                                                          ? std::string::npos
                                                          : next - start));
            if (next == std::string::npos) break;
            start = next + 1;
        }
        const double step = (bottom - top) / static_cast<double>(parts.size());
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const std::string text = squeeze(parts[i]);
            if (text.empty()) continue;
            fragments.push_back({text, top + step * static_cast<double>(i),
                                 top + step * static_cast<double>(i + 1), left, right,
                                 region.id});
        }
    }
    std::stable_sort(fragments.begin(), fragments.end(), [](const auto& a, const auto& b) {
        return (a.top + a.bottom) < (b.top + b.bottom);
    });
    std::vector<Line> lines;
    std::vector<std::vector<Fragment>> groups;
    for (const auto& f : fragments) {
        const double center = (f.top + f.bottom) / 2, height = f.bottom - f.top;
        bool joined = false;
        if (!groups.empty()) {
            auto& group = groups.back();
            double g_top = group[0].top, g_bottom = group[0].bottom;
            for (const auto& m : group) {
                g_top = std::min(g_top, m.top);
                g_bottom = std::max(g_bottom, m.bottom);
            }
            const double g_center = (g_top + g_bottom) / 2;
            if (std::abs(center - g_center) <= 0.4 * std::max(height, g_bottom - g_top)) {
                group.push_back(f);
                joined = true;
            }
        }
        if (!joined) groups.push_back({f});
    }
    for (auto& group : groups) {
        std::sort(group.begin(), group.end(),
                  [](const auto& a, const auto& b) { return a.left < b.left; });
        Line line;
        line.top = group[0].top;
        line.bottom = group[0].bottom;
        line.left = group[0].left;
        line.right = group[0].right;
        for (const auto& f : group) {
            if (!line.text.empty()) line.text += ' ';
            line.text += f.text;
            line.top = std::min(line.top, f.top);
            line.bottom = std::max(line.bottom, f.bottom);
            line.left = std::min(line.left, f.left);
            line.right = std::max(line.right, f.right);
            line.height = std::max(line.height, f.bottom - f.top);
        }
        line.text = squeeze(line.text);
        line.lower = lower(line.text);
        line.source = {content.page_index, content.revision, group[0].region,
                       std::nullopt, std::nullopt};
        lines.push_back(std::move(line));
    }
    return lines;
}

// ---------------------------------------------------------------- classifiers

// "0 1983 by ...", "O 2004 Publisher": a copyright sign that the text layer
// read as 0, O or C, at the start of a lower-cased line, with a year and a
// holder after it ("0 2000 4000 6000" on a figure axis is not one).
bool symbol_copyright(const std::string& l) {
    static const std::regex pattern(R"(^(?:0|o|c|\(c\))\s*\d{4},?\s+[a-z])");
    return std::regex_search(l, pattern);
}

// "... Copyright, Designs and Patents Act 1988", "the 1976 United States
// Copyright Act": the year of a law, not of the book.
bool cites_law(const std::string& l) {
    static const std::regex pattern(R"(\bact\b)");
    return std::regex_search(l, pattern);
}

bool copyright_line(const std::string& l) {
    return contains(l, "\xC2\xA9") || contains(l, "copyright") || contains(l, "(c) ") ||
           symbol_copyright(l) ||
           contains(l, "all rights reserved") || contains(l, "isbn") ||
           contains(l, "first published") || contains(l, "printed in") ||
           contains(l, "library of congress") || contains(l, "published by") ||
           contains(l, "cataloging") || contains(l, "cataloguing");
}

bool publisher_or_series_line(const std::string& l) {
    static const char* const words[] = {
        "press", "publish", "publications", "verlag", "series", "group", "llc",
        " inc", "ltd", "books", " book", "&", "www.", "http", "isbn", "imprint",
        "edition"};
    for (const char* w : words)
        if (contains(l, w)) return true;
    return copyright_line(l);
}

bool contents_line(const std::string& l) {
    const std::string k = key(l);
    return k == "contents" || k == "table of contents" || k == "brief contents" ||
           k == "contents at a glance";
}

// Words that never appear in personal names (title and boilerplate words).
bool name_stopword(const std::string& w) {
    static const std::set<std::string> stop = {
        "the", "a", "an", "of", "to", "and", "for", "in", "on", "with", "by", "from",
        "introduction", "computing", "science", "sciences", "engineering",
        "engineers", "scientists", "edition", "series", "press", "chapter",
        "contents", "volume", "university", "institute", "department", "school",
        "handbook", "guide", "manual", "principles", "programming", "performance",
        "high", "computer", "computational", "applications", "theory", "systems",
        "data", "analysis", "methods", "design", "advanced", "practical", "basic",
        "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth",
        "ninth", "tenth", "new", "revised", "published", "copyright",
        "printed", "library", "all", "rights", "reserved", "group", "foreword",
        "preface", "dedicated", "cover", "design", "international", "global"};
    return stop.count(w) != 0;
}

// "Georg Hager", "Craig E Rasmussen", "A. B. Smith", "GEORG HAGER".
// A no-break space (U+00A0) as a plain space: "Marius\u00A0Iulian".
std::string plain_spaces(std::string text) {
    std::size_t at;
    while ((at = text.find("\xC2\xA0")) != std::string::npos) text.replace(at, 2, " ");
    return text;
}

bool name_like(const std::string& raw) {
    const std::string text = squeeze(plain_spaces(raw));
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find(' ', start);
        tokens.push_back(text.substr(start, end == std::string::npos ? std::string::npos
                                                                     : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (tokens.size() < 2 || tokens.size() > 5) return false;
    // A soft hyphen (U+00AD) belongs to a typeset word or logo, not a name.
    if (contains(text, "\xC2\xAD")) return false;
    std::size_t words = 0;
    const bool all_caps = std::all_of(text.begin(), text.end(), [](char c) {
        return !ascii_lower(static_cast<unsigned char>(c));
    });
    // Small capitals split after each capital: "F IFTH E DITION".
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
        if (tokens[i].size() == 1 && name_stopword(lower(key(tokens[i] + tokens[i + 1]))))
            return false;
    for (const auto& token : tokens) {
        const auto c0 = static_cast<unsigned char>(token[0]);
        // A capital or a non-ASCII letter; U+0080..00BF (lead byte C2: "»",
        // "©") and U+2000.. (lead byte E2) hold
        // dashes, quotes and symbols (a dash before a name marks a quotation's source).
        if (!(ascii_upper(c0) || (c0 >= 0xC3 && c0 != 0xE2))) return false;
        // Initial: "B" or "A." (before the stop words: "A." is not "a"; a
        // bare "A" still is, as in "A History").
        if ((token.size() == 1 && token != "A") ||
            (token.size() == 2 && token[1] == '.'))
            continue;
        if (name_stopword(lower(key(token)))) return false;
        std::size_t letters = 0;
        for (std::size_t i = 1; i < token.size(); ++i) {
            const auto c = static_cast<unsigned char>(token[i]);
            if (ascii_lower(c) || c >= 0x80 || (all_caps && ascii_upper(c))) ++letters;
            else if (c == '-' || c == '\'' || c == '.' || (ascii_upper(c) && i > 1)) continue;
            else return false;
        }
        if (letters >= 1) ++words;
    }
    return words >= 2 || (words == 1 && tokens.size() >= 2);
}

ContributorRole organization_or(ContributorRole role, const std::string& name) {
    const std::string l = lower(name);
    for (const char* w : {" inc", " llc", " ltd", "group", "university", "institute",
                          "corporation", "company", "foundation", "association",
                          "society", "consortium"})
        if (contains(l, w)) return ContributorRole::Organization;
    return role;
}

// Splits "Georg Hager and Gerhard Wellein" / "A, B, and C" into names.
std::vector<std::string> split_names(const std::string& text) {
    std::string s = " " + squeeze(plain_spaces(text)) + " ";
    for (const char* sep : {" and ", " & ", ";"}) {
        std::size_t pos;
        while ((pos = lower(s).find(sep)) != std::string::npos)
            s.replace(pos, std::string(sep).size(), ",");
    }
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto end = s.find(',', start);
        const std::string item = squeeze(s.substr(start, end == std::string::npos
                                                             ? std::string::npos
                                                             : end - start));
        if (!item.empty()) out.push_back(item);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

// ---------------------------------------------------------------- roles

std::optional<double> median_height(const std::vector<Line>& lines) {
    if (lines.empty()) return std::nullopt;
    std::vector<double> h;
    for (const auto& l : lines) h.push_back(l.height);
    std::sort(h.begin(), h.end());
    return h[(h.size() - 1) / 2];  // Lower median: few-line title pages.
}

// Largest contiguous block of prominent, non-boilerplate lines.
void find_title_block(Page& page, const MetadataOptions& options) {
    const auto& lines = page.lines;
    std::size_t anchor = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (publisher_or_series_line(lines[i].lower) || contents_line(lines[i].lower))
            continue;
        if (key(lines[i].text).size() < 2) continue;
        if (anchor == lines.size() || lines[i].height > lines[anchor].height) anchor = i;
    }
    if (anchor == lines.size()) return;
    const double min_height = options.title_block_ratio * lines[anchor].height;
    const auto eligible = [&](std::size_t i) {
        return lines[i].height >= min_height && !publisher_or_series_line(lines[i].lower) &&
               !contents_line(lines[i].lower);
    };
    // A smaller line after a much wider gap than those inside the block
    // starts a subtitle ("Black Holes, / White Dwarfs, / and Neutron Stars"
    // then, set apart, "The Physics of Compact Objects").
    double widest_gap = -1;  // Largest gap inside the block; none yet.
    const auto set_apart = [&](std::size_t i, double gap) {
        return widest_gap >= 0 && gap > 2.5 * std::max(widest_gap, 1.0) &&
               lines[i].height < 0.9 * lines[anchor].height;
    };
    std::size_t first = anchor, last = anchor;
    while (first > 0 && eligible(first - 1)) {
        const double gap = lines[first].top - lines[first - 1].bottom;
        if (gap > 1.2 * lines[anchor].height || set_apart(first - 1, gap)) break;
        widest_gap = std::max(widest_gap, gap);
        --first;
    }
    while (last + 1 < lines.size() && eligible(last + 1)) {
        const double gap = lines[last + 1].top - lines[last].bottom;
        if (gap > 1.2 * lines[anchor].height || set_apart(last + 1, gap)) break;
        widest_gap = std::max(widest_gap, gap);
        ++last;
    }
    for (std::size_t i = first; i <= last; ++i) page.title_block.push_back(i);
    double next = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if ((i < first || i > last) && !publisher_or_series_line(lines[i].lower))
            next = std::max(next, lines[i].height);
    page.prominence = next > 0 ? lines[anchor].height / next : 1e9;
}

void classify(Page& page, PageIndex first_page, const MetadataOptions& options) {
    const auto& a = *page.acquisition;
    if (!page.content || page.lines.empty()) {
        page.role = PageRole::Unknown;
        page.reasons.push_back(page.content ? "No positioned text"
                                            : "No usable text (" +
                                                  std::string(a.outcome == text::Outcome::NoTextFound
                                                                  ? "no text found"
                                                                  : "acquisition failed or skipped") +
                                                  ")");
        return;
    }
    std::size_t copyright = 0, in_margin = 0, margin_line = 0;
    const double page_height = page.content->geometry.height_points;
    for (std::size_t i = 0; i < page.lines.size(); ++i) {
        const auto& line = page.lines[i];
        if (line.top < page_height * 0.3 && contents_line(line.lower)) {
            page.role = PageRole::Contents;
            page.reasons.push_back("Contents heading");
            return;
        }
        if (copyright_line(line.lower)) {
            ++copyright;
            if (line.bottom < page_height * 0.1 || line.top > page_height * 0.9) {
                ++in_margin;
                margin_line = i;
            }
        }
    }
    // One "Copyright ..." line in the header or footer of a full page is a
    // running head or foot, not a copyright page.
    if (copyright == 1 && in_margin == 1 && page.lines.size() >= 15) {
        copyright = 0;
        page.running_copyright = margin_line;
        page.reasons.push_back("Copyright line in the page margin only (running head or foot)");
    }
    if (copyright) {
        page.role = PageRole::Copyright;
        page.reasons.push_back(std::to_string(copyright) +
                               " copyright/publication/ISBN statement line(s)");
        return;
    }
    // Pages whose lines mostly end in a page number are table-of-contents
    // pages, also continuation pages without a "Contents" heading.
    std::size_t referenced = 0;
    static const std::regex trailing_ref(R"((^|\s)(\d{1,4}|[ivxlcdm]{1,7})$)");
    for (const auto& line : page.lines)
        if (key(line.text).size() > 4 && std::regex_search(line.lower, trailing_ref))
            ++referenced;
    if (referenced >= 2 && referenced * 2 >= page.lines.size()) {
        page.role = PageRole::Contents;
        page.reasons.push_back(std::to_string(referenced) +
                               " lines end with a page reference (contents rows)");
        return;
    }
    find_title_block(page, options);
    // Compare the title with the page's OTHER lines: on a half-title page
    // the title lines themselves dominate the page's median.
    std::vector<Line> others;
    for (std::size_t i = 0; i < page.lines.size(); ++i)
        if (std::find(page.title_block.begin(), page.title_block.end(), i) ==
            page.title_block.end())
            others.push_back(page.lines[i]);
    const std::optional<double> median = median_height(others);
    // The block's largest line (its first line may be a smaller lead-in such
    // as "Introduction to").
    double block_height = 0;
    for (const auto i : page.title_block)
        block_height = std::max(block_height, page.lines[i].height);
    // The title must stand out from the other lines; with no other lines it
    // must be display-sized (>= 16 pt), which excludes dedications/epigraphs.
    const bool prominent = block_height > 0 &&
                           (page.prominence >= 1.3 || page.lines.size() <= 6) &&
                           page.lines.size() <= 25 &&
                           (median ? block_height >= 1.2 * *median : block_height >= 16);
    if (prominent && a.page_index == first_page &&
        (a.assessment.image_object_count > 0 || page.content->source == text::Source::Ocr)) {
        page.role = PageRole::Cover;
        page.reasons.push_back("First page with prominent display text over an image");
        return;
    }
    if (prominent) {
        page.role = PageRole::TitlePage;
        page.reasons.push_back("Few lines with a prominent title block");
        return;
    }
    page.title_block.clear();
    page.role = PageRole::Other;
}

double role_weight(PageRole role) {
    return role == PageRole::TitlePage ? 3 : role == PageRole::Cover ? 2 : 1;
}

Evidence evidence(const Line& line, std::string reason) {
    return {line.source, line.text, std::move(reason)};
}

// ---------------------------------------------------------------- title

std::vector<Contributor> parse_contributors(const std::string& text, ContributorRole role);

// A line of personal names ("Joseph F. Boudreau and Eric S. Swanson").
bool names_line(const std::string& text) {
    return !parse_contributors(text, ContributorRole::Author).empty();
}

// "1 mathematical preliminaries", "chapter 3 ...": a chapter opening set
// large, not the book's title (a title key, lower case).
bool chapter_heading(const std::string& k) {
    if (k.rfind("chapter ", 0) == 0) return true;
    std::size_t digits = 0;
    while (digits < k.size() && ascii_digit(static_cast<unsigned char>(k[digits]))) ++digits;
    return digits >= 1 && digits <= 2 && digits < k.size() && k[digits] == ' ';
}

void resolve_title(std::vector<Page>& pages, const DocumentHints& hints,
                   const MetadataOptions& options, MetadataResult& out) {
    struct Found {
        const Page* page;
        TitleValue value;
        std::vector<Evidence> evidence;
    };
    std::vector<Found> found;
    for (auto& page : pages) {
        if (page.title_block.empty() ||
            (page.role != PageRole::Cover && page.role != PageRole::TitlePage))
            continue;
        std::string text;
        std::vector<Evidence> ev;
        for (const auto i : page.title_block) {
            text += (text.empty() ? "" : " ") + page.lines[i].text;
            ev.push_back(evidence(page.lines[i], "Prominent title line"));
        }
        TitleValue value{squeeze(text), std::nullopt};
        const auto colon = value.title.find(": ");
        if (colon != std::string::npos) {
            value.subtitle = value.title.substr(colon + 2);
            value.title = value.title.substr(0, colon);
        } else {
            // A smaller, non-boilerplate, non-name line right below the block,
            // continued by lines in the same style ("THE PHYSICS OF" /
            // "COMPACT OBJECTS").
            std::size_t below = page.title_block.back() + 1;
            double h = 0;  // Largest title line.
            for (const auto i : page.title_block) h = std::max(h, page.lines[i].height);
            if (below < page.lines.size()) {
                const auto& l = page.lines[below];
                if (l.height >= 0.35 * h && l.height < 0.9 * h &&
                    l.top - page.lines[below - 1].bottom <= 2 * h &&
                    !publisher_or_series_line(l.lower) && !names_line(l.text) &&
                    key(l.text).size() > 3) {
                    std::string subtitle = l.text;
                    page.subtitle_lines.push_back(below);
                    ev.push_back(evidence(l, "Subtitle below the title"));
                    while (++below < page.lines.size()) {
                        const auto& next = page.lines[below];
                        const auto& previous = page.lines[below - 1];
                        if (std::abs(next.height - l.height) > 0.15 * l.height ||
                            next.top - previous.bottom > 1.2 * l.height ||
                            publisher_or_series_line(next.lower) ||
                            (names_line(next.text) && !open_ending(previous.text)))
                            break;
                        subtitle += " " + next.text;
                        page.subtitle_lines.push_back(below);
                        ev.push_back(evidence(next, "Subtitle continued"));
                    }
                    value.subtitle = squeeze(subtitle);
                }
            }
        }
        found.push_back({&page, value, ev});
    }
    // A title block that ran on into its subtitle ("... and Neutron Stars
    // The Physics of Compact Objects") agrees with a page that sets the
    // subtitle apart: split it the same way.
    for (auto& f : found) {
        if (f.value.subtitle) continue;
        const std::string whole = key(f.value.title);
        for (const auto& g : found) {
            if (!g.value.subtitle || key(g.value.title + " " + *g.value.subtitle) != whole)
                continue;
            const std::string head = key(g.value.title);
            for (auto at = f.value.title.find(' '); at != std::string::npos;
                 at = f.value.title.find(' ', at + 1))
                if (key(f.value.title.substr(0, at)) == head) {
                    f.value.subtitle = squeeze(f.value.title.substr(at));
                    f.value.title = squeeze(f.value.title.substr(0, at));
                    break;
                }
            break;
        }
    }
    std::map<std::string, std::vector<Found>> by_key;
    std::vector<std::string> order;
    for (const auto& f : found) {
        const std::string k = key(f.value.title);
        if (k.empty() || chapter_heading(k)) continue;
        if (!by_key.count(k)) order.push_back(k);
        by_key[k].push_back(f);
    }
    if (by_key.empty()) {
        out.title.reasons.push_back("No prominent title block on a cover or title page");
        return;
    }
    // Rank: distinct supporting pages, then best page role, then prominence.
    const auto score = [&](const std::vector<Found>& f) {
        double best_role = 0, best_prominence = 0;
        for (const auto& x : f) {
            best_role = std::max(best_role, role_weight(x.page->role));
            best_prominence = std::max(best_prominence, std::min(x.page->prominence, 10.0));
        }
        return 100.0 * static_cast<double>(f.size()) + 10 * best_role + best_prominence;
    };
    std::stable_sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
        return score(by_key[a]) > score(by_key[b]);
    });
    const auto& best = by_key[order.front()];
    // Prefer native text over OCR, then a title page, for the returned value.
    const Found* chosen = &best.front();
    for (const auto& f : best) {
        const bool better_source = f.page->content->source == text::Source::EmbeddedPdf &&
                                   chosen->page->content->source != text::Source::EmbeddedPdf;
        if (better_source || (f.page->role == PageRole::TitlePage &&
                              chosen->page->role != PageRole::TitlePage &&
                              f.page->content->source == chosen->page->content->source))
            chosen = &f;
    }
    const bool agree = best.size() >= 2;
    const bool rivals = order.size() > 1;
    const bool single_clear = best.size() == 1 && !rivals &&
                              best.front().page->prominence >= options.title_prominence;
    for (std::size_t i = 1; i < order.size(); ++i) {
        const auto& alt = by_key[order[i]];
        Candidate<TitleValue> c{alt.front().value, score(alt), alt.front().evidence, {}};
        c.reasons.push_back("Title block on " + std::to_string(alt.size()) + " page(s)");
        out.title.alternatives.push_back(std::move(c));
    }
    if (agree || single_clear) {
        out.title.status = FieldStatus::Resolved;
        out.title.value = chosen->value;
        auto& value = *out.title.value;
        // Keep a subtitle found on any agreeing page.
        if (!value.subtitle)
            for (const auto& f : best)
                if (f.value.subtitle) { value.subtitle = f.value.subtitle; break; }
        // Show the mixed-case reading of the same words over an all-caps one
        // ("THE PHYSICS OF COMPACT OBJECTS" on the title page).
        for (const auto& f : best) {
            if (all_caps(value.title) && !all_caps(f.value.title))
                value.title = f.value.title;
            if (value.subtitle && f.value.subtitle && all_caps(*value.subtitle) &&
                !all_caps(*f.value.subtitle) && key(*value.subtitle) == key(*f.value.subtitle))
                value.subtitle = f.value.subtitle;
        }
        for (const auto& f : best)
            out.title.evidence.insert(out.title.evidence.end(), f.evidence.begin(),
                                      f.evidence.end());
        out.title.reasons.push_back(
            agree ? "Same title on " + std::to_string(best.size()) + " pages"
                  : "Single, clearly prominent title block on a " +
                        std::string(role_name(best.front().page->role)) + " page");
    } else {
        out.title.status = FieldStatus::Ambiguous;
        Candidate<TitleValue> c{chosen->value, score(best), chosen->evidence, {}};
        c.reasons.push_back("Best candidate lacks independent agreement");
        out.title.alternatives.insert(out.title.alternatives.begin(), std::move(c));
        out.title.reasons.push_back(rivals ? "Competing title candidates"
                                           : "Title seen on one page without clear prominence");
    }
    if (hints.title) {
        const bool match = out.title.value && key(*hints.title) == key(out.title.value->title);
        out.title.reasons.push_back(match ? "PDF metadata title agrees (hint only)"
                                          : "PDF metadata title differs (hint ignored)");
    }
}

// ---------------------------------------------------------------- contributors

struct NameSource {
    const Page* page;
    std::vector<Contributor> names;
    bool explicit_phrase = false;  // "by ...", "edited by ...".
    std::vector<Evidence> evidence;
    bool layout_block = false;     // Author block in title-page layout.
    // Keys of the names given by a responsibility statement on a cover or
    // title page; other name lines on that page are not confirmed by it.
    std::set<std::string> stated;
};

// Letters and digits only, lower case: "W I LEY<soft hyphen>VCH" -> "wileyvch".
std::string compact(const std::string& text) {
    std::string out;
    for (const char c : key(text))
        if (c != ' ') out += c;
    return out;
}

// A publisher's logo set as text: a short line whose letters, without
// letter-spacing, soft hyphens and punctuation, start an imprint line on
// the same page ("W I LEY-VCH" above "WILEY-VCH Verlag GmbH & Co. KGaA").
bool imprint_logo(const Page& page, std::size_t index) {
    std::string mark;
    for (const char c : compact(page.lines[index].text))
        if (static_cast<unsigned char>(c) < 0x80) mark += c;  // Drops U+00AD bytes.
    if (mark.size() < 3 || page.lines[index].text.size() > 40) return false;
    for (std::size_t i = 0; i < page.lines.size(); ++i)
        if (i != index && publisher_or_series_line(page.lines[i].lower) &&
            compact(page.lines[i].text).rfind(mark, 0) == 0)
            return true;
    return false;
}

// "Cornell University, Ithaca, New York": an affiliation under the names.
bool affiliation_line(const std::string& l) {
    for (const char* w : {"university", "institute", "college", "laboratory",
                          "laboratories", "department", "school of", "observatory",
                          "academy", "centre", "center for"})
        if (contains(l, w)) return true;
    return false;
}

// Parses a statement after "by"/"edited by"/"translated by" or a bare list.
std::vector<Contributor> parse_contributors(const std::string& text, ContributorRole role) {
    std::vector<Contributor> out;
    for (const auto& item : split_names(text)) {
        if (!name_like(item)) break;  // Stop at the first non-name.
        out.push_back({item, organization_or(role, item)});
    }
    return out;
}

std::optional<std::pair<ContributorRole, std::string>> role_phrase(const std::string& text) {
    const std::string l = lower(text);
    struct Phrase { const char* p; ContributorRole r; };
    for (const Phrase& ph : {Phrase{"edited by ", ContributorRole::Editor},
                             Phrase{"editor: ", ContributorRole::Editor},
                             Phrase{"editors: ", ContributorRole::Editor},
                             Phrase{"translated by ", ContributorRole::Translator},
                             Phrase{"by ", ContributorRole::Author}}) {
        const auto pos = l.find(ph.p);
        if (pos == 0 || (pos != std::string::npos && pos + 3 < l.size() &&
                         ph.r != ContributorRole::Author))
            return std::make_pair(ph.r, text.substr(pos + std::string(ph.p).size()));
    }
    return std::nullopt;
}

// The page's title block reads as the resolved title (not, for example,
// a quotation set large on a page of endorsements).
bool shows_title(const Page& page, const Field<TitleValue>& title) {
    if (page.title_block.empty() || !title.value) return false;
    std::string text;
    for (const auto i : page.title_block) text += " " + page.lines[i].text;
    const std::string wanted = key(title.value->title);
    return !wanted.empty() && key(text).rfind(wanted, 0) == 0;
}

void resolve_contributors(const std::vector<Page>& pages, const DocumentHints& hints,
                          MetadataResult& out) {
    std::vector<NameSource> sources;
    // (a) Cover / title pages: role phrases and name lines outside the title.
    for (const auto& page : pages) {
        if (page.role != PageRole::Cover && page.role != PageRole::TitlePage) continue;
        NameSource src{&page, {}, false, {}};
        const auto in = [](const std::vector<std::size_t>& list, std::size_t i) {
            return std::find(list.begin(), list.end(), i) != list.end();
        };
        // Kind of each line: title (block or subtitle), name, imprint
        // (publisher, logo), affiliation or other.
        enum class Kind { Title, Name, Imprint, Affiliation, Other };
        std::vector<Kind> kinds(page.lines.size(), Kind::Other);
        for (std::size_t i = 0; i < page.lines.size(); ++i) {
            const auto& line = page.lines[i];
            if (in(page.title_block, i) || in(page.subtitle_lines, i)) {
                kinds[i] = Kind::Title;
                continue;
            }
            if (auto phrase = role_phrase(line.text)) {
                auto names = parse_contributors(phrase->second, phrase->first);
                if (!names.empty()) {
                    src.explicit_phrase = true;
                    src.evidence.push_back(evidence(line, "Responsibility statement"));
                    for (const auto& n : names) src.stated.insert(key(n.name));
                    src.names.insert(src.names.end(), names.begin(), names.end());
                    kinds[i] = Kind::Name;
                    continue;
                }
            }
            if (publisher_or_series_line(line.lower) || imprint_logo(page, i)) {
                kinds[i] = Kind::Imprint;
                continue;
            }
            if (affiliation_line(line.lower)) {
                kinds[i] = Kind::Affiliation;
                continue;
            }
            auto names = parse_contributors(line.text, ContributorRole::Author);
            if (!names.empty()) {
                src.evidence.push_back(evidence(line, "Name line on " +
                                                          std::string(role_name(page.role)) +
                                                          " page"));
                src.names.insert(src.names.end(), names.begin(), names.end());
                kinds[i] = Kind::Name;
            }
        }
        // Title-page layout: the title (and subtitle), then one block of
        // name lines, then an affiliation, the imprint or nothing. Most
        // title pages list their authors so, without "by". (A cover alone
        // is not enough: covers also print series editors and endorsers.)
        if (!src.explicit_phrase && page.role == PageRole::TitlePage &&
            shows_title(page, out.title)) {
            std::size_t first = page.lines.size(), last = 0, blocks = 0;
            // Name lines form one block when they follow each other closely
            // in a similar size (a city line far below the author is not one).
            const auto joins = [&](std::size_t i) {
                const auto& a = page.lines[i - 1];
                const auto& b = page.lines[i];
                const double h = std::max(a.height, b.height);
                return kinds[i - 1] == Kind::Name && b.top - a.bottom <= 1.5 * h &&
                       std::min(a.height, b.height) >= 0.8 * h;
            };
            for (std::size_t i = 0; i < kinds.size(); ++i)
                if (kinds[i] == Kind::Name) {
                    if (i == 0 || !joins(i)) ++blocks;
                    first = std::min(first, i);
                    last = i;
                }
            const bool after_title = first < kinds.size() && first > 0 &&
                                     kinds[first - 1] == Kind::Title;
            const bool closed = last + 1 == kinds.size() ||
                                kinds[last + 1] == Kind::Affiliation ||
                                kinds[last + 1] == Kind::Imprint;
            const std::size_t count = first < kinds.size() ? last - first + 1 : 0;
            if (blocks == 1 && after_title && closed && count <= 4) {
                src.layout_block = true;
                for (auto& ev : src.evidence)
                    ev.reason = "Name in the author block below the title on the title page";
            }
        }
        if (!src.names.empty()) sources.push_back(std::move(src));
    }
    // (b) Statements naming this title followed by names, on any page
    //     ("TITLE, Georg Hager and Gerhard Wellein" in a series list).
    if (out.title.value) {
        const std::string title = lower(squeeze(out.title.value->title));
        for (const auto& page : pages) {
            std::string flat, flat_lower;
            std::vector<std::pair<std::size_t, const Line*>> starts;
            for (const auto& line : page.lines) {
                if (!flat.empty()) flat += ' ';
                starts.push_back({flat.size(), &line});
                flat += line.text;
            }
            flat_lower = lower(flat);
            const auto pos = flat_lower.find(title);
            if (pos == std::string::npos) continue;
            // Names follow on the rest of the line where the title ends.
            std::size_t line_end = flat.size();
            for (const auto& s : starts)
                if (s.first > pos + title.size()) { line_end = s.first; break; }
            std::string rest = flat.substr(pos + title.size(),
                                           line_end - (pos + title.size()));
            rest = trim(rest);
            if (rest.empty() || (rest[0] != ',' && lower(rest).rfind("by ", 0) != 0)) continue;
            rest = rest[0] == ',' ? rest.substr(1) : rest.substr(3);
            auto names = parse_contributors(rest.substr(0, std::min<std::size_t>(rest.size(), 160)),
                                            ContributorRole::Author);
            if (names.empty()) continue;
            const Line* where = starts.front().second;
            for (const auto& s : starts)
                if (s.first <= pos) where = s.second;
            NameSource src{&page, names, true, {evidence(*where, "Names given after this title")}};
            sources.push_back(std::move(src));
        }
    }
    if (sources.empty()) {
        out.contributors.reasons.push_back("No contributor names near the title");
        return;
    }
    // Support per distinct name across distinct pages.
    std::map<std::string, std::set<PageIndex>> support;
    std::map<std::string, bool> explicit_name;
    for (const auto& src : sources)
        for (const auto& n : src.names) {
            support[key(n.name)].insert(src.page->acquisition->page_index);
            const bool stated = src.stated.empty() ? src.explicit_phrase
                                                   : src.stated.count(key(n.name)) != 0;
            if ((stated || src.layout_block) &&
                (src.page->role == PageRole::TitlePage || src.page->role == PageRole::Cover))
                explicit_name[key(n.name)] = true;
        }
    std::vector<Contributor> resolved;
    std::set<std::string> taken;
    const NameSource* primary = &sources.front();
    for (const auto& src : sources)
        if (role_weight(src.page->role) > role_weight(primary->page->role)) primary = &src;
    const auto accept = [&](const Contributor& c) {
        const std::string k = key(c.name);
        if (taken.count(k)) return;
        if (support[k].size() >= 2 || explicit_name[k]) {
            resolved.push_back(c);
            taken.insert(k);
        }
    };
    for (const auto& c : primary->names) accept(c);
    for (const auto& src : sources)
        for (const auto& c : src.names) accept(c);
    for (const auto& src : sources) {
        Candidate<std::vector<Contributor>> cand{src.names, role_weight(src.page->role),
                                                 src.evidence, {}};
        cand.reasons.push_back("Names on page " +
                               std::to_string(src.page->acquisition->page_index));
        out.contributors.alternatives.push_back(std::move(cand));
    }
    if (!resolved.empty()) {
        out.contributors.status = FieldStatus::Resolved;
        out.contributors.value = resolved;
        for (const auto& src : sources)
            for (const auto& ev : src.evidence) out.contributors.evidence.push_back(ev);
        out.contributors.reasons.push_back(
            "Names confirmed on two or more pages, by an explicit responsibility "
            "statement on a title/cover page, or by the author block of a title page");
        std::size_t dropped = 0;
        for (const auto& s : support)
            if (!taken.count(s.first)) ++dropped;
        if (dropped)
            out.contributors.reasons.push_back(std::to_string(dropped) +
                                               " unconfirmed name(s) left as alternatives");
    } else {
        out.contributors.status = FieldStatus::Ambiguous;
        out.contributors.reasons.push_back("Names seen only once without a responsibility phrase");
    }
    if (hints.author && out.contributors.value) {
        bool match = false;
        for (const auto& c : *out.contributors.value)
            if (key(*hints.author).find(key(c.name)) != std::string::npos) match = true;
        out.contributors.reasons.push_back(match ? "PDF metadata author agrees (hint only)"
                                                 : "PDF metadata author differs (hint ignored)");
    }
}

// ---------------------------------------------------------------- edition

std::optional<std::uint32_t> ordinal_word(const std::string& w) {
    static const char* const words[] = {"first", "second", "third", "fourth", "fifth",
                                        "sixth", "seventh", "eighth", "ninth", "tenth",
                                        "eleventh", "twelfth"};
    for (std::uint32_t i = 0; i < 12; ++i)
        if (w == words[i]) return i + 1;
    std::size_t digits = 0;
    while (digits < w.size() && ascii_digit(static_cast<unsigned char>(w[digits]))) ++digits;
    if (digits > 0 && digits <= 3) {
        const std::string suffix = w.substr(digits);
        if (suffix == "st" || suffix == "nd" || suffix == "rd" || suffix == "th")
            return static_cast<std::uint32_t>(std::stoul(w.substr(0, digits)));
    }
    return std::nullopt;
}

struct EditionHit {
    EditionValue value;
    Evidence evidence;
    bool history = false;  // "... edition published YEAR" statements.
};

std::vector<EditionHit> edition_hits(const Page& page) {
    static const std::regex pattern(
        R"(\b((?:[a-z]+|\d{1,3}(?:st|nd|rd|th))\s+)?((?:[a-z]+\s+)?)edition\b)");
    std::vector<EditionHit> hits;
    for (const auto& line : page.lines) {
        std::smatch m;
        std::string l = line.lower;
        if (!std::regex_search(l, m, pattern)) continue;
        const std::string first = trim(m[1].str()), second = trim(m[2].str());
        std::optional<std::uint32_t> ordinal = ordinal_word(first);
        std::string qualifier = second;
        if (!ordinal && !first.empty()) {
            qualifier = first;  // "Revised edition", "International edition".
            if (!second.empty()) ordinal = ordinal_word(second);
        }
        static const std::set<std::string> qualifiers = {
            "revised", "updated", "expanded", "international", "special", "anniversary",
            "student", "global", "annotated", "illustrated", "definitive", "new"};
        if (!ordinal && !qualifiers.count(qualifier)) continue;
        // Preserve the printed statement (original case).
        const auto begin = static_cast<std::size_t>(m.position(0));
        std::string statement = line.text.substr(begin, static_cast<std::size_t>(m.length(0)));
        if (!ordinal && !first.empty() && !qualifiers.count(first))
            continue;
        hits.push_back({{squeeze(statement), ordinal},
                        evidence(line, "Edition statement"),
                        contains(l, "published")});
    }
    return hits;
}

void resolve_edition(const std::vector<Page>& pages, MetadataResult& out) {
    std::vector<EditionHit> strong, history;
    for (const auto& page : pages) {
        if (page.role != PageRole::Cover && page.role != PageRole::TitlePage &&
            page.role != PageRole::Copyright)
            continue;
        for (auto& hit : edition_hits(page))
            (hit.history ? history : strong).push_back(std::move(hit));
    }
    const auto same = [](const EditionValue& a, const EditionValue& b) {
        return a.ordinal && b.ordinal ? *a.ordinal == *b.ordinal
                                      : key(a.statement) == key(b.statement);
    };
    for (const auto& h : strong) {
        Candidate<EditionValue> c{h.value, 1, {h.evidence}, {"Edition statement"}};
        out.edition.alternatives.push_back(std::move(c));
    }
    for (const auto& h : history) {
        Candidate<EditionValue> c{h.value, 0.5, {h.evidence}, {"Publication history line"}};
        out.edition.alternatives.push_back(std::move(c));
    }
    if (!strong.empty()) {
        const bool agree = std::all_of(strong.begin(), strong.end(), [&](const auto& h) {
            return same(h.value, strong.front().value);
        });
        if (!agree) {
            out.edition.status = FieldStatus::Ambiguous;
            out.edition.reasons.push_back("Conflicting edition statements");
            return;
        }
        out.edition.status = FieldStatus::Resolved;
        out.edition.value = strong.front().value;
        for (const auto& h : strong) out.edition.evidence.push_back(h.evidence);
        out.edition.reasons.push_back("Explicit edition statement on a cover, title or "
                                      "copyright page");
        out.edition.alternatives.clear();
        return;
    }
    if (!history.empty()) {
        // "Second edition published 2012 / First edition published 2005":
        // the highest numbered edition published is the current one.
        const EditionHit* top = nullptr;
        for (const auto& h : history)
            if (h.value.ordinal && (!top || *h.value.ordinal > *top->value.ordinal)) top = &h;
        if (top) {
            out.edition.status = FieldStatus::Resolved;
            out.edition.value = top->value;
            out.edition.evidence.push_back(top->evidence);
            out.edition.reasons.push_back("Latest edition in the publication history");
            return;
        }
    }
    out.edition.reasons.push_back(
        "No edition statement on the cover, title or copyright pages searched "
        "(many first editions state none)");
}

// ---------------------------------------------------------------- years

struct YearHit {
    YearValue value;
    std::optional<std::uint32_t> edition;  // Edition the statement names.
    bool this_edition = false;
    Evidence evidence;
};

bool isbn_line(const Line& line);  // Defined with the ISBN rules below.

std::vector<int> years_in(const std::string& text, const MetadataOptions& options) {
    static const std::regex pattern(R"((^|[^0-9])(\d{4})(?![0-9]))");
    std::vector<int> out;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        const int y = std::stoi((*it)[2].str());
        if (y >= options.min_year && y <= options.max_year) out.push_back(y);
    }
    return out;
}

// True when each copyright statement names one year and statements with
// different years name different holders ("by John Wiley & Sons" and
// "WILEY-VCH Verlag"). One holder with several years ("© 2005, 2012
// Pearson") is an edition history and stays ambiguous.
bool separate_holders(const std::vector<YearHit>& hits, const MetadataOptions& options) {
    // The holder is the statement without the sign, its year and the words
    // "copyright", "by" and "all rights reserved".
    static const std::set<std::string> skip = {
        "copyright", "\xC2\xA9", "c", "o", "by", "all", "rights", "reserved"};
    std::map<int, std::set<std::string>> holders;  // Year -> holders.
    for (const auto& h : hits) {
        if (years_in(lower(h.value.statement), options).size() != 1) return false;
        const std::string k = key(h.value.statement);
        std::string holder;
        for (std::size_t start = 0; start < k.size();) {
            const auto end = std::min(k.find(' ', start), k.size());
            const std::string w = k.substr(start, end - start);
            start = end + 1;
            if (w.empty() || ascii_digit(static_cast<unsigned char>(w[0])) || skip.count(w))
                continue;
            holder += (holder.empty() ? "" : " ") + w;
        }
        if (holder.empty()) return false;
        holders[h.value.year].insert(holder);
    }
    for (auto a = holders.begin(); a != holders.end(); ++a)
        for (auto b = std::next(a); b != holders.end(); ++b)
            for (const auto& name : a->second)
                if (b->second.count(name)) return false;
    return true;
}

void resolve_years(const std::vector<Page>& pages, const MetadataOptions& options,
                   MetadataResult& out) {
    std::vector<YearHit> publication, copyright, printing;
    std::vector<YearHit> running_publication, running_copyright, running_printing;
    for (const auto& page : pages) {
        const bool title_page = page.role == PageRole::TitlePage || page.role == PageRole::Cover;
        const bool running = page.role != PageRole::Copyright && !title_page &&
                             page.running_copyright.has_value();
        if (page.role != PageRole::Copyright && !title_page && !running) continue;
        auto& publication_hits = running ? running_publication : publication;
        auto& copyright_hits = running ? running_copyright : copyright;
        auto& printing_hits = running ? running_printing : printing;
        for (std::size_t index = 0; index < page.lines.size(); ++index) {
            if (running && index != *page.running_copyright) continue;
            const auto& line = page.lines[index];
            const std::string& l = line.lower;
            if (isbn_line(line)) continue;  // ISBN digits are not years.
            if (cites_law(l)) continue;
            const auto ys = years_in(l, options);
            if (ys.empty()) continue;
            std::optional<std::uint32_t> edition;
            static const std::regex ed(R"(\b([a-z]+|\d{1,3}(?:st|nd|rd|th))\s+edition\b)");
            std::smatch m;
            if (std::regex_search(l, m, ed)) edition = ordinal_word(m[1].str());
            const bool this_edition = contains(l, "this edition");
            const auto push = [&](std::vector<YearHit>& list, YearKind kind, const char* why) {
                for (const int y : ys)
                    list.push_back({{y, kind, line.text}, edition, this_edition,
                                    evidence(line, why)});
            };
            if (contains(l, "reprint") || contains(l, "printing") ||
                (contains(l, "printed") && !contains(l, "published")))
                push(printing_hits, YearKind::Printing, "Printing statement");
            else if (contains(l, "published") || contains(l, "publication"))
                push(publication_hits, YearKind::Publication, "Publication statement");
            else if (contains(l, "\xC2\xA9") || contains(l, "copyright") || contains(l, "(c)"))
                push(copyright_hits, YearKind::Copyright, "Copyright statement");
            else if (symbol_copyright(l))
                push(copyright_hits, YearKind::Copyright,
                     "Copyright statement (the sign read as 0, O or C)");
            else if (title_page && key(line.text).size() == 4)
                push(publication_hits, YearKind::Publication, "Imprint year on the title page");
        }
    }
    // A running head or foot counts only for a kind of year that no
    // copyright or title page states.
    const auto fallback = [](std::vector<YearHit>& primary, std::vector<YearHit>& running) {
        if (!primary.empty()) return;
        for (auto& h : running) {
            h.evidence.reason += ", in a running head or foot (no copyright page states one)";
            primary.push_back(std::move(h));
        }
    };
    fallback(publication, running_publication);
    fallback(copyright, running_copyright);
    fallback(printing, running_printing);
    // Copyright year.
    std::set<int> cy;
    for (const auto& h : copyright) cy.insert(h.value.year);
    for (const auto& h : copyright)
        out.copyright_year.alternatives.push_back(
            {h.value, 1, {h.evidence}, {"Copyright statement"}});
    if (cy.size() == 1) {
        out.copyright_year.status = FieldStatus::Resolved;
        out.copyright_year.value = copyright.front().value;
        for (const auto& h : copyright) out.copyright_year.evidence.push_back(h.evidence);
        out.copyright_year.reasons.push_back("Single copyright year");
        out.copyright_year.alternatives.clear();
    } else if (cy.size() > 1 && separate_holders(copyright, options)) {
        // "© 1983 by A" and "© 2004 B": the original copyright and a reprint
        // or licensed edition. The earliest is the work's copyright.
        const int earliest = *cy.begin();
        out.copyright_year.status = FieldStatus::Resolved;
        out.copyright_year.alternatives.clear();
        for (const auto& h : copyright) {
            if (h.value.year == earliest) {
                if (!out.copyright_year.value) out.copyright_year.value = h.value;
                out.copyright_year.evidence.push_back(h.evidence);
            } else {
                out.copyright_year.alternatives.push_back(
                    {h.value, 0.5, {h.evidence},
                     {"Later copyright by another holder (reprint, licensed or new edition)"}});
            }
        }
        out.copyright_year.reasons.push_back(
            "Earliest of " + std::to_string(cy.size()) +
            " copyright years stated by different holders: the original copyright");
    } else if (cy.size() > 1) {
        out.copyright_year.status = FieldStatus::Ambiguous;
        out.copyright_year.reasons.push_back(
            "Several copyright years; the latest is not assumed to be current");
    } else {
        out.copyright_year.reasons.push_back("No copyright statement in the searched pages");
    }
    // Publication year, tied to the identified edition when possible.
    std::vector<const YearHit*> relevant;
    const std::optional<std::uint32_t> edition =
        out.edition.value ? out.edition.value->ordinal : std::nullopt;
    for (const auto& h : publication)
        if ((edition && h.edition && *h.edition == *edition) || h.this_edition)
            relevant.push_back(&h);
    std::string basis = relevant.empty() ? "" : "Publication statement for the identified edition";
    if (relevant.empty()) {
        // Statements naming another edition ("First published 2005" with a
        // resolved second edition) do not describe this edition.
        for (const auto& h : publication) {
            const bool first_published = contains(lower(h.value.statement), "first published");
            const bool other_edition = edition && ((h.edition && *h.edition != *edition) ||
                                                   (first_published && *edition > 1));
            if (!other_edition) relevant.push_back(&h);
        }
        basis = "Publication statement";
    }
    std::set<int> py;
    for (const auto* h : relevant) py.insert(h->value.year);
    for (const auto& h : publication)
        out.publication_year.alternatives.push_back(
            {h.value, 1, {h.evidence}, {"Publication statement"}});
    for (const auto& h : printing)
        out.publication_year.alternatives.push_back(
            {h.value, 0.2, {h.evidence}, {"Printing statement (not a publication year)"}});
    if (py.size() == 1) {
        out.publication_year.status = FieldStatus::Resolved;
        out.publication_year.value = relevant.front()->value;
        for (const auto* h : relevant) out.publication_year.evidence.push_back(h->evidence);
        out.publication_year.reasons.push_back(basis);
        out.publication_year.alternatives.erase(
            std::remove_if(out.publication_year.alternatives.begin(),
                           out.publication_year.alternatives.end(),
                           [&](const auto& c) { return c.value.year == *py.begin() &&
                                                       c.value.kind == YearKind::Publication; }),
            out.publication_year.alternatives.end());
    } else if (py.size() > 1) {
        out.publication_year.status = FieldStatus::Ambiguous;
        out.publication_year.reasons.push_back(
            "Several publication years not tied to one edition; the largest is not assumed");
    } else {
        std::string why = "No publication statement in the searched pages";
        if (out.copyright_year.value)
            why += "; only a copyright year (" + std::to_string(out.copyright_year.value->year) +
                   ") was found, which is reported separately";
        out.publication_year.reasons.push_back(why);
    }
}

// ---------------------------------------------------------------- ISBNs

bool ascii_alnum(unsigned char c) { return ascii_upper(c) || ascii_lower(c) || ascii_digit(c); }

// Copy for number scanning: Unicode hyphens/dashes (U+2010..U+2015) and the
// minus sign (U+2212) become '-', a no-break space becomes ' '.
std::string ascii_separators(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC2 && i + 1 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            out += ' ';
            ++i;
            continue;
        }
        if (c == 0xE2 && i + 2 < text.size()) {
            const auto b1 = static_cast<unsigned char>(text[i + 1]);
            const auto b2 = static_cast<unsigned char>(text[i + 2]);
            if ((b1 == 0x80 && b2 >= 0x90 && b2 <= 0x95) || (b1 == 0x88 && b2 == 0x92)) {
                out += '-';
                i += 2;
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

bool isbn_keyword(const std::string& l) {
    return contains(l, "isbn") || contains(l, "international standard book number");
}

bool valid_isbn13(const std::string& d) {
    if (d.size() != 13 || (d.rfind("978", 0) != 0 && d.rfind("979", 0) != 0)) return false;
    int sum = 0;
    for (std::size_t i = 0; i < 13; ++i) sum += (d[i] - '0') * (i % 2 ? 3 : 1);
    return sum % 10 == 0;
}

bool valid_isbn10(const std::string& d) {
    if (d.size() != 10) return false;
    int sum = 0;
    for (std::size_t i = 0; i < 10; ++i)
        sum += (d[i] == 'X' ? 10 : d[i] - '0') * static_cast<int>(10 - i);
    return sum % 11 == 0;
}

// An ISBN-10 is its ISBN-13 without the "978" prefix and with another check digit.
std::string isbn10_to_13(const std::string& d) {
    std::string out = "978" + d.substr(0, 9);
    int sum = 0;
    for (std::size_t i = 0; i < 12; ++i) sum += (out[i] - '0') * (i % 2 ? 3 : 1);
    out += static_cast<char>('0' + (10 - sum % 10) % 10);
    return out;
}

// Reads `count` ISBN characters from `pos`, with at most two '-' or ' '
// between neighbours. Returns the characters and the end offset.
std::optional<std::pair<std::string, std::size_t>> read_isbn(const std::string& s,
                                                             std::size_t pos,
                                                             std::size_t count) {
    std::string digits;
    std::size_t end = pos;
    while (digits.size() < count) {
        std::size_t j = end;
        if (!digits.empty())
            while (j < s.size() && j < end + 2 && (s[j] == '-' || s[j] == ' ')) ++j;
        if (j >= s.size()) return std::nullopt;
        const auto c = static_cast<unsigned char>(s[j]);
        const bool check_x = count == 10 && digits.size() == 9 && (c == 'X' || c == 'x');
        if (!ascii_digit(c) && !check_x) return std::nullopt;
        digits += check_x ? 'X' : static_cast<char>(c);
        end = j + 1;
    }
    // Not the start of a longer number or word.
    if (end < s.size()) {
        const auto next = static_cast<unsigned char>(s[end]);
        if (ascii_digit(next) || (digits.back() == 'X' && ascii_alnum(next))) return std::nullopt;
    }
    return std::make_pair(digits, end);
}

struct IsbnHit {
    std::string isbn13, printed;
    IsbnForm form;
    std::size_t begin, end;  // Offsets in the scanned text.
};

// ISBNs with a valid check digit in `s`. `rejected` receives the first
// ISBN-shaped number whose check fails.
std::vector<IsbnHit> isbn_hits(const std::string& s, std::string* rejected) {
    std::vector<IsbnHit> hits;
    std::size_t pos = 0;
    while (pos < s.size()) {
        if (!ascii_digit(static_cast<unsigned char>(s[pos])) ||
            (pos > 0 && ascii_digit(static_cast<unsigned char>(s[pos - 1])))) {
            ++pos;
            continue;
        }
        bool found = false;
        std::string failed;    // ISBN-shaped number here whose check fails.
        std::size_t next = pos + 1;
        for (const std::size_t count : {std::size_t{13}, std::size_t{10}}) {
            const auto m = read_isbn(s, pos, count);
            if (!m) continue;
            const std::string printed = s.substr(pos, m->second - pos);
            if (count == 13 ? valid_isbn13(m->first) : valid_isbn10(m->first)) {
                hits.push_back({count == 13 ? m->first : isbn10_to_13(m->first), printed,
                                count == 13 ? IsbnForm::Isbn13 : IsbnForm::Isbn10, pos,
                                m->second});
                pos = m->second;
                found = true;
                break;
            }
            if (failed.empty()) failed = printed;
            // A failed ISBN-13 is skipped whole, so that its last ten digits
            // are not read as an ISBN-10.
            if (count == 13 && (m->first.rfind("978", 0) == 0 || m->first.rfind("979", 0) == 0)) {
                next = m->second;
                break;
            }
        }
        if (found) continue;
        if (rejected && rejected->empty() && !failed.empty()) *rejected = failed;
        pos = next;
    }
    return hits;
}

// A line the ISBN list reads its numbers from: years are not read from it.
bool isbn_line(const Line& line) {
    if (isbn_keyword(line.lower)) return true;
    for (const auto& hit : isbn_hits(ascii_separators(line.text), nullptr))
        if (hit.form == IsbnForm::Isbn13) return true;
    return false;
}

IsbnFormat isbn_format(const std::string& text) {
    std::vector<std::string> words;
    const std::string k = key(text);
    std::size_t start = 0;
    while (start < k.size()) {
        const auto end = k.find(' ', start);
        words.push_back(k.substr(start, end == std::string::npos ? std::string::npos
                                                                 : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    const auto has = [&](std::initializer_list<const char*> list) {
        for (const char* w : list)
            if (std::find(words.begin(), words.end(), w) != words.end()) return true;
        return false;
    };
    // "e book", "hard cover": `first` directly followed by one of `second`.
    const auto pair = [&](const char* first, std::initializer_list<const char*> second) {
        for (std::size_t i = 0; i + 1 < words.size(); ++i)
            if (words[i] == first)
                for (const char* w : second)
                    if (words[i + 1] == w) return true;
        return false;
    };
    if (has({"ebook", "ebk", "eisbn", "epub", "epdf", "pdf", "electronic", "online", "digital",
             "kindle", "mobi"}) ||
        pair("e", {"book", "isbn", "pub"}))
        return IsbnFormat::Electronic;
    if (has({"hardback", "hardcover", "hardbound", "hbk", "hb", "hc", "cloth", "casebound"}) ||
        pair("hard", {"cover", "back"}))
        return IsbnFormat::Hardcover;
    if (has({"paperback", "softcover", "softback", "pbk", "pb"}) ||
        pair("soft", {"cover", "back"}))
        return IsbnFormat::Paperback;
    if (has({"print"})) return IsbnFormat::Print;
    return IsbnFormat::Unknown;
}

std::string trim_label(const std::string& text) {
    std::size_t first = 0, end = text.size();
    const auto edge = [](char c) { return c == ' ' || c == ',' || c == ';' || c == ':'; };
    while (first < end && edge(text[first])) ++first;
    while (end > first && edge(text[end - 1])) --end;
    return text.substr(first, end - first);
}

// Lists every valid ISBN on lines that name an ISBN, and on the lines
// directly below them (publishers print one format per line).
void collect_isbns(const std::vector<Page>& pages, MetadataResult& out) {
    std::map<std::string, std::size_t> index;  // isbn13 -> position in out.isbns.
    for (const auto& page : pages) {
        bool previous = false;  // The line above was part of an ISBN statement.
        for (const auto& line : page.lines) {
            const bool keyword = isbn_keyword(line.lower);
            if (!keyword && !previous) continue;
            const std::string s = ascii_separators(line.text);
            std::string rejected;
            auto hits = isbn_hits(s, keyword ? &rejected : nullptr);
            // Without the keyword only an ISBN-13 is accepted: any ten-digit
            // number (a printer's key, a phone number) can pass the ISBN-10 check.
            if (!keyword)
                hits.erase(std::remove_if(hits.begin(), hits.end(),
                                          [](const IsbnHit& h) { return h.form == IsbnForm::Isbn10; }),
                           hits.end());
            previous = keyword || !hits.empty();
            if (!rejected.empty())
                out.diagnostics.push_back(
                    "Page " + std::to_string(page.acquisition->page_index) + ": \"" + rejected +
                    "\" on an ISBN line fails the ISBN check digit and is not listed");
            std::size_t consumed = 0;  // End of the previous ISBN and its label.
            for (std::size_t h = 0; h < hits.size(); ++h) {
                const IsbnHit& hit = hits[h];
                // A label ends before the next ISBN on the line.
                const std::size_t limit = h + 1 < hits.size() ? hits[h + 1].begin : s.size();
                IsbnFormat format = IsbnFormat::Unknown;
                std::optional<std::string> label;
                // Label after the number: "(hardback : alk. paper)" or a bare "pbk".
                std::size_t after = hit.end;
                while (after < s.size() && s[after] == ' ') ++after;
                std::size_t next_consumed = hit.end;
                if (after < s.size() && s[after] == '(') {
                    const auto close = s.find(')', after);
                    if (close != std::string::npos && close < limit) {
                        const std::string inner = trim_label(s.substr(after + 1, close - after - 1));
                        if (!inner.empty()) label = inner;
                        format = isbn_format(inner);
                        next_consumed = close + 1;
                    }
                } else {
                    std::size_t word_end = after;
                    while (word_end < s.size() &&
                           (ascii_alnum(static_cast<unsigned char>(s[word_end])) ||
                            s[word_end] == '-'))
                        ++word_end;
                    const std::string word = s.substr(after, word_end - after);
                    // "Hardback ISBN 978-... Paperback ISBN 978-...": this ISBN has
                    // its qualifier in front, so the word qualifies the next ISBN.
                    const bool next_prefix =
                        isbn_keyword(lower(s.substr(after, limit - after))) &&
                        isbn_format(s.substr(consumed, hit.begin - consumed)) != IsbnFormat::Unknown;
                    if (isbn_format(word) != IsbnFormat::Unknown && !next_prefix) {
                        label = word;
                        format = isbn_format(word);
                        next_consumed = word_end;
                    }
                }
                // Qualifier before the number: "e-ISBN", "Print ISBN", "ISBN (eBook)".
                std::string before = s.substr(consumed, hit.begin - consumed);
                const std::string before_lower = lower(before);
                std::size_t kpos = before_lower.rfind("isbn");
                if (kpos == std::string::npos)
                    kpos = before_lower.rfind("international standard book number");
                if (kpos != std::string::npos) {
                    // Keep up to two words directly before the keyword.
                    std::size_t start = kpos;
                    for (int words = 0; words < 2 && start > 0; ++words) {
                        std::size_t j = start;
                        while (j > 0 && !ascii_alnum(static_cast<unsigned char>(before[j - 1])) &&
                               before[j - 1] != ')' && before[j - 1] != ',' &&
                               before[j - 1] != ';' && before[j - 1] != '.')
                            --j;
                        if (j == 0 || !ascii_alnum(static_cast<unsigned char>(before[j - 1]))) break;
                        while (j > 0 && ascii_alnum(static_cast<unsigned char>(before[j - 1]))) --j;
                        start = j;
                    }
                    before = before.substr(start);
                } else if (before.size() > 32) {
                    before.clear();  // Unrelated text, not a qualifier.
                }
                const IsbnFormat before_format = isbn_format(before);
                if (before_format != IsbnFormat::Unknown) {
                    if (format == IsbnFormat::Unknown) format = before_format;
                    if (!label) label = trim_label(before);
                }
                consumed = next_consumed;

                Evidence ev = evidence(line, keyword ? "ISBN statement"
                                                     : "Line directly below an ISBN statement");
                const auto known = index.find(hit.isbn13);
                if (known == index.end()) {
                    index[hit.isbn13] = out.isbns.size();
                    out.isbns.push_back(
                        {hit.isbn13, hit.printed, hit.form, format, label, {std::move(ev)}});
                } else {
                    IsbnValue& value = out.isbns[known->second];
                    const auto& last = value.evidence.back().source;
                    // One region can hold several lines, so the text is compared too.
                    if (last.page_index != ev.source.page_index ||
                        last.region_id != ev.source.region_id ||
                        value.evidence.back().text != ev.text)
                        value.evidence.push_back(std::move(ev));
                    if (value.format == IsbnFormat::Unknown) value.format = format;
                    if (!value.label) value.label = label;
                }
            }
        }
    }
}

}  // namespace

const char* status_name(FieldStatus status) {
    switch (status) {
        case FieldStatus::Resolved: return "resolved";
        case FieldStatus::Ambiguous: return "ambiguous";
        case FieldStatus::NotFoundInSearch: return "not_found_in_search";
    }
    return "unknown";
}
const char* role_name(PageRole role) {
    switch (role) {
        case PageRole::Cover: return "cover";
        case PageRole::TitlePage: return "title";
        case PageRole::Copyright: return "copyright";
        case PageRole::Contents: return "contents";
        case PageRole::Other: return "other";
        case PageRole::Unknown: return "unknown";
    }
    return "unknown";
}
const char* role_name(ContributorRole role) {
    switch (role) {
        case ContributorRole::Author: return "author";
        case ContributorRole::Editor: return "editor";
        case ContributorRole::Translator: return "translator";
        case ContributorRole::Organization: return "organization";
    }
    return "unknown";
}
const char* kind_name(YearKind kind) {
    switch (kind) {
        case YearKind::Publication: return "publication";
        case YearKind::Copyright: return "copyright";
        case YearKind::Printing: return "printing";
    }
    return "unknown";
}
const char* form_name(IsbnForm form) {
    switch (form) {
        case IsbnForm::Isbn10: return "isbn10";
        case IsbnForm::Isbn13: return "isbn13";
    }
    return "unknown";
}
const char* format_name(IsbnFormat format) {
    switch (format) {
        case IsbnFormat::Unknown: return "unknown";
        case IsbnFormat::Print: return "print";
        case IsbnFormat::Hardcover: return "hardcover";
        case IsbnFormat::Paperback: return "paperback";
        case IsbnFormat::Electronic: return "electronic";
    }
    return "unknown";
}

Result<MetadataResult> extract(const std::vector<text::PageAcquisition>& supplied,
                               const DocumentHints& hints, const MetadataOptions& options) {
    if (!(options.title_block_ratio > 0 && options.title_block_ratio <= 1) ||
        !(options.title_prominence >= 1) || options.min_year > options.max_year)
        return Error{ErrorCode::InvalidArgument, "Invalid metadata options"};
    std::set<PageIndex> seen;
    for (const auto& page : supplied) {
        if (!seen.insert(page.page_index).second)
            return Error{ErrorCode::InvalidArgument,
                         "Duplicate page index " + std::to_string(page.page_index)};
        if (page.selected && page.selected->page_index != page.page_index)
            return Error{ErrorCode::InvalidArgument, "Selected content index mismatch"};
    }
    std::vector<Page> pages;
    for (const auto& a : supplied) {
        Page p;
        p.acquisition = &a;
        if (a.selected) {
            p.content = &*a.selected;
            p.lines = build_lines(*a.selected);
        }
        pages.push_back(std::move(p));
    }
    std::sort(pages.begin(), pages.end(), [](const Page& a, const Page& b) {
        return a.acquisition->page_index < b.acquisition->page_index;
    });
    MetadataResult out;
    out.policy_id = kPolicyId;
    const PageIndex first = pages.empty() ? 0 : pages.front().acquisition->page_index;
    for (auto& page : pages) {
        classify(page, first, options);
        out.pages.push_back({page.acquisition->page_index, page.role, page.reasons});
        if (page.acquisition->outcome == text::Outcome::Degraded)
            out.diagnostics.push_back("Page " + std::to_string(page.acquisition->page_index) +
                                      " was read with reduced quality");
        if (page.role == PageRole::Unknown)
            out.diagnostics.push_back("Page " + std::to_string(page.acquisition->page_index) +
                                      " has no usable text");
    }
    resolve_title(pages, hints, options, out);
    resolve_contributors(pages, hints, out);
    resolve_edition(pages, out);
    resolve_years(pages, options, out);
    collect_isbns(pages, out);
    return out;
}

}  // namespace pdfbookmark::metadata
