#include <pdfbookmark/detection/detection.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pdfbookmark::detection {
namespace {

constexpr const char* kPolicyId = "s2-toc-detection-v2";

struct Fragment {
    std::string text;
    Quad quad;
    std::uint32_t region_id = 0;
};

struct Row {
    double middle_y = 0;
    double right = 0;
    bool leader = false;
    std::vector<std::uint32_t> region_ids;
    std::size_t fragments = 1;  // Fragments forming this visual line.
};

struct Features {
    PageReview review;
    std::vector<text::SourceReference> evidence;
    std::vector<double> anchors;
    std::size_t aligned_rows = 0;
    std::size_t leader_rows = 0;
    bool heading = false;
    bool excluded_heading = false;
    bool degraded = false;
};

bool ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' ||
           c == '\n' || c == '\f' || c == '\v';
}

bool ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool ascii_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
bool ascii_alnum(unsigned char c) {
    return ascii_digit(c) || ascii_alpha(c);
}

std::string trim_ascii(const std::string& input) {
    std::size_t begin = 0, end = input.size();
    while (begin < end && ascii_space(static_cast<unsigned char>(input[begin])))
        ++begin;
    while (end > begin && ascii_space(static_cast<unsigned char>(input[end - 1])))
        --end;
    return input.substr(begin, end - begin);
}

std::string lower_ascii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool title_like(const std::string& title) {
    std::size_t letters = 0;
    for (const unsigned char c : title)
        if (ascii_alpha(c) || c >= 0xc0) ++letters;
    return letters >= 2;
}

bool all_digits(const std::string& token) {
    if (token.empty() || token.size() > 4) return false;
    for (const unsigned char c : token)
        if (!ascii_digit(c)) return false;
    return true;
}

bool reference_like(const std::string& token) {
    if (all_digits(token)) return true;
    const auto dash = token.find('-');
    if (dash != std::string::npos && dash > 0 && dash <= 3 &&
        token.find('-', dash + 1) == std::string::npos &&
        all_digits(token.substr(dash + 1))) {
        for (std::size_t i = 0; i < dash; ++i)
            if (!ascii_alpha(static_cast<unsigned char>(token[i]))) return false;
        return true;
    }
    if (token.empty() || token.size() > 8) return false;
    for (const unsigned char c : token) {
        const char upper = c >= 'a' && c <= 'z' ?
            static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
        if (upper != 'I' && upper != 'V' && upper != 'X' &&
            upper != 'L' && upper != 'C' && upper != 'D' && upper != 'M')
            return false;
    }
    return true;
}

bool inline_reference(const std::string& text, bool& leader) {
    const std::string value = trim_ascii(text);
    if (value.empty()) return false;
    std::size_t begin = value.size();
    while (begin > 0 &&
           (ascii_alnum(static_cast<unsigned char>(value[begin - 1])) ||
            value[begin - 1] == '-'))
        --begin;
    if (begin == 0 || begin == value.size()) return false;
    const std::string token = value.substr(begin);
    if (!reference_like(token)) return false;
    const char delimiter = value[begin - 1];
    if (!ascii_space(static_cast<unsigned char>(delimiter)) &&
        delimiter != '.') return false;
    const std::string prefix = trim_ascii(value.substr(0, begin));
    if (!title_like(prefix)) return false;
    std::size_t dots = 0;
    for (const char c : prefix) {
        if (c == '.') {
            ++dots;
            if (dots >= 3) leader = true;
        } else {
            dots = 0;
        }
    }
    return true;
}

bool usable_quad(const Quad& quad) {
    for (const auto& point : quad.points)
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    return quad.points[1].x > quad.points[0].x &&
           quad.points[2].y > quad.points[0].y;
}

std::vector<Fragment> fragments_from(const text::PageContent& content) {
    std::vector<Fragment> fragments;
    for (const auto& region : content.regions) {
        if (!region.quad || !usable_quad(*region.quad)) continue;
        const std::string& value = region.text;
        std::size_t cursor = 0;
        std::size_t pieces = 1;
        for (const char c : value) if (c == '\n') ++pieces;
        std::size_t piece = 0;
        while (cursor <= value.size()) {
            const auto next = value.find('\n', cursor);
            const auto end = next == std::string::npos ? value.size() : next;
            std::string line = trim_ascii(value.substr(cursor, end - cursor));
            if (!line.empty()) {
                Quad quad = *region.quad;
                if (pieces > 1) {
                    const double top = quad.points[0].y;
                    const double height = quad.points[2].y - top;
                    const double line_top = top + height * piece / pieces;
                    const double line_bottom = top + height * (piece + 1) / pieces;
                    quad.points[0].y = quad.points[1].y = line_top;
                    quad.points[2].y = quad.points[3].y = line_bottom;
                }
                fragments.push_back({std::move(line), quad, region.id});
            }
            if (next == std::string::npos) break;
            cursor = next + 1;
            ++piece;
        }
    }
    return fragments;
}

