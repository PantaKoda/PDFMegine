// Stable public API test: uses ONLY <pdfbookmark/pdfbookmark.hpp> and the
// unified pdfbookmark::pdfbookmark library (as the CLI or a Qt app would).
// Usage: pdfbookmark_api_tests <engine fixtures dir> <front_matter.pdf> <work dir>
#include <pdfbookmark/pdfbookmark.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main(int argc, char** argv) {
    require(argc == 4, "usage: fixtures-dir front_matter.pdf work-dir");
    const fs::path fixtures = fs::u8path(argv[1]);
    const fs::path front = fs::u8path(argv[2]);
    const fs::path work = fs::u8path(argv[3]);
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work);

    // Library info: the loaded library matches the headers compiled against.
    require(std::strcmp(pdfbookmark::version(), PDFBOOKMARK_VERSION_STRING) == 0,
            "runtime version equals header version");
    require(!pdfbookmark::models_in(work / "no-models"), "missing models are reported");

    // Text.
    pdfbookmark::TextOptions text_options;
    text_options.mode = pdfbookmark::AcquisitionMode::EmbeddedOnly;
    const auto text = pdfbookmark::extract_text(
        fixtures / "boundary.pdf", std::vector<pdfbookmark::PageIndex>{39}, text_options);
    require(text && text.value().pages.size() == 1 &&
                text.value().pages[0].outcome == pdfbookmark::PageOutcome::Ok,
            "extract_text through the library");

    // Analysis -> plan -> JSON round trip -> apply.
    const fs::path input = fixtures / "boundary_outlined.pdf";
    std::size_t progress_calls = 0;
    const auto report = pdfbookmark::analyze(
        input, pdfbookmark::AnalysisOptions{}, pdfbookmark::RunControl{},
        [&](const pdfbookmark::AnalysisProgress&) { ++progress_calls; });
    require(report && report.value().outcome == pdfbookmark::AnalysisOutcome::PlanReady &&
                report.value().plan.plan && progress_calls > 0,
            "analyze returns a ready plan and reports progress");
    const auto& plan = *report.value().plan.plan;
    require(pdfbookmark::validate_plan(plan).valid, "validate_plan");
    const auto reloaded = pdfbookmark::plan_from_json(pdfbookmark::plan_to_json(plan));
    require(reloaded && reloaded.value().nodes.size() == plan.nodes.size() &&
                pdfbookmark::plan_to_json(reloaded.value()) == pdfbookmark::plan_to_json(plan),
            "plan JSON round trip");
    const auto identity = pdfbookmark::read_pdf_identity(input);
    require(identity && identity.value().sha256 == plan.input.sha256,
            "read_pdf_identity matches the plan's input");
    auto edited = reloaded.value();
    edited.nodes[0].title = "Edited \xE2\x80\x94 title";  // Client-side correction.
    const auto written = pdfbookmark::apply(input, work / "out.pdf", edited);
    require(written && written.value().committed &&
                written.value().verification.outline_items == plan.nodes.size(),
            "apply writes a verified copy with an edited title");
    const auto refused = pdfbookmark::apply(input, input, edited, pdfbookmark::ApplyOptions{true});
    require(!refused && refused.error().code == pdfbookmark::ErrorCode::InvalidArgument,
            "the input can never be the output");

    // Metadata.
    pdfbookmark::MetadataRunOptions meta_options;
    meta_options.mode = pdfbookmark::AcquisitionMode::EmbeddedOnly;
    const auto meta = pdfbookmark::extract_metadata(front, meta_options);
    require(meta && meta.value().result.title.value &&
                meta.value().result.title.value->title == "Parallel Worlds",
            "extract_metadata through the library");
    require(pdfbookmark::metadata_report_json(meta.value()).find("pdfbookmark.metadata") !=
                std::string::npos,
            "metadata JSON");

    // Cancellation.
    std::atomic_bool cancel{true};
    const auto cancelled = pdfbookmark::analyze(input, {}, pdfbookmark::RunControl{&cancel});
    require(cancelled && cancelled.value().outcome == pdfbookmark::AnalysisOutcome::Cancelled,
            "cooperative cancellation");

    // Errors are values, not exceptions.
    const auto missing = pdfbookmark::analyze(work / "missing.pdf");
    require(!missing && !missing.error().message.empty(), "missing input is an error value");

    std::cout << "pdfbookmark " << pdfbookmark::version() << " public API passed\n";
}
