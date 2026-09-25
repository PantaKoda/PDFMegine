#include "ocr_adapter.hpp"

#include <stdexcept>

namespace pdfbookmark::text::detail {

std::unique_ptr<OcrBackend> make_ocr_backend(const ModelResources& resources, int /*threads*/) {
    if (auto fake = make_override_ocr_backend(resources)) return fake;
    throw std::runtime_error("S1 was built without OCR support");
}

const char* ocr_backend_identity() noexcept { return "OCR disabled"; }
bool ocr_backend_available() noexcept { return false; }

}  // namespace pdfbookmark::text::detail
