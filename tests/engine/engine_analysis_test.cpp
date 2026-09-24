// Engine analysis orchestration tests on generated fixtures.
// Usage: pdfbookmark_engine_analysis_tests <tests/engine/fixtures dir>
#include <pdfbookmark/engine/analysis.hpp>

#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace pdfbookmark;
using namespace pdfbookmark::engine;
namespace fs = std::filesystem;

void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

AnalysisReport run(const fs::path& pdf, const AnalysisOptions& options = {},
                   const RunControl& control = {}) {
    auto result = analyze(pdf, options, control);
    if (!result) {
        std::cerr << "analyze failed: " << result.error().message << '\n';
        std::exit(1);
    }
    return result.take();
}

void dump(const AnalysisReport& r) {
    std::cerr << "outcome=" << outcome_name(r.outcome) << '\n';
    for (const auto& s : r.stop_reasons) std::cerr << "  stop: " << s << '\n';
    for (const auto& b : r.plan.blockers) std::cerr << "  blocked: " << b << '\n';
    for (const auto& c : r.plan.choices) std::cerr << "  choice: " << c << '\n';
    if (r.detection)
        for (const auto& c : r.detection->candidates) {
            std::cerr << "  candidate " << c.id << " score " << c.score << " pages";
            for (const auto& p : c.pages) std::cerr << ' ' << p.page_index;
            std::cerr << '\n';
        }
    if (r.parsed)
        for (const auto& e : r.parsed->entries)
            std::cerr << "  entry " << e.id << " '" << e.title << "' ref "
                      << (e.printed_reference ? e.printed_reference->literal : "-")
                      << " hierarchy " << static_cast<int>(e.hierarchy.kind)
                      << (e.hierarchy.parent_id ? " parent " + *e.hierarchy.parent_id : "")
                      << '\n';
    if (r.mapping)
        for (const auto& m : r.mapping->entries)
            std::cerr << "  map " << m.entry_id << " status "
                      << static_cast<int>(m.status) << " index "
                      << (m.pdf_page_index ? *m.pdf_page_index : -1)
                      << (m.reasons.empty() ? "" : " | " + m.reasons.back()) << '\n';
}