double middle_y(const Quad& quad) {
    return (quad.points[0].y + quad.points[2].y) / 2.0;
}

std::vector<Row> reference_rows(const std::vector<Fragment>& fragments,
                                double page_width) {
    std::vector<Row> rows;
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        const auto& fragment = fragments[i];
        bool leader = false;
        if (inline_reference(fragment.text, leader)) {
            rows.push_back({middle_y(fragment.quad), fragment.quad.points[1].x,
                            leader, {fragment.region_id}, 1});
            continue;
        }
        if (!reference_like(trim_ascii(fragment.text))) continue;
        const Fragment* title = nullptr;
        double best_right = -std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < fragments.size(); ++j) {
            if (i == j || !title_like(fragments[j].text)) continue;
            const auto& other = fragments[j];
            const double gap = fragment.quad.points[0].x - other.quad.points[1].x;
            const double heights = std::max(
                fragment.quad.points[2].y - fragment.quad.points[0].y,
                other.quad.points[2].y - other.quad.points[0].y);
            const double band = std::max(4.0, heights * 0.6);
            if (gap < 0 ||
                std::abs(middle_y(fragment.quad) - middle_y(other.quad)) > band)
                continue;
            if (gap > page_width * 0.35) {
                // Wide gap (right-aligned number after a short title or short
                // leaders): allowed up to 0.8 page width only when the title
                // is not itself a complete row and nothing else lies between
                // them on this row, so rows never pair across columns.
                bool unused = false;
                if (gap > page_width * 0.8 || inline_reference(other.text, unused))
                    continue;
                const double left = other.quad.points[1].x;
                const double right = fragment.quad.points[0].x;
                const bool blocked = std::any_of(
                    fragments.begin(), fragments.end(), [&](const Fragment& k) {
                        return &k != &fragment && &k != &other &&
                               std::abs(middle_y(k.quad) - middle_y(fragment.quad)) <=
                                   band &&
                               k.quad.points[1].x > left && k.quad.points[0].x < right;
                    });
                if (blocked) continue;
            }
            if (other.quad.points[1].x > best_right) {
                best_right = other.quad.points[1].x;
                title = &other;
            }
        }
        if (title)
            rows.push_back({middle_y(fragment.quad), fragment.quad.points[1].x,
                            false, {title->region_id, fragment.region_id}, 2});
    }
    // Multiple PDF regions may describe the same visual row. Do not count it twice.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.right != b.right) return a.right < b.right;
        return a.middle_y < b.middle_y;
    });
    std::vector<Row> unique;
    for (auto& row : rows) {
        if (!unique.empty() &&
            std::abs(unique.back().right - row.right) <= 3.0 &&
            std::abs(unique.back().middle_y - row.middle_y) <= 2.0) {
            unique.back().leader = unique.back().leader || row.leader;
            unique.back().fragments += row.fragments;  // Same visual line.
            unique.back().region_ids.insert(unique.back().region_ids.end(),
                                            row.region_ids.begin(),
                                            row.region_ids.end());
        } else {
            unique.push_back(std::move(row));
        }
    }
    return unique;
}

