#include <pdfbookmark/engine/analysis.hpp>

#include "codec.hpp"
#include "json.hpp"

#include <limits>
#include <set>
#include <utility>

namespace pdfbookmark::engine {
namespace {
using json::Value;
using codec::count;
using codec::strings;

const char* name(detection::BoundaryState v) {
    switch (v) {
        case detection::BoundaryState::Closed: return "closed";
        case detection::BoundaryState::MayContinue: return "may_continue";
        case detection::BoundaryState::Unknown: return "unknown";
    }
    return "unknown";
}
const char* name(detection::PageStatus v) {
    switch (v) {
        case detection::PageStatus::Candidate: return "candidate";
        case detection::PageStatus::Rejected: return "rejected";
        case detection::PageStatus::Skipped: return "skipped";
    }
    return "unknown";
}
const char* name(parsing::NumberingStyle v) {
    switch (v) {
        case parsing::NumberingStyle::Decimal: return "decimal";
        case parsing::NumberingStyle::Roman: return "roman";
        case parsing::NumberingStyle::PrefixedDecimal: return "prefixed_decimal";
        case parsing::NumberingStyle::Unknown: return "unknown";
    }
    return "unknown";
}
const char* name(parsing::HierarchyKind v) {
    switch (v) {
        case parsing::HierarchyKind::Root: return "root";
        case parsing::HierarchyKind::KnownParent: return "known_parent";
        case parsing::HierarchyKind::Unknown: return "unknown";
    }
    return "unknown";
}
const char* name(mapping::MappingStatus v) {
    switch (v) {
        case mapping::MappingStatus::Resolved: return "resolved";
        case mapping::MappingStatus::Ambiguous: return "ambiguous";
        case mapping::MappingStatus::Unresolved: return "unresolved";
    }
    return "unknown";
}
const char* name(mapping::ResolutionMethod v) {
    switch (v) {
        case mapping::ResolutionMethod::ManualEntry: return "manual_entry";
        case mapping::ResolutionMethod::ManualOffset: return "manual_offset";
        case mapping::ResolutionMethod::AssociatedLocalLink: return "associated_local_link";
        case mapping::ResolutionMethod::ViewerLabel: return "viewer_label";
        case mapping::ResolutionMethod::InferredOffset: return "inferred_offset";
        case mapping::ResolutionMethod::HeadingMatch: return "heading_match";
    }
    return "unknown";
}
Value indices(const std::vector<PageIndex>& values) {
    auto array = Value::make_array();
    for (const auto v : values) array.push(Value::of(v));
    return array;
}
Value optional_string(const std::optional<std::string>& value) {
    return value ? Value::of(*value) : Value::null();
}

Value plan_value(const writer::BookmarkPlan& plan) {
    auto root = Value::make_object();
    root.add("schema_version", Value::of(plan.schema_version));
    root.add("page_index_base", Value::of(plan.page_index_base));
    auto& input = root.add("input", Value::make_object());
    input.add("sha256", Value::of(codec::hex(plan.input.sha256)));
    input.add("page_count", Value::of(plan.input.page_count));
    root.add("existing_outline_policy", Value::of("replace_in_copy"));
    auto& nodes = root.add("nodes", Value::make_array());
    for (const auto& node : plan.nodes) {
        auto& n = nodes.push(Value::make_object());
        n.add("id", Value::of(node.id));
        n.add("parent_id", optional_string(node.parent_id));
        n.add("title", Value::of(node.title));
        n.add("destination", Value::make_object())
            .add("pdf_page_index", Value::of(node.destination.pdf_page_index));
    }
    auto& omitted = root.add("omitted_entries", Value::make_array());
    for (const auto& o : plan.omitted_entries) {
        auto& v = omitted.push(Value::make_object());
        v.add("entry_id", Value::of(o.entry_id));
        v.add("reason", Value::of(o.reason));
    }
    auto& promotions = root.add("promotions", Value::make_array());
    for (const auto& p : plan.promotions) {
        auto& v = promotions.push(Value::make_object());
        v.add("node_id", Value::of(p.node_id));
        v.add("original_parent_id", optional_string(p.original_parent_id));
        v.add("new_parent_id", optional_string(p.new_parent_id));
        v.add("reason", Value::of(p.reason));
    }
    return root;
}

// ---- strict decoding helpers

struct Decoder {
    std::string error;
    bool fail(const std::string& message) {
        if (error.empty()) error = message;
        return false;
    }
    bool members(const Value& object, const std::string& where,
                 std::initializer_list<const char*> allowed) {
        if (object.kind != Value::Kind::Object)
            return fail(where + " must be an object");
        std::set<std::string> names(allowed.begin(), allowed.end());
        for (const auto& member : object.object)
            if (!names.count(member.key))
                return fail(where + " has unsupported member '" + member.key + "'");
        for (const auto& required : names)
            if (!object.find(required))
                return fail(where + " is missing '" + required + "'");
        return true;
    }
    bool string(const Value& value, const std::string& where, std::string& out) {
        if (value.kind != Value::Kind::String) return fail(where + " must be a string");
        out = value.text;
        return true;
    }
    bool optional_string(const Value& value, const std::string& where,
                         std::optional<std::string>& out) {
        if (value.kind == Value::Kind::Null) { out.reset(); return true; }
        std::string text;
        if (!string(value, where, text)) return false;
        out = std::move(text);
        return true;
    }
    bool int32(const Value& value, const std::string& where, std::int32_t& out) {
        const auto v = value.as_int64();
        if (!v || *v < std::numeric_limits<std::int32_t>::min() ||
            *v > std::numeric_limits<std::int32_t>::max())
            return fail(where + " must be a 32-bit integer");
        out = static_cast<std::int32_t>(*v);
        return true;
    }
    bool array(const Value& value, const std::string& where) {
        return value.kind == Value::Kind::Array || fail(where + " must be an array");
    }
};

}  // namespace

std::string plan_json(const writer::BookmarkPlan& plan) {
    return json::write(plan_value(plan));
}

Result<writer::BookmarkPlan> parse_plan_json(const std::string& text) {
    auto parsed = json::parse(text);
    if (!parsed) return parsed.error();
    const Value& root = parsed.value();
    Decoder d;
    writer::BookmarkPlan plan;
    const auto invalid = [&] {
        return Error{ErrorCode::InvalidArgument, "Invalid plan JSON: " + d.error};
    };
    if (!d.members(root, "plan", {"schema_version", "page_index_base", "input",
                                   "existing_outline_policy", "nodes",
                                   "omitted_entries", "promotions"}))
        return invalid();
    if (!d.int32(*root.find("schema_version"), "schema_version", plan.schema_version) ||
        !d.int32(*root.find("page_index_base"), "page_index_base", plan.page_index_base))
        return invalid();
    if (plan.schema_version != writer::kPlanSchemaVersion)
        return Error{ErrorCode::InvalidArgument,
                     "Unsupported plan schema_version " +
                         std::to_string(plan.schema_version)};
    const Value& input = *root.find("input");
    std::string digest;
    if (!d.members(input, "input", {"sha256", "page_count"}) ||
        !d.string(*input.find("sha256"), "input.sha256", digest) ||
        !d.int32(*input.find("page_count"), "input.page_count", plan.input.page_count))
        return invalid();
    const auto bytes = codec::unhex(digest);
    if (!bytes) {
        d.fail("input.sha256 must be 64 hexadecimal digits");
        return invalid();
    }
    plan.input.sha256 = *bytes;
    std::string policy;
    if (!d.string(*root.find("existing_outline_policy"), "existing_outline_policy", policy))
        return invalid();
    if (policy != "replace_in_copy") {
        d.fail("existing_outline_policy must be \"replace_in_copy\"");
        return invalid();
    }
    const Value& nodes = *root.find("nodes");
    if (!d.array(nodes, "nodes")) return invalid();
    for (std::size_t i = 0; i < nodes.array.size(); ++i) {
        const std::string where = "nodes[" + std::to_string(i) + "]";
        const Value& n = nodes.array[i];
        writer::BookmarkNode node;
        if (!d.members(n, where, {"id", "parent_id", "title", "destination"}) ||
            !d.string(*n.find("id"), where + ".id", node.id) ||
            !d.optional_string(*n.find("parent_id"), where + ".parent_id", node.parent_id) ||
            !d.string(*n.find("title"), where + ".title", node.title) ||
            !d.members(*n.find("destination"), where + ".destination", {"pdf_page_index"}) ||
            !d.int32(*n.find("destination")->find("pdf_page_index"),
                     where + ".destination.pdf_page_index",
                     node.destination.pdf_page_index))
            return invalid();
        plan.nodes.push_back(std::move(node));
    }
    const Value& omitted = *root.find("omitted_entries");
    if (!d.array(omitted, "omitted_entries")) return invalid();
    for (std::size_t i = 0; i < omitted.array.size(); ++i) {
        const std::string where = "omitted_entries[" + std::to_string(i) + "]";
        writer::OmittedEntry entry;
        const Value& o = omitted.array[i];
        if (!d.members(o, where, {"entry_id", "reason"}) ||
            !d.string(*o.find("entry_id"), where + ".entry_id", entry.entry_id) ||
            !d.string(*o.find("reason"), where + ".reason", entry.reason))
            return invalid();
        plan.omitted_entries.push_back(std::move(entry));
    }
    const Value& promotions = *root.find("promotions");
    if (!d.array(promotions, "promotions")) return invalid();
    for (std::size_t i = 0; i < promotions.array.size(); ++i) {
        const std::string where = "promotions[" + std::to_string(i) + "]";
        writer::Promotion promotion;
        const Value& p = promotions.array[i];
        if (!d.members(p, where, {"node_id", "original_parent_id", "new_parent_id",
                                  "reason"}) ||
            !d.string(*p.find("node_id"), where + ".node_id", promotion.node_id) ||
            !d.optional_string(*p.find("original_parent_id"),
                               where + ".original_parent_id",
                               promotion.original_parent_id) ||
            !d.optional_string(*p.find("new_parent_id"), where + ".new_parent_id",
                               promotion.new_parent_id) ||
            !d.string(*p.find("reason"), where + ".reason", promotion.reason))
            return invalid();
        plan.promotions.push_back(std::move(promotion));
    }
    return plan;
}

std::string analysis_report_json(const AnalysisReport& report,
                                 const AnalysisOptions& options) {
    auto root = Value::make_object();
    root.add("schema_version", Value::of(1));
    root.add("kind", Value::of("pdfbookmark.analysis"));
    root.add("page_index_base", Value::of(0));
    auto& input = root.add("input", Value::make_object());
    input.add("sha256", Value::of(codec::hex(report.input.sha256)));
    input.add("page_count", Value::of(report.input.page_count));
    input.add("display_path", Value::of(report.input.display_path.u8string()));
    root.add("outcome", Value::of(outcome_name(report.outcome)));
    root.add("stop_reasons", strings(report.stop_reasons));
    root.add("diagnostics", strings(report.diagnostics));

    auto& opts = root.add("options", Value::make_object());
    const auto& l = options.limits;
    auto& limits = opts.add("limits", Value::make_object());
    limits.add("initial_pages", count(l.initial_pages));
    limits.add("batch_pages", count(l.batch_pages));
    limits.add("max_search_pages", count(l.max_search_pages));
    limits.add("max_evidence_pages", count(l.max_evidence_pages));
    limits.add("max_mapping_rounds", count(l.max_mapping_rounds));
    limits.add("ocr_budget", count(l.ocr_budget));
    opts.add("mode", Value::of(options.mode == text::AcquisitionMode::Auto ? "auto"
                               : options.mode == text::AcquisitionMode::OcrOnly
                                   ? "ocr" : "embedded"));
    opts.add("ocr_models_loaded", Value::of(options.models.has_value()));
    opts.add("candidate_id", optional_string(options.candidate_id));
    opts.add("candidate_tie_ratio", Value::of(options.candidate_tie_ratio, 4));
    opts.add("sections_supplied", Value::of(options.sections.has_value()));
    opts.add("override_count", count(options.overrides.size()));
    opts.add("allow_partial", Value::of(options.plan.allow_partial));
    opts.add("flat_outline_for_unknown_hierarchy",
             Value::of(options.plan.flat_outline_for_unknown_hierarchy));
    opts.add("title_style", Value::of(
        options.plan.title_style == PlanPolicy::TitleStyle::Chapter ? "chapter"
                                                                    : "printed"));

    auto& acquisition = root.add("acquisition", Value::make_object());
    acquisition.add("policy_id", Value::of(report.acquisition_policy_id));
    acquisition.add("model_identity", Value::of(report.model_identity));
    acquisition.add("configurations", strings(report.configurations));
    acquisition.add("ocr_budget", count(report.ocr_budget));
    acquisition.add("ocr_attempts_used", count(report.ocr_attempts_used));
    acquisition.add("search_pages", indices(report.search_pages));
    acquisition.add("search_covered_document", Value::of(report.search_covered_document));
    acquisition.add("evidence_pages", indices(report.evidence_pages));
    auto& pages = root.add("pages", Value::make_array());
    for (std::size_t i = 0; i < report.pages.size(); ++i)
        pages.push(codec::page(report.pages[i], report.page_configuration[i]));

    if (report.detection) {
        auto& det = root.add("detection", Value::make_object());
        det.add("policy_id", Value::of(report.detection->policy_id));
        det.add("diagnostics", strings(report.detection->diagnostics));
        auto& candidates = det.add("candidates", Value::make_array());
        for (const auto& c : report.detection->candidates) {
            auto& v = candidates.push(Value::make_object());
            v.add("id", Value::of(c.id));
            v.add("score", Value::of(c.score, 3));
            v.add("start", Value::of(name(c.start)));
            v.add("end", Value::of(name(c.end)));
            auto& cp = v.add("pages", Value::make_array());
            for (const auto& p : c.pages) {
                auto& pv = cp.push(Value::make_object());
                pv.add("page_index", Value::of(p.page_index));
                pv.add("revision", Value::of(static_cast<std::uint64_t>(p.revision)));
                pv.add("score", Value::of(p.score, 3));
                pv.add("degraded", Value::of(p.degraded));
            }
            auto& gaps = v.add("interruptions", Value::make_array());
            for (const auto& g : c.interruptions) {
                auto& gv = gaps.push(Value::make_object());
                gv.add("page_index", Value::of(g.page_index));
                gv.add("reason", Value::of(g.reason));
            }
            v.add("reasons", strings(c.reasons));
            v.add("limitations", strings(c.limitations));
        }
        auto& reviews = det.add("page_reviews", Value::make_array());
        for (const auto& r : report.detection->pages) {
            auto& v = reviews.push(Value::make_object());
            v.add("page_index", Value::of(r.page_index));
            v.add("status", Value::of(name(r.status)));
            v.add("score", Value::of(r.score, 3));
            v.add("reasons", strings(r.reasons));
        }
    } else {
        root.add("detection", Value::null());
    }

    auto& choice = root.add("candidate_choice", Value::make_object());
    choice.add("chosen_id", optional_string(report.candidate.chosen_id));
    choice.add("explicit_selection", Value::of(report.candidate.explicit_selection));
    choice.add("reason", Value::of(report.candidate.reason));
    choice.add("alternatives", strings(report.candidate.alternatives));

    if (report.parsed) {
        const auto& p = *report.parsed;
        auto& parse = root.add("parse", Value::make_object());
        parse.add("candidate_id", Value::of(p.candidate_id));
        parse.add("policy_id", Value::of(p.policy_id));
        parse.add("completeness", Value::of(
            p.completeness == parsing::ParseCompleteness::Complete ? "complete"
                                                                   : "incomplete"));
        parse.add("start", Value::of(name(p.start)));
        parse.add("end", Value::of(name(p.end)));
        parse.add("missing_pages", indices(p.missing_pages));
        parse.add("diagnostics", strings(p.diagnostics));
        auto& entries = parse.add("entries", Value::make_array());
        for (const auto& e : p.entries) {
            auto& v = entries.push(Value::make_object());
            v.add("id", Value::of(e.id));
            v.add("title", Value::of(e.title));
            v.add("order", count(e.order));
            if (e.printed_reference) {
                const auto& r = *e.printed_reference;
                auto& rv = v.add("reference", Value::make_object());
                rv.add("literal", Value::of(r.literal));
                rv.add("numbering", Value::of(name(r.numbering)));
                rv.add("prefix", Value::of(r.prefix));
                rv.add("ordinal", r.ordinal ? count(*r.ordinal) : Value::null());
                rv.add("is_range", Value::of(r.is_range));
                rv.add("range_end", r.range_end ? count(*r.range_end) : Value::null());
                rv.add("uncertain", Value::of(r.uncertain));
                rv.add("reasons", strings(r.reasons));
            } else {
                v.add("reference", Value::null());
            }
            auto& h = v.add("hierarchy", Value::make_object());
            h.add("kind", Value::of(name(e.hierarchy.kind)));
            h.add("parent_id", optional_string(e.hierarchy.parent_id));
            h.add("reasons", strings(e.hierarchy.reasons));
            v.add("sources", codec::sources(e.sources));
            v.add("diagnostics", strings(e.diagnostics));
        }
        auto& unparsed = parse.add("unparsed", Value::make_array());
        for (const auto& u : p.unparsed) {
            auto& v = unparsed.push(Value::make_object());
            v.add("text", Value::of(u.text));
            v.add("reason", Value::of(u.reason));
            v.add("sources", codec::sources(u.sources));
        }
    } else {
        root.add("parse", Value::null());
    }

    if (report.mapping) {
        const auto& m = *report.mapping;
        auto& map = root.add("mapping", Value::make_object());
        map.add("policy_id", Value::of(m.policy_id));
        map.add("rounds", count(report.mapping_rounds));
        auto& sections = map.add("sections", Value::make_array());
        for (const auto& s : report.sections) {
            auto& v = sections.push(Value::make_object());
            v.add("id", Value::of(s.id));
            v.add("first", Value::of(s.first));
            v.add("end", Value::of(s.end));
            v.add("style", Value::of(name(s.style)));
            v.add("prefix", Value::of(s.prefix));
            v.add("origin", Value::of(s.origin));
            v.add("viewer_labels_match_printed", Value::of(s.viewer_labels_match_printed));
        }
        auto& entries = map.add("entries", Value::make_array());
        for (const auto& e : m.entries) {
            auto& v = entries.push(Value::make_object());
            v.add("entry_id", Value::of(e.entry_id));
            v.add("status", Value::of(name(e.status)));
            v.add("pdf_page_index", e.pdf_page_index ? Value::of(*e.pdf_page_index)
                                                     : Value::null());
            v.add("method", e.method ? Value::of(name(*e.method)) : Value::null());
            v.add("section_id", optional_string(e.section_id));
            auto& alternatives = v.add("alternatives", Value::make_array());
            for (const auto& a : e.alternatives) {
                auto& av = alternatives.push(Value::make_object());
                av.add("pdf_page_index", Value::of(a.pdf_page_index));
                av.add("method", Value::of(name(a.method)));
                av.add("reason", Value::of(a.reason));
                av.add("sources", codec::sources(a.sources));
            }
            v.add("supporting_pages", indices(e.supporting_pages));
            v.add("supporting_sources", codec::sources(e.supporting_sources));
            v.add("reasons", strings(e.reasons));
        }
        auto& requests = map.add("unfulfilled_requests", Value::make_array());
        for (const auto& r : report.unfulfilled_requests) {
            auto& v = requests.push(Value::make_object());
            v.add("id", Value::of(r.id));
            v.add("first", Value::of(r.first));
            v.add("end", Value::of(r.end));
            v.add("purpose", Value::of(r.purpose));
            v.add("priority", Value::of(r.priority));
        }
        map.add("diagnostics", strings(m.diagnostics));
    } else {
        root.add("mapping", Value::null());
    }

    auto& plan = root.add("plan", Value::make_object());
    plan.add("ready", Value::of(report.plan.ready));
    plan.add("blockers", strings(report.plan.blockers));
    plan.add("choices", strings(report.plan.choices));
    auto& issues = plan.add("validation_issues", Value::make_array());
    if (report.plan.validation)
        for (const auto& issue : report.plan.validation->issues) {
            auto& v = issues.push(Value::make_object());
            v.add("node_id", Value::of(issue.node_id));
            v.add("field", Value::of(issue.field));
            v.add("message", Value::of(issue.message));
        }
    // A draft is embedded for inspection only; the plan file is written
    // separately and only when ready.
    plan.add("draft", report.plan.plan ? plan_value(*report.plan.plan) : Value::null());
    return json::write(root);
}

}  // namespace pdfbookmark::engine
