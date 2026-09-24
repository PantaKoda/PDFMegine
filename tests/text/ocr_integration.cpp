#include <pdfbookmark/text/acquisition.hpp>

#include <ocr/ocr.hpp>

#include <fpdfview.h>

#include <cmath>
#include <cstdlib>
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
    require(argc == 5, "PDF and three model paths required");
    using namespace pdfbookmark::text;
    OpenOptions open_options;
    open_options.ocr_models = ModelResources{argv[2], argv[3], argv[4]};
    TextAcquisition service;
    auto opened = service.open(argv[1], open_options);
    require(static_cast<bool>(opened), "open image PDF");
    auto document = opened.take();
    AcquisitionOptions acquire_options;
    acquire_options.mode = AcquisitionMode::OcrOnly;
    auto first = document.acquire({0}, acquire_options);
    require(static_cast<bool>(first), "OcrOnly acquisition");
    require(first.value().pages.size() == 1, "one page");
    require(first.value().model_identity.find(";det=") != std::string::npos &&
            first.value().model_identity.find(";rec=") != std::string::npos &&
            first.value().model_identity.find(";charset=") != std::string::npos,
            "exact model-resource identity recorded");
    require(!first.value().configuration_id.empty(),
            "acquisition configuration identity recorded");
    const auto& page = first.value().pages[0];
    require(page.outcome == Outcome::Ok && page.selected.has_value(),
            "OcrOnly page succeeded");
    const auto& geometry = page.selected->geometry;
    require(geometry.raster_width > 0 && geometry.raster_height > 0,
            "raster dimensions exposed");
    require(page.selected->source == Source::Ocr, "OCR selected");
    acquire_options.mode = AcquisitionMode::Auto;
    auto automatic = document.acquire({0}, acquire_options);
    require(automatic && automatic.value().pages[0].selected &&
            automatic.value().pages[0].selected->source == Source::Ocr,
            "Auto uses OCR on image-only page");
    acquire_options.max_ocr_attempts = 0;
    auto exhausted = document.acquire({0}, acquire_options);
    require(exhausted && exhausted.value().pages[0].outcome == Outcome::Failed,
            "Auto reports failure when required OCR budget is exhausted");
    acquire_options.max_ocr_attempts = 8;
    acquire_options.mode = AcquisitionMode::OcrOnly;
    acquire_options.raster.max_pixels = 1;
    auto limited = document.acquire({0}, acquire_options);
    require(limited && limited.value().pages[0].outcome == Outcome::Failed &&
            !limited.value().pages[0].attempts.empty() &&
            limited.value().pages[0].attempts.back().state == AttemptState::Failed,
            "bounded raster failure is a page outcome");
    acquire_options.raster.max_pixels = 80'000'000;
    acquire_options.mode = AcquisitionMode::EmbeddedOnly;
    auto embedded = document.acquire({0}, acquire_options);
    require(embedded && embedded.value().pages[0].outcome == Outcome::NoTextFound,
            "EmbeddedOnly reports no text on image-only page");
    require(embedded.value().model_identity == "not-used",
            "EmbeddedOnly batch does not claim a model used by a prior batch");
    acquire_options.mode = AcquisitionMode::OcrOnly;

    FPDF_DOCUMENT pdf = FPDF_LoadDocument(argv[1], nullptr);
    require(pdf != nullptr, "direct PDFium open");
    FPDF_PAGE pdf_page = FPDF_LoadPage(pdf, 0);
    require(pdf_page != nullptr, "direct PDFium page");
    const int width = geometry.raster_width, height = geometry.raster_height;
    std::vector<std::uint8_t> bgr(static_cast<std::size_t>(width) * height * 3);
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGR,
                                            bgr.data(), width * 3);
    require(bitmap != nullptr, "direct BGR bitmap");
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xffffffff);
    FPDF_RenderPageBitmap(bitmap, pdf_page, 0, 0, width, height, 0, 0);
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(pdf_page);
    FPDF_CloseDocument(pdf);

    ocr::Engine direct(argv[2], argv[3], argv[4]);
    const auto expected = direct.run({bgr.data(), width, height, width * 3});
    require(!expected.empty(), "image-only fixture has OCR text");
    require(expected.size() == page.selected->regions.size(), "same line count");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& actual = page.selected->regions[i];
        require(actual.text == expected[i].text, "identical OCR text");
        require(actual.ocr_confidence.has_value() &&
                std::abs(*actual.ocr_confidence - expected[i].confidence) < 1e-6f,
                "identical OCR confidence");
        require(actual.quad.has_value(), "OCR line geometry");
        for (std::size_t j = 0; j < 4; ++j) {
            const auto pixel = geometry.canonical_to_raster->map(
                actual.quad->points[j]);
            require(std::abs(pixel.x - expected[i].box.p[j].x) < 0.01 &&
                    std::abs(pixel.y - expected[i].box.p[j].y) < 0.01,
                    "OCR pixel-to-canonical round trip");
        }
    }
    auto second = document.acquire({0}, acquire_options);
    require(second && second.value().pages[0].selected &&
            second.value().pages[0].selected->regions.size() == expected.size(),
            "repeated acquisition reuses OCR session");
    std::cout << "S1 OCR adapter: " << expected.size()
              << " lines, exact text/confidence and geometry parity\n";
}
