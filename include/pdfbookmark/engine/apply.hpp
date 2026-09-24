#pragma once

// Engine apply facade: decodes plan JSON and delegates to S5 write_copy.
// Writing policy, input verification and the output transaction are S5's.

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/writer/plan.hpp>
#include <pdfbookmark/writer/writer.hpp>

#include <cstddef>
#include <filesystem>

namespace pdfbookmark::engine {

struct ApplyOptions {
    bool replace_existing_output = false;  // Never permits targeting the input.
    std::size_t max_plan_bytes = 64u * 1024 * 1024;
};

// Writes `plan` to a new PDF at `output`; the input is never modified.
Result<writer::WriteResult> apply(const std::filesystem::path& input,
                                  const std::filesystem::path& output,
                                  const writer::BookmarkPlan& plan,
                                  const ApplyOptions& options = {},
                                  const RunControl& control = {});

// Reads and strictly decodes a plan file (see parse_plan_json), then applies
// it. Malformed plans are InvalidArgument; stale plans are InputChanged (S5).
Result<writer::WriteResult> apply_plan_file(const std::filesystem::path& input,
                                            const std::filesystem::path& output,
                                            const std::filesystem::path& plan_file,
                                            const ApplyOptions& options = {},
                                            const RunControl& control = {});

}  // namespace pdfbookmark::engine
