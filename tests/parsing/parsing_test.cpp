#include <pdfbookmark/parsing/parsing.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdfbookmark::Point;
using pdfbookmark::Quad;
using pdfbookmark::detection::BoundaryState;
using pdfbookmark::detection::CandidatePage;
using pdfbookmark::detection::TocCandidate;
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

struct Line {
    std::string text;
    double left, right, top;
};

PageAcquisition page(int index, std::uint64_t revision,
                     const std::vector<Line>& lines) {
    PageAcquisition result;
    result.page_index = index;
    result.outcome = Outcome::Ok;
    PageContent content;
    content.page_index = index;
    content.revision = revision;
    content.geometry.width_points = 600;
    content.geometry.height_points = 800;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        TextRegion region;
        region.id = static_cast<std::uint32_t>(i);
        region.text = lines[i].text;
        const double left = lines[i].left, right = lines[i].right;
        const double top = lines[i].top;
        region.quad = Quad{{Point{left, top}, Point{right, top},
                            Point{right, top + 16}, Point{left, top + 16}}};
        content.regions.push_back(std::move(region));
    }
    result.selected = std::move(content);
    return result;
}

TocCandidate candidate(std::string id,
                       std::vector<std::pair<int, std::uint64_t>> pages) {
    TocCandidate result;
    result.id = std::move(id);
    result.start = BoundaryState::Closed;
    result.end = BoundaryState::Closed;
    for (const auto& p : pages) {
        CandidatePage entry;
        entry.page_index = p.first;
        entry.revision = p.second;
        result.pages.push_back(std::move(entry));
    }
    return result;
}

}  // namespace

