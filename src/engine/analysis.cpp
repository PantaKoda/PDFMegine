#include <pdfbookmark/engine/analysis.hpp>

#include "ledger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <utility>

namespace pdfbookmark::engine {
namespace {

const char* status_name(mapping::MappingStatus status) {
    switch (status) {
        case mapping::MappingStatus::Resolved: return "resolved";
        case mapping::MappingStatus::Ambiguous: return "ambiguous";
        case mapping::MappingStatus::Unresolved: return "unresolved";
    }
    return "unknown";
}

std::string score_text(double score) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", score);
    return buffer;
}

std::vector<PageIndex> range(PageIndex first, PageIndex end) {
    std::vector<PageIndex> out;
    for (PageIndex i = first; i < end; ++i) out.push_back(i);
    return out;
}

// First word of `title` and the rest, split at the first space.
std::pair<std::string, std::string> first_word(const std::string& title) {
    const auto space = title.find(' ');
    if (space == std::string::npos) return {title, {}};
    std::size_t rest = space;
    while (rest < title.size() && title[rest] == ' ') ++rest;
    return {title.substr(0, space), title.substr(rest)};
}

// E-18: "1 Modern processors" -> "Chapter 1: Modern processors" for
// top-level nodes; "A Topology" -> "Appendix A: Topology" only for clear
// appendices (it has "A.x" children, or it follows a recognised appendix
// letter). Returns the number of titles changed.
std::size_t style_chapter_titles(writer::BookmarkPlan& plan) {
    std::map<std::string, std::vector<const writer::BookmarkNode*>> children;
    for (const auto& node : plan.nodes)
        if (node.parent_id) children[*node.parent_id].push_back(&node);
    std::size_t changed = 0;
    char next_appendix = 0;  // Letter expected after a recognised appendix.
    for (auto& node : plan.nodes) {
        if (node.parent_id) continue;
        const auto [word, rest] = first_word(node.title);
        if (rest.empty()) continue;
        const bool number = std::all_of(word.begin(), word.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
        if (number && word.size() <= 3) {
            node.title = "Chapter " + word + ": " + rest;
            ++changed;
            continue;
        }
        if (word.size() != 1 || word[0] < 'A' || word[0] > 'Z') continue;
        const char letter = word[0];
        const std::string prefix = std::string(1, letter) + ".";
        const auto& kids = children[node.id];
        const bool has_sections = std::any_of(
            kids.begin(), kids.end(), [&](const writer::BookmarkNode* kid) {
                return kid->title.compare(0, prefix.size(), prefix) == 0;
            });
        if (has_sections || (next_appendix && letter == next_appendix)) {
            node.title = "Appendix " + word + ": " + rest;
            next_appendix = static_cast<char>(letter + 1);
            ++changed;
        }
    }
    return changed;
}

class Run {
public:
    Run(text::TextDocument& document, const AnalysisOptions& options,
        const RunControl& control, const AnalysisProgressCallback& progress,
        AnalysisReport& report)
        : document_(document), options_(options), control_(control),
          progress_(progress), report_(report),
          ledger_(options.mode, options.raster, options.limits.ocr_budget) {}

    Result<bool> execute();

private:
    text::TextDocument& document_;
    const AnalysisOptions& options_;
    const RunControl& control_;
    const AnalysisProgressCallback& progress_;
    AnalysisReport& report_;
    detail::Ledger ledger_;
    std::map<PageIndex, detail::AcquiredPage> acquired_;
    std::map<PageIndex, text::PdfPageFacts> facts_;
    bool cancelled_ = false;

    bool stop() {
        if (control_.is_cancelled()) cancelled_ = true;
        return cancelled_;
    }
    std::optional<Error> acquire(const std::vector<PageIndex>& pages,
                                 const std::string& stage) {
        if (pages.empty()) return std::nullopt;
        bool complete = true;
        auto batch = ledger_.acquire(document_, pages, control_, complete);
        if (!batch) return batch.error();
        for (auto& page : batch.value())
            acquired_[page.page.page_index] = std::move(page);
        if (!complete) cancelled_ = true;
        if (progress_) progress_({stage, acquired_.size()});
        return std::nullopt;
    }
    std::vector<text::PageAcquisition> pages_in(
        const std::vector<PageIndex>& indices) const {
        std::vector<text::PageAcquisition> out;
        for (const auto index : indices) {
            const auto found = acquired_.find(index);
            if (found != acquired_.end()) out.push_back(found->second.page);
        }
        return out;
    }
    void refresh_facts() {
        text::PdfFactsRequest request;
        for (const auto& entry : acquired_)
            if (!facts_.count(entry.first)) request.pages.push_back(entry.first);
        if (request.pages.empty()) return;
        auto facts = document_.read_facts(request);
        if (!facts) {
            report_.diagnostics.push_back("PDF facts unavailable: " +
                                          facts.error().message);
            return;
        }
        for (auto& fact : facts.value().pages)
            facts_[fact.page_index] = std::move(fact);
    }

    Result<bool> search();
    bool choose();
    std::vector<mapping::NumberingSection> default_sections(
        const detection::TocCandidate& chosen);
    Result<bool> map_entries();
    bool split_front_matter();
    void assemble();
    void finish();
};

Result<bool> Run::search() {
    const auto count = static_cast<std::size_t>(document_.page_count());
    const std::size_t limit = std::min(options_.limits.max_search_pages, count);
    std::size_t end = std::min(options_.limits.initial_pages, limit);
    if (auto error = acquire(range(0, static_cast<PageIndex>(end)), "search"))
        return *error;
    while (true) {
        if (stop()) break;
        auto detected = detection::detect(
            pages_in(range(0, static_cast<PageIndex>(end))), options_.detection);
        if (!detected) return detected.error();
        report_.detection = detected.take();
        std::string why;
        for (const auto& candidate : report_.detection->candidates)
            if (candidate.end != detection::BoundaryState::Closed &&
                static_cast<std::size_t>(candidate.pages.back().page_index) + 1 ==
                    end &&
                end < count) {
                if (end < limit)
                    why = "candidate " + candidate.id +
                          " may continue past the searched pages";
                else
                    report_.stop_reasons.push_back(
                        "Candidate " + candidate.id +
                        " may continue beyond the search page limit");
            }
        if (why.empty() && report_.detection->candidates.empty() && end < limit)
            why = "no TOC candidate in searched pages";
        if (why.empty()) break;
        const std::size_t next = std::min(end + options_.limits.batch_pages, limit);
        report_.diagnostics.push_back("Extended search to " +
                                      std::to_string(next) + " pages: " + why);
        if (auto error = acquire(range(static_cast<PageIndex>(end),
                                       static_cast<PageIndex>(next)),
                                 "search"))
            return *error;
        end = next;
    }
    report_.search_pages = range(0, static_cast<PageIndex>(end));
    report_.search_covered_document = end >= count;
    if (report_.detection && report_.detection->candidates.empty())
        report_.stop_reasons.push_back(
            "No TOC candidate in " + std::to_string(end) + " of " +
            std::to_string(count) + " pages searched" +
            (report_.search_covered_document ? "" : " (search page limit)"));
    return true;
}

bool Run::choose() {
    auto candidates = report_.detection->candidates;
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.score > b.score; });
    auto& choice = report_.candidate;
    if (options_.candidate_id) {
        choice.explicit_selection = true;
        const auto found = std::find_if(
            candidates.begin(), candidates.end(),
            [&](const auto& c) { return c.id == *options_.candidate_id; });
        if (found == candidates.end()) {
            choice.reason = "Requested candidate '" + *options_.candidate_id +
                            "' was not detected";
            report_.plan.blockers.push_back(choice.reason);
            for (const auto& c : candidates) choice.alternatives.push_back(c.id);
            return false;
        }
        choice.chosen_id = found->id;
        choice.reason = "Explicitly selected by caller";
    } else {
        const auto& best = candidates.front();
        if (candidates.size() > 1 &&
            candidates[1].score >= options_.candidate_tie_ratio * best.score) {
            choice.reason = "Candidates " + best.id + " (score " +
                            score_text(best.score) + ") and " +
                            candidates[1].id + " (score " +
                            score_text(candidates[1].score) +
                            ") are too close; explicit selection required";
            report_.plan.blockers.push_back(choice.reason);
            for (const auto& c : candidates) choice.alternatives.push_back(c.id);
            return false;
        }
        choice.chosen_id = best.id;
        choice.reason = candidates.size() == 1
                            ? "Only candidate"
                            : "Highest score by a clear margin";
    }
    for (const auto& c : candidates)
        if (c.id != *choice.chosen_id) choice.alternatives.push_back(c.id);
    return true;
}

