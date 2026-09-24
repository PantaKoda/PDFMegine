// S5 writer tests. qpdf is used here only to build fixture PDFs and to inspect
// output independently of the writer's own verifier.
#include <pdfbookmark/writer/writer.hpp>

#include "writer_testing.hpp"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace pdfbookmark;
using namespace pdfbookmark::writer;

namespace {

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

struct FixtureOptions {
    bool outline = false;       // Pre-existing (misleading) bookmarks.
    bool extra_navigation = false;  // Named destination + link annotation.
    bool encrypted = false;
    bool signature = false;
};

// Five pages, each with distinct text "Page N".
void make_pdf(const fs::path& path, const FixtureOptions& options) {
    QPDF pdf;
    pdf.emptyPDF();
    auto font = pdf.makeIndirectObject(QPDFObjectHandle::parse(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));
    QPDFPageDocumentHelper helper(pdf);
    for (int i = 0; i < 5; ++i) {
        auto page = QPDFObjectHandle::parse(
            "<< /Type /Page /MediaBox [0 0 300 300] >>");
        auto resources = QPDFObjectHandle::newDictionary();
        auto fonts = QPDFObjectHandle::newDictionary();
        fonts.replaceKey("/F1", font);
        resources.replaceKey("/Font", fonts);
        page.replaceKey("/Resources", resources);
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(
            &pdf, "BT /F1 18 Tf 40 150 Td (Page " + std::to_string(i + 1) +
                      ") Tj ET"));
        helper.addPage(QPDFPageObjectHelper(pdf.makeIndirectObject(page)),
                       false);
    }
    auto pages = helper.getAllPages();
    auto root = pdf.getRoot();
    if (options.outline) {
        auto outlines = pdf.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /Type /Outlines /Count 2 >>"));
        auto a = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        auto b = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        a.replaceKey("/Title", QPDFObjectHandle::newUnicodeString("Old A"));
        b.replaceKey("/Title", QPDFObjectHandle::newUnicodeString("Old B"));
        for (auto* item : {&a, &b}) item->replaceKey("/Parent", outlines);
        a.replaceKey("/Next", b);
        b.replaceKey("/Prev", a);
        a.replaceKey("/Dest", QPDFObjectHandle::newArray(
            {pages[4].getObjectHandle(), QPDFObjectHandle::newName("/Fit")}));
        b.replaceKey("/Dest", QPDFObjectHandle::newArray(
            {pages[0].getObjectHandle(), QPDFObjectHandle::newName("/Fit")}));
        outlines.replaceKey("/First", a);
        outlines.replaceKey("/Last", b);
        root.replaceKey("/Outlines", outlines);
    }
    if (options.extra_navigation) {
        auto dests = QPDFObjectHandle::newDictionary();
        dests.replaceKey("/keep", QPDFObjectHandle::newArray(
            {pages[2].getObjectHandle(), QPDFObjectHandle::newName("/Fit")}));
        root.replaceKey("/Dests", pdf.makeIndirectObject(dests));
        auto link = pdf.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /Type /Annot /Subtype /Link /Rect [0 0 50 50] >>"));
        link.replaceKey("/Dest", QPDFObjectHandle::newArray(
            {pages[3].getObjectHandle(), QPDFObjectHandle::newName("/Fit")}));
        pages[0].getObjectHandle().replaceKey(
            "/Annots", QPDFObjectHandle::newArray({link}));
    }
    if (options.signature) {
        auto field = pdf.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /FT /Sig /T (Signature1) /V << /Type /Sig /Filter "
            "/Adobe.PPKLite >> >>"));
        auto form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", QPDFObjectHandle::newArray({field}));
        // No SigFlags: detection must find the widget-less /Sig field itself.
        root.replaceKey("/AcroForm", form);
    }
    QPDFWriter writer(pdf, path.string().c_str());
    if (options.encrypted)
        writer.setR6EncryptionParameters("", "owner", true, true, true, true,
                                         true, true, qpdf_r3p_full, true);
    else
        writer.setDeterministicID(true);  // Reproducible fixture bytes.
    writer.write();
}

