#pragma once

// S5 Bookmark Writing. No qpdf type crosses this boundary; the backend is a
// private dependency of pdfbookmark::Writer.

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/writer/plan.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pdfbookmark::writer {

struct WriteOptions {
    // Separate from input protection: never permits targeting the input.
    bool replace_existing_output = false;
    std::uint64_t max_input_bytes = 512ull * 1024 * 1024;
};

struct WriteVerification {
    PageCount page_count = 0;         // Reopened output.
    std::size_t outline_items = 0;    // Reopened output outline items.
    bool structure_matches = false;   // Titles, hierarchy, order, destinations.
    bool input_unchanged = false;     // Input digest rechecked before commit.
};

struct WriteResult {
    std::filesystem::path output;
    std::array<std::uint8_t, 32> output_sha256{};
    bool committed = false;
    bool replaced_existing_output = false;
    bool input_had_outline = false;  // Replaced in the copy, never merged.
    WriteVerification verification;
    std::vector<std::string> diagnostics;
};

// Reads the exact bytes of a PDF and reports SHA-256 and page count, so a
// manual plan can be bound to its input without other subsystems.
Result<InputIdentity> read_input_identity(
    const std::filesystem::path& input,
    const WriteOptions& options = {});

// Validates `plan` (structure and actual input identity/count), writes a new
// PDF whose outline is exactly the plan tree, verifies it after reopening, and
// commits it to `output`. The input file is never modified. Errors:
// InvalidArgument (invalid plan or input/output alias), InputOpen,
// InputChanged (stale digest/count or input changed during the write),
// Unsupported (encrypted or signed input), OutputExists, OutputWrite,
// PdfBackend, ResourceLimit, Cancelled (only before commit).
Result<WriteResult> write_copy(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const BookmarkPlan& plan,
    const WriteOptions& options = {},
    const RunControl& control = {});

}  // namespace pdfbookmark::writer
