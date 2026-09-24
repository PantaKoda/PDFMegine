// Installed plan-only consumer: builds and validates a bookmark plan with no
// qpdf headers, libraries, or package available.
#include <pdfbookmark/writer/plan.hpp>

#include <iostream>

int main() {
    using namespace pdfbookmark::writer;
    BookmarkPlan plan;
    plan.input.sha256.fill(0x11);
    plan.input.page_count = 3;
    plan.nodes = {{"a", std::nullopt, "A", {0}},
                  {"b", std::string("a"), "B", {2}}};
    const bool ok = validate(plan).valid;
    plan.nodes[0].parent_id = "b";  // Cycle.
    const auto bad = validate(plan);
    std::cout << "valid=" << ok << " cycle_rejected=" << !bad.valid
              << " issues=" << bad.issues.size() << '\n';
    return ok && !bad.valid ? 0 : 1;
}