std::vector<mapping::NumberingSection> Run::default_sections(
    const detection::TocCandidate& chosen) {
    const PageCount count = document_.page_count();
    const PageIndex last = chosen.pages.back().page_index;
    const PageIndex first = chosen.pages.front().page_index;
    const std::string origin =
        "engine default: one decimal numbering section adjoining the selected "
        "TOC (assumption; supply sections to override)";
    if (last + 1 < count)
        return {{"body", last + 1, count, parsing::NumberingStyle::Decimal, "",
                 origin, false}};
    if (first > 0)
        return {{"body", 0, first, parsing::NumberingStyle::Decimal, "",
                 origin, false}};
    report_.diagnostics.push_back("No page range available for a default section");
    return {};
}

// With the default sections only: when the TOC cites Roman page numbers and
// the decimal body offset was inferred (two anchors plus target confirmation
// in S4), the pages before printed decimal page 1 become a Roman "front"
// section and the body starts at printed page 1. S4 still needs its own
// anchors inside the Roman section; nothing is inferred here beyond scope.
bool Run::split_front_matter() {
    const auto& entries = report_.parsed->entries;
    const bool roman = std::any_of(entries.begin(), entries.end(), [](const auto& e) {
        return e.printed_reference && e.printed_reference->ordinal &&
               e.printed_reference->numbering == parsing::NumberingStyle::Roman;
    });
    if (!roman || !report_.mapping || report_.sections.size() != 1) return false;
    std::map<std::string, const parsing::TocEntry*> by_id;
    for (const auto& e : entries) by_id[e.id] = &e;
    std::map<std::int64_t, std::size_t> offsets;
    for (const auto& m : report_.mapping->entries) {
        if (m.status != mapping::MappingStatus::Resolved ||
            m.method != mapping::ResolutionMethod::InferredOffset)
            continue;
        const auto* e = by_id.at(m.entry_id);
        if (e->printed_reference->numbering != parsing::NumberingStyle::Decimal)
            continue;
        ++offsets[static_cast<std::int64_t>(*m.pdf_page_index) -
                  static_cast<std::int64_t>(*e->printed_reference->ordinal)];
    }
    if (offsets.size() != 1) return false;  // None, or not a single offset.
    const std::int64_t first_decimal = offsets.begin()->first + 1;
    const PageCount count = document_.page_count();
    if (first_decimal < 1 || first_decimal >= count) return false;
    const auto body_first = static_cast<PageIndex>(first_decimal);
    const std::string origin =
        "engine default: Roman front matter before printed page 1 (physical "
        "index " + std::to_string(body_first) + ", from the inferred body "
        "offset); decimal body from there";
    report_.sections = {
        {"front", 0, body_first, parsing::NumberingStyle::Roman, "", origin, false},
        {"body", body_first, count, parsing::NumberingStyle::Decimal, "", origin, false}};
    report_.diagnostics.push_back("Assumed numbering sections: " + origin);
    return true;
}

