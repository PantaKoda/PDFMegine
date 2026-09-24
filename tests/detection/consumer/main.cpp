#include <pdfbookmark/detection/detection.hpp>

#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace pdfbookmark;
    std::vector<text::PageAcquisition> supplied;
    text::PageAcquisition page;
    page.page_index = 8;
    page.outcome = text::Outcome::Ok;
    text::PageContent content;
    content.page_index = 8;
    content.revision = 3;
    content.geometry.width_points = 600;
    content.geometry.height_points = 800;
    for (int i = 0; i < 3; ++i) {
        text::TextRegion region;
        region.id = static_cast<std::uint32_t>(i);
        region.text = std::string("Section ") + std::to_string(i + 1) +
                      " ........ " + std::to_string(i + 12);
        const double top = 100.0 + 30.0 * i;
        region.quad = Quad{{Point{40, top}, Point{550, top},
                            Point{550, top + 16}, Point{40, top + 16}}};
        content.regions.push_back(std::move(region));
    }
    page.selected = std::move(content);
    supplied.push_back(std::move(page));
    const auto result = detection::detect(supplied);
    if (!result || result.value().candidates.size() != 1) return 1;
    const auto& candidate = result.value().candidates[0];
    if (candidate.pages[0].page_index != 8 ||
        candidate.pages[0].revision != 3) return 2;
    std::cout << candidate.id << " pages=" << candidate.pages.size()
              << " score=" << candidate.score << '\n';
}
