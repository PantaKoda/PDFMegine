#include <pdfbookmark/detection/detection.hpp>
#include <pdfbookmark/mapping/mapping.hpp>
#include <pdfbookmark/parsing/parsing.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <fpdf_doc.h>
#include <fpdfview.h>

#include <array>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {
using namespace pdfbookmark;

// Test-only outline probe proving the paired fixture really has bookmarks.
int outline_items(const char* path) {
    FPDF_InitLibrary();
    int count = -1;
    if (FPDF_DOCUMENT doc = FPDF_LoadDocument(path, nullptr)) {
        count = 0;
        for (FPDF_BOOKMARK item = FPDFBookmark_GetFirstChild(doc, nullptr);
             item; item = FPDFBookmark_GetNextSibling(doc, item))
            ++count;
        FPDF_CloseDocument(doc);
    }
    FPDF_DestroyLibrary();
    return count;
}

// Returns a normalized S1-S4 semantic summary, excluding input identity and
// revision numbers, or an exit code on failure.
std::optional<std::string> run(const char* path, int& code) {
    text::TextAcquisition acquisition;
    auto opened = acquisition.open(path);
    if (!opened || opened.value().page_count() != 6) return code = 3, std::nullopt;
    auto document = opened.take();
    text::AcquisitionOptions options;
    options.mode = text::AcquisitionMode::EmbeddedOnly;
    auto pages = document.acquire({4, 0, 2, 5, 3, 1}, options);
    if (!pages) return code = 4, std::nullopt;
    text::PdfFactsRequest facts_request;
    facts_request.pages = {0, 1, 2, 3, 4, 5};
    facts_request.local_links = true;
    auto facts = document.read_facts(facts_request);
    if (!facts) return code = 4, std::nullopt;
    auto detected = detection::detect(pages.value().pages);
    if (!detected || detected.value().candidates.size() != 1)
        return code = 5, std::nullopt;
    const auto& candidate = detected.value().candidates.front();
    auto parsed = parsing::parse(candidate, pages.value().pages);
    if (!parsed || parsed.value().entries.size() != 3)
        return code = 6, std::nullopt;
    mapping::DocumentEvidence evidence;
    evidence.input = document.identity();
    evidence.pages = pages.value().pages;
    evidence.facts = facts.value().pages;
    evidence.sections.push_back({
        "body", 2, 6, parsing::NumberingStyle::Decimal,
        {}, "fixture's known body section", false});
    const auto mapped = mapping::map(parsed.value().entries, evidence);
    if (!mapped || mapped.value().entries.size() != 3)
        return code = 7, std::nullopt;

    std::ostringstream summary;
    for (const auto& fact : facts.value().pages)
        summary << "fact " << fact.page_index << " label="
                << static_cast<int>(fact.viewer_label.availability)
                << " links="
                << static_cast<int>(fact.local_link_destinations.availability)
                << '\n';
    summary << "candidate score=" << candidate.score << " pages=";
    for (const auto& page : candidate.pages) summary << page.page_index << ',';
    summary << '\n';
    for (const auto& entry : parsed.value().entries)
        summary << "entry " << entry.id << " title=" << entry.title
                << " ref=" << entry.printed_reference->literal
                << " hierarchy=" << static_cast<int>(entry.hierarchy.kind)
                << '\n';
    for (const auto& result : mapped.value().entries) {
        summary << "map " << result.entry_id << " status="
                << static_cast<int>(result.status) << " index="
                << (result.pdf_page_index ? *result.pdf_page_index : -1)
                << " method="
                << (result.method ? static_cast<int>(*result.method) : -1)
                << " alternatives=" << result.alternatives.size();
        for (const auto& reason : result.reasons) summary << " | " << reason;
        summary << '\n';
    }
    summary << "observations=" << mapped.value().observations.size()
            << " requests=" << mapped.value().requests.size()
            << " policy=" << mapped.value().policy_id << '\n';

    const std::array<const char*, 3> titles = {"Alpha", "Beta", "Gamma"};
    for (std::size_t i = 0; i < titles.size(); ++i) {
        const auto& entry = parsed.value().entries[i];
        const auto& result = mapped.value().entries[i];
        std::cout << entry.title << " ref="
                  << entry.printed_reference->literal << " index=";
        if (result.pdf_page_index) std::cout << *result.pdf_page_index;
        std::cout << '\n';
        if (entry.title != titles[i] || result.entry_id != entry.id ||
            result.status != mapping::MappingStatus::Resolved ||
            result.pdf_page_index != static_cast<PageIndex>(i + 2) ||
            result.method != mapping::ResolutionMethod::InferredOffset ||
            result.supporting_sources.size() < 2)
            return code = 8, std::nullopt;
    }
    std::cout << "mapped=3/3 offset=1\n";
    return summary.str();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    // Probe before any S1 session exists; S1 owns PDFium only per session.
    const int bookmarks = argc == 3 ? outline_items(argv[2]) : 0;
    if (argc == 3 && (outline_items(argv[1]) != 0 || bookmarks <= 0)) {
        std::cerr << "paired fixtures must differ by an existing outline\n";
        return 9;
    }
    int code = 0;
    const auto plain = run(argv[1], code);
    if (!plain) return code;
    if (argc == 2) return 0;
    const auto outlined = run(argv[2], code);
    if (!outlined) return code;
    if (*plain != *outlined) {
        std::cerr << "existing outline changed S1-S4 results\n--- plain\n"
                  << *plain << "--- outlined\n" << *outlined;
        return 10;
    }
    std::cout << "outline-independent: identical S1-S4 semantics with "
              << bookmarks << " misleading bookmarks present\n"
              << *plain;
}
