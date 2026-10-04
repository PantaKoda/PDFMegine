#include <pdfbookmark/detection/detection.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using pdfbookmark::PageIndex;
using pdfbookmark::Point;
using pdfbookmark::Quad;
using pdfbookmark::detection::BoundaryState;
using pdfbookmark::detection::PageStatus;
using pdfbookmark::text::Outcome;
using pdfbookmark::text::PageAcquisition;
using pdfbookmark::text::PageContent;
using pdfbookmark::text::TextRegion;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

TextRegion region(std::uint32_t id, std::string text, double left,
                  double right, double top) {
    TextRegion value;
    value.id = id;
    value.text = std::move(text);
    Quad quad;
    quad.points = {Point{left, top}, Point{right, top},
                   Point{right, top + 16}, Point{left, top + 16}};
    value.quad = quad;
    return value;
}

PageAcquisition page(PageIndex index, double right = 550,
                     bool heading = true, Outcome outcome = Outcome::Ok) {
    PageAcquisition value;
    value.page_index = index;
    value.outcome = outcome;
    PageContent content;
    content.page_index = index;
    content.revision = static_cast<std::uint64_t>(index + 1);
    content.geometry.width_points = 600;
    content.geometry.height_points = 800;
    if (heading)
        content.regions.push_back(region(0, "Contents", 40, 140, 35));
    const std::uint32_t first = heading ? 1U : 0U;
    content.regions.push_back(region(first, "First chapter ........ 12",
                                     40, right, 110));
    content.regions.push_back(region(first + 1, "Second chapter ...... 29",
                                     40, right, 140));
    content.regions.push_back(region(first + 2, "Third chapter ....... 41",
                                     40, right, 170));
    value.selected = std::move(content);
    return value;
}

// A contents page laid out in three column regions per row (section number,
// title, page number) with unnumbered chapter lines between, as in a scanned
// reprint's text layer (issue #13). `head` is an optional first line.
PageAcquisition columns_page(PageIndex index, const std::string& head,
                             int entries = 18, int chapters = 6,
                             double ref_left = 530) {
    PageAcquisition value;
    value.page_index = index;
    value.outcome = Outcome::Ok;
    PageContent content;
    content.page_index = index;
    content.revision = static_cast<std::uint64_t>(index + 1);
    content.geometry.width_points = 600;
    content.geometry.height_points = 800;
    std::uint32_t id = 0;
    if (!head.empty()) content.regions.push_back(region(id++, head, 40, 160, 40));
    double top = 100;
    for (int i = 0, chapter = 0; i < entries; ++i) {
        if (chapter < chapters && i % 3 == 0) {
            content.regions.push_back(region(id++, "Chapter heading without a number",
                                             40, 330, top));
            top += 20;
            ++chapter;
        }
        content.regions.push_back(region(id++, "8." + std::to_string(i + 1), 60, 76, top));
        content.regions.push_back(region(id++, "Section title", 86, 250, top));
        content.regions.push_back(
            region(id++, std::to_string(200 + 3 * i), ref_left, ref_left + 18, top));
        top += 20;
    }
    value.selected = std::move(content);
    return value;
}

PageAcquisition blank(PageIndex index) {
    PageAcquisition value;
    value.page_index = index;
    value.outcome = Outcome::NoTextFound;
    return value;
}

PageAcquisition failed(PageIndex index) {
    PageAcquisition value;
    value.page_index = index;
    value.outcome = Outcome::Failed;
    value.reasons.push_back("Injected acquisition failure");
    return value;
}

}  // namespace