int main() {
    using namespace pdfbookmark::parsing;
    const auto syntax_page = page(8, 7, {
        {"Contents", 40, 140, 35},
        {"Chapter 3 ........ 12", 40, 550, 100},
        {"Version 2.0 ........ 29", 40, 550, 130},
        {"Foreword ........ iv", 40, 550, 160},
        {"Appendix A ........ A-12", 40, 550, 190},
        {"Methods ........ 12\xE2\x80\x93" "15", 40, 550, 220},
        {"End ........ iv\xE2\x80\x93" "vi", 40, 550, 250},
        {"Appendices ........ A-12-A-15", 40, 550, 280},
    });
    auto selected = candidate("toc-syntax", {{8, 7}});
    selected.pages[0].row_evidence.push_back({8, 7, 1, std::nullopt, std::nullopt});
    const auto syntax = parse(selected, {syntax_page});
    require(syntax && syntax.value().entries.size() == 7 &&
            syntax.value().completeness == ParseCompleteness::Complete,
            "seven supported references form a complete parse");
    const auto& entries = syntax.value().entries;
    require(entries[0].title == "Chapter 3" &&
            entries[0].printed_reference->literal == "12" &&
            entries[0].printed_reference->ordinal == 12 &&
            entries[0].sources.size() == 1 &&
            entries[0].sources[0].page_index == 8 &&
            entries[0].sources[0].revision == 7 &&
            entries[0].sources[0].region_id == 1,
            "numeric title and source provenance are preserved");
    require(entries[1].title == "Version 2.0" &&
            entries[1].printed_reference->ordinal == 29,
            "meaningful internal title dots are preserved");
    require(entries[2].printed_reference->numbering == NumberingStyle::Roman &&
            entries[2].printed_reference->literal == "iv" &&
            entries[2].printed_reference->ordinal == 4,
            "Roman literal and syntax retained separately");
    require(entries[3].printed_reference->numbering ==
                NumberingStyle::PrefixedDecimal &&
            entries[3].printed_reference->prefix == "A" &&
            entries[3].printed_reference->ordinal == 12,
            "prefixed decimal reference parsed without a physical index");
    require(entries[4].printed_reference->is_range &&
            entries[4].printed_reference->ordinal == 12 &&
            entries[4].printed_reference->range_end == 15 &&
            entries[4].printed_reference->literal ==
                "12\xE2\x80\x93" "15",
            "decimal range and literal en dash preserved");
    require(entries[5].printed_reference->is_range &&
            entries[5].printed_reference->numbering == NumberingStyle::Roman &&
            entries[5].printed_reference->ordinal == 4 &&
            entries[5].printed_reference->range_end == 6,
            "Roman range parsed");
    require(entries[6].printed_reference->is_range &&
            entries[6].printed_reference->numbering ==
                NumberingStyle::PrefixedDecimal &&
            entries[6].printed_reference->prefix == "A" &&
            entries[6].printed_reference->ordinal == 12 &&
            entries[6].printed_reference->range_end == 15 &&
            entries[6].printed_reference->literal == "A-12-A-15",
            "hyphenated prefixed range retains its exact literal");
    require(entries[0].sources[0].utf8_begin == 0 &&
            entries[0].sources[0].utf8_end ==
                syntax_page.selected->regions[1].text.size(),
            "source offsets use UTF-8 bytes within the S1 region");
    for (const auto& entry : entries)
        require(entry.hierarchy.kind == HierarchyKind::Root,
                "aligned entries have known root hierarchy");

    const auto duplicates = parse(candidate("toc-dup", {{9, 2}}),
        {page(9, 2, {{"Repeat .... 12", 40, 550, 100},
                     {"Repeat .... 13", 40, 550, 130}})});
    require(duplicates && duplicates.value().entries.size() == 2 &&
            duplicates.value().entries[0].title ==
                duplicates.value().entries[1].title &&
            duplicates.value().entries[0].id !=
                duplicates.value().entries[1].id,
            "duplicate titles retain independent source/order identities");

    const auto hierarchy = parse(candidate("toc-hierarchy", {{10, 3}}),
        {page(10, 3, {{"PART I", 40, 100, 80},
                      {"Chapter 1 ........ 12", 62, 550, 110},
                      {"Chapter 2 ........ 14", 62, 550, 140},
                      {"Odd child ........ 18", 51, 550, 170},
                      {"PART II", 40, 110, 200}})});
    require(hierarchy && hierarchy.value().entries.size() == 5 &&
            hierarchy.value().entries[0].title == "PART I" &&
            !hierarchy.value().entries[0].printed_reference &&
            hierarchy.value().entries[0].hierarchy.kind ==
                HierarchyKind::Root &&
            hierarchy.value().entries[1].hierarchy.kind ==
                HierarchyKind::KnownParent &&
            hierarchy.value().entries[1].hierarchy.parent_id ==
                hierarchy.value().entries[0].id &&
            hierarchy.value().entries[2].hierarchy.parent_id ==
                hierarchy.value().entries[0].id &&
            hierarchy.value().entries[3].hierarchy.kind ==
                HierarchyKind::Unknown &&
            hierarchy.value().completeness == ParseCompleteness::Incomplete,
            "no-reference heading, clear parentage and ambiguous indent coexist");

    auto columns = page(11, 4, {
        {"Left one .... 12", 40, 270, 100},
        {"Right one .... 32", 310, 560, 100},
        {"Left two .... 18", 40, 270, 130},
        {"Right two .... 39", 310, 560, 130}});
    std::reverse(columns.selected->regions.begin(),
                 columns.selected->regions.end());
    const auto two_columns = parse(candidate("toc-columns", {{11, 4}}),
                                   {columns});
    require(two_columns && two_columns.value().entries.size() == 4 &&
            two_columns.value().entries[0].title == "Left one" &&
            two_columns.value().entries[1].title == "Left two" &&
            two_columns.value().entries[2].title == "Right one" &&
            two_columns.value().entries[3].title == "Right two" &&
            two_columns.value().entries[2].hierarchy.kind ==
                HierarchyKind::Root,
            "columns are parsed independently before final reading order");

    const auto split_regions = parse(candidate("toc-regions", {{15, 4}}),
        {page(15, 4, {{"A title", 40, 400, 100},
                      {"31", 520, 550, 100},
                      {"Another title", 40, 400, 130},
                      {"32", 520, 550, 130}})});
    require(split_regions && split_regions.value().entries.size() == 2 &&
            split_regions.value().entries[0].title == "A title" &&
            split_regions.value().entries[0].printed_reference->ordinal == 31 &&
            split_regions.value().entries[0].sources.size() == 2 &&
            split_regions.value().entries[0].sources[0].region_id == 0 &&
            split_regions.value().entries[0].sources[1].region_id == 1,
            "separate title and reference runs retain both source regions");

    const auto isolated_column = parse(candidate("toc-column-parent", {{14, 4}}),
        {page(14, 4, {{"Left root .... 1", 40, 270, 100},
                      {"Left next .... 2", 40, 270, 130},
                      {"Right indented .... 3", 332, 560, 100},
                      {"Right root .... 4", 310, 560, 130}})});
    require(isolated_column && isolated_column.value().entries.size() == 4 &&
            isolated_column.value().entries[2].hierarchy.kind ==
                HierarchyKind::Unknown &&
            !isolated_column.value().entries[2].hierarchy.parent_id,
            "a right-column child cannot inherit a left-column parent");

    const auto wrapped = parse(candidate("toc-wrap", {{12, 5}}),
        {page(12, 5, {{"A very long title that wraps", 40, 400, 100},
                      {"onto the second line .... 23", 40, 550, 120},
                      {"Next item .... 25", 40, 550, 160}})});
    require(wrapped && wrapped.value().entries.size() == 2 &&
            wrapped.value().entries[0].title ==
                "A very long title that wraps onto the second line" &&
            wrapped.value().entries[0].sources.size() == 2,
            "wrapped title and both source regions preserved");
    const auto across = parse(candidate("toc-cross", {{20, 6}, {21, 7}}),
        {page(20, 6, {{"A very long title continues across pages",
                       40, 450, 760}}),
         page(21, 7, {{"and finishes here .... 51", 40, 550, 40}})});
    require(across && across.value().entries.size() == 1 &&
            across.value().entries[0].title ==
                "A very long title continues across pages and finishes here" &&
            across.value().entries[0].sources.size() == 2 &&
            across.value().entries[0].sources[0].page_index == 20 &&
            across.value().entries[0].sources[1].page_index == 21,
            "supported boundary continuation preserves both page references");

    const auto unsupported = parse(candidate("toc-unsupported", {{13, 8}}),
        {page(13, 8, {{"Title ........ ???", 40, 550, 100},
                      {"Unexplained fragment with no reference",
                       40, 450, 140},
                      {"He said... hello ???", 40, 450, 180}})});
    require(unsupported && unsupported.value().entries.size() == 1 &&
            unsupported.value().entries[0].printed_reference->literal == "???" &&
            unsupported.value().entries[0].printed_reference->uncertain &&
            unsupported.value().unparsed.size() == 2 &&
            unsupported.value().unparsed[0].sources[0].region_id == 1 &&
            unsupported.value().unparsed[1].sources[0].region_id == 2 &&
            unsupported.value().completeness == ParseCompleteness::Incomplete,
            "unsupported literal and internal title ellipsis remain distinct");

    auto missing_candidate = candidate("toc-missing", {{30, 9}, {31, 10}});
    missing_candidate.end = BoundaryState::MayContinue;
    const auto missing = parse(missing_candidate,
        {page(30, 9, {{"Available .... 12", 40, 550, 100}})});
    require(missing && missing.value().missing_pages ==
                std::vector<pdfbookmark::PageIndex>{31} &&
            missing.value().end == BoundaryState::MayContinue &&
            missing.value().completeness == ParseCompleteness::Incomplete,
            "missing page and incomplete candidate boundary remain explicit");
    auto degraded_candidate = candidate("toc-degraded", {{30, 9}});
    degraded_candidate.pages[0].degraded = true;
    degraded_candidate.limitations.push_back("OCR reading order uncertain");
    const auto degraded = parse(degraded_candidate,
        {page(30, 9, {{"Available .... 12", 40, 550, 100}})});
    require(degraded &&
            degraded.value().completeness == ParseCompleteness::Incomplete &&
            degraded.value().diagnostics.size() == 2,
            "degraded S2/S1 evidence and its limitation remain visible");
    auto stale = page(30, 11, {{"Available .... 12", 40, 550, 100}});
    require(!parse(candidate("toc-stale", {{30, 9}}), {stale}),
            "stale evidence revision is a request mismatch");
    auto bad_evidence = candidate("toc-evidence", {{30, 9}});
    bad_evidence.pages[0].row_evidence.push_back(
        {30, 9, 999, std::nullopt, std::nullopt});
    require(!parse(bad_evidence,
                   {page(30, 9, {{"Available .... 12", 40, 550, 100}})}),
            "missing candidate region is rejected");
    // Issue #13: a reprint's text layer with OCR artifacts ("I" for 1, a
    // stray space inside numbers), section numbers in their own column,
    // chapter rows with their page number, hanging-indent wraps and the
    // page's own "xiv Contents" running head.
    const auto noisy_page = page(11, 12, {
        {"xiv Contents", 40, 130, 40},
        {"Chapter 1. Star Deaths and the Formation of Compact", 40, 330, 100},
        {"Objects", 98, 150, 118}, {"1", 540, 548, 118},
        {"1.1 What are Compact Objects?", 45, 200, 150}, {"3", 540, 548, 150},
        {"Chapter 2. Cold Equation of State", 40, 250, 180}, {"17", 536, 548, 180},
        {"2. I Thermodynamic Preliminaries", 45, 220, 210}, {"17", 536, 548, 210},
        {"8.1 1", 45, 62, 240}, {"Hartree Analysis", 70, 180, 240}, {"21 1", 530, 548, 240},
        {"8. I3", 45, 62, 270}, {"Unresolved Issues", 70, 200, 270}, {"I I9", 530, 548, 270},
        {"Appendix G. Spherical Accretion Onto a Black Hole:", 40, 300, 300},
        {"the Relativistic Equations", 98, 260, 318}, {"568", 536, 548, 318},
        {"Part I Foundations", 40, 180, 350},
        {"Introduction", 60, 150, 368}, {"5", 540, 548, 368},
    });
    const auto noisy = parse(candidate("toc-noisy", {{11, 12}}), {noisy_page});
    require(noisy && noisy.value().entries.size() == 9 && noisy.value().unparsed.empty(),
            "noisy text layer: every row becomes one entry");
    const auto& n = noisy.value().entries;
    require(n[0].title == "Chapter 1. Star Deaths and the Formation of Compact Objects" &&
                n[0].printed_reference && n[0].printed_reference->ordinal == 1u,
            "a hanging-indent continuation joins its chapter line");
    require(n[1].hierarchy.kind == HierarchyKind::KnownParent &&
                n[1].hierarchy.parent_id == n[0].id,
            "1.1 is under 'Chapter 1.'");
    require(n[2].title == "Chapter 2. Cold Equation of State" &&
                n[2].printed_reference && n[2].printed_reference->ordinal == 17u,
            "a chapter row keeps its page number");
    require(n[3].title == "2.1 Thermodynamic Preliminaries" &&
                n[3].hierarchy.parent_id == n[2].id && !n[3].diagnostics.empty(),
            "'2. I' is read as section 2.1, with a diagnostic");
    require(n[4].title == "8.11 Hartree Analysis" && n[4].printed_reference &&
                n[4].printed_reference->literal == "21 1" &&
                n[4].printed_reference->ordinal == 211u &&
                !n[4].printed_reference->uncertain &&
                !n[4].printed_reference->reasons.empty(),
            "'8.1 1' and '21 1' are read as 8.11 and 211; the literal stays as printed");
    require(n[5].title == "8.13 Unresolved Issues" && n[5].printed_reference &&
                n[5].printed_reference->ordinal == 119u,
            "'8. I3' and 'I I9' are read as 8.13 and 119");
    require(n[6].title ==
                    "Appendix G. Spherical Accretion Onto a Black Hole: the Relativistic Equations" &&
                n[6].printed_reference && n[6].printed_reference->ordinal == 568u,
            "a line ending in ':' continues on the next line");
    require(n[7].title == "Part I Foundations" && !n[7].printed_reference &&
                n[8].title == "Introduction" && n[8].printed_reference,
            "a short heading does not swallow its indented first entry");
    require(std::any_of(noisy.value().diagnostics.begin(), noisy.value().diagnostics.end(),
                        [](const std::string& d) {
                            return d.find("running head 'xiv Contents'") != std::string::npos;
                        }),
            "the TOC page's running head is ignored, not parsed as an entry");

    // Counterexample: a real title starting with a number keeps its text and
    // gets no section-number parent.
    const auto numeric_title = parse(candidate("toc-numeric", {{12, 13}}),
                                     {page(12, 13, {{"3 Basics", 40, 300, 100}, {"40", 536, 548, 100},
                                                    {"3. 10 Things to Know", 40, 300, 130},
                                                    {"45", 536, 548, 130}})});
    require(numeric_title && numeric_title.value().entries.size() == 2 &&
                numeric_title.value().entries[1].title == "3. 10 Things to Know" &&
                numeric_title.value().entries[1].diagnostics.empty(),
            "a title such as '3. 10 Things to Know' is not rewritten as section 3.10");

    ParsingOptions invalid;
    invalid.child_indent_min_points = 5;
    require(!parse(candidate("toc-invalid", {{30, 9}}), {}, invalid),
            "invalid parsing policy rejected");
    std::cout << "S3 pure fixtures: references, columns, wraps, hierarchy, "
                 "provenance and incomplete parses passed\n";
}
