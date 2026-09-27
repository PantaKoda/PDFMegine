#include <pdfbookmark/engine/metadata.hpp>

#include "codec.hpp"
#include "json.hpp"
#include "ledger.hpp"
#include "runs.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace pdfbookmark::engine {
namespace {

bool settled(const metadata::MetadataResult& r) {
    return r.title.status == metadata::FieldStatus::Resolved &&
           r.contributors.status == metadata::FieldStatus::Resolved &&
           r.edition.status == metadata::FieldStatus::Resolved &&
           r.publication_year.status == metadata::FieldStatus::Resolved;
}

}  // namespace

std::optional<Error> detail::check_metadata_options(const MetadataRunOptions& options) {
    if (options.initial_pages == 0 || options.batch_pages == 0 || options.max_pages == 0)
        return Error{ErrorCode::InvalidArgument, "Invalid metadata page limits"};
    return std::nullopt;
}

Result<MetadataReport> extract_metadata(const std::filesystem::path& input,
                                        const MetadataRunOptions& options,
                                        const RunControl& control) {
    if (auto error = detail::check_metadata_options(options)) return *error;
    text::OpenOptions open;
    open.ocr_threads = options.ocr_threads;
    open.ocr_models = options.models;
    auto opened = text::TextAcquisition{}.open(input, open);
    if (!opened) return opened.error();
    auto document = opened.take();
    return detail::run_metadata(document, options, control, nullptr);
}

Result<MetadataReport> detail::run_metadata(text::TextDocument& document,
                                            const MetadataRunOptions& options,
                                            const RunControl& control,
                                            PageCache* cache,
                                            const AnalysisProgressCallback& progress) {
    detail::Ledger ledger(options.mode, options.raster, options.ocr_budget, cache);
    MetadataReport report;
    report.input = document.identity();
    const auto count = static_cast<std::size_t>(document.page_count());
    const std::size_t limit = std::min(options.max_pages, count);
    std::map<PageIndex, text::PageAcquisition> pages;
    std::size_t end = 0;
    std::size_t next = std::min(options.initial_pages, limit);
    while (true) {
        if (control.is_cancelled()) { report.cancelled = true; break; }
        std::vector<PageIndex> batch;
        for (std::size_t i = end; i < next; ++i) batch.push_back(static_cast<PageIndex>(i));
        if (!batch.empty()) {
            bool complete = true;
            auto acquired = ledger.acquire(document, batch, control, complete);
            if (!acquired) return acquired.error();
            for (auto& a : acquired.value()) pages[a.page.page_index] = std::move(a.page);
            if (!complete) report.cancelled = true;
            end = next;
            if (progress) progress({"metadata", pages.size()});
        }
        std::vector<text::PageAcquisition> supplied;
        for (const auto& p : pages) supplied.push_back(p.second);
        auto result = metadata::extract(supplied, options.hints, options.metadata);
        if (!result) return result.error();
        report.result = result.take();
        if (report.cancelled) { report.stop_reasons.push_back("Cancelled"); break; }
        if (settled(report.result)) {
            report.stop_reasons.push_back("All fields resolved");
            break;
        }
        if (end >= limit) {
            report.stop_reasons.push_back(
                end >= count ? "Searched the whole document"
                             : "Reached the page limit (" + std::to_string(limit) +
                                   " pages); unresolved fields are not found in "
                                   "these pages, not proven absent");
            break;
        }
        next = std::min(end + options.batch_pages, limit);
        report.stop_reasons.push_back("Some fields unresolved; reading pages up to " +
                                      std::to_string(next));
    }
    for (const auto& p : pages) report.searched_pages.push_back(p.first);
    report.search_covered_document = end >= count;
    report.acquisition_policy_id = ledger.policy_id();
    report.model_identity = ledger.model_identity();
    report.ocr_budget = ledger.budget();
    report.ocr_attempts_used = ledger.used();
    return report;
}