Result<bool> Run::map_entries() {
    const auto& entries = report_.parsed->entries;
    const std::size_t count = static_cast<std::size_t>(document_.page_count());
    std::set<PageIndex> requested;
    std::size_t evidence_used = 0;
    while (true) {
        if (stop()) {
            report_.stop_reasons.push_back("Cancelled during mapping");
            break;
        }
        refresh_facts();
        mapping::DocumentEvidence evidence;
        evidence.input = report_.input;
        for (const auto& entry : acquired_) evidence.pages.push_back(entry.second.page);
        for (const auto& entry : facts_) evidence.facts.push_back(entry.second);
        evidence.sections = report_.sections;
        evidence.entry_sections = options_.entry_sections;
        evidence.limitations.push_back(
            "Evidence covers " + std::to_string(acquired_.size()) + " of " +
            std::to_string(count) + " pages");
        auto mapped = mapping::map(entries, evidence, options_.overrides,
                                   options_.mapping);
        if (!mapped) return mapped.error();
        report_.mapping = mapped.take();
        ++report_.mapping_rounds;
        std::vector<PageIndex> fetch;
        bool budget_hit = false;
        for (const auto& request : report_.mapping->requests)
            for (PageIndex p = request.first; p < request.end; ++p) {
                if (acquired_.count(p) || requested.count(p)) continue;
                if (evidence_used + fetch.size() >=
                    options_.limits.max_evidence_pages) {
                    budget_hit = true;
                    continue;
                }
                requested.insert(p);
                fetch.push_back(p);
            }
        if (fetch.empty()) {
            report_.stop_reasons.push_back(
                budget_hit ? "Evidence page budget exhausted"
                : report_.mapping->requests.empty()
                    ? "Mapping requested no further evidence"
                    : "Mapping requests would not add new evidence");
            break;
        }
        if (report_.mapping_rounds >= options_.limits.max_mapping_rounds) {
            report_.stop_reasons.push_back("Mapping round limit reached");
            break;
        }
        std::sort(fetch.begin(), fetch.end());
        if (auto error = acquire(fetch, "evidence")) return *error;
        evidence_used += fetch.size();
        report_.evidence_pages.insert(report_.evidence_pages.end(),
                                      fetch.begin(), fetch.end());
    }
    for (const auto& request : report_.mapping->requests) {
        bool satisfied = true;
        for (PageIndex p = request.first; p < request.end; ++p)
            if (!acquired_.count(p)) satisfied = false;
        if (!satisfied) report_.unfulfilled_requests.push_back(request);
    }
    return true;
}

