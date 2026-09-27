#include <pdfbookmark/engine/book.hpp>

#include "ledger.hpp"
#include "runs.hpp"

#include <algorithm>

namespace pdfbookmark::engine {

Result<BookReport> analyze_book(const std::filesystem::path& input,
                                const AnalysisOptions& analysis,
                                const MetadataRunOptions& metadata,
                                const RunControl& control,
                                const AnalysisProgressCallback& progress,
                                const MetadataCallback& on_metadata) {
    if (auto error = detail::check_analysis_options(analysis)) return *error;
    if (auto error = detail::check_metadata_options(metadata)) return *error;
    text::OpenOptions open;
    open.ocr_threads = analysis.ocr_threads;
    open.ocr_models = analysis.models;
    auto opened = text::TextAcquisition{}.open(input, open);
    if (!opened) return opened.error();
    auto document = opened.take();

    // Metadata first (PR #4 consumer review): clients publish the title
    // early and keep it when the TOC analysis fails. The analysis then
    // reuses those pages from the cache, so no page is OCR'd twice.
    // One run-wide OCR cap, analysis.limits.ocr_budget; cache hits are free.
    const std::size_t cap = analysis.limits.ocr_budget;
    detail::PageCache cache;
    MetadataRunOptions metadata_stage = metadata;
    metadata_stage.ocr_budget = std::min(metadata.ocr_budget, cap);
    auto described = detail::run_metadata(document, metadata_stage, control, &cache, progress);
    if (!described) return described.error();
    if (on_metadata) on_metadata(described.value());

    AnalysisOptions analysis_stage = analysis;
    const std::size_t used = described.value().ocr_attempts_used;
    analysis_stage.limits.ocr_budget = cap > used ? cap - used : 0;
    const std::size_t before = cache.reused();
    auto analysed = detail::run_analysis(document, analysis_stage, control, progress, &cache);

    BookReport book;
    book.metadata = described.take();
    book.pages_reused = cache.reused() - before;
    if (analysed)
        book.analysis = analysed.take();
    else
        book.analysis_error = analysed.error();
    return book;
}

}  // namespace pdfbookmark::engine