struct OutlineItem {
    std::string title;
    int depth;
    int page;
};
std::vector<OutlineItem> read_outline(const fs::path& path) {
    QPDF pdf;
    pdf.processFile(path.string().c_str());
    std::vector<OutlineItem> result;
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    const auto page_of = [&](QPDFObjectHandle page) {
        for (std::size_t i = 0; i < pages.size(); ++i)
            if (pages[i].getObjectHandle().getObjGen() == page.getObjGen())
                return static_cast<int>(i);
        return -1;
    };
    std::function<void(std::vector<QPDFOutlineObjectHelper>, int)> walk =
        [&](std::vector<QPDFOutlineObjectHelper> items, int depth) {
            for (auto& item : items) {
                result.push_back(
                    {item.getTitle(), depth, page_of(item.getDestPage())});
                walk(item.getKids(), depth + 1);
            }
        };
    QPDFOutlineDocumentHelper outlines(pdf);
    walk(outlines.getTopLevelOutlines(), 0);
    return result;
}

std::size_t leftover_temps(const fs::path& dir) {
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().filename().string().find(".pdfbookmark-") !=
            std::string::npos)
            ++count;
    return count;
}

BookmarkPlan make_plan(const InputIdentity& identity) {
    BookmarkPlan plan;
    plan.input = identity;
    plan.nodes = {
        {"part-1", std::nullopt, "Part I", {0}},
        {"ch-1", std::string("part-1"), "Chapter 1", {1}},
        {"ch-2", std::string("part-1"), "Chapter 2", {1}},  // Repeated dest.
        {"part-2", std::nullopt, u8"Ünïcødé — 日本語 😀", {3}},
        {"ch-3", std::string("part-2"), "Chapter 3", {3}},
        {"sec-3-1", std::string("ch-3"), "Section 3.1", {4}}};
    return plan;
}
const std::vector<OutlineItem> kExpected = {
    {"Part I", 0, 0}, {"Chapter 1", 1, 1}, {"Chapter 2", 1, 1},
    {u8"Ünïcødé — 日本語 😀", 0, 3}, {"Chapter 3", 1, 3},
    {"Section 3.1", 2, 4}};
