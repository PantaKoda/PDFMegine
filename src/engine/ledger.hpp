#pragma once

// Engine-private acquisition ledger: run-wide OCR budget and S1 identity
// bookkeeping across repeated acquire() calls on one persistent session.

#include <pdfbookmark/text/acquisition.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace pdfbookmark::engine::detail {

struct AcquiredPage {
    text::PageAcquisition page;
    std::size_t configuration = 0;  // Index into Ledger::configurations.
};

class Ledger {
public:
    Ledger(text::AcquisitionMode mode, text::RasterLimits raster,
           std::size_t ocr_budget)
        : mode_(mode), raster_(raster), budget_(ocr_budget) {}

    // One S1 call with the remaining run-wide OCR allowance. `complete` is
    // false when S1 stopped early for cancellation.
    Result<std::vector<AcquiredPage>> acquire(
        text::TextDocument& document, const std::vector<PageIndex>& pages,
        const RunControl& control, bool& complete);

    std::size_t budget() const { return budget_; }
    std::size_t used() const { return used_; }
    const std::vector<std::string>& configurations() const {
        return configurations_;
    }
    const std::string& policy_id() const { return policy_id_; }
    const std::string& model_identity() const { return model_identity_; }
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }

private:
    text::AcquisitionMode mode_;
    text::RasterLimits raster_;
    std::size_t budget_;
    std::size_t used_ = 0;
    std::vector<std::string> configurations_;
    std::string policy_id_, model_identity_;
    std::vector<std::string> diagnostics_;
};

}  // namespace pdfbookmark::engine::detail
