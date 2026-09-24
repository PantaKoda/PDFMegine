#pragma once

// S5-owned bookmark plan contract and structural validator. Backend free:
// usable without qpdf headers or linkage (target pdfbookmark::WriterPlan).

#include <pdfbookmark/core/types.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::writer {

inline constexpr int kPlanSchemaVersion = 1;
inline constexpr int kPageIndexBase = 0;

// Page-level local destination on a physical, zero-based PDF page.
struct PageDestination {
    PageIndex pdf_page_index = 0;
};

// Array order in BookmarkPlan::nodes is sibling order; parent_id defines the
// hierarchy. A parent may appear before or after its children.
struct BookmarkNode {
    std::string id;                        // Unique, nonempty.
    std::optional<std::string> parent_id;  // nullopt: top-level item.
    std::string title;                     // Nonempty valid UTF-8.
    PageDestination destination;
};

// Fixed v1 behavior: the output copy's outline is replaced by exactly the plan
// tree. The input file and its bookmarks are never modified.
enum class ExistingOutlinePolicy { ReplaceInCopy };

// Recorded plan-assembly decisions (Engine policy); S5 validates references
// but never makes these decisions itself.
struct OmittedEntry {
    std::string entry_id;
    std::string reason;
};
struct Promotion {
    std::string node_id;
    std::optional<std::string> original_parent_id;
    std::optional<std::string> new_parent_id;
    std::string reason;
};

struct BookmarkPlan {
    int schema_version = kPlanSchemaVersion;
    int page_index_base = kPageIndexBase;
    InputIdentity input;  // SHA-256 and page count of the analyzed input.
    ExistingOutlinePolicy existing_outline_policy =
        ExistingOutlinePolicy::ReplaceInCopy;
    std::vector<BookmarkNode> nodes;
    std::vector<OmittedEntry> omitted_entries;
    std::vector<Promotion> promotions;
};

enum class PlanIssueCode {
    UnsupportedSchemaVersion,
    UnsupportedPageIndexBase,
    MissingInputDigest,
    InvalidPageCount,
    EmptyPlan,
    EmptyNodeId,
    DuplicateNodeId,
    EmptyTitle,
    InvalidUtf8Title,
    MissingParent,
    SelfParent,
    ParentCycle,
    DestinationOutOfRange,
    InvalidOmission,
    InvalidPromotion
};

struct PlanIssue {
    PlanIssueCode code = PlanIssueCode::EmptyPlan;
    std::optional<std::size_t> node_index;  // Index into nodes, if node-scoped.
    std::string node_id;
    std::string field;  // e.g. "parent_id", "title", "destination.pdf_page_index".
    std::string message;
};

struct PlanValidation {
    bool valid = false;
    std::vector<PlanIssue> issues;  // Deterministic order.
};

// Structural checks against the plan's declared input identity/count only.
// Applying a plan re-validates and additionally checks the actual input.
PlanValidation validate(const BookmarkPlan& plan);

}  // namespace pdfbookmark::writer
