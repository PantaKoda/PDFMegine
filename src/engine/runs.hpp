#pragma once

// Engine-private pipelines that run on an already open S1 session, so one
// session (and one PageCache) can serve several stages (issue #3).

#include <pdfbookmark/engine/analysis.hpp>
#include <pdfbookmark/engine/metadata.hpp>

#include "ledger.hpp"

namespace pdfbookmark::engine::detail {

std::optional<Error> check_analysis_options(const AnalysisOptions& options);
std::optional<Error> check_metadata_options(const MetadataRunOptions& options);

// `cache` may be null (no reuse). Options must have passed the checks above.
Result<AnalysisReport> run_analysis(text::TextDocument& document,
                                    const AnalysisOptions& options,
                                    const RunControl& control,
                                    const AnalysisProgressCallback& progress,
                                    PageCache* cache);

// `progress` (optional) receives stage "metadata" after each batch.
Result<MetadataReport> run_metadata(text::TextDocument& document,
                                    const MetadataRunOptions& options,
                                    const RunControl& control,
                                    PageCache* cache,
                                    const AnalysisProgressCallback& progress = {});

}  // namespace pdfbookmark::engine::detail
