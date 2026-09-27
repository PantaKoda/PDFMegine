#pragma once

/// @file pdfbookmark.hpp
/// Stable public API of the pdfbookmark library.
///
/// Include only this header. Link `pdfbookmark::pdfbookmark` (CMake package
/// `pdfbookmark`). Everything a client needs (the CLI, a Qt app, ...) is in
/// namespace `pdfbookmark`; see docs/API.md for the guide.
///
/// For C and other languages (Python, C#, Rust, ...) use the C API in
/// <pdfbookmark/pdfbookmark.h> instead: same library, plain C types, JSON
/// results, usable from any compiler.
///
/// ## Operations
/// | Function            | Does                                                   |
/// |---------------------|--------------------------------------------------------|
/// | extract_text()      | Positioned text of pages (native PDF text or OCR)      |
/// | analyze()           | Find the TOC, map entries to pages, build a plan       |
/// | apply()             | Write a NEW PDF with a plan's bookmarks                |
/// | extract_metadata()  | Title, contributors, edition, publication/copyright year |
/// | validate_plan(), plan_to_json(), plan_from_json(), read_pdf_identity()      |
/// | find_models(), models_in(), version()                                        |
///
/// ## Conventions
/// * Page indices in the API and in JSON are zero based (first page = 0).
/// * Text is UTF-8; paths are std::filesystem::path (build them from wide
///   strings on Windows to keep non-ASCII names).
/// * Every operation returns Result<T>: check `if (result)`, then use
///   `result.value()`; on failure `result.error()` has an ErrorCode and a
///   message. Expected per-page/per-entry problems are part of the value.
/// * Operations block; run them off a UI thread. Cancel cooperatively with a
///   RunControl pointing at a std::atomic_bool. Progress callbacks run on the
///   calling (worker) thread.
/// * The input PDF is never modified. Existing bookmarks are never used as
///   evidence; apply() replaces them in the NEW copy only.
/// * One PDF engine is shared process-wide; concurrent calls are serialized.
///
/// Advanced use: the subsystem headers (pdfbookmark/text, detection, parsing,
/// mapping, writer, metadata) remain available but may change between minor
/// versions; this header is the stable surface.

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/engine/analysis.hpp>
#include <pdfbookmark/engine/book.hpp>
#include <pdfbookmark/engine/apply.hpp>
#include <pdfbookmark/engine/metadata.hpp>
#include <pdfbookmark/engine/text.hpp>
#include <pdfbookmark/version.hpp>
#include <pdfbookmark/writer/plan.hpp>
#include <pdfbookmark/writer/writer.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace pdfbookmark {

// ------------------------------------------------------------ library info

/// Version of the loaded library ("MAJOR.MINOR.PATCH"). Compare with
/// PDFBOOKMARK_VERSION_STRING (the headers you compiled against).
const char* version() noexcept;

// ------------------------------------------------------------ OCR models

/// OCR model files (detector, recognizer, character set).
using ModelResources = text::ModelResources;

/// Models in `dir` laid out as dir/det/inference.onnx, dir/rec/inference.onnx
/// and dir/rec/charset.txt; nullopt if any file is missing.
std::optional<ModelResources> models_in(const std::filesystem::path& dir);

/// Default model search: the PDFBOOKMARK_MODELS environment variable, then
/// "models" next to the pdfbookmark library, then
/// "../share/pdfbookmark/models" relative to it (installed SDK layout).
/// Nullopt means OCR is unavailable; native-text PDFs still work.
std::optional<ModelResources> find_models();

// ------------------------------------------------------------ common types

using text::AcquisitionMode;  ///< Auto, EmbeddedOnly, OcrOnly.
using text::RasterLimits;     ///< OCR raster resolution and size limits.
using PageOutcome = text::Outcome;  ///< Ok, Degraded, NoTextFound, Failed, Cancelled.

// ------------------------------------------------------------ text

using engine::TextOptions;           ///< Mode, OCR models, budgets.
using engine::TextProgress;
using engine::TextProgressCallback;
using engine::TextStatus;            ///< Complete, Partial, Cancelled.
using engine::TextReport;            ///< Per-page text with positions.
using engine::extract_text;          ///< (pdf, pages or nullopt=all, options, control, progress)
using engine::text_report_json;      ///< TextReport as JSON (schema 1).

// ------------------------------------------------------------ analysis

using engine::SearchLimits;          ///< Search/evidence/OCR budgets.
using engine::PlanPolicy;            ///< allow_partial, flat outline, title style.
using engine::AnalysisOptions;
using engine::AnalysisOutcome;       ///< PlanReady, AnalysisPartial, NoTocFoundInSearch, ...
using engine::AnalysisProgress;
using engine::AnalysisProgressCallback;
using engine::AnalysisReport;        ///< Candidates, entries, mappings, plan + blockers.
using engine::analyze;               ///< (pdf, options, control, progress)
using engine::analysis_report_json;  ///< AnalysisReport as JSON (schema 1).
using engine::outcome_name;          ///< Stable string for an AnalysisOutcome.

// ------------------------------------------------------------ plans

using writer::BookmarkPlan;          ///< Ordered nodes, parent ids, page destinations.
using writer::BookmarkNode;          ///< id, parent_id, title (UTF-8), destination.
using writer::PlanValidation;        ///< valid + issues tied to node/field.

/// Structural checks of a plan (ids, parents, cycles, titles, page bounds).
inline PlanValidation validate_plan(const BookmarkPlan& plan) {
    return writer::validate(plan);
}
/// Plan as JSON (schema_version 1, page_index_base 0, "replace_in_copy").
inline std::string plan_to_json(const BookmarkPlan& plan) {
    return engine::plan_json(plan);
}
/// Strict JSON decoding; malformed plans are InvalidArgument errors.
inline Result<BookmarkPlan> plan_from_json(const std::string& json) {
    return engine::parse_plan_json(json);
}
/// SHA-256 and page count of a PDF, e.g. to bind a hand-made plan.
inline Result<InputIdentity> read_pdf_identity(const std::filesystem::path& pdf) {
    return writer::read_input_identity(pdf);
}

// ------------------------------------------------------------ writing

using engine::ApplyOptions;          ///< replace_existing_output (never the input).
using writer::WriteResult;           ///< committed, verification, diagnostics.
using engine::apply;                 ///< (pdf, output, plan, options, control)
using engine::apply_plan_file;       ///< (pdf, output, plan.json, options, control)

// ------------------------------------------------------------ metadata

using engine::MetadataRunOptions;    ///< Pages to search (10, up to 30), OCR, hints.
using engine::MetadataReport;        ///< Fields with status, evidence, alternatives.
using engine::extract_metadata;      ///< (pdf, options, control)
using engine::metadata_report_json;  ///< MetadataReport as JSON (schema 1).

// ------------------------------------------------------------ both at once

using engine::BookReport;            ///< metadata + analysis (or analysis_error) + pages_reused.
using engine::MetadataCallback;      ///< Receives the metadata before the TOC analysis starts.
using engine::analyze_book;          ///< (pdf, analysis options, metadata options, control, progress,
                                     ///< on_metadata): metadata first, one session, pages OCR'd once.

}  // namespace pdfbookmark
