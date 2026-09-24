// pdfbookmark example: analyze a PDF and, when the plan is ready, write
// "<name> (bookmarked).pdf" next to it. Also prints the book's metadata.
#include <pdfbookmark/pdfbookmark.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "usage: pdfbookmark_example <book.pdf>\n";
        return 2;
    }
    const std::filesystem::path input = argv[1];  // Wide path: Unicode-safe.
    std::cout << "pdfbookmark " << pdfbookmark::version() << '\n';

    pdfbookmark::AnalysisOptions options;
    options.models = pdfbookmark::find_models();  // OCR for scanned pages.
    options.plan.allow_partial = true;             // Skip unplaceable entries.
    const auto report = pdfbookmark::analyze(
        input, options, {}, [](const pdfbookmark::AnalysisProgress& p) {
            std::cerr << p.stage << ": " << p.pages_acquired << " pages read\n";
        });
    if (!report) {
        std::cerr << "error: " << report.error().message << '\n';
        return 1;
    }
    const auto& r = report.value();
    std::cout << "outcome: " << pdfbookmark::outcome_name(r.outcome) << '\n';
    if (!r.plan.ready) {
        for (const auto& blocker : r.plan.blockers) std::cout << "  " << blocker << '\n';
        return 3;
    }
    for (const auto& node : r.plan.plan->nodes)
        std::cout << "  " << (node.parent_id ? "    " : "") << node.title << "  -> page "
                  << node.destination.pdf_page_index + 1 << '\n';

    auto output = input.parent_path() / input.stem();
    output += " (bookmarked).pdf";
    const auto written = pdfbookmark::apply(input, output, *r.plan.plan);
    if (!written) {
        std::cerr << "error: " << written.error().message << '\n';
        return 1;
    }
    std::cout << "wrote " << output.u8string() << " ("
              << written.value().verification.outline_items << " bookmarks)\n";

    pdfbookmark::MetadataRunOptions meta_options;
    meta_options.models = options.models;
    if (const auto meta = pdfbookmark::extract_metadata(input, meta_options);
        meta && meta.value().result.title.value)
        std::cout << "title: " << meta.value().result.title.value->title << '\n';
    return 0;
}
