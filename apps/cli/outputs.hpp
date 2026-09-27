#pragma once

// CLI output-destination checks and the analyze/add exit status, kept out of
// main.cpp so they can be unit tested (PR #4 review).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::cli {

// Process exit codes (documented in `pdfbookmark help`).
constexpr int kComplete = 0, kFailed = 1, kUsage = 2, kPartial = 3, kCancelled = 4;

// True when `a` and `b` name the same file: an existing file reached through
// either (hard link, symlink, different spelling), or the same normalised
// absolute path (case-insensitive on Windows) for files that do not exist yet.
bool same_destination(const std::filesystem::path& a, const std::filesystem::path& b);

struct NamedOutput {
    std::string option;                        // e.g. "--report"
    std::optional<std::filesystem::path> path;  // Absent: not requested (or stdout).
};

// The first pair of requested outputs that name the same file, as a message
// ("--report and --metadata name the same file: …"), or nullopt.
std::optional<std::string> output_collision(const std::vector<NamedOutput>& outputs);

// analyze/add exit status after the analysis (and, with --metadata, the
// metadata stage): cancellation of EITHER stage wins, then plan readiness.
int analysis_exit_code(bool analysis_cancelled, bool metadata_cancelled, bool plan_ready);

}  // namespace pdfbookmark::cli
