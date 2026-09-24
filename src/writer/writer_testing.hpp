#pragma once

// Private fault-injection seam for S5 tests. Not installed; not public API.

#include <filesystem>
#include <functional>

namespace pdfbookmark::writer::testing {

enum class FaultPoint {
    AfterTempWritten,  // Temp output exists and is closed, before reopen check.
    BeforeCommit,      // All checks passed; final rename not yet attempted.
    AfterCommit        // Output committed.
};

// Return false to simulate a failure at that point (ignored for AfterCommit).
// The hook may also modify the temporary file to simulate corruption.
using FaultHook =
    std::function<bool(FaultPoint, const std::filesystem::path& temp)>;

void set_fault_hook(FaultHook hook);  // Empty hook clears it.

}  // namespace pdfbookmark::writer::testing
