#pragma once

#include <pdfbookmark/core/types.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::text {

enum class Source { EmbeddedPdf, Ocr };
enum class Granularity { PdfTextRun, OcrLine };
enum class ReadingOrder { Estimated, Uncertain };
enum class Readability { Acceptable, Suspect, Unknown };
enum class Coverage { NoOmissionIndicated, SuspectedIncomplete, Unknown };
enum class Outcome { Ok, Degraded, NoTextFound, Failed, Cancelled };
enum class AttemptState { Completed, Failed, Skipped };
enum class AcquisitionMode { Auto, EmbeddedOnly, OcrOnly };

struct SourceReference {
    PageIndex page_index = 0;
    std::uint64_t revision = 0;
    std::uint32_t region_id = 0;
    std::optional<std::size_t> utf8_begin;
    std::optional<std::size_t> utf8_end;
};

struct TextRegion {
    std::uint32_t id = 0;
    std::string text;  // Valid UTF-8; preserves source punctuation and spaces.
    std::optional<Quad> quad;
    Granularity granularity = Granularity::PdfTextRun;
    std::optional<float> ocr_confidence;
    std::optional<std::int32_t> pdf_char_begin;
    std::optional<std::int32_t> pdf_char_end;
};

struct PageGeometry {
    double width_points = 0;
    double height_points = 0;
    int rotation_quarters = 0;
    // Null when the pinned PDFium API cannot establish physical scaling.
    std::optional<double> user_unit;
    Affine pdf_to_canonical;
    Affine canonical_to_pdf;
    std::optional<Affine> canonical_to_raster;
    std::optional<Affine> raster_to_canonical;
    int raster_width = 0;
    int raster_height = 0;
    double effective_dpi_x = 0;
    double effective_dpi_y = 0;
};

struct PageContent {
    PageIndex page_index = 0;
    std::uint64_t revision = 0;
    Source source = Source::EmbeddedPdf;
    PageGeometry geometry;
    std::vector<TextRegion> regions;
    ReadingOrder reading_order = ReadingOrder::Estimated;
    // Joined in backend reading order with '\n' between regions.
    std::string flat_text() const;
};

struct TextAssessment {
    Readability readability = Readability::Unknown;
    Coverage coverage = Coverage::Unknown;
    std::string policy_id;
    std::size_t unicode_scalars = 0;
    std::size_t visible_scalars = 0;
    std::size_t replacement_count = 0;
    std::size_t control_count = 0;
    std::size_t image_object_count = 0;
    std::vector<std::string> reasons;
};

struct AcquisitionAttempt {
    Source source = Source::EmbeddedPdf;
    AttemptState state = AttemptState::Skipped;
    std::string reason;
};

struct PageAcquisition {
    PageIndex page_index = 0;
    Outcome outcome = Outcome::Failed;
    std::optional<PageContent> selected;
    TextAssessment assessment;
    std::vector<AcquisitionAttempt> attempts;
    std::vector<std::string> reasons;
};

struct AcquisitionBatch {
    std::vector<PageAcquisition> pages;  // Caller request order.
    bool complete = true;
    std::string policy_id;
    std::string configuration_id;
    std::string model_identity;
};

struct ModelResources {
    std::filesystem::path detector;
    std::filesystem::path recognizer;
    std::filesystem::path charset;
};

struct OpenOptions {
    std::size_t max_pdf_bytes = 512ULL * 1024 * 1024;
    std::optional<ModelResources> ocr_models;
    // CPU threads for OCR inference. 0 = automatic: half the logical
    // processors, at least 1 and at most 8 (speed levels off beyond 8 and
    // the rest stay free for the client's UI). Results are identical except
    // for OCR confidence differences around 1e-6.
    int ocr_threads = 0;
};

// The OCR thread count `requested` resolves to (0 = automatic).
int resolve_ocr_threads(int requested) noexcept;

struct RasterLimits {
    int dpi = 300;
    std::uint64_t max_pixels = 80'000'000;
    std::uint64_t max_bytes = 320'000'000;
    int max_dimension = 20'000;
};

struct AcquisitionOptions {
    AcquisitionMode mode = AcquisitionMode::Auto;
    RasterLimits raster;
    std::size_t max_ocr_attempts = 8;  // Per acquire() call.
    bool collect_diagnostics = true;
};

enum class FactAvailability { Present, Absent, Unsupported, Failed };

template <typename T>
struct Fact {
    FactAvailability availability = FactAvailability::Unsupported;
    std::optional<T> value;
    std::string reason;
};

struct PdfPageFacts {
    PageIndex page_index = 0;
    PageGeometry geometry;
    Fact<std::string> viewer_label;
    Fact<std::vector<PageIndex>> local_link_destinations;
};

struct PdfFactsRequest {
    std::vector<PageIndex> pages;  // Empty requests only document facts.
    bool viewer_labels = true;
    bool local_links = false;
};

struct PdfFactsResult {
    InputIdentity input;
    std::vector<PdfPageFacts> pages;
};

class TextDocument {
public:
    ~TextDocument();
    TextDocument(TextDocument&&) noexcept;
    TextDocument& operator=(TextDocument&&) noexcept;
    TextDocument(const TextDocument&) = delete;
    TextDocument& operator=(const TextDocument&) = delete;

    PageCount page_count() const noexcept;
    const InputIdentity& identity() const noexcept;
    Result<AcquisitionBatch> acquire(const std::vector<PageIndex>& pages,
                                     const AcquisitionOptions& options = {},
                                     const RunControl& control = {});
    Result<PdfFactsResult> read_facts(const PdfFactsRequest& request);

private:
    friend class TextAcquisition;
    struct Impl;
    explicit TextDocument(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

class TextAcquisition {
public:
    Result<TextDocument> open(const std::filesystem::path& path,
                              const OpenOptions& options = {}) const;
};

}  // namespace pdfbookmark::text