Features assess(const text::PageAcquisition& page,
                const DetectionOptions& options) {
    Features out;
    out.review.page_index = page.page_index;
    if (page.selected) out.review.revision = page.selected->revision;
    if (page.outcome == text::Outcome::Failed ||
        page.outcome == text::Outcome::Cancelled ||
        (!page.selected && page.outcome != text::Outcome::NoTextFound)) {
        out.review.status = PageStatus::Skipped;
        out.review.reasons.push_back("Page evidence unavailable");
        for (const auto& reason : page.reasons)
            out.review.reasons.push_back(reason);
        return out;
    }
    out.review.status = PageStatus::Rejected;
    if (!page.selected) {
        out.review.reasons.push_back("Successful acquisition found no text");
        return out;
    }
    const auto& content = *page.selected;
    out.degraded = page.outcome == text::Outcome::Degraded ||
        page.assessment.readability == text::Readability::Suspect ||
        page.assessment.coverage == text::Coverage::SuspectedIncomplete;
    if (out.degraded)
        out.review.reasons.push_back("Acquisition was degraded or coverage uncertain");
    const double width = content.geometry.width_points;
    const double height = content.geometry.height_points;
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0) {
        out.review.status = PageStatus::Skipped;
        out.review.reasons.push_back("Page geometry unavailable");
        return out;
    }
    const auto fragments = fragments_from(content);
    for (const auto& fragment : fragments) {
        if (fragment.quad.points[0].y > height * 0.25 ||
            fragment.text.size() > 48) continue;
        const std::string lower = lower_ascii(fragment.text);
        if (lower.find("contents") != std::string::npos)
            out.heading = true;
        bool ignored_leader = false;
        const bool row_with_reference =
            inline_reference(fragment.text, ignored_leader);
        if (!row_with_reference &&
            (lower == "index" || lower == "glossary" ||
             lower.find("list of figures") != std::string::npos ||
             lower.find("list of tables") != std::string::npos))
            out.excluded_heading = true;
    }
    auto rows = reference_rows(fragments, width);
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.right != b.right) return a.right < b.right;
        return a.middle_y < b.middle_y;
    });
    std::set<std::uint32_t> evidence_ids;
    for (std::size_t first = 0; first < rows.size();) {
        std::size_t end = first + 1;
        while (end < rows.size() &&
               rows[end].right - rows[first].right <=
                   options.alignment_tolerance_points)
            ++end;
        if (end - first >= 2) {
            double anchor = 0;
            for (std::size_t i = first; i < end; ++i) {
                ++out.aligned_rows;
                if (rows[i].leader) ++out.leader_rows;
                anchor += rows[i].right;
                for (const auto id : rows[i].region_ids) evidence_ids.insert(id);
            }
            out.anchors.push_back(anchor / (end - first) / width);
        }
        first = end;
    }
    for (const auto id : evidence_ids)
        out.evidence.push_back({page.page_index, content.revision, id,
                                std::nullopt, std::nullopt});
    // Density is rows per visual line: fragments that together form one row
    // (a title region plus its separate reference region, or duplicate
    // observations) count once, so split native runs are not penalized.
    std::size_t merged = 0;
    for (const auto& row : rows) merged += row.fragments - 1;
    const std::size_t lines =
        fragments.size() > merged ? fragments.size() - merged : 1;
    const double density = fragments.empty() ? 0.0 :
        static_cast<double>(out.aligned_rows) / static_cast<double>(lines);
    out.review.score = 2.0 * out.aligned_rows +
                       static_cast<double>(std::min<std::size_t>(out.leader_rows, 3)) +
                       2.0 * density + (out.heading ? 2.0 : 0.0) -
                       (out.excluded_heading ? 8.0 : 0.0);
    if (out.heading) out.review.reasons.push_back("Contents heading cue");
    if (out.excluded_heading)
        out.review.reasons.push_back("Index/glossary/list heading counterexample");
    if (out.aligned_rows)
        out.review.reasons.push_back(
            std::to_string(out.aligned_rows) +
            " aligned title/reference rows across " +
            std::to_string(out.anchors.size()) + " reference columns");
    if (out.leader_rows)
        out.review.reasons.push_back(
            std::to_string(out.leader_rows) + " dot-leader rows");
    if (out.aligned_rows >= options.min_reference_rows &&
        density >= options.min_aligned_ratio &&
        out.review.score >= options.min_page_score &&
        !out.excluded_heading)
        out.review.status = PageStatus::Candidate;
    else if (out.excluded_heading)
        out.review.reasons.push_back("Explicit non-TOC heading veto");
    else
        out.review.reasons.push_back("Insufficient repeated aligned row evidence");
    return out;
}

bool compatible(const Features& left, const Features& right) {
    for (const double a : left.anchors)
        for (const double b : right.anchors)
            if (std::abs(a - b) <= 0.08) return true;
    return false;
}

BoundaryState boundary(PageIndex neighbor, bool beginning,
                       const std::map<PageIndex, std::size_t>& by_index,
                       const std::vector<Features>& features) {
    if (beginning && neighbor < 0) return BoundaryState::Closed;
    const auto found = by_index.find(neighbor);
    if (found == by_index.end()) return BoundaryState::MayContinue;
    return features[found->second].review.status == PageStatus::Skipped ?
        BoundaryState::Unknown : BoundaryState::Closed;
}

}  // namespace

