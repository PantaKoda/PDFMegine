#pragma once

// Engine-private acquisition ledger: run-wide OCR budget and S1 identity
// bookkeeping across repeated acquire() calls on one persistent session.
//
// A PageCache shared by several ledgers on the SAME session lets one stage
// reuse pages another stage already acquired (issue #3). S1 still performs
// every acquisition; the cache only avoids asking it twice for a page.

#include <pdfbookmark/text/acquisition.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pdfbookmark::engine::detail {

struct AcquiredPage {
    text::PageAcquisition page;
    std::size_t configuration = 0;  // Index into Ledger::configurations.
};

// Pages acquired on one TextDocument session, with the S1 identities of the
// batch that produced each. Valid only for that session (same input bytes).
class PageCache {
public:
    struct Entry {
        text::PageAcquisition page;
        std::string acquisition_key;  // Mode and raster limits (see Ledger).
        std::string configuration_id, policy_id, model_identity;
    };
    const Entry* find(PageIndex page, const std::string& acquisition_key) const;
    void store(Entry entry);
    std::size_t reused() const { return reused_; }
    void count_reuse() { ++reused_; }

private:
    std::map<PageIndex, Entry> entries_;
    std::size_t reused_ = 0;
};

class Ledger {
public:
    Ledger(text::AcquisitionMode mode, text::RasterLimits raster,
           std::size_t ocr_budget, PageCache* cache = nullptr)
        : mode_(mode), raster_(raster), budget_(ocr_budget), cache_(cache) {}

    // Pages in request order. Pages found in the cache under the same
    // acquisition settings are reused (they cost no OCR budget); the rest
    // come from one S1 call with the remaining run-wide OCR allowance.
    // `complete` is false when S1 stopped early for cancellation.
    Result<std::vector<AcquiredPage>> acquire(
        text::TextDocument& document, const std::vector<PageIndex>& pages,
        const RunControl& control, bool& complete);

    std::size_t budget() const { return budget_; }
    std::size_t used() const { return used_; }
    std::size_t reused() const { return reused_; }
    const std::vector<std::string>& configurations() const {
        return configurations_;
    }
    const std::string& policy_id() const { return policy_id_; }
    const std::string& model_identity() const { return model_identity_; }
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }

private:
    std::string acquisition_key() const;
    void note_identities(const std::string& policy_id, const std::string& model_identity);
    std::size_t configuration_index(const std::string& configuration_id);

    text::AcquisitionMode mode_;
    text::RasterLimits raster_;
    std::size_t budget_;
    PageCache* cache_;
    std::size_t used_ = 0;
    std::size_t reused_ = 0;
    std::vector<std::string> configurations_;
    std::string policy_id_, model_identity_;
    std::vector<std::string> diagnostics_;
};

}  // namespace pdfbookmark::engine::detail