bool same(const std::vector<OutlineItem>& a,
          const std::vector<OutlineItem>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](auto& x, auto& y) {
               return x.title == y.title && x.depth == y.depth &&
                      x.page == y.page;
           });
}
std::string page_text(const fs::path& path, int index) {
    QPDF pdf;
    pdf.processFile(path.string().c_str());
    auto page = QPDFPageDocumentHelper(pdf).getAllPages().at(
        static_cast<std::size_t>(index));
    const auto data = page.getObjectHandle().getKey("/Contents").getStreamData();
    return {reinterpret_cast<const char*>(data->getBuffer()), data->getSize()};
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "pdfbookmark_s5_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto plain = dir / "plain.pdf";
    const auto outlined = dir / "outlined.pdf";
    const auto encrypted = dir / "encrypted.pdf";
    const auto signed_pdf = dir / "signed.pdf";
    make_pdf(plain, {false, false, false, false});
    make_pdf(outlined, {true, true, false, false});
    make_pdf(encrypted, {false, false, true, false});
    make_pdf(signed_pdf, {false, false, false, true});

    // Identity of a manual plan's input, without S1-S4.
    const auto identity = read_input_identity(plain);
    require(identity && identity.value().page_count == 5,
            "read_input_identity reports page count");
    const auto plain_bytes = slurp(plain);

    // 1. Valid tree, Unicode, repeated destinations, hierarchy and order.
    const auto out1 = dir / "out1.pdf";
    const auto written = write_copy(plain, out1, make_plan(identity.value()));
    require(written && written.value().committed &&
            written.value().verification.structure_matches &&
            written.value().verification.input_unchanged &&
            written.value().verification.outline_items == 6 &&
            written.value().verification.page_count == 5 &&
            !written.value().input_had_outline,
            "valid plan commits a verified output");
    require(same(read_outline(out1), kExpected),
            "independent qpdf reading matches titles, hierarchy, order, pages");
    require(page_text(out1, 2) == page_text(plain, 2) &&
            page_text(out1, 4) == page_text(plain, 4),
            "representative page content is preserved");
    require(slurp(plain) == plain_bytes, "input bytes unchanged after success");
    require(leftover_temps(dir) == 0, "no temporary files after success");
    std::cout << "out1.pdf sha256=";
    for (const auto byte : written.value().output_sha256)
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(byte);
    std::cout << std::dec << '\n';

    // 2. Existing outline is replaced in the copy only; navigation kept.
    const auto outlined_bytes = slurp(outlined);
    const auto outlined_id = read_input_identity(outlined);
    require(static_cast<bool>(outlined_id), "outlined identity");
    const auto before = read_outline(outlined);
    require(before.size() == 2 && before[0].title == "Old A",
            "fixture has pre-existing bookmarks");
    const auto out2 = dir / "out2.pdf";
    const auto replaced =
        write_copy(outlined, out2, make_plan(outlined_id.value()));
    require(replaced && replaced.value().committed &&
            replaced.value().input_had_outline,
            "existing bookmarks do not block writing");
    require(same(read_outline(out2), kExpected),
            "output outline is exactly the plan tree, no merged old entries");
    require(slurp(outlined) == outlined_bytes &&
            read_outline(outlined).size() == 2,
            "original bytes and bookmarks are retained");
    {
        QPDF check;
        check.processFile(out2.string().c_str());
        auto pages = QPDFPageDocumentHelper(check).getAllPages();
        auto keep = check.getRoot().getKey("/Dests").getKey("/keep");
        auto annots = pages[0].getObjectHandle().getKey("/Annots");
        require(keep.isArray() &&
                keep.getArrayItem(0).getObjGen() ==
                    pages[2].getObjectHandle().getObjGen() &&
                annots.isArray() && annots.getArrayNItems() == 1 &&
                annots.getArrayItem(0).getKey("/Dest").getArrayItem(0)
                        .getObjGen() == pages[3].getObjectHandle().getObjGen(),
                "named destinations and link annotations are preserved");
    }

    // 3. Stale digest and page count.
    auto stale = make_plan(identity.value());
    stale.input.sha256[0] ^= 0xFF;
    const auto out3 = dir / "out3.pdf";
    const auto stale_result = write_copy(plain, out3, stale);
    require(!stale_result &&
            stale_result.error().code == ErrorCode::InputChanged &&
            !fs::exists(out3),
            "stale digest is rejected without output");
    auto wrong_count = make_plan(identity.value());
    wrong_count.input.page_count = 6;
    const auto count_result = write_copy(plain, out3, wrong_count);
    require(!count_result &&
            count_result.error().code == ErrorCode::InputChanged &&
            !fs::exists(out3),
            "declared page count must match the actual input");

    // 4. Structurally invalid plans never reach the PDF backend.
    auto cyclic = make_plan(identity.value());
    cyclic.nodes[0].parent_id = "ch-1";
    auto empty = make_plan(identity.value());
    empty.nodes.clear();
    for (const auto& bad : {cyclic, empty}) {
        const auto r = write_copy(plain, out3, bad);
        require(!r && r.error().code == ErrorCode::InvalidArgument &&
                !fs::exists(out3),
                "cyclic or empty plan is rejected");
    }

    // 5. Existing output: refused by default, replaced only explicitly.
    const auto existing = dir / "existing.pdf";
    spit(existing, "keep me");
    const auto refused =
        write_copy(plain, existing, make_plan(identity.value()));
    require(!refused && refused.error().code == ErrorCode::OutputExists &&
            slurp(existing) == "keep me",
            "existing output is not overwritten by default");
    WriteOptions replace;
    replace.replace_existing_output = true;
    const auto overwritten =
        write_copy(plain, existing, make_plan(identity.value()), replace);
    require(overwritten && overwritten.value().replaced_existing_output &&
            same(read_outline(existing), kExpected),
            "explicit output replacement works");

    // 6. Input aliases are rejected even with replacement enabled.
    const auto alias_plan = make_plan(identity.value());
    for (const auto& alias :
         {plain, dir / "." / plain.filename(), dir / "PLAIN.PDF"}) {
        const auto r = write_copy(plain, alias, alias_plan, replace);
        require(!r && r.error().code == ErrorCode::InvalidArgument,
                "same-file output path is rejected");
    }
    const auto hard = dir / "hardlink.pdf";
    fs::create_hard_link(plain, hard, ec);
    if (!ec) {
        const auto r = write_copy(plain, hard, alias_plan, replace);
        require(!r && r.error().code == ErrorCode::InvalidArgument,
                "hard link to input is rejected even with replacement");
        const auto reverse = write_copy(hard, plain, alias_plan, replace);
        require(!reverse && reverse.error().code == ErrorCode::InvalidArgument,
                "input reached through a hard link is protected");
    } else {
        std::cout << "note: hard-link alias check skipped: " << ec.message()
                  << '\n';
    }
    require(slurp(plain) == plain_bytes, "input bytes unchanged after aliases");

    // 7. Encrypted and signed inputs are explicitly unsupported.
    for (const auto& input : {encrypted, signed_pdf}) {
        const auto id = read_input_identity(input);
        require(static_cast<bool>(id), "protected fixture identity");
        const auto r = write_copy(input, out3, make_plan(id.value()));
        if (r || r.error().code != ErrorCode::Unsupported)
            std::cerr << input.filename().string() << ": "
                      << (r ? std::string("written")
                            : r.error().message) << '\n';
        require(!r && r.error().code == ErrorCode::Unsupported &&
                !fs::exists(out3),
                "encrypted/signed input is rejected as unsupported");
    }

    // 8. Cancellation before commit aborts cleanly.
    std::atomic_bool cancel{true};
    RunControl control{&cancel};
    const auto cancelled =
        write_copy(plain, out3, make_plan(identity.value()), {}, control);
    require(!cancelled && cancelled.error().code == ErrorCode::Cancelled &&
            !fs::exists(out3) && leftover_temps(dir) == 0 &&
            slurp(plain) == plain_bytes,
            "pre-commit cancellation leaves no output and an intact input");

#ifdef PDFBOOKMARK_TEST_HOOKS
    // 9. Injected temp corruption and write failure.
    using testing::FaultPoint;
    testing::set_fault_hook([](FaultPoint point, const fs::path& temp) {
        if (point == FaultPoint::AfterTempWritten) {
            auto data = slurp(temp);
            spit(temp, data.substr(0, data.size() / 2));
        }
        return true;
    });
    const auto corrupt = write_copy(plain, out3, make_plan(identity.value()));
    require(!corrupt && corrupt.error().code == ErrorCode::OutputWrite &&
            !fs::exists(out3) && leftover_temps(dir) == 0,
            "corrupted temporary output fails verification and is removed");
    testing::set_fault_hook([](FaultPoint point, const fs::path&) {
        return point != FaultPoint::AfterTempWritten;
    });
    const auto write_fail =
        write_copy(plain, out3, make_plan(identity.value()));
    require(!write_fail && write_fail.error().code == ErrorCode::OutputWrite &&
            !fs::exists(out3) && leftover_temps(dir) == 0,
            "temporary write failure leaves no output");

    // 10. Commit failure preserves an existing requested output.
    spit(existing, "previous output");
    testing::set_fault_hook([](FaultPoint point, const fs::path&) {
        return point != FaultPoint::BeforeCommit;
    });
    const auto commit_fail =
        write_copy(plain, existing, make_plan(identity.value()), replace);
    require(!commit_fail &&
            commit_fail.error().code == ErrorCode::OutputWrite &&
            slurp(existing) == "previous output" && leftover_temps(dir) == 0,
            "commit failure keeps the previous requested output intact");

    // 11. Cancellation arriving just after commit reports the committed file.
    std::atomic_bool late{false};
    testing::set_fault_hook([&late](FaultPoint point, const fs::path&) {
        if (point == FaultPoint::AfterCommit) late = true;
        return true;
    });
    const auto after = write_copy(plain, out3, make_plan(identity.value()), {},
                                  RunControl{&late});
    require(after && after.value().committed && fs::exists(out3) &&
            !after.value().diagnostics.empty(),
            "post-commit cancellation still reports a committed output");
    testing::set_fault_hook({});
#else
    std::cout << "note: fault-injection cases need PDFBOOKMARK_TEST_HOOKS
";
#endif

    require(slurp(plain) == plain_bytes && slurp(outlined) == outlined_bytes,
            "inputs unchanged after success, failure and cancellation");
    fs::remove_all(dir, ec);
    std::cout << "S5 writer fixtures passed\n";
}