void Run::assemble() {
    auto& out = report_.plan;
    const auto& parsed = *report_.parsed;
    const auto& policy = options_.plan;
    std::map<std::string, const parsing::TocEntry*> by_id;
    for (const auto& entry : parsed.entries) by_id[entry.id] = &entry;
    std::map<std::string, const mapping::EntryMapping*> mapped;
    for (const auto& entry : report_.mapping->entries) mapped[entry.entry_id] = &entry;
    std::set<std::string> retained;
    for (const auto& entry : parsed.entries) {
        const auto m = mapped.find(entry.id);
        if (m != mapped.end() && m->second->status == mapping::MappingStatus::Resolved)
            retained.insert(entry.id);
    }

    // S3 reports a single completeness flag. Entry-level causes (unknown
    // hierarchy, uncertain references) are handled per entry below by the
    // hierarchy policy and mapping; every other cause needs allow_partial.
    if (parsed.completeness == parsing::ParseCompleteness::Incomplete) {
        std::vector<std::string> causes;
        if (!parsed.unparsed.empty())
            causes.push_back(std::to_string(parsed.unparsed.size()) +
                             " unparsed fragments");
        if (!parsed.missing_pages.empty())
            causes.push_back(std::to_string(parsed.missing_pages.size()) +
                             " missing candidate pages");
        if (parsed.start != detection::BoundaryState::Closed)
            causes.push_back("TOC start boundary not closed");
        if (parsed.end != detection::BoundaryState::Closed)
            causes.push_back("TOC end boundary not closed");
        const bool entry_level = std::any_of(
            parsed.entries.begin(), parsed.entries.end(), [](const auto& e) {
                return e.hierarchy.kind == parsing::HierarchyKind::Unknown ||
                       (e.printed_reference && e.printed_reference->uncertain);
            });
        if (causes.empty() && !entry_level) {
            // Name the degraded TOC pages and S1's reasons instead of a
            // generic "evidence limits" message.
            std::string pages, reasons;
            std::set<std::string> seen;
            if (report_.detection && report_.candidate.chosen_id)
                for (const auto& candidate : report_.detection->candidates) {
                    if (candidate.id != *report_.candidate.chosen_id) continue;
                    for (const auto& page : candidate.pages) {
                        const auto found = acquired_.find(page.page_index);
                        if (found == acquired_.end() ||
                            found->second.page.outcome != text::Outcome::Degraded)
                            continue;
                        pages += (pages.empty() ? "" : ", ") +
                                 std::to_string(page.page_index);
                        const auto& acquired = found->second.page;
                        for (const auto* list : {&acquired.reasons,
                                                 &acquired.assessment.reasons})
                            for (const auto& reason : *list)
                                if (seen.insert(reason).second)
                                    reasons += (reasons.empty() ? "" : "; ") + reason;
                    }
                }
            causes.push_back(pages.empty()
                ? "evidence limits reported by the parser (see parse diagnostics)"
                : "TOC page(s) " + pages + " were read with reduced quality (" +
                      reasons + ")");
        }
        for (const auto& cause : causes) {
            const std::string why = "Parse of " + parsed.candidate_id +
                                    " is incomplete: " + cause;
            if (policy.allow_partial) out.choices.push_back("Accepted: " + why);
            else out.blockers.push_back(why);
        }
    }
    writer::BookmarkPlan plan;
    plan.input = report_.input;
    for (const auto& entry : parsed.entries) {
        if (retained.count(entry.id)) continue;
        const auto m = mapped.find(entry.id);
        const std::string status =
            m == mapped.end() ? "unmapped" : status_name(m->second->status);
        std::string reason = status;
        if (m != mapped.end() && !m->second->reasons.empty())
            reason += ": " + m->second->reasons.back();
        if (policy.allow_partial) {
            plan.omitted_entries.push_back({entry.id, reason});
            out.choices.push_back("Omitted '" + entry.title + "' (" + reason + ")");
        } else {
            out.blockers.push_back("Entry '" + entry.title + "' is " + reason);
        }
    }
    for (const auto& entry : parsed.entries) {
        if (!retained.count(entry.id)) continue;
        std::optional<std::string> parent;
        std::optional<std::string> original;
        bool ok = true;
        const auto flatten = [&](const std::string& why) {
            if (policy.flat_outline_for_unknown_hierarchy) {
                out.choices.push_back("Flattened '" + entry.title + "': " + why);
                return true;
            }
            out.blockers.push_back("Hierarchy of '" + entry.title +
                                   "' is unknown: " + why);
            return false;
        };
        if (entry.hierarchy.kind == parsing::HierarchyKind::Unknown) {
            ok = flatten("parser could not establish its parent");
        } else if (entry.hierarchy.kind == parsing::HierarchyKind::KnownParent &&
                   entry.hierarchy.parent_id) {
            std::string p = *entry.hierarchy.parent_id;
            original = p;
            for (std::size_t guard = 0; guard <= parsed.entries.size(); ++guard) {
                if (retained.count(p)) { parent = p; break; }
                if (!policy.allow_partial) {  // Blocked above; keep draft honest.
                    parent = p;
                    break;
                }
                const auto found = by_id.find(p);
                if (found == by_id.end()) {
                    out.blockers.push_back("Parent '" + p + "' of '" +
                                           entry.title + "' is not an entry");
                    ok = false;
                    break;
                }
                const auto& h = found->second->hierarchy;
                if (h.kind == parsing::HierarchyKind::Root) break;  // To top level.
                if (h.kind == parsing::HierarchyKind::Unknown) {
                    ok = flatten("an omitted ancestor has unknown hierarchy");
                    break;
                }
                p = *h.parent_id;
            }
            if (ok && policy.allow_partial && parent != original) {
                plan.promotions.push_back(
                    {entry.id, original, parent,
                     parent ? "Promoted to nearest retained ancestor"
                            : "Promoted to top level: no retained ancestor"});
                out.choices.push_back("Promoted '" + entry.title + "'");
            }
        }
        if (!ok) continue;
        plan.nodes.push_back({entry.id, parent, entry.title,
                              {*mapped.at(entry.id)->pdf_page_index}});
    }
    if (plan.nodes.empty()) {
        out.blockers.push_back(
            "No bookmark nodes remain; an empty outline is never written");
        return;
    }
    if (policy.title_style == PlanPolicy::TitleStyle::Chapter) {
        const std::size_t styled = style_chapter_titles(plan);
        out.choices.push_back("Chapter-style titles applied to " +
                              std::to_string(styled) + " top-level bookmark(s)");
    }
    out.validation = writer::validate(plan);
    // A draft that is already blocked (e.g. a child of an unresolved entry
    // keeps its real parent) fails validation as a consequence; list
    // validation issues as blockers only when they are the root cause. They
    // are always kept in `validation` and the JSON report.
    if (out.blockers.empty())
        for (const auto& issue : out.validation->issues)
            out.blockers.push_back("Plan validation: [" + issue.node_id + "] " +
                                   issue.field + ": " + issue.message);
    out.plan = std::move(plan);
    out.ready = out.blockers.empty() && !cancelled_;
}

