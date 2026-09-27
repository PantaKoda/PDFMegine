#pragma once

// Engine: metadata extraction and TOC analysis of one PDF in one run,
// sharing one S1 session so pages are acquired (and OCR'd) once (issue #3).

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/engine/analysis.hpp>
#include <pdfbookmark/engine/metadata.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>

namespace pdfbookmark::engine {

struct BookReport {
    MetadataReport metadata;                 // Same as extract_metadata() would return.
    std::optional<AnalysisReport> analysis;  // Same as analyze(); absent if it failed,
    std::optional<Error> analysis_error;     // and then this says why.
    // Analysis pages served from pages the metadata stage already acquired,
    // instead of being read (and possibly OCR'd) again.
    std::size_t pages_reused = 0;
};

// Receives the metadata report as soon as it is ready, before the (much
// longer) TOC analysis starts. Called on the calling thread.
using MetadataCallback = std::function<void(const MetadataReport&)>;

// Runs extract_metadata() and then analyze() on ONE session of `input`.
// - Metadata first: a client can show the title within the metadata stage's
//   time (e.g. 10 front pages) via `on_metadata`, and the metadata survives a
//   failed analysis (`analysis_error`). Errors in opening the input or in the
//   metadata stage fail the whole call.
// - Session settings come from `analysis`: `models` and `ocr_threads`
//   (`metadata.models` and `metadata.ocr_threads` are not used).
// - A page is reused only when both stages use the same `mode` and `raster`
//   (the defaults do). Pages that were cancelled, or whose OCR was skipped for
//   budget, are read again. Reused pages cost no OCR budget. With the
//   defaults the metadata pages (at most 30) lie inside the analysis's first
//   40-page batch, so every page is OCR'd once.
// - OCR budget: `analysis.limits.ocr_budget` caps the WHOLE run. The metadata
//   stage may use at most `metadata.ocr_budget` of it; the analysis gets what
//   is left. Each report's `ocr_budget` is its stage's effective allowance.
// - Progress: `progress` receives stage "metadata" first, then the analysis
//   stages; `pages_acquired` counts pages of the CURRENT stage.
// - Both reports describe the same input bytes (one immutable session copy).
// With enough OCR budget, results equal separate extract_metadata() and
// analyze() calls; only the OCR attempt counts differ, because reused pages
// are not OCR'd again.
Result<BookReport> analyze_book(const std::filesystem::path& input,
                                const AnalysisOptions& analysis = {},
                                const MetadataRunOptions& metadata = {},
                                const RunControl& control = {},
                                const AnalysisProgressCallback& progress = {},
                                const MetadataCallback& on_metadata = {});

}  // namespace pdfbookmark::engine