int main() {
    using namespace pdfbookmark::detection;
    const auto single = detect({page(4)});
    require(single && single.value().candidates.size() == 1,
            "single page without supplied neighbors is a candidate");
    const auto& first = single.value().candidates[0];
    require(first.pages.size() == 1 && first.pages[0].page_index == 4 &&
            first.pages[0].revision == 5 &&
            first.pages[0].row_evidence.size() == 3,
            "candidate retains exact physical page, revision and region evidence");
    require(first.start == BoundaryState::MayContinue &&
            first.end == BoundaryState::MayContinue,
            "batch edges do not imply TOC closure");
    require(first.score > 7 && !first.reasons.empty() &&
            single.value().policy_id == "s2-toc-detection-v3",
            "versioned, inspectable ranking evidence");
    const auto no_heading = detect({page(4, 550, false)});
    require(no_heading && no_heading.value().candidates.size() == 1,
            "repeated layout works without literal Contents");
    auto unicode_titles = page(4, 550, false);
    for (auto& line : unicode_titles.selected->regions)
        line.text = "\xE7\xAB\xA0\xE7\xAB\xA0 ........ 12";
    require(detect({unicode_titles}).value().candidates.size() == 1,
            "two non-ASCII title scalars qualify");
    for (auto& line : unicode_titles.selected->regions)
        line.text = "\xE7\xAB\xA0 ........ 12";
    require(detect({unicode_titles}).value().candidates.empty(),
            "one multibyte glyph is one scalar, not three letters");

    auto heading_only = page(1);
    heading_only.selected->regions.resize(1);
    auto numbers_only = page(2, 550, false);
    numbers_only.selected->regions = {
        region(0, "12", 520, 550, 110),
        region(1, "29", 520, 550, 140),
        region(2, "41", 520, 550, 170)};
    auto body = page(3, 550, false);
    body.selected->regions = {
        region(0, "This paragraph describes the chapter.", 40, 540, 100),
        region(1, "It continues through the page.", 40, 510, 130),
        region(2, "There is no aligned reference.", 40, 530, 160)};
    auto figures = page(5);
    figures.selected->regions[0].text = "List of Figures";
    figures.selected->regions.push_back(
        region(4, "Figure four ........ 60", 40, 550, 200));
    figures.selected->regions.push_back(
        region(5, "Figure five ........ 71", 40, 550, 230));
    figures.selected->regions.push_back(
        region(6, "Figure six ......... 82", 40, 550, 260));
    const auto counterexamples =
        detect({heading_only, numbers_only, body, figures});
    require(counterexamples && counterexamples.value().candidates.empty() &&
            counterexamples.value().pages.size() == 4,
            "heading, bare numbers, body text and dense list of figures are rejected");
    require(counterexamples.value().pages[3].reasons.back() ==
            "Explicit non-TOC heading veto",
            "a high-scoring alternate list explains its rejection");
    auto figure_entry = page(6);
    figure_entry.selected->regions[1].text =
        "List of Figures .......... 12";
    require(detect({figure_entry}).value().candidates.size() == 1,
            "a List of Figures entry inside Contents is not a list heading");

    auto split = page(6, 550, false);
    split.selected->regions.clear();
    for (int i = 0; i < 3; ++i) {
        const auto id = static_cast<std::uint32_t>(2 * i);
        const double top = 110.0 + 30.0 * i;
        split.selected->regions.push_back(
            region(id, "Part title", 40, 410, top));
        split.selected->regions.push_back(
            region(id + 1, std::to_string(i + 12), 520, 550, top));
    }
    const auto split_result = detect({split});
    require(split_result && split_result.value().candidates.size() == 1 &&
            split_result.value().candidates[0].pages[0].row_evidence.size() == 6,
            "separate native title and reference regions pair by geometry");

    auto columns = page(7, 270, false);
    columns.selected->regions.push_back(
        region(3, "Right alpha ...... 52", 310, 565, 110));
    columns.selected->regions.push_back(
        region(4, "Right beta ....... 63", 310, 565, 140));
    columns.selected->regions.push_back(
        region(5, "Right gamma ...... 77", 310, 565, 170));
    columns.selected->reading_order = pdfbookmark::text::ReadingOrder::Uncertain;
    std::reverse(columns.selected->regions.begin(),
                 columns.selected->regions.end());
    const auto two_columns = detect({columns});
    require(two_columns && two_columns.value().candidates.size() == 1 &&
            two_columns.value().candidates[0].pages[0].row_evidence.size() == 6,
            "two columns detected without relying on flattened reading order");

    // Right-aligned numbers far from short titles (gap > 0.35 page width).
    auto wide = page(8, 550, false);
    wide.selected->regions.clear();
    for (int i = 0; i < 3; ++i) {
        const auto id = static_cast<std::uint32_t>(2 * i);
        const double top = 110.0 + 30.0 * i;
        wide.selected->regions.push_back(region(id, "Short title", 40, 110, top));
        wide.selected->regions.push_back(
            region(id + 1, std::to_string(i + 3), 540, 548, top));
    }
    const auto wide_result = detect({wide});
    require(wide_result && wide_result.value().candidates.size() == 1 &&
            wide_result.value().candidates[0].pages[0].row_evidence.size() == 6,
            "wide right-aligned references pair when the row between is empty");

    // Counterexample: complete left-column rows never title bare right numbers.
    auto complete_left = page(9, 270, false);
    for (int i = 0; i < 3; ++i)
        complete_left.selected->regions.push_back(region(
            static_cast<std::uint32_t>(10 + i), std::to_string(i + 90), 545, 560,
            110.0 + 30.0 * i));
    const auto complete_result = detect({complete_left});
    require(complete_result && complete_result.value().candidates.size() == 1 &&
            complete_result.value().candidates[0].pages[0].row_evidence.size() == 3,
            "bare right-column numbers do not pair with complete left rows");

    // Counterexample: a left title's own number blocks cross-column pairing.
    auto blocked = page(10, 550, false);
    blocked.selected->regions.clear();
    for (int i = 0; i < 3; ++i) {
        const auto id = static_cast<std::uint32_t>(3 * i);
        const double top = 110.0 + 30.0 * i;
        blocked.selected->regions.push_back(region(id, "Left title", 40, 150, top));
        blocked.selected->regions.push_back(
            region(id + 1, std::to_string(i + 20), 250, 262, top));
        blocked.selected->regions.push_back(
            region(id + 2, std::to_string(i + 70), 545, 557, top));
    }
    const auto blocked_result = detect({blocked});
    require(blocked_result && blocked_result.value().candidates.size() == 1 &&
            blocked_result.value().candidates[0].pages[0].row_evidence.size() == 6,
            "an intervening left-column number blocks cross-column pairing");

    // Issue #13: a five-page contents with the "Contents" heading on its
    // first page only, "xiv Contents" / "Contents xv" running heads, and
    // rows split into number, title and page regions.
    const auto five = detect({columns_page(10, "Contents"), columns_page(11, "xiv Contents"),
                              columns_page(12, ""), columns_page(13, "xvi Contents"),
                              columns_page(14, "Contents xvii")});
    require(five && five.value().candidates.size() == 1 &&
                five.value().candidates[0].pages.size() == 5 &&
                five.value().pages[2].status == PageStatus::Candidate,
            "three-region rows count once per visual line; one five-page candidate");
    const auto& head_reasons = five.value().pages[1].reasons;
    require(std::find(head_reasons.begin(), head_reasons.end(),
                      "Contents running head (continued page)") != head_reasons.end() &&
                std::find(head_reasons.begin(), head_reasons.end(),
                          "Contents heading cue") == head_reasons.end(),
            "a 'xiv Contents' running head marks a continued page, not a new heading");

    // One sparse page between two TOC pages, with aligned rows in the same
    // reference column, continues the TOC.
    const auto sparse_page = [&](PageIndex index, double ref_left) {
        auto value = columns_page(index, "", 3, 0, ref_left);
        auto id = static_cast<std::uint32_t>(value.selected->regions.size());
        for (int k = 0; k < 10; ++k)
            value.selected->regions.push_back(
                region(id++, "A paragraph of plain running text", 40, 500, 200.0 + 20 * k));
        return value;
    };
    const auto bridged = detect({columns_page(30, "Contents"), sparse_page(31, 530),
                                 columns_page(32, "")});
    require(bridged && bridged.value().candidates.size() == 1 &&
                bridged.value().candidates[0].pages.size() == 3 &&
                bridged.value().pages[1].status == PageStatus::Rejected &&
                std::any_of(bridged.value().candidates[0].reasons.begin(),
                            bridged.value().candidates[0].reasons.end(),
                            [](const std::string& r) {
                                return r.find("included between compatible TOC pages") !=
                                       std::string::npos;
                            }),
            "a sparse middle page with rows in the same column joins the candidate");
    const auto unbridged = detect({columns_page(30, "Contents"), sparse_page(31, 300),
                                   columns_page(32, "")});
    require(unbridged && unbridged.value().candidates.size() == 2,
            "a middle page whose numbers sit in another column is not bridged");

    const auto boundary_input = detect({page(39, 550, true)});
    require(boundary_input &&
            boundary_input.value().candidates[0].end ==
                BoundaryState::MayContinue,
            "physical index 39 at supplied boundary may continue");
    const auto continued = detect({blank(41), page(40, 550, false),
                                   page(39, 550, true)});
    require(continued && continued.value().candidates.size() == 1 &&
            continued.value().candidates[0].pages.size() == 2 &&
            continued.value().candidates[0].pages[0].page_index == 39 &&
            continued.value().candidates[0].pages[1].page_index == 40 &&
            continued.value().candidates[0].end == BoundaryState::Closed,
            "continuation groups by physical index and blank neighbor closes end");
    const auto separate = detect({page(3), page(4, 550, false),
                                  blank(5), page(10, 260),
                                  page(11, 260, false)});
    require(separate && separate.value().candidates.size() == 2 &&
            separate.value().candidates[0].pages.size() == 2 &&
            separate.value().candidates[1].pages.size() == 2,
            "two distinct TOCs remain separate across supplied blank page");
    const auto adjacent_styles = detect({page(3, 260), page(4, 550)});
    require(adjacent_styles && adjacent_styles.value().candidates.size() == 2,
            "adjacent incompatible reference-column styles stay separate");

    const auto interrupted = detect({page(20), failed(21),
                                     page(22, 550, false)});
    require(interrupted && interrupted.value().candidates.size() == 1 &&
            interrupted.value().candidates[0].pages.size() == 2 &&
            interrupted.value().candidates[0].interruptions.size() == 1 &&
            interrupted.value().candidates[0].interruptions[0].page_index == 21 &&
            interrupted.value().candidates[0].interruptions[0].reason.find(
                "Injected acquisition failure") != std::string::npos &&
            interrupted.value().pages[1].status == PageStatus::Skipped,
            "supplied failed page is an explicit group interruption");
    const auto unsupplied = detect({page(20), page(22, 550, false)});
    require(unsupplied && unsupplied.value().candidates.size() == 2 &&
            unsupplied.value().candidates[0].end == BoundaryState::MayContinue,
            "unrequested page is never silently filled as an interruption");
    auto degraded = page(8, 550, false, Outcome::Degraded);
    const auto uncertain = detect({failed(7), degraded, blank(9)});
    require(uncertain && uncertain.value().candidates.size() == 1 &&
            uncertain.value().candidates[0].start == BoundaryState::Unknown &&
            uncertain.value().candidates[0].end == BoundaryState::Closed &&
            uncertain.value().candidates[0].pages[0].degraded &&
            !uncertain.value().candidates[0].limitations.empty(),
            "degraded provenance and failed-neighbor uncertainty retained");

    const auto duplicate = detect({page(1), page(1)});
    require(!duplicate && duplicate.error().code ==
            pdfbookmark::ErrorCode::InvalidArgument,
            "duplicate physical page indices rejected");
    auto mismatch = page(2);
    mismatch.selected->revision = 0;
    require(!detect({mismatch}), "missing selected revision rejected");
    DetectionOptions invalid;
    invalid.min_reference_rows = 1;
    require(!detect({page(1)}, invalid), "invalid threshold rejected");
    std::cout << "S2 pure fixtures: single, columns, continuation, groups, "
                 "gaps, degraded and counterexamples passed\n";
}
