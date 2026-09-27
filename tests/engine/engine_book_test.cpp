// analyze_book(): analysis and metadata in one run reuse acquired pages
// (issue #3). A counting fake OCR (test hooks) makes OCR work observable.
// Usage: pdfbookmark_engine_book_tests <engine fixtures dir>
//   needs scan4.pdf (tools/bench/make_scan_fixture.py, 4 image-only pages)
//   and boundary.pdf (text layer).
#include <pdfbookmark/engine/analysis.hpp>
#include <pdfbookmark/engine/book.hpp>
#include <pdfbookmark/engine/metadata.hpp>

#include "ocr_adapter.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace pdfbookmark;

namespace {

std::atomic<int> ocr_calls{0};

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class CountingOcr final : public text::detail::OcrBackend {
public:
    std::vector<text::detail::OcrLine> run(const text::detail::Raster&) override {
        ++ocr_calls;
        text::detail::OcrLine line;
        line.text = "Recovered page body";
        line.confidence = 0.9f;
        line.pixel_quad.points = {Point{10, 10}, Point{200, 10}, Point{200, 40}, Point{10, 40}};
        return {line};
    }
};

std::unique_ptr<text::detail::OcrBackend> make_counting(const text::ModelResources&) {
    return std::make_unique<CountingOcr>();
}

// The metadata JSON of a book run equals a separate run's, except that
// reused pages were not OCR'd again: normalise that one member.
std::string without_attempts(std::string json) {
    const std::string key = "\"ocr_attempts_used\": ";
    const auto at = json.find(key);
    if (at == std::string::npos) return json;
    const auto end = json.find_first_of(",\n}", at + key.size());
    return json.erase(at + key.size(), end - at - key.size());
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: fixtures-dir");
    const fs::path fixtures = fs::u8path(argv[1]);
    const fs::path scan = fixtures / "scan4.pdf";
    text::detail::set_ocr_factory_for_testing(&make_counting);
    // The fake ignores the files; S1 only needs them to exist (it hashes them
    // for the model identity).
    const text::ModelResources fake_models{scan, scan, scan};

    engine::AnalysisOptions analysis;
    analysis.models = fake_models;
    engine::MetadataRunOptions metadata;  // Same mode and raster as analysis.

    // 1. One OCR pass for both stages.
    ocr_calls = 0;
    auto book = engine::analyze_book(scan, analysis, metadata);
    require(static_cast<bool>(book), "analyze_book on a scan");
    const auto& b = book.value();
    require(ocr_calls == 4, "each of the 4 scanned pages is OCR'd exactly once (got " +
                                std::to_string(ocr_calls.load()) + ")");
    require(b.analysis.ocr_attempts_used == 4, "analysis did the OCR");
    require(b.metadata.ocr_attempts_used == 0, "metadata reused pages at no OCR cost");
    require(b.pages_reused == 4, "4 metadata pages served from the analysis");
    require(b.metadata.model_identity == b.analysis.model_identity,
            "reused pages carry the model identity");

    // 2. Same results as two separate calls (apart from the OCR attempt count).
    ocr_calls = 0;
    auto alone_analysis = engine::analyze(scan, analysis);
    auto alone_metadata = engine::extract_metadata(scan, [&] {
        auto m = metadata;
        m.models = fake_models;
        return m;
    }());
    require(alone_analysis && alone_metadata, "separate calls");
    require(ocr_calls == 8, "separate calls OCR the pages twice (the cost being removed)");
    require(engine::analysis_report_json(b.analysis, analysis) ==
                engine::analysis_report_json(alone_analysis.value(), analysis),
            "analysis report identical to a separate analyze()");
    require(without_attempts(engine::metadata_report_json(b.metadata)) ==
                without_attempts(engine::metadata_report_json(alone_metadata.value())),
            "metadata report identical to a separate extract_metadata()");

    // 3. One input: both reports describe the same bytes.
    require(b.analysis.input.sha256 == b.metadata.input.sha256 &&
                b.analysis.input.page_count == b.metadata.input.page_count,
            "both stages read the same input");

    // 4. Different acquisition settings: no reuse, pages are read again.
    ocr_calls = 0;
    auto different = metadata;
    different.raster.dpi = 200;
    auto split = engine::analyze_book(scan, analysis, different);
    require(split && split.value().pages_reused == 0, "no reuse across different DPI");
    require(ocr_calls == 8 && split.value().metadata.ocr_attempts_used == 4,
            "metadata OCR'd its pages under its own settings");

    // 5. Cancellation: both reports say so; cancelled pages are not reused.
    std::atomic_bool cancel{true};
    auto cancelled = engine::analyze_book(scan, analysis, metadata, RunControl{&cancel});
    require(cancelled && cancelled.value().analysis.outcome == engine::AnalysisOutcome::Cancelled &&
                cancelled.value().metadata.cancelled && cancelled.value().pages_reused == 0,
            "cancellation reaches both stages without reusing cancelled pages");

    // 6. Budget-skipped OCR is not reused: metadata may still OCR the page.
    ocr_calls = 0;
    auto tight = analysis;
    tight.limits.ocr_budget = 1;
    auto budget = engine::analyze_book(scan, tight, metadata);
    require(static_cast<bool>(budget), "tight budget run");
    require(budget.value().analysis.ocr_attempts_used == 1, "analysis spent its budget of 1");
    require(budget.value().pages_reused == 1 && budget.value().metadata.ocr_attempts_used == 3,
            "the OCR'd page is reused; the 3 budget-skipped pages are OCR'd by metadata");

    // 7. Text-layer PDF: pages are reused too (no OCR involved at all).
    ocr_calls = 0;
    auto native = engine::analyze_book(fixtures / "boundary.pdf", analysis, metadata);
    require(native && native.value().pages_reused > 0 && ocr_calls == 0,
            "native pages reused, no OCR");
    auto native_alone = engine::extract_metadata(fixtures / "boundary.pdf", metadata);
    require(native_alone &&
                engine::metadata_report_json(native.value().metadata) ==
                    engine::metadata_report_json(native_alone.value()),
            "native metadata identical to a separate call");

    std::cout << "engine book (page reuse) tests passed\n";
    return 0;
}
