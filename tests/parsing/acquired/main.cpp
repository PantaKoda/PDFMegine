#include <pdfbookmark/detection/detection.hpp>
#include <pdfbookmark/parsing/parsing.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <array>
#include <iostream>
#include <string>

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
    auto detected = pdfbookmark::detection::detect(pages.value().pages);
    if (!detected || detected.value().candidates.size() != 1) return 5;
    const auto& candidate = detected.value().candidates.front();
    auto parsed = pdfbookmark::parsing::parse(candidate, pages.value().pages);
    if (!parsed) {
        std::cerr << parsed.error().message << '\n';
        return 6;
    }
    const std::array<std::string, 8> titles = {
        "Foundations", "Methods", "Experiments", "Results",
        "Discussion", "Applications", "Appendix A", "References"};
    const std::array<std::string, 8> references = {
        "1", "17", "34", "52", "70", "84", "95", "110"};
    const auto& result = parsed.value();
    for (const auto& entry : result.entries) {
        std::cout << entry.order << ' ' << entry.title << " -> ";
        if (entry.printed_reference)
            std::cout << entry.printed_reference->literal;
        std::cout << " sources=" << entry.sources.size() << '\n';
    }
    if (result.entries.size() != titles.size() ||
        !result.unparsed.empty() || !result.missing_pages.empty() ||
        result.completeness !=
            pdfbookmark::parsing::ParseCompleteness::Complete)
        return 7;
    for (std::size_t i = 0; i < titles.size(); ++i) {
        const auto& entry = result.entries[i];
        if (entry.title != titles[i] || !entry.printed_reference ||
            entry.printed_reference->literal != references[i] ||
            entry.order != i || entry.sources.empty() ||
            entry.sources.front().page_index !=
                static_cast<pdfbookmark::PageIndex>(i / 4) ||
            entry.sources.front().revision !=
                candidate.pages[i / 4].revision ||
            entry.hierarchy.kind !=
                pdfbookmark::parsing::HierarchyKind::Root)
            return 8;
    }
    std::cout << "candidate=" << result.candidate_id
              << " entries=" << result.entries.size()
              << " completeness=complete\n";
}