Result<DetectionResult> detect(
    const std::vector<text::PageAcquisition>& supplied_pages,
    const DetectionOptions& options) {
    if (options.min_reference_rows < 2 ||
        !std::isfinite(options.min_aligned_ratio) ||
        options.min_aligned_ratio <= 0 || options.min_aligned_ratio > 1 ||
        !std::isfinite(options.alignment_tolerance_points) ||
        options.alignment_tolerance_points <= 0 ||
        !std::isfinite(options.min_page_score))
        return Error{ErrorCode::InvalidArgument, "Invalid TOC detection options"};
    std::vector<const text::PageAcquisition*> ordered;
    ordered.reserve(supplied_pages.size());
    for (const auto& page : supplied_pages) {
        if (page.page_index < 0)
            return Error{ErrorCode::InvalidArgument, "Negative physical page index"};
        if (page.selected &&
            (page.selected->page_index != page.page_index ||
             page.selected->revision == 0))
            return Error{ErrorCode::InvalidArgument,
                         "Selected page index or revision mismatch"};
        ordered.push_back(&page);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* a, const auto* b) {
                         return a->page_index < b->page_index;
                     });
    for (std::size_t i = 1; i < ordered.size(); ++i)
        if (ordered[i - 1]->page_index == ordered[i]->page_index)
            return Error{ErrorCode::InvalidArgument,
                         "Duplicate physical page index"};

    DetectionResult result;
    result.policy_id = kPolicyId;
    std::vector<Features> features;
    features.reserve(ordered.size());
    std::map<PageIndex, std::size_t> by_index;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        features.push_back(assess(*ordered[i], options));
        by_index.emplace(ordered[i]->page_index, i);
        result.pages.push_back(features.back().review);
        if (features.back().review.status == PageStatus::Skipped)
            result.diagnostics.push_back(
                "Page " + std::to_string(ordered[i]->page_index) +
                " was not assessable");
    }
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < features.size(); ++i)
        if (features[i].review.status == PageStatus::Candidate)
            candidates.push_back(i);
    for (std::size_t cursor = 0; cursor < candidates.size();) {
        const std::size_t first = candidates[cursor];
        TocCandidate candidate;
        candidate.id = "toc-p" + std::to_string(ordered[first]->page_index) +
                       "-r" + std::to_string(*features[first].review.revision);
        const auto add_page = [&](std::size_t index, TocCandidate& target) {
            CandidatePage page;
            page.page_index = ordered[index]->page_index;
            page.revision = *features[index].review.revision;
            page.score = features[index].review.score;
            page.row_evidence = features[index].evidence;
            page.degraded = features[index].degraded;
            if (page.degraded)
                target.limitations.push_back(
                    "Page " + std::to_string(page.page_index) +
                    " has degraded acquisition evidence");
            target.pages.push_back(std::move(page));
        };
        add_page(first, candidate);
        ++cursor;
        while (cursor < candidates.size()) {
            const std::size_t previous = candidates[cursor - 1];
            const std::size_t next = candidates[cursor];
            const auto distance =
                static_cast<std::int64_t>(ordered[next]->page_index) -
                ordered[previous]->page_index;
            if (distance < 1 ||
                static_cast<std::uint64_t>(distance - 1) >
                    options.max_interrupted_pages ||
                !compatible(features[previous], features[next]))
                break;
            std::vector<CandidateGap> gaps;
            bool supplied_interruption = true;
            for (std::int64_t index =
                     static_cast<std::int64_t>(ordered[previous]->page_index) + 1;
                 index < ordered[next]->page_index; ++index) {
                const auto found = by_index.find(static_cast<PageIndex>(index));
                if (found == by_index.end() ||
                    features[found->second].review.status != PageStatus::Skipped) {
                    supplied_interruption = false;
                    break;
                }
                std::string reason = "Supplied page could not be assessed";
                for (const auto& detail : features[found->second].review.reasons)
                    reason += "; " + detail;
                gaps.push_back({static_cast<PageIndex>(index), std::move(reason)});
            }
            if (!supplied_interruption) break;
            candidate.interruptions.insert(candidate.interruptions.end(),
                                           gaps.begin(), gaps.end());
            add_page(next, candidate);
            ++cursor;
        }
        double total = 0;
        for (const auto& page : candidate.pages) total += page.score;
        candidate.score = total / candidate.pages.size() +
                          0.5 * (candidate.pages.size() - 1) -
                          candidate.interruptions.size();
        candidate.reasons.push_back(
            std::to_string(candidate.pages.size()) +
            " supplied page(s) with repeated aligned title/reference rows");
        if (features[first].heading)
            candidate.reasons.push_back("Contents heading supports first page");
        if (candidate.pages.size() > 1)
            candidate.reasons.push_back("Compatible reference-column positions");
        if (!candidate.interruptions.empty())
            candidate.limitations.push_back(
                "Supplied but unassessable page interrupts this group");
        candidate.start = boundary(candidate.pages.front().page_index - 1, true,
                                   by_index, features);
        const auto last_index = candidate.pages.back().page_index;
        candidate.end = last_index == std::numeric_limits<PageIndex>::max() ?
            BoundaryState::Unknown :
            boundary(last_index + 1, false, by_index, features);
        result.candidates.push_back(std::move(candidate));
    }
    return result;
}

}  // namespace pdfbookmark::detection
