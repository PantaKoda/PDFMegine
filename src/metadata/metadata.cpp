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

constexpr const char* kPolicyId = "s6-document-metadata-v1";

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

bool copyright_line(const std::string& l) {
    return contains(l, "\xC2\xA9") || contains(l, "copyright") || contains(l, "(c) ") ||
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
        "first", "second", "third", "new", "revised", "published", "copyright",
        "printed", "library", "all", "rights", "reserved", "group", "foreword",
        "preface", "dedicated", "cover", "design", "international", "global"};
    return stop.count(w) != 0;
}

// "Georg Hager", "Craig E Rasmussen", "A. B. Smith", "GEORG HAGER".
bool name_like(const std::string& raw) {
    const std::string text = squeeze(raw);
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
    std::size_t words = 0;
    const bool all_caps = std::all_of(text.begin(), text.end(), [](char c) {
        return !ascii_lower(static_cast<unsigned char>(c));
    });
    for (const auto& token : tokens) {
        if (name_stopword(lower(key(token)))) return false;
        const auto c0 = static_cast<unsigned char>(token[0]);
        if (!(ascii_upper(c0) || c0 >= 0xC0)) return false;
        // Initial: "A" or "A."
        if (token.size() == 1 || (token.size() == 2 && token[1] == '.')) continue;
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
    std::string s = " " + squeeze(text) + " ";
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
    std::size_t first = anchor, last = anchor;
    while (first > 0 && eligible(first - 1) &&
           lines[first].top - lines[first - 1].bottom <= 1.2 * lines[anchor].height)
        --first;
    while (last + 1 < lines.size() && eligible(last + 1) &&
           lines[last + 1].top - lines[last].bottom <= 1.2 * lines[anchor].height)
        ++last;
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
    std::size_t copyright = 0;
    for (const auto& line : page.lines) {
        if (line.top < page.content->geometry.height_points * 0.3 && contents_line(line.lower)) {
            page.role = PageRole::Contents;
            page.reasons.push_back("Contents heading");
            return;
        }
        if (copyright_line(line.lower)) ++copyright;
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

void resolve_title(std::vector<Page>& pages, const DocumentHints& hints,
                   const MetadataOptions& options, MetadataResult& out) {
    struct Found {
        const Page* page;
        TitleValue value;
        std::vector<Evidence> evidence;
    };
    std::map<std::string, std::vector<Found>> by_key;
    std::vector<std::string> order;
    for (const auto& page : pages) {
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
            // A smaller, non-boilerplate, non-name line right below the block.
            const std::size_t below = page.title_block.back() + 1;
            double h = 0;  // Largest title line.
            for (const auto i : page.title_block) h = std::max(h, page.lines[i].height);
            if (below < page.lines.size()) {
                const auto& l = page.lines[below];
                if (l.height >= 0.35 * h && l.height < options.title_block_ratio * h &&
                    l.top - page.lines[below - 1].bottom <= 2 * h &&
                    !publisher_or_series_line(l.lower) && !name_like(l.text) &&
                    key(l.text).size() > 3) {
                    value.subtitle = l.text;
                    ev.push_back(evidence(l, "Subtitle below the title"));
                }
            }
        }
        const std::string k = key(value.title);
        if (k.empty()) continue;
        if (!by_key.count(k)) order.push_back(k);
        by_key[k].push_back({&page, value, ev});
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
        // Keep a subtitle found on any agreeing page.
        if (!out.title.value->subtitle)
            for (const auto& f : best)
                if (f.value.subtitle) { out.title.value->subtitle = f.value.subtitle; break; }
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
};

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

void resolve_contributors(const std::vector<Page>& pages, const DocumentHints& hints,
                          MetadataResult& out) {
    std::vector<NameSource> sources;
    // (a) Cover / title pages: role phrases and name lines outside the title.
    for (const auto& page : pages) {
        if (page.role != PageRole::Cover && page.role != PageRole::TitlePage) continue;
        NameSource src{&page, {}, false, {}};
        for (std::size_t i = 0; i < page.lines.size(); ++i) {
            if (std::find(page.title_block.begin(), page.title_block.end(), i) !=
                page.title_block.end())
                continue;
            const auto& line = page.lines[i];
            if (auto phrase = role_phrase(line.text)) {
                auto names = parse_contributors(phrase->second, phrase->first);
                if (!names.empty()) {
                    src.explicit_phrase = true;
                    src.evidence.push_back(evidence(line, "Responsibility statement"));
                    src.names.insert(src.names.end(), names.begin(), names.end());
                    continue;
                }
            }
            if (publisher_or_series_line(line.lower)) continue;
            auto names = parse_contributors(line.text, ContributorRole::Author);
            if (!names.empty()) {
                src.evidence.push_back(evidence(line, "Name line on " +
                                                          std::string(role_name(page.role)) +
                                                          " page"));
                src.names.insert(src.names.end(), names.begin(), names.end());
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
            if (src.explicit_phrase &&
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
            "Names confirmed on two or more pages or by an explicit responsibility "
            "statement on a title/cover page");
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

std::vector<int> years_in(const std::string& text, const MetadataOptions& options) {
    static const std::regex pattern(R"((^|[^0-9])(\d{4})(?![0-9]))");
    std::vector<int> out;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        const int y = std::stoi((*it)[2].str());
        if (y >= options.min_year && y <= options.max_year) out.push_back(y);
    }
    return out;
}

void resolve_years(const std::vector<Page>& pages, const MetadataOptions& options,
                   MetadataResult& out) {
    std::vector<YearHit> publication, copyright, printing;
    for (const auto& page : pages) {
        const bool title_page = page.role == PageRole::TitlePage || page.role == PageRole::Cover;
        if (page.role != PageRole::Copyright && !title_page) continue;
        for (const auto& line : page.lines) {
            const std::string& l = line.lower;
            if (contains(l, "isbn")) continue;  // ISBN digits are not years.
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
                push(printing, YearKind::Printing, "Printing statement");
            else if (contains(l, "published") || contains(l, "publication"))
                push(publication, YearKind::Publication, "Publication statement");
            else if (contains(l, "\xC2\xA9") || contains(l, "copyright") || contains(l, "(c)"))
                push(copyright, YearKind::Copyright, "Copyright statement");
            else if (title_page && key(line.text).size() == 4)
                push(publication, YearKind::Publication, "Imprint year on the title page");
        }
    }
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
    return out;
}

}  // namespace pdfbookmark::metadata
