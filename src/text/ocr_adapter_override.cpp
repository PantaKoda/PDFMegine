#include "ocr_adapter.hpp"

// Test-only fake-OCR factory; empty unless PDFBOOKMARK_TEST_HOOKS is defined.
#ifdef PDFBOOKMARK_TEST_HOOKS

#include <atomic>

namespace pdfbookmark::text::detail {
namespace {
std::atomic<OcrFactory> override_factory{nullptr};
}

void set_ocr_factory_for_testing(OcrFactory factory) noexcept {
    override_factory.store(factory, std::memory_order_release);
}

std::unique_ptr<OcrBackend> make_override_ocr_backend(
    const ModelResources& resources) {
    const auto factory = override_factory.load(std::memory_order_acquire);
    return factory ? factory(resources) : nullptr;
}

}  // namespace pdfbookmark::text::detail

#endif  // PDFBOOKMARK_TEST_HOOKS