Result<bool> Run::execute() {
    report_.input = document_.identity();
    const auto searched = search();
    if (!searched) return searched.error();
    if (!cancelled_ && report_.detection &&
        !report_.detection->candidates.empty() && choose()) {
        const auto& candidates = report_.detection->candidates;
        const auto chosen = std::find_if(
            candidates.begin(), candidates.end(),
            [&](const auto& c) { return c.id == *report_.candidate.chosen_id; });
        auto parsed = parsing::parse(*chosen, pages_in(report_.search_pages),
                                     options_.parsing);
        if (!parsed) return parsed.error();
        report_.parsed = parsed.take();
        report_.sections = options_.sections ? *options_.sections
                                             : default_sections(*chosen);
        if (!options_.sections && !report_.sections.empty())
            report_.diagnostics.push_back("Assumed numbering section: " +
                                          report_.sections.front().origin);
        if (report_.parsed->entries.empty()) {
            report_.plan.blockers.push_back("Selected TOC has no parsed entries");
        } else {
            const auto mapped = map_entries();
            if (!mapped) return mapped.error();
            if (!options_.sections && split_front_matter()) {
                report_.unfulfilled_requests.clear();
                const auto remapped = map_entries();
                if (!remapped) return remapped.error();
            }
            assemble();
        }
    }
    finish();
    return true;
}

