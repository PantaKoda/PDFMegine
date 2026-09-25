// C API (include/pdfbookmark/pdfbookmark.h): a thin translation layer over
// the C++ public API. It decodes JSON options, calls the same operations
// the CLI uses, and returns the same JSON formats. No domain logic here.

#include <pdfbookmark/pdfbookmark.h>
#include <pdfbookmark/pdfbookmark.hpp>

#include "codec.hpp"
#include "json.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct pdfb_cancel_token {
    std::atomic_bool cancelled{false};
};

namespace pdfbookmark::capi {
namespace {

namespace fs = std::filesystem;
namespace json = engine::json;
namespace codec = engine::codec;
using json::Value;

thread_local std::string g_last_error;

pdfb_status status_of(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument: return PDFB_INVALID_ARGUMENT;
        case ErrorCode::InputOpen: return PDFB_INPUT_OPEN;
        case ErrorCode::InputChanged: return PDFB_INPUT_CHANGED;
        case ErrorCode::PdfBackend: return PDFB_PDF_BACKEND;
        case ErrorCode::OcrConfiguration: return PDFB_OCR_CONFIGURATION;
        case ErrorCode::ResourceLimit: return PDFB_RESOURCE_LIMIT;
        case ErrorCode::Unsupported: return PDFB_UNSUPPORTED;
        case ErrorCode::OutputExists: return PDFB_OUTPUT_EXISTS;
        case ErrorCode::OutputWrite: return PDFB_OUTPUT_WRITE;
        case ErrorCode::Cancelled: return PDFB_CANCELLED;
    }
    return PDFB_INTERNAL;
}

// Internal failure signal: carries a status and message to the boundary.
struct Failure {
    pdfb_status status;
    std::string message;
};

[[noreturn]] void fail(pdfb_status status, std::string message) {
    throw Failure{status, std::move(message)};
}

template <typename T>
T take(Result<T> result) {
    if (!result) fail(status_of(result.error().code), result.error().message);
    return result.take();
}

char* copy_out(const std::string& text) {
    auto* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) throw std::bad_alloc();
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// Runs `body` with the C boundary rules: outputs reset to NULL, exceptions
// and failures turned into a status plus a thread-local message.
template <typename Body>
pdfb_status guarded(std::initializer_list<char**> outputs, Body&& body) noexcept {
    for (char** out : outputs)
        if (out) *out = nullptr;
    pdfb_status status = PDFB_OK;
    try {
        g_last_error.clear();
        body();
    } catch (const Failure& failure) {
        g_last_error = failure.message;
        status = failure.status;
    } catch (const std::bad_alloc&) {
        g_last_error = "out of memory";
        status = PDFB_RESOURCE_LIMIT;
    } catch (const std::exception& error) {
        g_last_error = std::string("internal error: ") + error.what();
        status = PDFB_INTERNAL;
    } catch (...) {
        g_last_error = "internal error";
        status = PDFB_INTERNAL;
    }
    if (status != PDFB_OK)
        for (char** out : outputs)
            if (out && *out) {
                std::free(*out);
                *out = nullptr;
            }
    return status;
}

fs::path path_arg(const char* path, const char* name) {
    if (!path || !*path) fail(PDFB_INVALID_ARGUMENT, std::string(name) + " is required");
    return fs::u8path(path);
}

void require_out(char** out, const char* name) {
    if (!out) fail(PDFB_INVALID_ARGUMENT, std::string(name) + " must not be NULL");
}

RunControl control_of(pdfb_cancel_token* token) {
    return RunControl{token ? &token->cancelled : nullptr};
}

// ------------------------------------------------------------ options JSON

class Options {
public:
    Options(const char* text, std::set<std::string> allowed) {
        if (!text || !*text) return;
        auto parsed = json::parse(text);
        if (!parsed) fail(PDFB_INVALID_ARGUMENT, "options: " + parsed.error().message);
        root_ = parsed.take();
        if (root_.kind != Value::Kind::Object)
            fail(PDFB_INVALID_ARGUMENT, "options must be a JSON object");
        for (const auto& member : root_.object)
            if (!allowed.count(member.key))
                fail(PDFB_INVALID_ARGUMENT, "options: unknown key \"" + member.key + "\"");
    }

    const Value* get(const std::string& key) const {
        return root_.kind == Value::Kind::Object ? root_.find(key) : nullptr;
    }

    std::optional<bool> boolean(const std::string& key) const {
        const Value* v = get(key);
        if (!v) return std::nullopt;
        if (v->kind != Value::Kind::Bool) fail(PDFB_INVALID_ARGUMENT, key + " must be true or false");
        return v->boolean;
    }

