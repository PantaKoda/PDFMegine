#include "ledger.hpp"

#include <algorithm>

namespace pdfbookmark::engine::detail {
namespace {

// A cached page is reused only when it is a complete answer: not cancelled,
// and no OCR attempt was skipped (e.g. for budget), so a later stage with
// budget left can still try OCR on it.
bool reusable(const text::PageAcquisition& page) {
    if (page.outcome == text::Outcome::Cancelled) return false;
    return std::none_of(page.attempts.begin(), page.attempts.end(), [](const auto& a) {
        return a.source == text::Source::Ocr && a.state == text::AttemptState::Skipped;
    });
}

}  // namespace

const PageCache::Entry* PageCache::find(PageIndex page,
                                        const std::string& acquisition_key) const {
    const auto found = entries_.find(page);
    if (found == entries_.end() || found->second.acquisition_key != acquisition_key ||
        !reusable(found->second.page))
        return nullptr;
    return &found->second;
}

void PageCache::store(Entry entry) {
    const auto index = entry.page.page_index;
    entries_[index] = std::move(entry);
}

std::string Ledger::acquisition_key() const {
    // Session-level settings (input bytes, OCR models, OCR threads) are
    // shared by construction: one cache belongs to one TextDocument.
    return "mode=" + std::to_string(static_cast<int>(mode_)) +
           ";dpi=" + std::to_string(raster_.dpi) +
           ";max_pixels=" + std::to_string(raster_.max_pixels) +
           ";max_bytes=" + std::to_string(raster_.max_bytes) +
           ";max_dimension=" + std::to_string(raster_.max_dimension);
}

void Ledger::note_identities(const std::string& policy_id,
                             const std::string& model_identity) {
    if (policy_id_.empty() && configurations_.empty())
        policy_id_ = policy_id;
    else if (policy_id != policy_id_)
        diagnostics_.push_back("S1 acquisition policy changed between batches");
    // S1 reports "not-used" for batches without OCR (models load lazily), so
    // the run's identity is the first real one; two different real
    // identities are a genuine inconsistency.
    constexpr const char* kNotUsed = "not-used";
    if (model_identity != kNotUsed && !model_identity.empty()) {
        if (model_identity_.empty() || model_identity_ == kNotUsed)
            model_identity_ = model_identity;
        else if (model_identity_ != model_identity)
            diagnostics_.push_back("S1 OCR model identity changed between batches");
    } else if (model_identity_.empty()) {
        model_identity_ = model_identity;
    }
}

std::size_t Ledger::configuration_index(const std::string& configuration_id) {
    const auto found =
        std::find(configurations_.begin(), configurations_.end(), configuration_id);
    if (found != configurations_.end())
        return static_cast<std::size_t>(found - configurations_.begin());
    configurations_.push_back(configuration_id);
    return configurations_.size() - 1;
}

Result<std::vector<AcquiredPage>> Ledger::acquire(
    text::TextDocument& document, const std::vector<PageIndex>& pages,
    const RunControl& control, bool& complete) {
    complete = true;
    const std::string key = acquisition_key();
    std::vector<AcquiredPage> result(pages.size());
    std::vector<bool> filled(pages.size(), false);
    std::vector<PageIndex> missing;
    std::vector<std::size_t> missing_slots;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const PageCache::Entry* cached = cache_ ? cache_->find(pages[i], key) : nullptr;
        if (!cached) {
            missing.push_back(pages[i]);
            missing_slots.push_back(i);
            continue;
        }
        note_identities(cached->policy_id, cached->model_identity);
        result[i] = {cached->page, configuration_index(cached->configuration_id)};
        filled[i] = true;
        ++reused_;
        cache_->count_reuse();
    }

    if (!missing.empty()) {
        text::AcquisitionOptions call;
        call.mode = mode_;
        call.raster = raster_;
        call.max_ocr_attempts = budget_ > used_ ? budget_ - used_ : 0;
        auto batch = document.acquire(missing, call, control);
        if (!batch) return batch.error();
        auto& value = batch.value();
        complete = value.complete;
        note_identities(value.policy_id, value.model_identity);
        const std::size_t index = configuration_index(value.configuration_id);
        for (std::size_t j = 0; j < value.pages.size() && j < missing_slots.size(); ++j) {
            auto& page = value.pages[j];
            used_ += static_cast<std::size_t>(std::count_if(
                page.attempts.begin(), page.attempts.end(), [](const auto& a) {
                    return a.source == text::Source::Ocr &&
                           a.state != text::AttemptState::Skipped;
                }));
            if (cache_)
                cache_->store({page, key, value.configuration_id, value.policy_id,
                               value.model_identity});
            result[missing_slots[j]] = {std::move(page), index};
            filled[missing_slots[j]] = true;
        }
        if (used_ > budget_)
            diagnostics_.push_back("OCR attempts exceeded the run budget");
    }

    // S1 returns every requested page (cancelled ones marked as such); keep
    // only filled slots so a short batch never yields default pages.
    std::vector<AcquiredPage> ordered;
    for (std::size_t i = 0; i < result.size(); ++i)
        if (filled[i]) ordered.push_back(std::move(result[i]));
    return ordered;
}

}  // namespace pdfbookmark::engine::detail
