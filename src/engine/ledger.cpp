#include "ledger.hpp"

#include <algorithm>

namespace pdfbookmark::engine::detail {

Result<std::vector<AcquiredPage>> Ledger::acquire(
    text::TextDocument& document, const std::vector<PageIndex>& pages,
    const RunControl& control, bool& complete) {
    text::AcquisitionOptions call;
    call.mode = mode_;
    call.raster = raster_;
    call.max_ocr_attempts = budget_ > used_ ? budget_ - used_ : 0;
    auto batch = document.acquire(pages, call, control);
    if (!batch) return batch.error();
    auto& value = batch.value();
    complete = value.complete;
    if (policy_id_.empty() && configurations_.empty())
        policy_id_ = value.policy_id;
    else if (value.policy_id != policy_id_)
        diagnostics_.push_back("S1 acquisition policy changed between batches");
    // S1 reports "not-used" for batches without OCR (models load lazily), so
    // the run's identity is the first real one; two different real
    // identities are a genuine inconsistency.
    constexpr const char* kNotUsed = "not-used";
    if (value.model_identity != kNotUsed && !value.model_identity.empty()) {
        if (model_identity_.empty() || model_identity_ == kNotUsed)
            model_identity_ = value.model_identity;
        else if (model_identity_ != value.model_identity)
            diagnostics_.push_back("S1 OCR model identity changed between batches");
    } else if (model_identity_.empty()) {
        model_identity_ = value.model_identity;
    }
    auto found = std::find(configurations_.begin(), configurations_.end(),
                           value.configuration_id);
    const auto index =
        static_cast<std::size_t>(found - configurations_.begin());
    if (found == configurations_.end())
        configurations_.push_back(value.configuration_id);
    std::vector<AcquiredPage> result;
    for (auto& page : value.pages) {
        used_ += static_cast<std::size_t>(std::count_if(
            page.attempts.begin(), page.attempts.end(), [](const auto& a) {
                return a.source == text::Source::Ocr &&
                       a.state != text::AttemptState::Skipped;
            }));
        result.push_back({std::move(page), index});
    }
    if (used_ > budget_)
        diagnostics_.push_back("OCR attempts exceeded the run budget");
    return result;
}

}  // namespace pdfbookmark::engine::detail
