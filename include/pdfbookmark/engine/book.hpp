#pragma once

// Engine: TOC analysis and metadata extraction of one PDF in one run, sharing
// one S1 session so pages are acquired (and OCR'd) once (issue #3).

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/engine/analysis.hpp>
#include <pdfbookmark/engine/metadata.hpp>

#include <cstddef>
#include <filesystem>

namespace pdfbookmark::engine {

struct BookReport {
    AnalysisReport analysis;   // Same as analyze() would return.
    MetadataReport metadata;   // Same as extract_metadata() would return.
    // Metadata pages served from pages the analysis already acquired, instead
    // of being read (and possibly OCR'd) again.
    std::size_t pages_reused = 0;
};

// Runs analyze() and then extract_metadata() on ONE session of `input`.
// - Session settings come from `analysis`: `models` and `ocr_threads`
//   (`metadata.models` and `metadata.ocr_threads` are not used).
// - A page is reused only when both stages use the same `mode` and `raster`
//   (the defaults do). Pages that were cancelled, or whose OCR was skipped for
//   budget, are read again. Reused pages cost no OCR budget.
// - OCR budget: `analysis.limits.ocr_budget` caps the WHOLE run. The metadata
//   stage may use only what the analysis left, and at most
//   `metadata.ocr_budget` of it; the metadata report's `ocr_budget` is that
//   effective allowance.
// - Both reports describe the same input bytes (one immutable session copy).
// With enough OCR budget, results equal separate analyze() and
// extract_metadata() calls; only the OCR attempt counts differ, because
// reused pages are not OCR'd again. (Separate calls each get a full budget,
// so under a tight budget they may OCR more pages than this run allows.)
Result<BookReport> analyze_book(const std::filesystem::path& input,
                                const AnalysisOptions& analysis = {},
                                const MetadataRunOptions& metadata = {},
                                const RunControl& control = {},
                                const AnalysisProgressCallback& progress = {});

}  // namespace pdfbookmark::engine