void Run::finish() {
    for (const auto& entry : acquired_) {
        report_.pages.push_back(entry.second.page);
        report_.page_configuration.push_back(entry.second.configuration);
    }
    report_.configurations = ledger_.configurations();
    report_.acquisition_policy_id = ledger_.policy_id();
    report_.model_identity = ledger_.model_identity();
    report_.ocr_budget = ledger_.budget();
    report_.ocr_attempts_used = ledger_.used();
    for (const auto& d : ledger_.diagnostics()) report_.diagnostics.push_back(d);
    if (ledger_.used() >= ledger_.budget())
        report_.stop_reasons.push_back("OCR budget exhausted");

    if (cancelled_) {
        report_.outcome = AnalysisOutcome::Cancelled;
        report_.plan.ready = false;
        report_.plan.blockers.push_back("Analysis was cancelled");
        return;
    }
    if (!report_.detection || report_.detection->candidates.empty()) {
        const bool unassessable = std::any_of(
            report_.pages.begin(), report_.pages.end(), [](const auto& p) {
                return p.outcome == text::Outcome::Failed ||
                       p.outcome == text::Outcome::Cancelled;
            });
        report_.outcome = unassessable ? AnalysisOutcome::SearchIncomplete
                                       : AnalysisOutcome::NoTocFoundInSearch;
        return;
    }
    report_.outcome = report_.plan.ready ? AnalysisOutcome::PlanReady
                                         : AnalysisOutcome::AnalysisPartial;
}

}  // namespace

const char* outcome_name(AnalysisOutcome outcome) {
    switch (outcome) {
        case AnalysisOutcome::PlanReady: return "plan_ready";
        case AnalysisOutcome::AnalysisPartial: return "analysis_partial";
        case AnalysisOutcome::NoTocFoundInSearch: return "no_toc_found_in_search";
        case AnalysisOutcome::SearchIncomplete: return "search_incomplete";
        case AnalysisOutcome::Cancelled: return "cancelled";
    }
    return "unknown";
}

Result<AnalysisReport> analyze(const std::filesystem::path& input,
                               const AnalysisOptions& options,
                               const RunControl& control,
                               const AnalysisProgressCallback& progress) {
    const auto& l = options.limits;
    if (l.initial_pages == 0 || l.batch_pages == 0 || l.max_search_pages == 0 ||
        l.max_mapping_rounds == 0 || !std::isfinite(options.candidate_tie_ratio) ||
        options.candidate_tie_ratio <= 0 || options.candidate_tie_ratio > 1)
        return Error{ErrorCode::InvalidArgument, "Invalid analysis limits"};
    text::OpenOptions open;
    open.ocr_models = options.models;
    auto opened = text::TextAcquisition{}.open(input, open);
    if (!opened) return opened.error();
    auto document = opened.take();
    AnalysisReport report;
    Run run(document, options, control, progress, report);
    const auto done = run.execute();
    if (!done) return done.error();
    return report;
}

}  // namespace pdfbookmark::engine
