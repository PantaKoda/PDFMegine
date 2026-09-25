#include <pdfbookmark/text/acquisition.hpp>

#include "ocr_adapter.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
std::atomic<int> calls{0};
std::atomic<bool> fail{false};
std::atomic<bool> multilingual{false};
std::atomic_bool* cancel_after_ocr = nullptr;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FakeOcr final : public pdfbookmark::text::detail::OcrBackend {
public:
    std::vector<pdfbookmark::text::detail::OcrLine> run(
        const pdfbookmark::text::detail::Raster& raster) override {
        ++calls;
        require(raster.width > 0 && raster.height > 0 &&
                raster.stride == raster.width * 3 &&
                raster.bgr.size() ==
                    static_cast<std::size_t>(raster.stride) * raster.height,
                "OCR receives bounded, packed BGR");
        if (fail) throw std::runtime_error("injected OCR failure");
        if (cancel_after_ocr) cancel_after_ocr->store(true);
        pdfbookmark::text::detail::OcrLine line;
        line.text = multilingual ?
            "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD"
            "\xE4\xB8\xAD\xE4\xB8\xAD" :
            "Recovered page body";
        line.confidence = 0.91f;
        line.pixel_quad.points = {
            pdfbookmark::Point{10, 10}, pdfbookmark::Point{100, 10},
            pdfbookmark::Point{100, 30}, pdfbookmark::Point{10, 30}};
        return {line};
    }
};

std::unique_ptr<pdfbookmark::text::detail::OcrBackend> make_fake(
    const pdfbookmark::text::ModelResources&) {
    return std::make_unique<FakeOcr>();
}
}

int main(int argc, char** argv) {
    require(argc == 2, "fixture path");

    // OCR thread count (issue #1): explicit values are kept, 0 = automatic,
    // which is half the logical processors clamped to 1..8.
    require(pdfbookmark::text::resolve_ocr_threads(3) == 3 &&
                pdfbookmark::text::resolve_ocr_threads(12) == 12,
            "explicit OCR thread count is used");
    require(pdfbookmark::text::resolve_ocr_threads(0) >= 1 &&
                pdfbookmark::text::resolve_ocr_threads(0) <= 8,
            "automatic OCR thread count is 1..8");
    using namespace pdfbookmark::text;
    detail::set_ocr_factory_for_testing(&make_fake);
    TextAcquisition service;
    OpenOptions open_options;
    open_options.ocr_models = ModelResources{argv[1], argv[1], argv[1]};
    auto opened = service.open(argv[1], open_options);
    require(static_cast<bool>(opened), "open");
    auto document = opened.take();
    AcquisitionOptions options;
    options.mode = AcquisitionMode::Auto;
    auto clean = document.acquire({0}, options);
    require(clean && clean.value().pages[0].selected &&
            clean.value().pages[0].selected->source == Source::EmbeddedPdf &&
            calls == 0, "clean native does not invoke OCR");
    auto mixed = document.acquire({4}, options);
    require(mixed && mixed.value().pages[0].selected &&
            mixed.value().pages[0].selected->source == Source::Ocr &&
            mixed.value().pages[0].selected->flat_text() == "Recovered page body" &&
            calls == 1, "mixed page selects fuller OCR candidate");
    require(mixed.value().pages[0].outcome == Outcome::Degraded,
            "native/OCR coverage uncertainty remains visible");
    multilingual = true;
    auto multibyte = document.acquire({4}, options);
    require(multibyte && multibyte.value().pages[0].selected &&
            multibyte.value().pages[0].selected->source == Source::EmbeddedPdf,
            "Auto compares Unicode scalars, not UTF-8 byte lengths");
    multilingual = false;
    auto blank = document.acquire({1}, options);
    require(blank && blank.value().pages[0].outcome == Outcome::Ok &&
            blank.value().pages[0].selected &&
            blank.value().pages[0].selected->source == Source::Ocr,
            "absent native text invokes OCR");
    fail = true;
    auto fallback = document.acquire({4}, options);
    require(fallback && fallback.value().pages[0].outcome == Outcome::Degraded &&
            fallback.value().pages[0].selected &&
            fallback.value().pages[0].selected->source == Source::EmbeddedPdf,
            "OCR failure preserves usable native text");
    require(fallback.value().pages[0].attempts.size() == 2 &&
            fallback.value().pages[0].attempts[1].state == AttemptState::Failed,
            "OCR failure attempt retained");
    fail = false;
    options.mode = AcquisitionMode::OcrOnly;
    auto scaled = document.acquire({6}, options);
    require(scaled && scaled.value().pages[0].selected &&
            scaled.value().pages[0].selected->geometry.raster_width == 1667 &&
            scaled.value().pages[0].selected->geometry.user_unit &&
            *scaled.value().pages[0].selected->geometry.user_unit == 2,
            "UserUnit affects OCR raster dimensions");
    std::atomic_bool cancelled{false};
    cancel_after_ocr = &cancelled;
    options.mode = AcquisitionMode::Auto;
    auto partial = document.acquire({1, 4}, options, {&cancelled});
    cancel_after_ocr = nullptr;
    require(partial && !partial.value().complete &&
            partial.value().pages.size() == 2 &&
            partial.value().pages[0].outcome == Outcome::Ok &&
            partial.value().pages[1].outcome == Outcome::Cancelled,
            "cancellation retains completed page and marks unstarted page");
    detail::set_ocr_factory_for_testing(nullptr);
    std::cout << "S1 policy: clean/native, mixed/OCR, blank/OCR, "
                 "failed-OCR fallback passed\n";
}
