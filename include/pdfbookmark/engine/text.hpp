#pragma once

// Engine text facade: one persistent S1 session serving the CLI `text`
// operation and other clients. Engine owns the run-wide OCR budget and the
// JSON encoding; S1 owns acquisition policy.

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::engine {

struct TextOptions {
    text::AcquisitionMode mode = text::AcquisitionMode::Auto;
    text::RasterLimits raster;
    std::optional<text::ModelResources> models;  // Absent: OCR unavailable.
    int ocr_threads = 0;  // OCR CPU threads; 0 = automatic (text::OpenOptions)
    std::size_t ocr_budget = 64;   // Run-wide OCR attempts across batches.
    std::size_t batch_pages = 8;   // Pages per S1 acquire() call.
};

struct TextProgress {
    std::size_t pages_done = 0;
    std::size_t pages_total = 0;
};
using TextProgressCallback = std::function<void(const TextProgress&)>;

enum class TextStatus {
    Complete,   // Every page Ok or NoTextFound.
    Partial,    // Some page Degraded or Failed (results still returned).
    Cancelled   // Cancellation left some pages unprocessed.
};

struct TextReport {
    InputIdentity input;
    std::vector<text::PageAcquisition> pages;  // Request order.
    // Distinct S1 configuration identities, and for each page the index of
    // the configuration that produced it (per-call OCR allowance differs).
    std::vector<std::string> configurations;
    std::vector<std::size_t> page_configuration;
    std::string acquisition_policy_id;
    std::string model_identity;
    std::size_t ocr_budget = 0;
    std::size_t ocr_attempts_used = 0;
    TextStatus status = TextStatus::Complete;
    std::vector<std::string> diagnostics;
};

// `pages`: zero-based physical indices in caller order; nullopt means every
// page of the document. Duplicates and out-of-range indices are rejected
// before any acquisition.
Result<TextReport> extract_text(
    const std::filesystem::path& input,
    const std::optional<std::vector<PageIndex>>& pages,
    const TextOptions& options = {},
    const RunControl& control = {},
    const TextProgressCallback& progress = {});

// Text JSON, schema_version 1, page_index_base 0 (docs/handoffs/ENGINE_HANDOFF.md).
std::string text_report_json(const TextReport& report);

}  // namespace pdfbookmark::engine