// (title, parent title or "", destination) in plan order.
struct Node {
    std::string title, parent;
    PageIndex page;
    bool operator==(const Node& o) const {
        return title == o.title && parent == o.parent && page == o.page;
    }
};
std::vector<Node> nodes(const writer::BookmarkPlan& plan) {
    std::map<std::string, std::string> titles;
    for (const auto& n : plan.nodes) titles[n.id] = n.title;
    std::vector<Node> out;
    for (const auto& n : plan.nodes)
        out.push_back({n.title, n.parent_id ? titles[*n.parent_id] : "",
                       n.destination.pdf_page_index});
    return out;
}
void expect(const AnalysisReport& r, AnalysisOutcome outcome,
            const std::string& message) {
    if (r.outcome != outcome) dump(r);
    require(r.outcome == outcome, message);
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: fixtures-dir");
    const fs::path dir = fs::u8path(argv[1]);

    // 1. TOC spanning physical indices 39/40 across the first search batch.
    const auto boundary = run(dir / "boundary.pdf");
    expect(boundary, AnalysisOutcome::PlanReady, "boundary book yields a ready plan");
    require(boundary.detection->candidates.size() == 1 &&
                boundary.detection->candidates[0].pages.size() == 2 &&
                boundary.detection->candidates[0].pages[0].page_index == 39 &&
                boundary.detection->candidates[0].pages[1].page_index == 40,
            "pages 39 and 40 form one candidate (page 39 retained across batches)");
    require(boundary.search_pages.size() == 60 && !boundary.search_covered_document,
            "search extended by one 20-page batch only");
    require(boundary.evidence_pages == std::vector<PageIndex>{60},
            "S4's bounded request for unsearched target index 60 was fulfilled");
    const std::vector<Node> expected = {
        {"Part One", "", 41}, {"Getting Started", "Part One", 41},
        {"Installation", "Part One", 43}, {"Configuration", "Part One", 46},
        {"Part Two", "", 50}, {"Networking", "Part Two", 51},
        {"Storage", "Part Two", 55}, {"Appendix Notes", "", 60}};
    if (!(nodes(*boundary.plan.plan) == expected)) dump(boundary);
    require(nodes(*boundary.plan.plan) == expected,
            "correct chapter destinations and hierarchy (printed n -> index 40+n)");
    require(boundary.plan.plan->input.sha256 == boundary.input.sha256 &&
                boundary.plan.plan->input.page_count == 70 &&
                boundary.plan.validation && boundary.plan.validation->valid,
            "plan is bound to the analyzed input and validated by S5");

    // 1b. Regression (S2-08): short leaders, right-aligned numbers far from
    //     their titles. Page 39 was once dropped, yielding half a TOC.
    const auto short_leaders = run(dir / "boundary_short_leaders.pdf");
    if (!short_leaders.plan.plan || !(nodes(*short_leaders.plan.plan) == expected))
        dump(short_leaders);
    require(short_leaders.outcome == AnalysisOutcome::PlanReady &&
                short_leaders.detection->candidates[0].pages.size() == 2 &&
                nodes(*short_leaders.plan.plan) == expected,
            "wide-gap TOC rows keep both TOC pages and the full plan");

    // 2. Existing outlines never influence analysis (paired fixture).
    const auto outlined = run(dir / "boundary_outlined.pdf");
    require(outlined.input.sha256 != boundary.input.sha256 &&
                outlined.outcome == boundary.outcome &&
                nodes(*outlined.plan.plan) == expected &&
                outlined.candidate.chosen_id == boundary.candidate.chosen_id &&
                outlined.parsed->entries.size() == boundary.parsed->entries.size() &&
                outlined.evidence_pages == boundary.evidence_pages,
            "misleading existing bookmarks do not change semantic results");
    {
        // Normalized reports must match apart from input identity.
        AnalysisOptions options;
        auto a = json::parse(analysis_report_json(boundary, options)).take();
        auto b = json::parse(analysis_report_json(outlined, options)).take();
        for (auto* v : {&a, &b}) {
            for (auto& m : v->object)
                if (m.key == "input") m.value = json::Value::null();
            for (auto& m : v->object)
                if (m.key == "plan")
                    for (auto& pm : m.value.object)
                        if (pm.key == "draft")
                            for (auto& dm : pm.value.object)
                                if (dm.key == "input") dm.value = json::Value::null();
        }
        require(json::write(a) == json::write(b),
                "paired reports are identical after normalizing input identity");
    }

    // 3. Plan JSON codec: round trip and strictness.
    const auto encoded = plan_json(*boundary.plan.plan);
    const auto decoded = parse_plan_json(encoded);
    require(decoded && plan_json(decoded.value()) == encoded &&
                writer::validate(decoded.value()).valid,
            "plan JSON round-trips exactly");
    require(encoded.find("\"existing_outline_policy\": \"replace_in_copy\"") !=
                    std::string::npos &&
                encoded.find("\"page_index_base\": 0") != std::string::npos,
            "plan JSON declares policy and index base");
    const auto mutate = [&](const std::string& from, const std::string& to) {
        std::string copy = encoded;
        const auto at = copy.find(from);
        require(at != std::string::npos, "mutation anchor " + from);
        copy.replace(at, from.size(), to);
        return parse_plan_json(copy);
    };
    require(!mutate("\"replace_in_copy\"", "\"merge\"") &&
                !mutate("\"schema_version\": 1", "\"schema_version\": 2") &&
                !mutate("\"schema_version\": 1", "\"schema_version\": 1, \"extra\": 1") &&
                !mutate("\"page_count\": 70", "\"page_count\": 99999999999") &&
                !mutate("\"page_count\": 70", "\"page_count\": \"70\"") &&
                !mutate("\"omitted_entries\": [],", "") &&
                !mutate("\"sha256\": \"", "\"sha256\": \"zz") &&
                !parse_plan_json(analysis_report_json(boundary, {})),
            "strict plan decoding rejects bad policy, version, members, types");
    {
        auto bad = mutate("\"pdf_page_index\": 41", "\"pdf_page_index\": 700");
        require(bad && !writer::validate(bad.value()).valid,
                "decoded out-of-range destination is caught by S5 validation");
    }

    // 4. Finite evidence budget: target index 60 cannot be confirmed.
    AnalysisOptions no_evidence;
    no_evidence.limits.max_evidence_pages = 0;
    const auto starved = run(dir / "boundary.pdf", no_evidence);
    expect(starved, AnalysisOutcome::AnalysisPartial,
           "unconfirmed target keeps the plan unready");
    require(starved.evidence_pages.empty() &&
                std::any_of(starved.unfulfilled_requests.begin(),
                            starved.unfulfilled_requests.end(),
                            [](const auto& q) { return q.first == 60; }) &&
                std::find(starved.stop_reasons.begin(), starved.stop_reasons.end(),
                          "Evidence page budget exhausted") !=
                    starved.stop_reasons.end() &&
                starved.plan.plan &&
                !std::any_of(starved.plan.plan->nodes.begin(),
                             starved.plan.plan->nodes.end(),
                             [](const auto& n) { return n.title == "Appendix Notes"; }),
            "budget exhaustion is visible and no destination is guessed");
    AnalysisOptions starved_partial = no_evidence;
    starved_partial.plan.allow_partial = true;
    const auto partial = run(dir / "boundary.pdf", starved_partial);
    expect(partial, AnalysisOutcome::PlanReady, "explicit partial policy readies plan");
    require(partial.plan.plan->nodes.size() == 7 &&
                partial.plan.plan->omitted_entries.size() == 1,
            "omission of the unresolved entry is recorded");

    // 5. Search limit without a TOC in reach.
    AnalysisOptions narrow;
    narrow.limits.max_search_pages = 30;
    const auto limited = run(dir / "boundary.pdf", narrow);
    expect(limited, AnalysisOutcome::NoTocFoundInSearch, "TOC beyond the search limit");
    require(limited.search_pages.size() == 30 && !limited.search_covered_document,
            "search stops at its finite limit and says so");

    // 6. Multiple close candidates require explicit selection.
    const auto multi = run(dir / "multi.pdf");
    expect(multi, AnalysisOutcome::AnalysisPartial, "tied candidates block automation");
    require(multi.detection->candidates.size() == 2 && !multi.candidate.chosen_id &&
                multi.candidate.alternatives.size() == 2 && !multi.parsed,
            "both candidates remain distinct and nothing is parsed");
    AnalysisOptions pick;
    for (const auto& c : multi.detection->candidates)
        if (c.pages[0].page_index == 3) pick.candidate_id = c.id;
    const auto picked = run(dir / "multi.pdf", pick);
    expect(picked, AnalysisOutcome::PlanReady, "explicit candidate selection proceeds");
    require(picked.candidate.explicit_selection &&
                nodes(*picked.plan.plan) ==
                    std::vector<Node>{{"Intro", "", 5}, {"Methods", "", 8},
                                      {"Results", "", 12}, {"Summary", "", 16}},
            "selected detailed TOC maps printed n -> index 4+n");
    AnalysisOptions missing_pick;
    missing_pick.candidate_id = "toc-nope";
    const auto no_such = run(dir / "multi.pdf", missing_pick);
    expect(no_such, AnalysisOutcome::AnalysisPartial, "unknown candidate ID is reported");

    // 7. Unknown hierarchy requires correction or explicit flattening.
    const auto unknown = run(dir / "unknown_hierarchy.pdf");
    expect(unknown, AnalysisOutcome::AnalysisPartial, "unknown hierarchy is not flattened silently");
    AnalysisOptions flat;
    flat.plan.flat_outline_for_unknown_hierarchy = true;
    const auto flattened = run(dir / "unknown_hierarchy.pdf", flat);
    expect(flattened, AnalysisOutcome::PlanReady, "explicit flat policy readies plan");
    require(nodes(*flattened.plan.plan) ==
                    std::vector<Node>{{"Alpha", "", 2}, {"Beta", "", 3},
                                      {"Gamma", "", 4}, {"Delta", "", 5}} &&
                !flattened.plan.choices.empty(),
            "flattening is recorded");

    // 8. Unresolved entry: blocked by default; omission + promotion explicit.
    const auto unresolved = run(dir / "unresolved.pdf");
    expect(unresolved, AnalysisOutcome::AnalysisPartial, "unresolved entry blocks default plan");
    AnalysisOptions allow;
    allow.plan.allow_partial = true;
    const auto promoted = run(dir / "unresolved.pdf", allow);
    expect(promoted, AnalysisOutcome::PlanReady, "partial policy readies plan");
    require(nodes(*promoted.plan.plan) ==
                    std::vector<Node>{{"Alpha", "", 2}, {"Beta One", "", 3},
                                      {"Gamma", "", 4}} &&
                promoted.plan.plan->omitted_entries.size() == 1 &&
                promoted.plan.plan->promotions.size() == 1 &&
                !promoted.plan.plan->promotions[0].new_parent_id,
            "child of the omitted entry is promoted to top level, recorded");

    // 8b. Title styles (E-18): printed by default; chapter style renames only
    //     top-level numbered chapters and clear appendices.
    const auto printed = run(dir / "numbered.pdf");
    expect(printed, AnalysisOutcome::PlanReady, "numbered book yields a ready plan");
    const std::vector<Node> printed_nodes = {
        {"1 Introduction", "", 2}, {"1.1 Scope", "1 Introduction", 3},
        {"2 Methods", "", 4}, {"A Short History", "", 5}, {"A Tables", "", 6},
        {"A.1 Units", "A Tables", 7}, {"B Glossary", "", 8}};
    if (!(nodes(*printed.plan.plan) == printed_nodes)) dump(printed);
    require(nodes(*printed.plan.plan) == printed_nodes,
            "default titles are exactly as printed; numbering gives hierarchy");
    AnalysisOptions chapter_titles;
    chapter_titles.plan.title_style = PlanPolicy::TitleStyle::Chapter;
    const auto styled = run(dir / "numbered.pdf", chapter_titles);
    const std::vector<Node> styled_nodes = {
        {"Chapter 1: Introduction", "", 2},
        {"1.1 Scope", "Chapter 1: Introduction", 3},
        {"Chapter 2: Methods", "", 4},
        {"A Short History", "", 5},  // An article, not an appendix letter.
        {"Appendix A: Tables", "", 6},
        {"A.1 Units", "Appendix A: Tables", 7},
        {"Appendix B: Glossary", "", 8}};
    if (!(nodes(*styled.plan.plan) == styled_nodes)) dump(styled);
    require(styled.outcome == AnalysisOutcome::PlanReady &&
                nodes(*styled.plan.plan) == styled_nodes &&
                styled.parsed->entries[0].title == "1 Introduction",
            "chapter style renames chapters/appendices only; evidence unchanged");

    // 9. No TOC in a fully searched document.
    const auto none = run(dir / "no_toc.pdf");
    expect(none, AnalysisOutcome::NoTocFoundInSearch, "no TOC");
    require(none.search_covered_document && none.search_pages.size() == 50 &&
                !none.plan.plan,
            "whole 50-page document searched in 40 + 10 pages; no plan");

    // 10. Cancellation.
    std::atomic_bool cancel{true};
    const auto cancelled = run(dir / "boundary.pdf", {}, RunControl{&cancel});
    expect(cancelled, AnalysisOutcome::Cancelled, "cancelled analysis");
    require(!cancelled.plan.ready, "cancelled analysis never offers a ready plan");

    // 11. Invalid limits and inputs.
    AnalysisOptions invalid;
    invalid.limits.batch_pages = 0;
    require(!analyze(dir / "boundary.pdf", invalid) &&
                !analyze(dir / "missing.pdf"),
            "invalid limits and missing input are errors");

    // 12. Analysis JSON is strictly valid and deterministic.
    const auto again = run(dir / "boundary.pdf");
    const auto report = analysis_report_json(boundary, {});
    require(analysis_report_json(again, {}) == report && json::parse(report),
            "analysis JSON is deterministic and valid");
    std::cout << "Engine analysis fixtures passed\n";
}