namespace {
using json::Value;

Value evidence_json(const std::vector<metadata::Evidence>& list) {
    auto array = Value::make_array();
    for (const auto& e : list) {
        auto& v = array.push(Value::make_object());
        v.add("source", codec::source_ref(e.source));
        v.add("text", Value::of(e.text));
        v.add("reason", Value::of(e.reason));
    }
    return array;
}

Value value_json(const metadata::TitleValue& t) {
    auto v = Value::make_object();
    v.add("title", Value::of(t.title));
    v.add("subtitle", t.subtitle ? Value::of(*t.subtitle) : Value::null());
    return v;
}
Value value_json(const std::vector<metadata::Contributor>& list) {
    auto v = Value::make_array();
    for (const auto& c : list) {
        auto& o = v.push(Value::make_object());
        o.add("name", Value::of(c.name));
        o.add("role", Value::of(metadata::role_name(c.role)));
    }
    return v;
}
Value value_json(const metadata::EditionValue& e) {
    auto v = Value::make_object();
    v.add("statement", Value::of(e.statement));
    v.add("ordinal", e.ordinal ? codec::count(*e.ordinal) : Value::null());
    return v;
}
Value value_json(const metadata::YearValue& y) {
    auto v = Value::make_object();
    v.add("year", Value::of(y.year));
    v.add("kind", Value::of(metadata::kind_name(y.kind)));
    v.add("statement", Value::of(y.statement));
    return v;
}

template <typename T>
Value field_json(const metadata::Field<T>& f) {
    auto v = Value::make_object();
    v.add("status", Value::of(metadata::status_name(f.status)));
    v.add("value", f.value ? value_json(*f.value) : Value::null());
    v.add("evidence", evidence_json(f.evidence));
    auto& alternatives = v.add("alternatives", Value::make_array());
    for (const auto& a : f.alternatives) {
        auto& o = alternatives.push(Value::make_object());
        o.add("value", value_json(a.value));
        o.add("score", Value::of(a.score, 3));
        o.add("evidence", evidence_json(a.evidence));
        o.add("reasons", codec::strings(a.reasons));
    }
    v.add("reasons", codec::strings(f.reasons));
    return v;
}

}  // namespace

std::string metadata_report_json(const MetadataReport& report) {
    const auto& r = report.result;
    auto root = Value::make_object();
    root.add("schema_version", Value::of(1));
    root.add("kind", Value::of("pdfbookmark.metadata"));
    root.add("page_index_base", Value::of(0));
    auto& input = root.add("input", Value::make_object());
    input.add("sha256", Value::of(codec::hex(report.input.sha256)));
    input.add("page_count", Value::of(report.input.page_count));
    input.add("display_path", Value::of(report.input.display_path.u8string()));
    root.add("policy_id", Value::of(r.policy_id));
    auto& fields = root.add("fields", Value::make_object());
    fields.add("title", field_json(r.title));
    fields.add("contributors", field_json(r.contributors));
    fields.add("edition", field_json(r.edition));
    fields.add("publication_year", field_json(r.publication_year));
    fields.add("copyright_year", field_json(r.copyright_year));
    auto& pages = root.add("pages", Value::make_array());
    for (const auto& p : r.pages) {
        auto& o = pages.push(Value::make_object());
        o.add("page_index", Value::of(p.page_index));
        o.add("role", Value::of(metadata::role_name(p.role)));
        o.add("reasons", codec::strings(p.reasons));
    }
    auto& search = root.add("search", Value::make_object());
    auto& searched = search.add("pages", Value::make_array());
    for (const auto p : report.searched_pages) searched.push(Value::of(p));
    search.add("covered_document", Value::of(report.search_covered_document));
    search.add("stop_reasons", codec::strings(report.stop_reasons));
    search.add("cancelled", Value::of(report.cancelled));
    auto& acquisition = root.add("acquisition", Value::make_object());
    acquisition.add("policy_id", Value::of(report.acquisition_policy_id));
    acquisition.add("model_identity", Value::of(report.model_identity));
    acquisition.add("ocr_budget", codec::count(report.ocr_budget));
    acquisition.add("ocr_attempts_used", codec::count(report.ocr_attempts_used));
    root.add("diagnostics", codec::strings(r.diagnostics));
    return json::write(root);
}

}  // namespace pdfbookmark::engine
