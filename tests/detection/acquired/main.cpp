#include <pdfbookmark/detection/detection.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    pdfbookmark::text::TextAcquisition acquisition;
    auto opened = acquisition.open(argv[1]);
    if (!opened || opened.value().page_count() != 3) return 3;
    auto document = opened.take();
    pdfbookmark::text::AcquisitionOptions options;
    options.mode = pdfbookmark::text::AcquisitionMode::EmbeddedOnly;
    auto pages = document.acquire({2, 0, 1}, options);
    if (!pages) return 4;
    const auto result = pdfbookmark::detection::detect(pages.value().pages);
    if (!result) return 5;
    for (const auto& review : result.value().pages)
        std::cout << "page=" << review.page_index << " score="
                  << review.score << " status="
                  << static_cast<int>(review.status) << '\n';
    if (result.value().candidates.size() != 1 ||
        result.value().candidates[0].pages.size() != 2 ||
        result.value().candidates[0].pages[0].page_index != 0 ||
        result.value().candidates[0].pages[1].page_index != 1 ||
        result.value().candidates[0].end !=
            pdfbookmark::detection::BoundaryState::Closed)
        return 6;
    const auto& candidate = result.value().candidates[0];
    if (candidate.pages[0].revision == 0 ||
        candidate.pages[1].revision == 0 ||
        candidate.pages[0].row_evidence.size() < 3 ||
        candidate.pages[1].row_evidence.size() < 3)
        return 7;
    std::cout << "candidate=" << candidate.id
              << " pages=" << candidate.pages.size()
              << " score=" << candidate.score << '\n';
}
