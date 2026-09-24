#include <pdfbookmark/engine/text.hpp>

#include "codec.hpp"
#include "json.hpp"
#include "ledger.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

namespace pdfbookmark::engine {
namespace {

text::PageAcquisition not_started(PageIndex index) {
    text::PageAcquisition page;
    page.page_index = index;
    page.outcome = text::Outcome::Cancelled;
    page.reasons.push_back("Not started: run cancelled");
    return page;
}

const char* name(TextStatus v) {
    switch (v) {
        case TextStatus::Complete: return "complete";
        case TextStatus::Partial: return "partial";
        case TextStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}

}  // namespace

Result<TextReport> extract_text(
    const std::filesystem::path& input,
    const std::optional<std::vector<PageIndex>>& pages,
    const TextOptions& options, const RunControl& control,
    const TextProgressCallback& progress) {
    if (options.batch_pages == 0)
        return Error{ErrorCode::InvalidArgument, "batch_pages must be positive"};
    text::OpenOptions open;
    open.ocr_models = options.models;
    auto opened = text::TextAcquisition{}.open(input, open);
    if (!opened) return opened.error();
    auto document = opened.take();

    std::vector<PageIndex> selected;
    if (pages) {
        selected = *pages;
        std::set<PageIndex> seen;
        for (const auto index : selected) {
            if (index < 0 || index >= document.page_count())
                return Error{ErrorCode::InvalidArgument,
                             "Page index " + std::to_string(index) +
                                 " is outside [0, " +
                                 std::to_string(document.page_count()) + ")"};
            if (!seen.insert(index).second)
                return Error{ErrorCode::InvalidArgument,
                             "Duplicate page index " + std::to_string(index)};
        }
    } else {
        for (PageIndex i = 0; i < document.page_count(); ++i)
            selected.push_back(i);
    }

    detail::Ledger ledger(options.mode, options.raster, options.ocr_budget);
    TextReport report;
    report.input = document.identity();
    bool cancelled = false;
    for (std::size_t first = 0; first < selected.size();
         first += options.batch_pages) {
        const std::size_t end =
            std::min(selected.size(), first + options.batch_pages);
        if (cancelled || control.is_cancelled()) {
            cancelled = true;
            for (std::size_t i = first; i < end; ++i)
                report.pages.push_back(not_started(selected[i]));
            continue;
        }
        bool complete = true;
        auto batch = ledger.acquire(
            document,
            std::vector<PageIndex>(selected.begin() + first,
                                   selected.begin() + end),
            control, complete);
        if (!batch) return batch.error();
        for (auto& acquired : batch.value()) {
            report.page_configuration.push_back(acquired.configuration);
            report.pages.push_back(std::move(acquired.page));
        }
        if (!complete) cancelled = true;
        if (progress) progress({report.pages.size(), selected.size()});
    }
    report.configurations = ledger.configurations();
    report.acquisition_policy_id = ledger.policy_id();
    report.model_identity = ledger.model_identity();
    report.ocr_budget = ledger.budget();
    report.ocr_attempts_used = ledger.used();
    report.diagnostics = ledger.diagnostics();
    // Not-started pages (always trailing) have no producing configuration.
    report.page_configuration.resize(report.pages.size(),
                                     report.configurations.size());

    report.status = TextStatus::Complete;
    for (const auto& page : report.pages) {
        if (page.outcome == text::Outcome::Cancelled) {
            report.status = TextStatus::Cancelled;
            break;
        }
        if (page.outcome == text::Outcome::Degraded ||
            page.outcome == text::Outcome::Failed)
            report.status = TextStatus::Partial;
    }
    return report;
}

std::string text_report_json(const TextReport& report) {
    using json::Value;
    auto root = Value::make_object();
    root.add("schema_version", Value::of(1));
    root.add("kind", Value::of("pdfbookmark.text"));
    root.add("page_index_base", Value::of(0));
    auto& input = root.add("input", Value::make_object());
    input.add("sha256", Value::of(codec::hex(report.input.sha256)));
    input.add("page_count", Value::of(report.input.page_count));
    input.add("display_path", Value::of(report.input.display_path.u8string()));
    root.add("status", Value::of(name(report.status)));
    auto& acquisition = root.add("acquisition", Value::make_object());
    acquisition.add("policy_id", Value::of(report.acquisition_policy_id));
    acquisition.add("model_identity", Value::of(report.model_identity));
    acquisition.add("configurations", codec::strings(report.configurations));
    acquisition.add("ocr_budget", codec::count(report.ocr_budget));
    acquisition.add("ocr_attempts_used", codec::count(report.ocr_attempts_used));
    root.add("diagnostics", codec::strings(report.diagnostics));
    auto& pages = root.add("pages", Value::make_array());
    for (std::size_t i = 0; i < report.pages.size(); ++i) {
        const std::size_t config = i < report.page_configuration.size()
                                       ? report.page_configuration[i]
                                       : report.configurations.size();
        pages.push(codec::page(report.pages[i],
                               config < report.configurations.size()
                                   ? std::optional<std::size_t>(config)
                                   : std::nullopt));
    }
    return json::write(root);
}

}  // namespace pdfbookmark::engine
