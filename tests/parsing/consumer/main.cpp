#include <pdfbookmark/parsing/parsing.hpp>

#include <iostream>
#include <utility>
#include <vector>

int main() {
    using namespace pdfbookmark;
    detection::TocCandidate candidate;
    candidate.id = "manual-toc";
    candidate.start = detection::BoundaryState::Closed;
    candidate.end = detection::BoundaryState::Closed;
    candidate.pages.push_back({8, 3, 0, {}, false});
    text::PageAcquisition page;
    page.page_index = 8;
    page.outcome = text::Outcome::Ok;
    text::PageContent content;
    content.page_index = 8;
    content.revision = 3;
    content.geometry.width_points = 600;
    content.geometry.height_points = 800;
    const std::vector<std::string> lines = {
        "Origins ........ 12", "Methods ........ 29", "Results ........ 41"};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text::TextRegion region;
        region.id = static_cast<std::uint32_t>(i);
        region.text = lines[i];
        const double top = 100.0 + 30.0 * i;
        region.quad = Quad{{Point{40, top}, Point{550, top},
                            Point{550, top + 16}, Point{40, top + 16}}};
        content.regions.push_back(std::move(region));
    }
    page.selected = std::move(content);
    const auto result = parsing::parse(candidate, {page});
    if (!result || result.value().entries.size() != 3 ||
        result.value().entries[0].title != "Origins" ||
        result.value().entries[0].printed_reference->literal != "12")
        return 1;
    std::cout << result.value().candidate_id << " entries="
              << result.value().entries.size() << '\n';
}
