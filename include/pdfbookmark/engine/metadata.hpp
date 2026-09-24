#pragma once

// Engine facade for S6 Document Metadata Extraction: decides which pages to
// acquire (first 10, then 10 more at a time up to 30 while fields are missing
// or conflicting) and enforces budgets. Never modifies the PDF.

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/metadata/metadata.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::engine {

struct MetadataRunOptions {
    text::AcquisitionMode mode = text::AcquisitionMode::Auto;
    text::RasterLimits raster;
    std::optional<text::ModelResources> models;
    std::size_t initial_pages = 10;
    std::size_t batch_pages = 10;
    std::size_t max_pages = 30;    // "Not found" means not in these pages.
    std::size_t ocr_budget = 16;   // Run-wide OCR attempts.
    metadata::DocumentHints hints;
    metadata::MetadataOptions metadata;
};

struct MetadataReport {
    InputIdentity input;
    metadata::MetadataResult result;
    std::vector<PageIndex> searched_pages;
    bool search_covered_document = false;
    std::vector<std::string> stop_reasons;
    std::string acquisition_policy_id;
    std::string model_identity;
    std::size_t ocr_budget = 0;
    std::size_t ocr_attempts_used = 0;
    bool cancelled = false;
};

Result<MetadataReport> extract_metadata(const std::filesystem::path& input,
                                        const MetadataRunOptions& options = {},
                                        const RunControl& control = {});

// JSON (schema_version 1, kind "pdfbookmark.metadata", page_index_base 0).
std::string metadata_report_json(const MetadataReport& report);

}  // namespace pdfbookmark::engine
