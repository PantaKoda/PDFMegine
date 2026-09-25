#pragma once

// Engine analysis: coordinates S1-S4 under finite budgets and assembles an
// S5 BookmarkPlan. Never modifies the PDF and never reads existing outlines.

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/detection/detection.hpp>
#include <pdfbookmark/mapping/mapping.hpp>
#include <pdfbookmark/parsing/parsing.hpp>
#include <pdfbookmark/text/acquisition.hpp>
#include <pdfbookmark/writer/plan.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::engine {

// Finite, configurable defaults (see docs/IMPLEMENTATION_DECISIONS.md E-05).
struct SearchLimits {
    std::size_t initial_pages = 40;       // First search batch.
    std::size_t batch_pages = 20;         // Each later search batch.
    std::size_t max_search_pages = 200;   // Contiguous search from index 0.
    std::size_t max_evidence_pages = 500; // Extra pages for S4 requests
                                          // (target confirmation per entry).
    std::size_t max_mapping_rounds = 6;   // S4 calls.
    std::size_t ocr_budget = 64;          // Run-wide OCR attempts.
};

struct PlanPolicy {
    // Omit unresolved entries (recorded) and promote resolved descendants of
    // known ancestry to the nearest retained ancestor, or to top level when
    // none is retained (each promotion recorded). Unknown ancestry is never
    // repaired by this rule. Also accepts an incomplete parse, which stays
    // visible in the report.
    bool allow_partial = false;
    // Treat unknown hierarchy as top level (recorded) instead of blocking.
    bool flat_outline_for_unknown_hierarchy = false;
    // Bookmark title presentation (E-18). AsPrinted keeps TOC text exactly;
    // Chapter turns top-level "1 Title" into "Chapter 1: Title" and clear
    // appendices "A Title" into "Appendix A: Title". Evidence is unchanged.
    enum class TitleStyle { AsPrinted, Chapter };
    TitleStyle title_style = TitleStyle::AsPrinted;
};

struct AnalysisOptions {
    text::AcquisitionMode mode = text::AcquisitionMode::Auto;
    text::RasterLimits raster;
    std::optional<text::ModelResources> models;
    int ocr_threads = 0;  // OCR CPU threads; 0 = automatic (text::OpenOptions)
    SearchLimits limits;
    detection::DetectionOptions detection;
    parsing::ParsingOptions parsing;
    // Engine lets S4 request every target it needs confirmed in a round
    // (S4's own default of 8 suits small callers, not whole books).
    mapping::MappingOptions mapping = [] {
        mapping::MappingOptions o;
        o.max_requests = 500;
        return o;
    }();
    std::optional<std::string> candidate_id;  // Explicit candidate selection.
    // Automatic choice needs the best score to exceed the runner-up by this
    // ratio; otherwise explicit selection is required.
    double candidate_tie_ratio = 0.9;
    // Replaces the default single decimal body section when supplied.
    std::optional<std::vector<mapping::NumberingSection>> sections;
    std::vector<mapping::EntrySection> entry_sections;
    std::vector<mapping::MappingOverride> overrides;
    PlanPolicy plan;
};

enum class AnalysisOutcome {
    PlanReady,           // A validated writable plan is available.
    AnalysisPartial,     // TOC found; plan blocked or knowingly partial.
    NoTocFoundInSearch,  // Searched pages assessed; no candidate. Not proof.
    SearchIncomplete,    // No candidate, and searched pages were unassessable.
    Cancelled
    // Failures are returned as Result errors, never as an outcome.
};

struct CandidateChoice {
    std::optional<std::string> chosen_id;
    bool explicit_selection = false;
    std::string reason;
    std::vector<std::string> alternatives;  // Other candidate IDs, by score.
};

struct PlanAssembly {
    bool ready = false;
    std::vector<std::string> blockers;  // Why no ready plan was produced.
    std::vector<std::string> choices;   // Policy decisions applied.
    std::optional<writer::PlanValidation> validation;
    // Present when nodes could be assembled; writable only when `ready`.
    std::optional<writer::BookmarkPlan> plan;
};

struct AnalysisProgress {
    std::string stage;
    std::size_t pages_acquired = 0;
};
using AnalysisProgressCallback = std::function<void(const AnalysisProgress&)>;

struct AnalysisReport {
    InputIdentity input;
    AnalysisOutcome outcome = AnalysisOutcome::Cancelled;  // Set by analyze().
    std::vector<std::string> stop_reasons;
    std::vector<std::string> diagnostics;
    // Every acquired page in physical order, with its S1 configuration index.
    std::vector<text::PageAcquisition> pages;
    std::vector<std::size_t> page_configuration;
    std::vector<PageIndex> search_pages;    // Contiguous search from 0.
    std::vector<PageIndex> evidence_pages;  // Acquired for S4 requests.
    bool search_covered_document = false;
    std::string acquisition_policy_id;
    std::string model_identity;
    std::vector<std::string> configurations;
    std::size_t ocr_budget = 0;
    std::size_t ocr_attempts_used = 0;
    std::optional<detection::DetectionResult> detection;
    CandidateChoice candidate;
    std::optional<parsing::ParsedToc> parsed;
    std::vector<mapping::NumberingSection> sections;  // As supplied to S4.
    std::optional<mapping::MappingResult> mapping;
    std::vector<mapping::EvidenceRequest> unfulfilled_requests;
    std::size_t mapping_rounds = 0;
    PlanAssembly plan;
};

Result<AnalysisReport> analyze(const std::filesystem::path& input,
                               const AnalysisOptions& options = {},
                               const RunControl& control = {},
                               const AnalysisProgressCallback& progress = {});

// Analysis JSON (schema_version 1). Includes acquired page evidence so every
// source reference (page, revision, region) is interpretable.
std::string analysis_report_json(const AnalysisReport& report,
                                 const AnalysisOptions& options);

// Plan JSON codec for the S5-owned schema (schema_version 1,
// page_index_base 0, existing_outline_policy "replace_in_copy"). Decoding is
// strict: unknown or missing members, wrong types, and out-of-range numbers
// are errors. Structural validation remains writer::validate().
std::string plan_json(const writer::BookmarkPlan& plan);
Result<writer::BookmarkPlan> parse_plan_json(const std::string& text);

const char* outcome_name(AnalysisOutcome outcome);

}  // namespace pdfbookmark::engine
