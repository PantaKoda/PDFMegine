#pragma once

#include <pdfbookmark/parsing/parsing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::mapping {

// Physical bounds are zero-based and half-open. Sections are supplied
// numbering evidence, not automatically guessed document segmentation.
struct NumberingSection {
    std::string id;
    PageIndex first = 0;
    PageIndex end = 0;
    parsing::NumberingStyle style = parsing::NumberingStyle::Unknown;
    std::string prefix;
    std::string origin;
    bool viewer_labels_match_printed = false;
};

struct EntrySection {
    std::string entry_id;
    std::string section_id;
    std::string origin;
};

// Only a caller-established entry/region association can make an S1 page-level
// local link relevant to a particular entry. The page fact is checked too.
struct AssociatedLocalLink {
    std::string entry_id;
    text::SourceReference source;
    PageIndex destination = 0;
};

struct DocumentEvidence {
    InputIdentity input;
    std::vector<text::PageAcquisition> pages;
    std::vector<text::PdfPageFacts> facts;
    std::vector<NumberingSection> sections;
    std::vector<EntrySection> entry_sections;
    std::vector<AssociatedLocalLink> associated_links;
    std::vector<std::string> limitations;
};

enum class OverrideKind { EntryDestination, SectionOffset };
struct MappingOverride {
    OverrideKind kind = OverrideKind::EntryDestination;
    std::string target_id;  // Entry ID or numbering-section ID.
    std::optional<PageIndex> destination;
    std::optional<std::int64_t> offset;
    std::string origin;  // Caller-supplied manual-decision provenance.
};

enum class MappingStatus { Resolved, Ambiguous, Unresolved };
enum class ResolutionMethod {
    ManualEntry, ManualOffset, AssociatedLocalLink, ViewerLabel,
    InferredOffset, HeadingMatch
};
struct DestinationAlternative {
    PageIndex pdf_page_index = 0;
    ResolutionMethod method = ResolutionMethod::InferredOffset;
    std::string reason;
    std::vector<text::SourceReference> sources;
};
struct EntryMapping {
    std::string entry_id;
    MappingStatus status = MappingStatus::Unresolved;
    std::optional<PageIndex> pdf_page_index;
    std::optional<ResolutionMethod> method;
    std::optional<std::string> section_id;
    std::vector<DestinationAlternative> alternatives;
    std::vector<text::SourceReference> supporting_sources;
    std::vector<PageIndex> supporting_pages;
    std::vector<std::string> reasons;
};

enum class ObservationKind {
    PrintedFooter, ViewerLabel, HeadingMatch,
    PrintedHeader  // Page number in the top band (running header).
};
struct MappingObservation {
    ObservationKind kind = ObservationKind::PrintedFooter;
    PageIndex page_index = 0;
    std::string literal;
    parsing::NumberingStyle style = parsing::NumberingStyle::Unknown;
    std::optional<std::uint32_t> ordinal;
    std::string prefix;
    std::optional<text::SourceReference> source;
    std::optional<std::string> entry_id;
};

struct EvidenceRequest {
    std::string id;  // Stable for the same evidence and request purpose.
    PageIndex first = 0;
    PageIndex end = 0;  // Exclusive.
    std::string purpose;
    int priority = 0;
};

struct MappingOptions {
    double footer_band_fraction = 0.15;
    double header_band_fraction = 0.10;  // Printed numbers in running headers.
    double heading_band_fraction = 0.25;
    std::size_t max_requests = 8;
    std::size_t max_request_pages = 3;
    bool require_target_confirmation = true;
};

struct MappingResult {
    std::vector<EntryMapping> entries;  // Input order.
    std::vector<MappingObservation> observations;
    std::vector<EvidenceRequest> requests;
    std::vector<std::string> diagnostics;
    std::string policy_id;
};

// Pure computation. No PDF, OCR, parser runtime, or page acquisition.
Result<MappingResult> map(
    const std::vector<parsing::TocEntry>& entries,
    const DocumentEvidence& evidence,
    const std::vector<MappingOverride>& overrides = {},
    const MappingOptions& options = {});

}  // namespace pdfbookmark::mapping
