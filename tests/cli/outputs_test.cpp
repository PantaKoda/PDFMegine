// analyze/add output-collision and exit-status rules (PR #4 review).
// Usage: pdfbookmark_cli_outputs_tests <work dir>
#include "outputs.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace pdfbookmark::cli;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: work-dir");
    const fs::path work = fs::u8path(argv[1]);
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "sub");

    // Files that do not exist yet: same normalised path.
    require(same_destination(work / "same.json", work / "same.json"), "identical paths");
    require(same_destination(work / "same.json", work / "." / "same.json"), "'.' spelling");
    require(same_destination(work / "same.json", work / "sub" / ".." / "same.json"), "'..' spelling");
    require(!same_destination(work / "a.json", work / "b.json"), "different names");
    require(!same_destination(work / "a.json", work / "sub" / "a.json"), "different folders");
#ifdef _WIN32
    require(same_destination(work / "Same.JSON", work / "same.json"), "case-insensitive on Windows");
#endif

    // Existing files: a hard link is the same file under another name.
    { std::ofstream(work / "existing.json") << "{}"; }
    fs::create_hard_link(work / "existing.json", work / "alias.json", ec);
    if (!ec) require(same_destination(work / "existing.json", work / "alias.json"), "hard link alias");
    require(!same_destination(work / "existing.json", work / "other.json"), "existing vs new file");

    // Pairwise collisions among requested outputs; absent entries are skipped.
    const auto clash = output_collision({{"--report", work / "same.json"},
                                         {"--plan", std::nullopt},
                                         {"--metadata", work / "./same.json"},
                                         {"--output", work / "book.pdf"}});
    require(clash && clash->find("--report and --metadata") != std::string::npos,
            "report/metadata collision detected");
    require(output_collision({{"--plan", work / "p.json"}, {"--output", work / "p.json"}}).has_value(),
            "plan/output collision detected");
    require(!output_collision({{"--report", work / "r.json"}, {"--plan", work / "p.json"},
                               {"--metadata", work / "m.json"}, {"--output", work / "o.pdf"},
                               {"--report", std::nullopt}})
                 .has_value(),
            "distinct outputs accepted");

    // Exit status: cancellation of either stage wins.
    require(analysis_exit_code(false, true, true) == kCancelled,
            "metadata cancelled after a ready analysis -> 4");
    require(analysis_exit_code(true, false, false) == kCancelled, "analysis cancelled -> 4");
    require(analysis_exit_code(false, false, true) == kComplete, "ready -> 0");
    require(analysis_exit_code(false, false, false) == kPartial, "not ready -> 3");

    std::cout << "CLI output and exit-status rules passed\n";
    return 0;
}
