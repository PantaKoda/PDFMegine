#pragma once

#include <pdfbookmark/text/acquisition.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::detection {

enum class BoundaryState { Closed, MayContinue, Unknown };
enum class PageStatus { Candidate, Rejected, Skipped };

struct DetectionOptions {
    // Versioned ranking policy; scores are not probabilities.
    std::size_t min_reference_rows = 3;
    double min_aligned_ratio = 0.45;
    double alignment_tolerance_points = 18.0;
    double min_page_score = 7.0;
    std::size_t max_interrupted_pages = 1;
};

struct CandidatePage {
    PageIndex page_index = 0;
    std::uint64_t revision = 0;
    double score = 0;
    std::vector<text::SourceReference> row_evidence;
    bool degraded = false;
};

struct CandidateGap {
    PageIndex page_index = 0;
    std::string reason;
};

struct TocCandidate {
    std::string id;  // Analysis-local and deterministic for identical evidence.
    std::vector<CandidatePage> pages;  // Explicit physical pages, in index order.
    std::vector<CandidateGap> interruptions;  // Supplied but unusable pages only.
    double score = 0;
    BoundaryState start = BoundaryState::Unknown;
    BoundaryState end = BoundaryState::Unknown;
    std::vector<std::string> reasons;
    std::vector<std::string> limitations;
};

struct PageReview {
    PageIndex page_index = 0;
    std::optional<std::uint64_t> revision;
    PageStatus status = PageStatus::Skipped;
    double score = 0;
    std::vector<std::string> reasons;
};

struct DetectionResult {
    std::vector<TocCandidate> candidates;
    std::vector<PageReview> pages;  // All supplied pages, in physical index order.
    std::vector<std::string> diagnostics;
    std::string policy_id;
};

// Pure computation on caller-owned S1 values. Never opens a document or fetches
// pages. Duplicate physical indices or mismatched selected revisions are errors.
Result<DetectionResult> detect(
    const std::vector<text::PageAcquisition>& supplied_pages,
    const DetectionOptions& options = {});

}  // namespace pdfbookmark::detection