    std::optional<std::string> string(const std::string& key) const {
        const Value* v = get(key);
        if (!v) return std::nullopt;
        if (v->kind != Value::Kind::String) fail(PDFB_INVALID_ARGUMENT, key + " must be a string");
        return v->text;
    }

    std::optional<std::int64_t> integer(const std::string& key, std::int64_t min,
                                        std::int64_t max) const {
        const Value* v = get(key);
        if (!v) return std::nullopt;
        const auto n = v->kind == Value::Kind::Number ? v->as_int64() : std::nullopt;
        if (!n || *n < min || *n > max)
            fail(PDFB_INVALID_ARGUMENT, key + " must be an integer from " +
                                            std::to_string(min) + " to " + std::to_string(max));
        return n;
    }

    std::optional<std::size_t> count(const std::string& key) const {
        const auto n = integer(key, 0, 1'000'000'000);
        return n ? std::optional<std::size_t>(static_cast<std::size_t>(*n)) : std::nullopt;
    }

private:
    Value root_;
};

// Adds the reading options every page-reading operation accepts.
std::set<std::string> with_reading(std::set<std::string> keys) {
    keys.insert({"mode", "models", "dpi", "ocr_budget", "ocr_threads"});
    return keys;
}

struct Reading {
    text::AcquisitionMode mode = text::AcquisitionMode::Auto;
    RasterLimits raster;
    std::optional<ModelResources> models;
    std::optional<std::size_t> ocr_budget;
    int ocr_threads = 0;
};

Reading reading_of(const Options& options) {
    Reading r;
    if (const auto mode = options.string("mode")) {
        if (*mode == "auto") r.mode = text::AcquisitionMode::Auto;
        else if (*mode == "embedded") r.mode = text::AcquisitionMode::EmbeddedOnly;
        else if (*mode == "ocr") r.mode = text::AcquisitionMode::OcrOnly;
        else fail(PDFB_INVALID_ARGUMENT, "mode must be \"auto\", \"embedded\" or \"ocr\"");
    }
    const Value* models = options.get("models");
    if (!models) {
        r.models = find_models();
    } else if (models->kind == Value::Kind::String) {
        r.models = models_in(fs::u8path(models->text));
        if (!r.models)
            fail(PDFB_OCR_CONFIGURATION,
                 "no OCR models in \"" + models->text +
                     "\" (expected det/inference.onnx, rec/inference.onnx, rec/charset.txt)");
    } else if (models->kind != Value::Kind::Null) {
        fail(PDFB_INVALID_ARGUMENT, "models must be a folder path or null");
    }
    if (const auto dpi = options.integer("dpi", 50, 1200)) r.raster.dpi = static_cast<int>(*dpi);
    r.ocr_budget = options.count("ocr_budget");
    if (const auto n = options.integer("ocr_threads", 0, 64)) r.ocr_threads = static_cast<int>(*n);
    return r;
}

// ------------------------------------------------------------ result JSON

std::string identity_json(const InputIdentity& identity) {
    Value root = Value::make_object();
    root.add("sha256", Value::of(codec::hex(identity.sha256)));
    root.add("page_count", codec::count(identity.page_count));
    return json::write(root);
}

std::string write_result_json(const WriteResult& result) {
    Value root = Value::make_object();
    root.add("schema_version", Value::of(1));
    root.add("output", Value::of(result.output.u8string()));
    root.add("output_sha256", Value::of(codec::hex(result.output_sha256)));
    root.add("committed", Value::of(result.committed));
    root.add("replaced_existing_output", Value::of(result.replaced_existing_output));
    root.add("input_had_outline", Value::of(result.input_had_outline));
    auto& v = root.add("verification", Value::make_object());
    v.add("page_count", codec::count(result.verification.page_count));
    v.add("outline_items", codec::count(result.verification.outline_items));
    v.add("structure_matches", Value::of(result.verification.structure_matches));
    v.add("input_unchanged", Value::of(result.verification.input_unchanged));
    root.add("diagnostics", codec::strings(result.diagnostics));
    return json::write(root);
}

const char* issue_code_name(writer::PlanIssueCode code) {
    using C = writer::PlanIssueCode;
    switch (code) {
        case C::UnsupportedSchemaVersion: return "unsupported_schema_version";
        case C::UnsupportedPageIndexBase: return "unsupported_page_index_base";
        case C::MissingInputDigest: return "missing_input_digest";
        case C::InvalidPageCount: return "invalid_page_count";
        case C::EmptyPlan: return "empty_plan";
        case C::EmptyNodeId: return "empty_node_id";
        case C::DuplicateNodeId: return "duplicate_node_id";
        case C::EmptyTitle: return "empty_title";
        case C::InvalidUtf8Title: return "invalid_utf8_title";
        case C::MissingParent: return "missing_parent";
        case C::SelfParent: return "self_parent";
        case C::ParentCycle: return "parent_cycle";
        case C::DestinationOutOfRange: return "destination_out_of_range";
        case C::InvalidOmission: return "invalid_omission";
        case C::InvalidPromotion: return "invalid_promotion";
    }
    return "unknown";
}

std::string validation_json(const PlanValidation& validation) {
    Value root = Value::make_object();
    root.add("valid", Value::of(validation.valid));
    auto& issues = root.add("issues", Value::make_array());
    for (const auto& issue : validation.issues) {
        auto& v = issues.push(Value::make_object());
        v.add("code", Value::of(issue_code_name(issue.code)));
        v.add("node_index", issue.node_index ? codec::count(*issue.node_index) : Value::null());
        v.add("node_id", Value::of(issue.node_id));
        v.add("field", Value::of(issue.field));
        v.add("message", Value::of(issue.message));
    }
    return json::write(root);
}

}  // namespace
}  // namespace pdfbookmark::capi

