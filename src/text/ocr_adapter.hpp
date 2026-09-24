#pragma once

#include <pdfbookmark/text/acquisition.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pdfbookmark::text::detail {

struct Raster {
    std::vector<std::uint8_t> bgr;  // Top-down, exactly three bytes per pixel.
    int width = 0;
    int height = 0;
    int stride = 0;
};

struct OcrLine {
    Quad pixel_quad;
    std::string text;
    float confidence = 0;
};

class OcrBackend {
public:
    virtual ~OcrBackend() = default;
    virtual std::vector<OcrLine> run(const Raster&) = 0;
};

// This seam is intentionally backend-free so S1 policy tests can use a fake.
// It is compiled only in test builds (PDFBOOKMARK_TEST_HOOKS); release
// builds contain no override path.
#ifdef PDFBOOKMARK_TEST_HOOKS
using OcrFactory = std::unique_ptr<OcrBackend> (*)(const ModelResources&);
void set_ocr_factory_for_testing(OcrFactory factory) noexcept;
std::unique_ptr<OcrBackend> make_override_ocr_backend(
    const ModelResources& resources);
#else
inline std::unique_ptr<OcrBackend> make_override_ocr_backend(
    const ModelResources&) {
    return nullptr;
}
#endif
std::unique_ptr<OcrBackend> make_ocr_backend(const ModelResources& resources);
const char* ocr_backend_identity() noexcept;
bool ocr_backend_available() noexcept;

}  // namespace pdfbookmark::text::detail
