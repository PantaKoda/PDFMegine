// End-to-end analyze -> plan JSON -> apply on generated fixtures.
// Separately verifies (a) written outline structure (independent qpdf read)
// and (b) correct chapter destinations (S1 reads the heading on each
// destination page of the OUTPUT file).
// Usage: pdfbookmark_engine_apply_tests <tests/engine/fixtures dir> <work dir>
#include <pdfbookmark/engine/analysis.hpp>
#include <pdfbookmark/engine/apply.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace pdfbookmark;
namespace fs = std::filesystem;

void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void spit(const fs::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary) << data;
}

struct Item {
    std::string title;
    int depth;
    int page;
};
std::vector<Item> outline_of(const fs::path& path) {
    QPDF pdf;
    pdf.processFile(path.string().c_str());
    const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    std::vector<Item> out;
    std::function<void(std::vector<QPDFOutlineObjectHelper>, int)> walk =
        [&](std::vector<QPDFOutlineObjectHelper> items, int depth) {
            for (auto& item : items) {
                int index = -1;
                const auto target = item.getDestPage().getObjGen();
                for (std::size_t i = 0; i < pages.size(); ++i)
                    if (pages[i].getObjectHandle().getObjGen() == target)
                        index = static_cast<int>(i);
                out.push_back({item.getTitle(), depth, index});
                walk(item.getKids(), depth + 1);
            }
        };
    QPDFOutlineDocumentHelper helper(pdf);
    walk(helper.getTopLevelOutlines(), 0);
    return out;
}