// ================================================================ C ABI

using namespace pdfbookmark;
using namespace pdfbookmark::capi;

extern "C" {

const char* pdfb_version(void) { return version(); }

int pdfb_c_api_version(void) { return PDFB_C_API_VERSION; }

const char* pdfb_status_name(pdfb_status status) {
    switch (status) {
        case PDFB_OK: return "ok";
        case PDFB_INVALID_ARGUMENT: return "invalid_argument";
        case PDFB_INPUT_OPEN: return "input_open";
        case PDFB_INPUT_CHANGED: return "input_changed";
        case PDFB_PDF_BACKEND: return "pdf_backend";
        case PDFB_OCR_CONFIGURATION: return "ocr_configuration";
        case PDFB_RESOURCE_LIMIT: return "resource_limit";
        case PDFB_UNSUPPORTED: return "unsupported";
        case PDFB_OUTPUT_EXISTS: return "output_exists";
        case PDFB_OUTPUT_WRITE: return "output_write";
        case PDFB_CANCELLED: return "cancelled";
        case PDFB_INTERNAL: return "internal";
    }
    return "unknown";
}

const char* pdfb_last_error(void) { return g_last_error.c_str(); }

void pdfb_free(char* text) { std::free(text); }

pdfb_cancel_token* pdfb_cancel_token_new(void) {
    return new (std::nothrow) pdfb_cancel_token();
}

void pdfb_cancel_token_cancel(pdfb_cancel_token* token) {
    if (token) token->cancelled.store(true);
}

void pdfb_cancel_token_free(pdfb_cancel_token* token) { delete token; }

pdfb_status pdfb_find_models(char** out_json) {
    return guarded({out_json}, [&] {
        require_out(out_json, "out_json");
        const auto models = find_models();
        json::Value root = json::Value::null();
        if (models) {
            root = json::Value::make_object();
            root.add("detector", json::Value::of(models->detector.u8string()));
            root.add("recognizer", json::Value::of(models->recognizer.u8string()));
            root.add("charset", json::Value::of(models->charset.u8string()));
        }
        *out_json = copy_out(json::write(root));
    });
}

pdfb_status pdfb_extract_text(const char* pdf_path, const char* options_json,
                              pdfb_cancel_token* cancel, pdfb_progress_fn progress,
                              void* user_data, char** out_report_json) {
    return guarded({out_report_json}, [&] {
        require_out(out_report_json, "out_report_json");
        const auto input = path_arg(pdf_path, "pdf_path");
        const Options options(options_json, with_reading({"pages"}));
        const Reading reading = reading_of(options);
        TextOptions text_options;
        text_options.mode = reading.mode;
        text_options.raster = reading.raster;
        text_options.ocr_threads = reading.ocr_threads;
        text_options.models = reading.models;
        if (reading.ocr_budget) text_options.ocr_budget = *reading.ocr_budget;

        std::optional<std::vector<PageIndex>> pages;
        if (const auto* list = options.get("pages")) {
            if (list->kind != json::Value::Kind::Array)
                fail(PDFB_INVALID_ARGUMENT, "pages must be an array of page indices");
            pages.emplace();
            for (const auto& item : list->array) {
                const auto n = item.kind == json::Value::Kind::Number ? item.as_int64()
                                                                      : std::nullopt;
                if (!n || *n < 0)
                    fail(PDFB_INVALID_ARGUMENT, "pages must contain zero-based page indices");
                pages->push_back(static_cast<PageIndex>(*n));
            }
        }
        TextProgressCallback on_progress;
        if (progress)
            on_progress = [&](const TextProgress& p) {
                progress(user_data, "text", p.pages_done, p.pages_total);
            };
        const auto report =
            take(extract_text(input, pages, text_options, control_of(cancel), on_progress));
        *out_report_json = copy_out(text_report_json(report));
    });
}

pdfb_status pdfb_analyze(const char* pdf_path, const char* options_json,
                         pdfb_cancel_token* cancel, pdfb_progress_fn progress,
                         void* user_data, char** out_report_json, char** out_plan_json) {
    return guarded({out_report_json, out_plan_json}, [&] {
        require_out(out_report_json, "out_report_json");
        const auto input = path_arg(pdf_path, "pdf_path");
        const Options options(options_json,
                              with_reading({"allow_partial", "flat_outline", "titles",
                                            "candidate", "max_search_pages",
                                            "max_evidence_pages"}));
        const Reading reading = reading_of(options);
        AnalysisOptions analysis;
        analysis.mode = reading.mode;
        analysis.raster = reading.raster;
        analysis.ocr_threads = reading.ocr_threads;
        analysis.models = reading.models;
        if (reading.ocr_budget) analysis.limits.ocr_budget = *reading.ocr_budget;
        if (const auto v = options.boolean("allow_partial")) analysis.plan.allow_partial = *v;
        if (const auto v = options.boolean("flat_outline"))
            analysis.plan.flat_outline_for_unknown_hierarchy = *v;
        if (const auto titles = options.string("titles")) {
            if (*titles == "printed") analysis.plan.title_style = PlanPolicy::TitleStyle::AsPrinted;
            else if (*titles == "chapter") analysis.plan.title_style = PlanPolicy::TitleStyle::Chapter;
            else fail(PDFB_INVALID_ARGUMENT, "titles must be \"printed\" or \"chapter\"");
        }
        analysis.candidate_id = options.string("candidate");
        if (const auto n = options.count("max_search_pages")) analysis.limits.max_search_pages = *n;
        if (const auto n = options.count("max_evidence_pages"))
            analysis.limits.max_evidence_pages = *n;

        AnalysisProgressCallback on_progress;
        if (progress)
            on_progress = [&](const AnalysisProgress& p) {
                progress(user_data, p.stage.c_str(), p.pages_acquired, 0);
            };
        const auto report = take(analyze(input, analysis, control_of(cancel), on_progress));
        *out_report_json = copy_out(analysis_report_json(report, analysis));
        if (out_plan_json && report.plan.ready && report.plan.plan)
            *out_plan_json = copy_out(plan_to_json(*report.plan.plan));
    });
}

pdfb_status pdfb_apply(const char* pdf_path, const char* output_path, const char* plan_json,
                       const char* options_json, pdfb_cancel_token* cancel,
                       char** out_result_json) {
    return guarded({out_result_json}, [&] {
        const auto input = path_arg(pdf_path, "pdf_path");
        const auto output = path_arg(output_path, "output_path");
        if (!plan_json) fail(PDFB_INVALID_ARGUMENT, "plan_json is required");
        const Options options(options_json, {"replace_existing_output"});
        ApplyOptions apply_options;
        if (const auto v = options.boolean("replace_existing_output"))
            apply_options.replace_existing_output = *v;
        const auto plan = take(plan_from_json(plan_json));
        const auto result = take(apply(input, output, plan, apply_options, control_of(cancel)));
        if (out_result_json) *out_result_json = copy_out(write_result_json(result));
    });
}

pdfb_status pdfb_extract_metadata(const char* pdf_path, const char* options_json,
                                  pdfb_cancel_token* cancel, char** out_report_json) {
    return guarded({out_report_json}, [&] {
        require_out(out_report_json, "out_report_json");
        const auto input = path_arg(pdf_path, "pdf_path");
        const Options options(options_json, with_reading({"max_pages"}));
        const Reading reading = reading_of(options);
        MetadataRunOptions run;
        run.mode = reading.mode;
        run.raster = reading.raster;
        run.ocr_threads = reading.ocr_threads;
        run.models = reading.models;
        if (reading.ocr_budget) run.ocr_budget = *reading.ocr_budget;
        if (const auto n = options.count("max_pages")) run.max_pages = *n;
        const auto report = take(extract_metadata(input, run, control_of(cancel)));
        *out_report_json = copy_out(metadata_report_json(report));
    });
}

pdfb_status pdfb_validate_plan(const char* plan_json, char** out_result_json) {
    return guarded({out_result_json}, [&] {
        require_out(out_result_json, "out_result_json");
        if (!plan_json) fail(PDFB_INVALID_ARGUMENT, "plan_json is required");
        const auto plan = take(plan_from_json(plan_json));
        *out_result_json = copy_out(validation_json(validate_plan(plan)));
    });
}

pdfb_status pdfb_read_pdf_identity(const char* pdf_path, char** out_json) {
    return guarded({out_json}, [&] {
        require_out(out_json, "out_json");
        const auto input = path_arg(pdf_path, "pdf_path");
        *out_json = copy_out(identity_json(take(read_pdf_identity(input))));
    });
}

}  // extern "C"
