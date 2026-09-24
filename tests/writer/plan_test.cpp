// Links only pdfbookmark::WriterPlan: no qpdf headers or libraries.
#include <pdfbookmark/writer/plan.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace pdfbookmark;
using namespace pdfbookmark::writer;

void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
BookmarkPlan base() {
    BookmarkPlan plan;
    plan.input.sha256.fill(0xAB);
    plan.input.page_count = 10;
    plan.nodes = {
        {"part-1", std::nullopt, "Part I", {0}},
        {"ch-1", std::string("part-1"), "Chapter 1", {1}},
        {"ch-2", std::string("part-1"), "Chapter 2", {1}},  // Repeated dest.
        {"part-2", std::nullopt, u8"Ünïcødé — 日本語 😀", {9}}};
    return plan;
}
bool has(const PlanValidation& v, PlanIssueCode code,
         const std::string& id = {}) {
    return std::any_of(v.issues.begin(), v.issues.end(), [&](const auto& i) {
        return i.code == code && (id.empty() || i.node_id == id);
    });
}
}  // namespace

int main() {
    const auto ok = validate(base());
    require(ok.valid && ok.issues.empty(),
            "valid tree with Unicode title and repeated destinations");

    auto child_first = base();
    std::swap(child_first.nodes[0], child_first.nodes[1]);
    require(validate(child_first).valid,
            "a parent may appear after its child; array order is sibling order");

    auto duplicate = base();
    duplicate.nodes.push_back({"ch-1", std::nullopt, "Again", {2}});
    require(has(validate(duplicate), PlanIssueCode::DuplicateNodeId, "ch-1"),
            "duplicate IDs are rejected");

    auto missing = base();
    missing.nodes[1].parent_id = "nope";
    const auto missing_v = validate(missing);
    require(!missing_v.valid &&
            has(missing_v, PlanIssueCode::MissingParent, "ch-1") &&
            missing_v.issues.front().node_index == 1 &&
            missing_v.issues.front().field == "parent_id",
            "missing parent is tied to node and field");

    auto self = base();
    self.nodes[0].parent_id = "part-1";
    require(has(validate(self), PlanIssueCode::SelfParent, "part-1"),
            "self parent is rejected");

    auto cycle = base();
    cycle.nodes[0].parent_id = "ch-2";  // part-1 -> ch-2 -> part-1
    const auto cycle_v = validate(cycle);
    require(has(cycle_v, PlanIssueCode::ParentCycle) &&
            std::count_if(cycle_v.issues.begin(), cycle_v.issues.end(),
                          [](const auto& i) {
                              return i.code == PlanIssueCode::ParentCycle;
                          }) == 1,
            "a parent cycle is reported once");

    auto bounds = base();
    bounds.nodes[3].destination.pdf_page_index = 10;
    bounds.nodes[2].destination.pdf_page_index = -1;
    const auto bounds_v = validate(bounds);
    require(has(bounds_v, PlanIssueCode::DestinationOutOfRange, "part-2") &&
            has(bounds_v, PlanIssueCode::DestinationOutOfRange, "ch-2"),
            "destinations must lie in [0, page_count)");

    auto empty = base();
    empty.nodes.clear();
    require(has(validate(empty), PlanIssueCode::EmptyPlan),
            "an empty writable plan is rejected");

    auto unbound = base();
    unbound.input.sha256.fill(0);
    unbound.input.page_count = 0;
    const auto unbound_v = validate(unbound);
    require(has(unbound_v, PlanIssueCode::MissingInputDigest) &&
            has(unbound_v, PlanIssueCode::InvalidPageCount),
            "plans must be bound to an input digest and count");

    auto version = base();
    version.schema_version = 2;
    version.page_index_base = 1;
    const auto version_v = validate(version);
    require(has(version_v, PlanIssueCode::UnsupportedSchemaVersion) &&
            has(version_v, PlanIssueCode::UnsupportedPageIndexBase),
            "unsupported schema version and one-based indices are rejected");

    for (const char* bad : {"\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                            "abc\xE2\x82"}) {
        auto utf8 = base();
        utf8.nodes[0].title = bad;
        require(has(validate(utf8), PlanIssueCode::InvalidUtf8Title, "part-1"),
                "overlong, surrogate, out-of-range, truncated UTF-8 rejected");
    }
    auto blank = base();
    blank.nodes[1].title = " \t ";
    blank.nodes[2].id.clear();
    const auto blank_v = validate(blank);
    require(has(blank_v, PlanIssueCode::EmptyTitle, "ch-1") &&
            has(blank_v, PlanIssueCode::EmptyNodeId),
            "blank titles and empty IDs are rejected");

    auto recorded = base();
    recorded.omitted_entries = {{"e9", "unresolved destination"}};
    recorded.promotions = {{"ch-1", std::string("e9"), std::string("part-1"),
                            "nearest retained ancestor"}};
    require(validate(recorded).valid,
            "recorded omissions and promotions consistent with nodes are valid");
    recorded.omitted_entries.push_back({"ch-2", "cannot omit a node"});
    recorded.promotions.push_back({"ch-2", std::string("part-1"),
                                   std::nullopt, "root"});
    const auto recorded_v = validate(recorded);
    require(has(recorded_v, PlanIssueCode::InvalidOmission) &&
            has(recorded_v, PlanIssueCode::InvalidPromotion, "ch-2"),
            "omissions must not be nodes; promotions must match node parents");

    require(validate(missing).issues.size() ==
                validate(missing).issues.size(),
            "validation is deterministic");
    std::cout << "S5 plan validation fixtures passed\n";
}
