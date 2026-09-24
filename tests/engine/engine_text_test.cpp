// Engine text facade and JSON codec tests.
// Usage: pdfbookmark_engine_text_tests <6-page fixture.pdf> [models-dir]
#include <pdfbookmark/engine/text.hpp>

#include "json.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace pdfbookmark;
using namespace pdfbookmark::engine;

void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void json_strictness() {
    for (const char* bad :
         {"{\"a\":1,}", "[1,]", "{\"a\":1,\"a\":2}", "\"\\ud800\"",
          "\"\\udc00\"", "01", "1.", "-", "{\"a\":1} x", "// c\n1",
          "\"\xC0\xAF\"", "\"tab\there\"", "[1 2]", "nul"}) {
        require(!json::parse(bad), std::string("rejects ") + bad);
    }
    const auto pair = json::parse("\"\\ud83d\\ude00 \\u00e9\"");
    require(pair && pair.value().text == "\xF0\x9F\x98\x80 \xC3\xA9",
            "surrogate pairs decode to UTF-8");
    const auto big = json::parse("[9223372036854775807, 9223372036854775808, 1.5]");
    require(big && big.value().array[0].as_int64() &&
                !big.value().array[1].as_int64() &&
                !big.value().array[2].as_int64() &&
                *big.value().array[2].as_double() == 1.5,
            "checked integer conversion");
    auto object = json::Value::make_object();
    object.add("s", json::Value::of("q\"\\\n\x01 \xE6\x97\xA5"));
    object.add("d", json::Value::of(0.1 + 0.2, 3));
    object.add("n", json::Value::of(std::numeric_limits<double>::infinity(), 3));
    const auto text = json::write(object);
    const auto back = json::parse(text);
    require(back && back.value().find("s")->text == object.find("s")->text &&
                back.value().find("d")->text == "0.3" &&
                back.value().find("n")->kind == json::Value::Kind::Null &&
                json::write(back.value()) == text,
            "writer output round-trips deterministically");
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2 || argc == 3, "usage: fixture [models-dir]");
    json_strictness();
    const std::filesystem::path fixture = std::filesystem::u8path(argv[1]);

    TextOptions embedded;
    embedded.mode = text::AcquisitionMode::EmbeddedOnly;
    embedded.batch_pages = 2;
    std::size_t progress_calls = 0;
    const auto ordered = extract_text(
        fixture, std::vector<PageIndex>{4, 0, 2, 5, 3, 1}, embedded, {},
        [&](const TextProgress& p) {
            ++progress_calls;
            require(p.pages_total == 6 && p.pages_done == progress_calls * 2,
                    "progress reports cumulative pages");
        });
    require(ordered && ordered.value().pages.size() == 6 &&
                ordered.value().pages[0].page_index == 4 &&
                ordered.value().pages[5].page_index == 1 &&
                ordered.value().status == TextStatus::Complete &&
                ordered.value().input.page_count == 6 &&
                ordered.value().configurations.size() == 1 &&
                ordered.value().ocr_attempts_used == 0 && progress_calls == 3,
            "batched acquisition keeps caller order in one session");
    require(ordered.value().pages[2].selected &&
                ordered.value().pages[2].selected->flat_text().find("Alpha") !=
                    std::string::npos,
            "page index 2 carries its native text");

    const auto all = extract_text(fixture, std::nullopt, embedded);
    require(all && all.value().pages.size() == 6 &&
                all.value().pages[3].page_index == 3,
            "nullopt selects every page in order");
    require(!extract_text(fixture, std::vector<PageIndex>{1, 1}, embedded) &&
                !extract_text(fixture, std::vector<PageIndex>{6}, embedded) &&
                !extract_text(fixture, std::vector<PageIndex>{-1}, embedded),
            "duplicates and out-of-range indices are rejected up front");
    TextOptions zero = embedded;
    zero.batch_pages = 0;
    require(!extract_text(fixture, std::nullopt, zero), "batch size validated");

    std::atomic_bool cancel{true};
    const auto cancelled =
        extract_text(fixture, std::nullopt, embedded, RunControl{&cancel});
    require(cancelled && cancelled.value().status == TextStatus::Cancelled &&
                cancelled.value().pages.size() == 6 &&
                cancelled.value().pages[5].outcome == text::Outcome::Cancelled,
            "cancellation marks every unstarted page explicitly");
    const auto cancelled_json = json::parse(text_report_json(cancelled.value()));
    require(cancelled_json &&
                cancelled_json.value().find("pages")->array[0]
                        .find("content")->kind == json::Value::Kind::Null,
            "cancelled pages serialize without content");

    // JSON contract and determinism.
    const auto encoded = text_report_json(ordered.value());
    const auto again = extract_text(
        fixture, std::vector<PageIndex>{4, 0, 2, 5, 3, 1}, embedded);
    require(again && text_report_json(again.value()) == encoded,
            "identical input and options produce identical JSON");
    const auto parsed = json::parse(encoded);
    require(static_cast<bool>(parsed), "text JSON is strictly valid");
    const auto& root = parsed.value();
    const auto* pages = root.find("pages");
    require(root.find("schema_version")->as_int64() == 1 &&
                root.find("page_index_base")->as_int64() == 0 &&
                root.find("status")->text == "complete" &&
                root.find("input")->find("sha256")->text.size() == 64 &&
                root.find("input")->find("page_count")->as_int64() == 6 &&
                pages && pages->array.size() == 6 &&
                pages->array[0].find("page_index")->as_int64() == 4 &&
                pages->array[2].find("content")->find("text")->text ==
                    ordered.value().pages[2].selected->flat_text() &&
                pages->array[2].find("content")->find("regions")->array.size() ==
                    ordered.value().pages[2].selected->regions.size(),
            "JSON carries identity, zero-based indices, order and regions");

    const auto missing = extract_text(fixture, std::nullopt, [] {
        TextOptions o;
        o.mode = text::AcquisitionMode::OcrOnly;
        return o;
    }());
    require(!missing, "OcrOnly without models is a configuration failure");

    if (argc == 3) {
        // Run-wide OCR budget across S1 calls (real models, low DPI).
        const std::filesystem::path models = std::filesystem::u8path(argv[2]);
        TextOptions ocr;
        ocr.mode = text::AcquisitionMode::OcrOnly;
        ocr.models = text::ModelResources{models / "det" / "inference.onnx",
                                          models / "rec" / "inference.onnx",
                                          models / "rec" / "charset.txt"};
        ocr.raster.dpi = 100;
        ocr.ocr_budget = 1;
        ocr.batch_pages = 1;
        const auto budgeted =
            extract_text(fixture, std::vector<PageIndex>{2, 3, 4}, ocr);
        require(budgeted && budgeted.value().ocr_attempts_used == 1 &&
                    budgeted.value().pages[0].selected &&
                    budgeted.value().pages[0].selected->source ==
                        text::Source::Ocr &&
                    budgeted.value().pages[1].outcome == text::Outcome::Failed &&
                    budgeted.value().pages[2].outcome == text::Outcome::Failed &&
                    budgeted.value().status == TextStatus::Partial &&
                    budgeted.value().configurations.size() == 2 &&
                    budgeted.value().model_identity != "not-used" &&
                    budgeted.value().model_identity.find("det=") != std::string::npos &&
                    budgeted.value().diagnostics.empty(),
                "a budget consumed in one batch does not reset in the next");
        std::cout << "OCR page index 2 text: "
                  << budgeted.value().pages[0].selected->flat_text() << '\n';
    } else {
        std::cout << "note: OCR budget case skipped (no models dir)\n";
    }
    std::cout << "Engine text fixtures passed\n";
}
