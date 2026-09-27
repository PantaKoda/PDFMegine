#include <pdfbookmark/engine/book.hpp>

#include "ledger.hpp"
#include "runs.hpp"

#include <algorithm>

namespace pdfbookmark::engine {

Result<BookReport> analyze_book(const std::filesystem::path& input,
                                const AnalysisOptions& analysis,
                                const MetadataRunOptions& metadata,
                                const RunControl& control,
                                const AnalysisProgressCallback& progress) {
    if (auto error = detail::check_analysis_options(analysis)) return *error;
    if (auto error = detail::check_metadata_options(metadata)) return *error;
    text::OpenOptions open;
    open.ocr_threads = analysis.ocr_threads;
    open.ocr_models = analysis.models;
    auto opened = text::TextAcquisition{}.open(input, open);
    if (!opened) return opened.error();
    auto document = opened.take();

    // Analysis first: it reads the most pages (the TOC search), so the
    // metadata stage (front matter, at most 30 pages) is usually served
    // entirely from the cache.
    detail::PageCache cache;
    auto analysed = detail::run_analysis(document, analysis, control, progress, &cache);
    if (!analysed) return analysed.error();
    const std::size_t before = cache.reused();
    // One run-wide OCR cap: analysis.limits.ocr_budget. The metadata stage
    // gets what the analysis left, further limited by its own budget; cache
    // hits cost nothing (PR #4 review).
    MetadataRunOptions stage = metadata;
    const std::size_t used = analysed.value().ocr_attempts_used;
    const std::size_t left = analysis.limits.ocr_budget > used ? analysis.limits.ocr_budget - used : 0;
    stage.ocr_budget = std::min(metadata.ocr_budget, left);
    auto described = detail::run_metadata(document, stage, control, &cache, progress);
    if (!described) return described.error();

    BookReport book;
    book.analysis = analysed.take();
    book.metadata = described.take();
    book.pages_reused = cache.reused() - before;
    return book;
}

}  // namespace pdfbookmark::engine