// Expected tree as (title, depth, page) in preorder, from the plan.
std::vector<Item> expected_of(const writer::BookmarkPlan& plan) {
    std::map<std::string, std::vector<const writer::BookmarkNode*>> kids;
    std::vector<const writer::BookmarkNode*> roots;
    for (const auto& n : plan.nodes)
        (n.parent_id ? kids[*n.parent_id] : roots).push_back(&n);
    std::vector<Item> out;
    std::function<void(const std::vector<const writer::BookmarkNode*>&, int)> walk =
        [&](const auto& nodes, int depth) {
            for (const auto* n : nodes) {
                out.push_back({n->title, depth, n->destination.pdf_page_index});
                walk(kids[n->id], depth + 1);
            }
        };
    walk(roots, 0);
    return out;
}
bool same(const std::vector<Item>& a, const std::vector<Item>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].title != b[i].title || a[i].depth != b[i].depth ||
            a[i].page != b[i].page)
            return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 3, "usage: fixtures-dir work-dir");
    const fs::path fixtures = fs::u8path(argv[1]);
    const fs::path work = fs::u8path(argv[2]);
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work);

    // Input WITH misleading pre-existing bookmarks.
    const fs::path input = fixtures / "boundary_outlined.pdf";
    const std::string input_bytes = slurp(input);
    const auto original_outline = outline_of(input);
    require(original_outline.size() == 3 && original_outline[2].title == "Zeta Chapter",
            "fixture carries misleading bookmarks");

    engine::AnalysisOptions options;
    auto analysis = engine::analyze(input, options);
    require(analysis && analysis.value().plan.ready, "analysis produces a ready plan");
    const auto& plan = *analysis.value().plan.plan;
    const fs::path plan_file = work / "plan.json";
    spit(plan_file, engine::plan_json(plan));

    // Apply through the plan file, as the CLI does.
    const fs::path output = work / "bookmarked.pdf";
    const auto written = engine::apply_plan_file(input, output, plan_file);
    require(written && written.value().committed &&
                written.value().input_had_outline &&
                written.value().verification.outline_items == plan.nodes.size(),
            "apply commits a verified output");

    // (a) Written outline structure equals the plan tree exactly.
    const auto actual = outline_of(output);
    require(same(actual, expected_of(plan)) && actual.size() == 8,
            "output outline is exactly the plan (no merged old bookmarks)");
    for (const auto& item : actual)
        require(item.title != "Zeta Chapter", "old bookmarks are not retained");

    // (b) Each destination page of the OUTPUT shows its chapter heading.
    text::TextAcquisition acquisition;
    auto opened = acquisition.open(output);
    require(static_cast<bool>(opened), "output opens in S1");
    auto document = opened.take();
    std::vector<PageIndex> targets;
    for (const auto& n : plan.nodes)
        if (std::find(targets.begin(), targets.end(),
                      n.destination.pdf_page_index) == targets.end())
            targets.push_back(n.destination.pdf_page_index);
    text::AcquisitionOptions embedded;
    embedded.mode = text::AcquisitionMode::EmbeddedOnly;
    auto pages = document.acquire(targets, embedded);
    require(static_cast<bool>(pages), "destination pages acquire");
    std::map<PageIndex, std::string> text_of;
    for (const auto& page : pages.value().pages)
        if (page.selected) text_of[page.page_index] = page.selected->flat_text();
    for (const auto& n : plan.nodes) {
        const auto& text = text_of[n.destination.pdf_page_index];
        require(text.rfind(n.title, 0) == 0 || text.find("\n" + n.title) != std::string::npos,
                "destination of '" + n.title + "' shows that heading in the output");
    }

    // Input preserved byte-for-byte, including its own (misleading) outline.
    require(slurp(input) == input_bytes && outline_of(input).size() == 3,
            "input bytes and bookmarks unchanged");

    // Manual title edit keeps node identity and is written (Unicode).
    auto edited = plan;
    edited.nodes[0].title = "Part One \xE2\x80\x94 \xC3\x9C" "berblick";
    require(writer::validate(edited).valid, "edited plan validates");
    const fs::path edited_out = work / "edited.pdf";
    require(engine::apply(input, edited_out, edited) &&
                outline_of(edited_out)[0].title == edited.nodes[0].title,
            "manual title correction is applied");

    // Refusals leave no output and the input intact.
    const fs::path refused = work / "refused.pdf";
    const auto stale = engine::apply_plan_file(fixtures / "multi.pdf", refused, plan_file);
    require(!stale && stale.error().code == ErrorCode::InputChanged && !fs::exists(refused),
            "a plan for another input is rejected as stale");
    const fs::path report_as_plan = work / "report.json";
    spit(report_as_plan, engine::analysis_report_json(analysis.value(), options));
    const auto malformed = engine::apply_plan_file(input, refused, report_as_plan);
    require(!malformed && malformed.error().code == ErrorCode::InvalidArgument &&
                !fs::exists(refused),
            "an analysis report is never accepted as a plan");
    engine::ApplyOptions replace;
    replace.replace_existing_output = true;
    const auto onto_plan = engine::apply_plan_file(input, plan_file, plan_file, replace);
    require(!onto_plan && slurp(plan_file) == engine::plan_json(plan),
            "the plan file cannot be overwritten by the output");
    const auto onto_input = engine::apply_plan_file(input, input, plan_file, replace);
    require(!onto_input && slurp(input) == input_bytes,
            "the input cannot be the output even with replacement");
    const auto exists = engine::apply_plan_file(input, output, plan_file);
    require(!exists && exists.error().code == ErrorCode::OutputExists,
            "existing output is refused by default");
    require(static_cast<bool>(engine::apply_plan_file(input, output, plan_file, replace)),
            "explicit replacement is allowed");
    std::atomic_bool cancel{true};
    const auto cancelled =
        engine::apply_plan_file(input, refused, plan_file, {}, RunControl{&cancel});
    require(!cancelled && cancelled.error().code == ErrorCode::Cancelled &&
                !fs::exists(refused),
            "cancellation before commit writes nothing");
    require(slurp(input) == input_bytes, "input unchanged after all operations");
    std::cout << "Engine apply end-to-end passed: " << actual.size()
              << " bookmarks, destinations verified against output headings\n";
}
