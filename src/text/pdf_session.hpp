#pragma once

#include <pdfbookmark/text/acquisition.hpp>

#include <fpdfview.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace pdfbookmark::text::detail {

std::mutex& pdfium_mutex();

class PdfSession {
public:
    PdfSession(const std::filesystem::path& path, std::size_t max_bytes);
    ~PdfSession();
    PdfSession(const PdfSession&) = delete;
    PdfSession& operator=(const PdfSession&) = delete;

    // Call only while holding pdfium_mutex().
    FPDF_DOCUMENT handle() const noexcept { return document_; }
    PageCount page_count() const noexcept { return identity_.page_count; }
    const InputIdentity& identity() const noexcept { return identity_; }
    std::optional<double> user_unit(PageIndex index) const noexcept {
        return page_units_[static_cast<std::size_t>(index)];
    }

private:
    std::vector<std::uint8_t> bytes_;  // PDFium borrows this immutable snapshot.
    FPDF_DOCUMENT document_ = nullptr;
    InputIdentity identity_;
    std::vector<std::optional<double>> page_units_;
};

}  // namespace pdfbookmark::text::detail
