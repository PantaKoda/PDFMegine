#include <pdfbookmark/text/acquisition.hpp>

#include "sha256.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    require(argc == 2, "fixture path argument");
    using namespace pdfbookmark;
    using namespace pdfbookmark::text;
    TextAcquisition service;
    auto opened = service.open(argv[1]);
    require(static_cast<bool>(opened), "open fixture");
    OpenOptions tiny_limit;
    tiny_limit.max_pdf_bytes = 1;
    auto too_large = service.open(argv[1], tiny_limit);
    require(!too_large &&
            too_large.error().code == ErrorCode::ResourceLimit,
            "PDF snapshot byte limit");
    TextDocument document = opened.take();
    require(document.page_count() == 8, "eight physical pages");
    require(document.identity().page_count == 8, "identity page count");
    const auto original_digest = document.identity().sha256;
    const std::uint8_t abc[] = {'a', 'b', 'c'};
    require(text::detail::sha256_hex(text::detail::sha256(abc, sizeof(abc))) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 known answer");
    require(text::detail::sha256_file(argv[1]) == original_digest,
            "streamed and snapshot SHA-256 agree");
    AcquisitionOptions options;
    options.mode = AcquisitionMode::EmbeddedOnly;
    require(!document.acquire({0, 0}), "duplicate request rejected");
    require(!document.acquire({8}), "out-of-range request rejected");

    const auto temp = std::filesystem::temp_directory_path() /
        ("s1-snapshot-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".pdf");
    std::filesystem::copy_file(argv[1], temp);
    auto frozen = service.open(temp);
    require(static_cast<bool>(frozen) &&
            frozen.value().identity().sha256 == original_digest,
            "snapshot identity matches analyzed bytes");
    {
        std::ofstream altered(temp, std::ios::binary | std::ios::app);
        altered << "\n% source changed after open\n";
    }
    auto frozen_text = frozen.value().acquire({0}, options);
    require(frozen_text && frozen_text.value().pages[0].selected &&
            frozen_text.value().pages[0].selected->flat_text().find("Hello World") !=
                std::string::npos, "snapshot remains readable after source mutation");
    auto changed = service.open(temp);
    require(changed && changed.value().identity().sha256 != original_digest,
            "new open detects changed bytes");
    std::filesystem::remove(temp);
    auto empty = document.acquire({});
    require(empty && empty.value().pages.empty(), "empty selection succeeds");

    auto acquired = document.acquire({2, 0, 1, 3}, options);
    require(static_cast<bool>(acquired), "acquire nonconsecutive pages");
    auto pages = acquired.take().pages;
    require(pages.size() == 4, "request count preserved");
    require(pages[0].page_index == 2 && pages[1].page_index == 0 &&
            pages[2].page_index == 1 && pages[3].page_index == 3,
            "caller order preserved");
    require(pages[0].selected && pages[0].selected->flat_text().find("Second page") !=
            std::string::npos, "second text");
    require(pages[1].selected && pages[1].selected->flat_text().find("Hello World") !=
            std::string::npos, "first text");
    require(pages[2].outcome == Outcome::NoTextFound && !pages[2].selected,
            "blank page is no-text outcome");
    require(pages[3].selected && !pages[3].selected->regions.empty(),
            "rotated page text");
    require(pages[1].selected->regions[0].quad.has_value(),
            "native positioned region");
    auto mixed = document.acquire({4}, options);
    require(mixed && mixed.value().pages[0].selected &&
            mixed.value().pages[0].selected->flat_text().find("Header") !=
                std::string::npos, "mixed page native header");
    require(mixed.value().pages[0].assessment.coverage ==
            Coverage::SuspectedIncomplete,
            "large scanned body raises specific omission signal");
    auto unicode = document.acquire({5}, options);
    if (unicode && !unicode.value().pages[0].selected) {
        const auto& diagnostic = unicode.value().pages[0];
        std::cerr << "unicode outcome=" << static_cast<int>(diagnostic.outcome)
                  << " scalars=" << diagnostic.assessment.unicode_scalars
                  << " replacements=" << diagnostic.assessment.replacement_count
                  << " controls=" << diagnostic.assessment.control_count << '\n';
        for (const auto& attempt : diagnostic.attempts)
            std::cerr << "attempt=" << attempt.reason << '\n';
        for (const auto& reason : diagnostic.reasons)
            std::cerr << "reason=" << reason << '\n';
    }
    require(unicode && unicode.value().pages[0].selected,
            "ToUnicode page extraction");
    const auto decoded = unicode.value().pages[0].selected->flat_text();
    require(decoded.find("\xE4\xB8\xAD") != std::string::npos &&
            decoded.find("\xF0\x9F\x98\x80") != std::string::npos &&
            decoded.find("\xC3\xA9") != std::string::npos,
            "non-Latin, supplementary and diacritic UTF-8 preserved");
    require(unicode.value().pages[0].assessment.replacement_count == 1,
            "lone malformed surrogate recorded once");
    auto hidden = document.acquire({7}, options);
    require(hidden && hidden.value().pages[0].selected &&
            hidden.value().pages[0].selected->source == Source::EmbeddedPdf &&
            hidden.value().pages[0].selected->flat_text().find("Hidden layer") !=
                std::string::npos, "hidden OCR layer remains embedded PDF source");

    auto facts = document.read_facts({{0, 2, 3}, true, true});
    require(static_cast<bool>(facts), "read page facts");
    require(facts.value().pages.size() == 3, "facts selection");
    require(facts.value().pages[0].viewer_label.availability ==
            FactAvailability::Present, "viewer label present");
    require(facts.value().pages[0].viewer_label.value.value() == "i",
            "roman viewer label");
    require(facts.value().pages[1].viewer_label.value.value() == "A-1",
            "prefixed viewer label");
    require(facts.value().pages[0].local_link_destinations.availability ==
            FactAvailability::Present &&
            facts.value().pages[0].local_link_destinations.value.value() ==
                std::vector<PageIndex>{2}, "local link destination");
    require(facts.value().pages[2].local_link_destinations.availability ==
            FactAvailability::Absent, "absent local links");
    require(std::abs(facts.value().pages[2].geometry.width_points - 180) < 1,
            "rotated crop width");
    require(std::abs(facts.value().pages[2].geometry.height_points - 160) < 1,
            "rotated crop height");
    auto scaled_facts = document.read_facts({{6}, false, false});
    require(scaled_facts && scaled_facts.value().pages.size() == 1,
            "user-unit facts");
    require(std::abs(scaled_facts.value().pages[0].geometry.width_points - 400) < 1 &&
            scaled_facts.value().pages[0].geometry.user_unit &&
            std::abs(*scaled_facts.value().pages[0].geometry.user_unit - 2) < 0.01,
            "PDF UserUnit expands physical page geometry");
    const Point original{30, 50};
    const auto& rotated = facts.value().pages[2].geometry;
    const auto round_trip =
        rotated.canonical_to_pdf.map(rotated.pdf_to_canonical.map(original));
    require(std::abs(round_trip.x - original.x) < 0.01 &&
            std::abs(round_trip.y - original.y) < 0.01,
            "PDF/canonical geometry round trip");

    std::atomic_bool cancelled{true};
    auto cancelled_batch = document.acquire({0, 2}, options, {&cancelled});
    require(cancelled_batch && !cancelled_batch.value().complete &&
            cancelled_batch.value().pages[0].outcome == Outcome::Cancelled &&
            cancelled_batch.value().pages[1].outcome == Outcome::Cancelled,
            "cooperative cancellation preserves request positions");
    options.mode = AcquisitionMode::OcrOnly;
    auto no_models = document.acquire({0}, options);
    require(!no_models && no_models.error().code == ErrorCode::OcrConfiguration,
            "missing OcrOnly models is configuration failure");
    auto no_work = document.acquire({}, options);
    require(no_work && no_work.value().pages.empty(),
            "empty OcrOnly selection succeeds without model initialization");
    auto reopened = service.open(argv[1]);
    require(static_cast<bool>(reopened), "reopen fixture");
    document = reopened.take();  // Closes the first session.
    require(pages[0].selected->flat_text().find("Second page") !=
            std::string::npos, "results own data after session closure");
    std::cout << "S1 smoke: 4 pages, ordered native text, facts, crop rotation, "
                 "cancellation, error scope passed\n";
}
