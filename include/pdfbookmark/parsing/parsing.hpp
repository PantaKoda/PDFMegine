#pragma once

#include <pdfbookmark/detection/detection.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::parsing {

enum class NumberingStyle { Decimal, Roman, PrefixedDecimal, Unknown };
enum class HierarchyKind { Root, KnownParent, Unknown };
enum class ParseCompleteness { Complete, Incomplete };

struct PrintedReference {
    std::string literal;  // Exact token as printed; never a physical PDF index.
    NumberingStyle numbering = NumberingStyle::Unknown;
    bool is_range = false;
    std::optional<std::uint32_t> ordinal;
    std::optional<std::uint32_t> range_end;
    std::string prefix;
    bool uncertain = false;
    std::vector<std::string> reasons;
};

struct Hierarchy {
    HierarchyKind kind = HierarchyKind::Unknown;
    std::optional<std::string> parent_id;
    std::vector<std::string> reasons;
};

struct TocEntry {
    std::string id;  // Source/order identity, independent of title uniqueness.
    std::string title;
    std::size_t order = 0;
    std::optional<PrintedReference> printed_reference;
    Hierarchy hierarchy;
    std::vector<text::SourceReference> sources;
    std::vector<std::string> diagnostics;
};

struct UnparsedFragment {
    std::string text;
    std::vector<text::SourceReference> sources;
    std::string reason;
};

struct ParsingOptions {
    double row_y_tolerance_points = 5.0;
    double column_separation_fraction = 0.25;
    double wrap_line_gap_factor = 1.8;
    double root_indent_tolerance_points = 8.0;
    double child_indent_min_points = 16.0;
};

struct ParsedToc {
    std::string candidate_id;
    std::string policy_id;
    std::vector<TocEntry> entries;
    std::vector<UnparsedFragment> unparsed;
    std::vector<PageIndex> missing_pages;
    ParseCompleteness completeness = ParseCompleteness::Incomplete;
    detection::BoundaryState start = detection::BoundaryState::Unknown;
    detection::BoundaryState end = detection::BoundaryState::Unknown;
    std::vector<std::string> diagnostics;
};

// Pure computation on supplied S1 values and an S2-compatible candidate.
// The candidate may be caller-built; no Detection runtime is required.
Result<ParsedToc> parse(
    const detection::TocCandidate& candidate,
    const std::vector<text::PageAcquisition>& supplied_pages,
    const ParsingOptions& options = {});

}  // namespace pdfbookmark::parsing
