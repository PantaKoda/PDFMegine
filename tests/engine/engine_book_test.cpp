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
#include <vector>

namespace fs = std::filesystem;
using namespace pdfbookmark;

namespace {

std::atomic<int> ocr_calls{0};
// When set, the fake sets *cancel_flag on OCR call number cancel_on_call.
std::atomic_bool* cancel_flag = nullptr;
int cancel_on_call = 0;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class CountingOcr final : public text::detail::OcrBackend {
public:
    std::vector<text::detail::OcrLine> run(const text::detail::Raster&) override {
        if (++ocr_calls == cancel_on_call && cancel_flag) cancel_flag->store(true);
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

// What an analysis concluded, without acquisition bookkeeping (OCR attempt
// counts and configuration lists differ when pages are reused).
std::string conclusions(const engine::AnalysisReport& r) {
    std::string out = engine::outcome_name(r.outcome);
    if (r.parsed)
        for (const auto& e : r.parsed->entries) out += "|" + e.id + "=" + e.title;
    if (r.mapping)
        for (const auto& m : r.mapping->entries)
            out += "|" + m.entry_id + "->" +
                   (m.pdf_page_index ? std::to_string(*m.pdf_page_index) : std::string("none"));
    if (r.plan.plan) out += "|" + engine::plan_json(*r.plan.plan);
    for (const auto& b : r.plan.blockers) out += "|blocker:" + b;
    return out;
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

    // 1. Metadata first, one OCR pass for both stages; the metadata is
    //    delivered before the analysis starts (PR #4 consumer review).
    ocr_calls = 0;
    std::vector<std::string> stages;
    int calls_at_metadata = -1;
    std::size_t stages_at_metadata = 0;
    std::string early_json;
    auto book = engine::analyze_book(
        scan, analysis, metadata, {},
        [&](const engine::AnalysisProgress& p) { stages.push_back(p.stage); },
        [&](const engine::MetadataReport& m) {
            calls_at_metadata = ocr_calls;
            stages_at_metadata = stages.size();
            early_json = engine::metadata_report_json(m);
        });
    require(static_cast<bool>(book) && book.value().analysis && !book.value().analysis_error,
            "analyze_book on a scan");
    const auto& b = book.value();
    require(ocr_calls == 4, "each of the 4 scanned pages is OCR'd exactly once (got " +
                                std::to_string(ocr_calls.load()) + ")");
    require(b.metadata.ocr_attempts_used == 4 && b.analysis->ocr_attempts_used == 0,
            "metadata did the OCR; the analysis reused its pages for free");
    require(b.pages_reused == 4, "4 analysis pages served from the metadata stage");
    require(b.analysis->model_identity == b.metadata.model_identity,
            "reused pages carry the model identity");
    require(calls_at_metadata == 4, "metadata delivered right after its own OCR");
    bool analysis_started_before = false;
    for (std::size_t i = 0; i < stages_at_metadata; ++i)
        analysis_started_before |= stages[i] != "metadata";
    require(!analysis_started_before && stages.size() > stages_at_metadata,
            "metadata delivered before any analysis stage ran");
    require(early_json == engine::metadata_report_json(b.metadata),
            "the early metadata equals the returned metadata");

    // 2. Same results as two separate calls.
    ocr_calls = 0;
    auto alone_metadata = engine::extract_metadata(scan, [&] {
        auto m = metadata;
        m.models = fake_models;
        return m;
    }());
    auto alone_analysis = engine::analyze(scan, analysis);
    require(alone_analysis && alone_metadata, "separate calls");
    require(ocr_calls == 8, "separate calls OCR the pages twice (the cost being removed)");
    require(engine::metadata_report_json(b.metadata) ==
                engine::metadata_report_json(alone_metadata.value()),
            "metadata report identical to a separate extract_metadata()");
    require(conclusions(*b.analysis) == conclusions(alone_analysis.value()),
            "analysis conclusions identical to a separate analyze()");

    // 3. One input: both reports describe the same bytes.
    require(b.analysis->input.sha256 == b.metadata.input.sha256 &&
                b.analysis->input.page_count == b.metadata.input.page_count,
            "both stages read the same input");

    // 4. Different acquisition settings: no reuse, pages are read again.
    ocr_calls = 0;
    auto other_dpi = metadata;
    other_dpi.raster.dpi = 200;
    auto split = engine::analyze_book(scan, analysis, other_dpi);
    require(split && split.value().pages_reused == 0, "no reuse across different DPI");
    require(ocr_calls == 8 && split.value().analysis->ocr_attempts_used == 4,
            "the analysis OCR'd its pages under its own settings");

    // 5. Cancellation before start: both reports say so; nothing is reused.
    std::atomic_bool cancel{true};
    auto cancelled = engine::analyze_book(scan, analysis, metadata, RunControl{&cancel});
    require(cancelled && cancelled.value().analysis &&
                cancelled.value().analysis->outcome == engine::AnalysisOutcome::Cancelled &&
                cancelled.value().metadata.cancelled && cancelled.value().pages_reused == 0,
            "cancellation reaches both stages without reusing cancelled pages");

    // 6. One run-wide OCR budget (PR #4 review): analysis.limits.ocr_budget
    //    caps both stages together; metadata may use at most its own budget
    //    of it, the analysis gets the rest; cache hits are free.
    ocr_calls = 0;
    auto tight = analysis;
    tight.limits.ocr_budget = 1;
    auto budget = engine::analyze_book(scan, tight, metadata);
    require(budget && budget.value().analysis, "tight budget run");
    const auto& t = budget.value();
    require(ocr_calls == 1, "a run-wide budget of 1 allows exactly 1 OCR attempt in total (got " +
                                std::to_string(ocr_calls.load()) + ")");
    require(t.metadata.ocr_attempts_used == 1 && t.analysis->ocr_attempts_used == 0 &&
                t.metadata.ocr_budget == 1 && t.analysis->ocr_budget == 0,
            "metadata spent the cap; the analysis's effective allowance is what was left (0)");
    require(t.pages_reused == 1, "the OCR'd page is reused; budget-skipped pages are not");
    //    With room left under the cap, the analysis may use it.
    ocr_calls = 0;
    auto roomy = analysis;
    roomy.limits.ocr_budget = 6;
    auto shared = engine::analyze_book(scan, roomy, other_dpi);
    require(shared && ocr_calls == 6 && shared.value().analysis->ocr_attempts_used == 2 &&
                shared.value().analysis->ocr_budget == 2,
            "the analysis gets the 2 attempts the metadata left under the cap of 6");

    // 6b. Cancellation DURING the second stage (the analysis): the metadata
    //     stays complete and delivered; the analysis says cancelled (the CLI
    //     maps this to exit 4).
    ocr_calls = 0;
    std::atomic_bool mid{false};
    cancel_flag = &mid;
    cancel_on_call = 5;  // Calls 1-4: metadata (DPI 200); call 5: first analysis page.
    bool delivered = false;
    auto during = engine::analyze_book(scan, analysis, other_dpi, RunControl{&mid}, {},
                                       [&](const engine::MetadataReport&) { delivered = true; });
    cancel_flag = nullptr;
    cancel_on_call = 0;
    require(during && delivered && !during.value().metadata.cancelled &&
                during.value().analysis &&
                during.value().analysis->outcome == engine::AnalysisOutcome::Cancelled,
            "cancellation during the analysis: metadata complete and delivered, analysis cancelled");
    require(ocr_calls == 5, "no OCR after the cancellation point");

    // 7. The metadata survives a failed analysis (PR #4 consumer review). An
    //    invalid numbering section passes the upfront checks but is rejected
    //    by S4 after the pages were read and the TOC parsed.
    ocr_calls = 0;
    auto failing = analysis;
    mapping::NumberingSection bad;
    bad.id = "body";
    bad.first = 0;
    bad.end = 100000;  // Beyond the document.
    bad.style = parsing::NumberingStyle::Decimal;
    bad.origin = "test";
    failing.sections = std::vector<mapping::NumberingSection>{bad};
    bool failed_delivered = false;
    auto failed = engine::analyze_book(fixtures / "boundary.pdf", failing, metadata, {}, {},
                                       [&](const engine::MetadataReport&) { failed_delivered = true; });
    require(static_cast<bool>(failed), "an analysis error does not fail the whole call");
    require(failed.value().analysis_error && !failed.value().analysis &&
                failed.value().analysis_error->code == ErrorCode::InvalidArgument,
            "the analysis error is reported separately");
    require(failed_delivered && !failed.value().metadata.searched_pages.empty(),
            "the metadata was delivered and returned despite the analysis error");

    // 8. Text-layer PDF: pages are reused too (no OCR involved at all).
    ocr_calls = 0;
    auto native = engine::analyze_book(fixtures / "boundary.pdf", analysis, metadata);
    require(native && native.value().pages_reused > 0 && ocr_calls == 0,
            "native pages reused, no OCR");
    auto native_alone = engine::extract_metadata(fixtures / "boundary.pdf", metadata);
    auto native_analysis = engine::analyze(fixtures / "boundary.pdf", analysis);
    require(native_alone && native_analysis &&
                engine::metadata_report_json(native.value().metadata) ==
                    engine::metadata_report_json(native_alone.value()) &&
                conclusions(*native.value().analysis) == conclusions(native_analysis.value()),
            "native results identical to separate calls");

    std::cout << "engine book (page reuse) tests passed\n";
    return 0;
}
