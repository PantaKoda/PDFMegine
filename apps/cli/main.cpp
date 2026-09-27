// pdfbookmark CLI: argument parsing and presentation only. All work goes
// through the Engine facade. Exit codes: 0 complete, 1 failed, 2 usage,
// 3 partial (results written, but incomplete or no ready plan), 4 cancelled.

#include <pdfbookmark/pdfbookmark.hpp>  // The library's stable public API.

#include "page_selection.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kComplete = 0, kFailed = 1, kUsage = 2, kPartial = 3,
              kCancelled = 4;

std::atomic_bool g_cancel{false};
extern "C" void on_interrupt(int) { g_cancel.store(true); }

const char* kOverview =
    "pdfbookmark - add clickable bookmarks to a PDF from its printed table\n"
    "of contents. The input PDF is never modified.\n"
    "\n"
    "Usage:\n"
    "  pdfbookmark <command> <input.pdf> [options]\n"
    "  pdfbookmark help <command>        Options of one command\n"
    "  pdfbookmark <command> --help      (same)\n"
    "  pdfbookmark --version\n"
    "\n"
    "Commands:\n"
    "  add       Do everything in one step: analyze the PDF and, when the\n"
    "            bookmarks are verified, write '<name> (bookmarked).pdf'\n"
    "  analyze   Find the table of contents, map each entry to a page, and\n"
    "            write a bookmark plan (only when it is complete and verified)\n"
    "  apply     Write a NEW PDF with the bookmarks from a plan\n"
    "  metadata  Find the title, authors, edition and years of a book\n"
    "  text      Show the text found on pages, as JSON\n"
    "\n"
    "Typical use:\n"
    "  pdfbookmark add book.pdf                 (writes \"book (bookmarked).pdf\")\n"
    "  pdfbookmark add book.pdf --allow-partial (skip entries it cannot place)\n"
    "\n"
    "Two steps (to review or edit the plan before writing):\n"
    "  pdfbookmark analyze book.pdf --plan plan.json\n"
    "  pdfbookmark apply book.pdf --plan plan.json --output \"book (bookmarked).pdf\"\n"
    "\n"
    "Exit codes: 0 done / plan ready, 1 error, 2 wrong usage,\n"
    "            3 no ready plan or partial result, 4 cancelled (Ctrl+C).\n";

// Options shared by `text` and `analyze` (text acquisition and OCR).
#define PDFBOOKMARK_READING_OPTIONS                                             \
    "Reading options:\n"                                                      \
    "  --mode MODE              auto (default): PDF text, OCR only where needed;\n"\
    "                           embedded: PDF text only (fastest, no OCR);\n" \
    "                           ocr: always use text recognition\n"           \
    "  --models DIR             OCR model folder (default: 'models' next to\n"\
    "                           pdfbookmark.exe)\n"                           \
    "  --no-ocr-models          Do not load OCR models\n"                     \
    "  --dpi N                  OCR image resolution, 50-1200 (default: 300)\n"\
    "  --ocr-budget N           Maximum pages to OCR in this run (default: 64)\n"\
    "  --ocr-threads N          CPU threads for OCR (default 0 = automatic:\n"\
    "                           half the processors, at most 8)\n"

const char* kAnalyzeHelp =
    "pdfbookmark analyze <input.pdf> [options]\n"
    "\n"
    "Finds the printed table of contents (searching the first 40 pages, then\n"
    "20 more at a time), maps every entry to a PDF page, and writes a plan.\n"
    "The plan file is written only when every bookmark is verified (unless\n"
    "you allow a partial result). Existing bookmarks are ignored.\n"
    "\n"
    "Output:\n"
    "  --plan PATH              Write the bookmark plan here (for 'apply')\n"
    "  --report PATH|-          Write the detailed JSON report here\n"
    "                           ('-' = standard output, the default)\n"
    "  --metadata PATH          Also extract title, authors, edition and years\n"
    "                           to this JSON file, in the same run (pages read\n"
    "                           for the contents are reused, not OCR'd twice)\n"
    "  --force                  Replace existing plan/report files\n"
    "\n"
    "Choices:\n"
    "  --allow-partial          Leave out entries that cannot be placed\n"
    "                           reliably (they are listed in the report)\n"
    "  --flat-outline           Put entries whose nesting is unclear at the\n"
    "                           top level\n"
    "  --titles STYLE           printed (default): titles as in the contents,\n"
    "                           e.g. '1 Modern processors'; chapter: top-level\n"
    "                           'Chapter 1: Modern processors' and\n"
    "                           'Appendix A: ...' (sections unchanged)\n"
    "  --candidate ID           Use this table of contents when several are\n"
    "                           found (IDs are shown, e.g. toc-p3-r4)\n"
    "  --max-search-pages N     Pages to search for the contents (default 200)\n"
    "  --max-evidence-pages N   Extra pages to read to confirm page numbers\n"
    "                           (default 500)\n"
    "\n"
    PDFBOOKMARK_READING_OPTIONS
    "\n"
    "Example:\n"
    "  pdfbookmark analyze book.pdf --plan plan.json --report report.json\n"
    "\n"
    "Exit codes: 0 plan ready, 3 no ready plan (report still written),\n"
    "            1 error, 2 wrong usage, 4 cancelled.\n";

const char* kAddHelp =
    "pdfbookmark add <input.pdf> [options]\n"
    "\n"
    "Analyzes the PDF and, when every bookmark is verified, writes a NEW PDF\n"
    "with them (same as 'analyze' followed by 'apply'). The input PDF is\n"
    "never modified. Nothing is written if the bookmarks cannot be verified.\n"
    "\n"
    "Output:\n"
    "  --output PATH            New PDF (default: '<name> (bookmarked).pdf'\n"
    "                           next to the input)\n"
    "  --plan PATH              Also save the bookmark plan\n"
    "  --report PATH            Also save the detailed JSON report\n"
    "  --metadata PATH          Also save the book's metadata (title, authors, ...)\n"
    "                           as JSON, reusing the pages already read\n"
    "  --force                  Replace existing output files\n"
    "\n"
    "Choices:\n"
    "  --allow-partial          Leave out entries that cannot be placed\n"
    "                           reliably (they are listed in the report)\n"
    "  --flat-outline           Put entries whose nesting is unclear at the\n"
    "                           top level\n"
    "  --titles STYLE           printed (default): titles as in the contents,\n"
    "                           e.g. '1 Modern processors'; chapter: top-level\n"
    "                           'Chapter 1: Modern processors' and\n"
    "                           'Appendix A: ...' (sections unchanged)\n"
    "  --candidate ID           Use this table of contents when several are\n"
    "                           found (IDs are shown, e.g. toc-p3-r4)\n"
    "  --max-search-pages N     Pages to search for the contents (default 200)\n"
    "  --max-evidence-pages N   Extra pages to read to confirm page numbers\n"
    "                           (default 500)\n"
    "\n"
    PDFBOOKMARK_READING_OPTIONS
    "\n"
    "Examples:\n"
    "  pdfbookmark add book.pdf\n"
    "  pdfbookmark add book.pdf --allow-partial --output \"book (bm).pdf\"\n"
    "\n"
    "Exit codes: 0 written and verified, 3 not written (no ready plan),\n"
    "            1 error, 2 wrong usage, 4 cancelled.\n";

const char* kMetadataHelp =
    "pdfbookmark metadata <input.pdf> [options]\n"
    "\n"
    "Finds the document's title (and subtitle), authors/editors, edition,\n"
    "publication year and copyright year from its first pages (cover, title\n"
    "and copyright pages), with the evidence for each. Reads the first 10\n"
    "pages, then up to 30 while fields are missing. The PDF is not changed.\n"
    "Publication, copyright and printing years are kept apart: a copyright\n"
    "year is never reported as the publication year.\n"
    "\n"
    "Options:\n"
    "  --json PATH|-            Write the full JSON result (with evidence)\n"
    "                           instead of the summary ('-' = standard output)\n"
    "  --force                  Replace an existing JSON file\n"
    "  --max-pages N            Pages to search at most (default 30)\n"
    "\n"
    PDFBOOKMARK_READING_OPTIONS
    "\n"
    "Example:\n"
    "  pdfbookmark metadata book.pdf\n"
    "\n"
    "Exit codes: 0 all fields found, 3 some fields not found or ambiguous\n"
    "            (the summary shows which), 1 error, 2 wrong usage.\n";

const char* kApplyHelp =
    "pdfbookmark apply <input.pdf> --plan PATH --output PATH [--force]\n"
    "\n"
    "Writes a NEW PDF whose bookmarks are exactly those in the plan. Any\n"
    "bookmarks already in the PDF are replaced in the new copy only. The\n"
    "input PDF is never modified. The plan must have been made by 'analyze'\n"
    "for exactly this input file (it is checked).\n"
    "\n"
    "Options:\n"
    "  --plan PATH              Plan written by 'analyze' (titles may be edited)\n"
    "  --output PATH            New PDF to create\n"
    "  --force                  Replace the output file if it already exists\n"
    "                           (never the input or the plan)\n"
    "\n"
    "Example:\n"
    "  pdfbookmark apply book.pdf --plan plan.json --output \"book (bookmarked).pdf\"\n"
    "\n"
    "Exit codes: 0 written and verified, 1 error (nothing written),\n"
    "            2 wrong usage, 4 cancelled.\n";

const char* kTextHelp =
    "pdfbookmark text <input.pdf> [options]\n"
    "\n"
    "Shows the text found on pages, with positions, as JSON. Useful to see\n"
    "what the program reads (for example on a scanned page).\n"
    "\n"
    "Options:\n"
    "  --pages SPEC             Pages to read, counting from 1: all (default), 5,\n"
    "                           1-40, or a list such as 3,7,10-12\n"
    "  --json PATH|-            Write JSON here ('-' = standard output, the default)\n"
    "  --force                  Replace an existing JSON file\n"
    "\n"
    PDFBOOKMARK_READING_OPTIONS
    "\n"
    "Example:\n"
    "  pdfbookmark text book.pdf --pages 1-5 --json pages.json\n"
    "\n"
    "In the JSON, page_index counts from 0 (page 1 = page_index 0).\n"
    "Exit codes: 0 complete, 3 some pages degraded or failed, 1 error,\n"
    "            2 wrong usage, 4 cancelled.\n";

const char* command_help(const std::string& command) {
    if (command == "add") return kAddHelp;
    if (command == "metadata") return kMetadataHelp;
    if (command == "analyze") return kAnalyzeHelp;
    if (command == "apply") return kApplyHelp;
    if (command == "text") return kTextHelp;
    return nullptr;
}

// The command whose usage error is being reported; set by run().
std::string g_command;

int usage(const std::string& message) {
    if (!message.empty()) std::cerr << "error: " << message << "\n";
    if (g_command.empty()) {
        if (!message.empty()) std::cerr << "\n";
        std::cerr << kOverview;
    } else {
        std::cerr << "Run 'pdfbookmark " << g_command
                  << " --help' for its options.\n";
    }
    return kUsage;
}

std::optional<std::size_t> number(const std::string& text) {
    if (text.empty() || text.size() > 9) return std::nullopt;
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    return value;
}

// Options shared by text and analyze.
struct Common {
    std::optional<fs::path> input;
    pdfbookmark::text::AcquisitionMode mode = pdfbookmark::text::AcquisitionMode::Auto;
    std::optional<pdfbookmark::text::ModelResources> models;
    pdfbookmark::text::RasterLimits raster;
    std::size_t ocr_budget = 64;
    int ocr_threads = 0;  // 0 = automatic
    bool force = false;
};

// Parses the arguments; command-specific flags go to `extra`, which returns
// 1 if it consumed the flag (and value), 0 if unknown, -1 on a usage error.
template <typename Extra>
int parse_common(const std::vector<std::string>& args, const std::string& argv0,
                 Common& common, Extra&& extra) {
    (void)argv0;  // Models are located by the library (find_models).
    std::string mode_name = "auto";
    std::optional<fs::path> models_dir;
    bool no_models = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i];
            return true;
        };
        std::string text;
        if (arg == "--mode") {
            if (!value(mode_name)) return usage("--mode needs a value");
        } else if (arg == "--models") {
            if (!value(text)) return usage("--models needs a directory");
            models_dir = fs::u8path(text);
        } else if (arg == "--no-ocr-models") {
            no_models = true;
        } else if (arg == "--force") {
            common.force = true;
        } else if (arg == "--dpi" || arg == "--ocr-budget" || arg == "--ocr-threads") {
            if (!value(text)) return usage(arg + " needs a value");
            const auto n = number(text);
            if (!n) return usage(arg + " must be a non-negative integer");
            if (arg == "--dpi") {
                if (*n < 50 || *n > 1200) return usage("--dpi must be 50..1200");
                common.raster.dpi = static_cast<int>(*n);
            } else if (arg == "--ocr-threads") {
                if (*n > 64) return usage("--ocr-threads must be 0..64");
                common.ocr_threads = static_cast<int>(*n);
            } else {
                common.ocr_budget = *n;
            }
        } else {
            const int used = extra(arg, value);
            if (used < 0) return kUsage;
            if (used > 0) continue;
            if (!arg.empty() && arg[0] == '-' && arg != "-")
                return usage("Unknown option " + arg);
            if (common.input) return usage("Only one input PDF is accepted");
            common.input = fs::u8path(arg);
        }
    }
    if (!common.input) return usage("An input PDF is required");
    const std::map<std::string, pdfbookmark::text::AcquisitionMode> modes = {
        {"auto", pdfbookmark::text::AcquisitionMode::Auto},
        {"embedded", pdfbookmark::text::AcquisitionMode::EmbeddedOnly},
        {"ocr", pdfbookmark::text::AcquisitionMode::OcrOnly}};
    const auto mode = modes.find(mode_name);
    if (mode == modes.end()) return usage("--mode must be auto, embedded or ocr");
    common.mode = mode->second;
    if (models_dir && no_models)
        return usage("--models and --no-ocr-models conflict");
    if (models_dir) {
        common.models = pdfbookmark::models_in(*models_dir);
        if (!common.models)
            return usage("OCR models not found under " + models_dir->u8string());
    } else if (!no_models &&
               common.mode != pdfbookmark::text::AcquisitionMode::EmbeddedOnly) {
        common.models = pdfbookmark::find_models();  // Env var, then next to the library.
    }
    return kComplete;
}

// Checks an output target before any work: never the input, no clobbering.
bool output_allowed(const std::optional<fs::path>& target, const fs::path& input,
                    bool force) {
    if (!target) return true;
    std::error_code ec;
    if (!fs::exists(*target, ec)) return true;
    if (fs::equivalent(*target, input, ec) || ec) {
        std::cerr << "error: output must not be the input PDF\n";
        return false;
    }
    if (!force) {
        std::cerr << "error: " << target->u8string()
                  << " exists; use --force to replace it\n";
        return false;
    }
    return true;
}

bool write_output(const fs::path& path, const std::string& data) {
    fs::path temp = path;
    temp += ".partial";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(data.data(),
                               static_cast<std::streamsize>(data.size()))) {
            std::cerr << "error: cannot write " << temp.u8string() << '\n';
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        std::cerr << "error: cannot commit " << path.u8string() << '\n';
        return false;
    }
    return true;
}

bool emit(const std::optional<fs::path>& target, const std::string& data) {
    if (target) return write_output(*target, data);
    std::fwrite(data.data(), 1, data.size(), stdout);
    std::fflush(stdout);
    return true;
}

std::optional<fs::path> target_of(const std::string& value) {
    return value == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(value));
}

int run_text(const std::vector<std::string>& args, const std::string& argv0) {
    using namespace pdfbookmark;
    Common common;
    std::string pages_spec = "all", json_target = "-";
    const int parsed = parse_common(args, argv0, common,
        [&](const std::string& arg, auto& value) {
            if (arg == "--pages") return value(pages_spec) ? 1 : (usage("--pages needs a value"), -1);
            if (arg == "--json") return value(json_target) ? 1 : (usage("--json needs a value"), -1);
            return 0;
        });
    if (parsed != kComplete) return parsed;
    std::string error;
    const auto selection = cli::parse_page_selection(pages_spec, error);
    if (!selection) return usage(error);
    const auto json_path = target_of(json_target);
    if (!output_allowed(json_path, *common.input, common.force)) return kFailed;

    engine::TextOptions options;
    options.mode = common.mode;
    options.models = common.models;
    options.raster = common.raster;
    options.ocr_threads = common.ocr_threads;
    options.ocr_budget = common.ocr_budget;
    std::signal(SIGINT, on_interrupt);
    const auto report = engine::extract_text(
        *common.input,
        selection->all ? std::nullopt
                       : std::optional<std::vector<PageIndex>>(selection->indices),
        options, RunControl{&g_cancel}, [](const engine::TextProgress& p) {
            std::cerr << "text: " << p.pages_done << "/" << p.pages_total
                      << " pages\n";
        });
    if (!report) {
        std::cerr << "error: " << report.error().message << '\n';
        return kFailed;
    }
    if (!emit(json_path, engine::text_report_json(report.value()))) return kFailed;

    const auto& r = report.value();
    std::map<text::Outcome, std::size_t> counts;
    for (const auto& page : r.pages) ++counts[page.outcome];
    std::cerr << "text: " << r.pages.size() << " pages (ok=" << counts[text::Outcome::Ok]
              << " degraded=" << counts[text::Outcome::Degraded]
              << " no_text=" << counts[text::Outcome::NoTextFound]
              << " failed=" << counts[text::Outcome::Failed]
              << " cancelled=" << counts[text::Outcome::Cancelled]
              << "), OCR attempts " << r.ocr_attempts_used << "/" << r.ocr_budget
              << (options.models ? "" : ", OCR models not loaded") << '\n';
    for (const auto& page : r.pages)
        if (page.outcome == text::Outcome::Degraded ||
            page.outcome == text::Outcome::Failed) {
            std::cerr << "  page " << page.page_index + 1 << " (index "
                      << page.page_index << "): "
                      << (page.outcome == text::Outcome::Failed ? "failed" : "degraded");
            if (!page.reasons.empty())
                std::cerr << " - " << page.reasons.front();
            else if (!page.attempts.empty())
                std::cerr << " - " << page.attempts.back().reason;
            std::cerr << '\n';
        }
    switch (r.status) {
        case engine::TextStatus::Complete: return kComplete;
        case engine::TextStatus::Partial:
            std::cerr << "text: partial result (see page outcomes)\n";
            return kPartial;
        case engine::TextStatus::Cancelled:
            std::cerr << "text: cancelled; completed pages were written\n";
            return kCancelled;
    }
    return kFailed;
}

// analyze (add == false) or add (add == true): add = analyze, then apply the
// ready plan in the same run to a new PDF.
int run_analyze(const std::vector<std::string>& args, const std::string& argv0,
                bool add) {
    using namespace pdfbookmark;
    const char* name = add ? "add" : "analyze";
    Common common;
    std::string report_target = add ? "" : "-", plan_target, output_target,
                candidate, number_text, metadata_target;
    engine::AnalysisOptions options;
    const int parsed = parse_common(args, argv0, common,
        [&](const std::string& arg, auto& value) {
            if (arg == "--report") return value(report_target) ? 1 : (usage("--report needs a value"), -1);
            if (arg == "--plan") return value(plan_target) ? 1 : (usage("--plan needs a path"), -1);
            if (arg == "--metadata") {
#ifdef PDFBOOKMARK_WITH_METADATA
                return value(metadata_target) ? 1 : (usage("--metadata needs a path"), -1);
#else
                return (usage("--metadata: this build has no metadata extraction"), -1);
#endif
            }
            if (add && arg == "--output")
                return value(output_target) ? 1 : (usage("--output needs a path"), -1);
            if (arg == "--candidate") {
                if (!value(candidate)) return (usage("--candidate needs an ID"), -1);
                options.candidate_id = candidate;
                return 1;
            }
            if (arg == "--allow-partial") { options.plan.allow_partial = true; return 1; }
            if (arg == "--titles") {
                std::string style;
                if (!value(style)) return (usage("--titles needs printed or chapter"), -1);
                if (style == "printed")
                    options.plan.title_style = engine::PlanPolicy::TitleStyle::AsPrinted;
                else if (style == "chapter")
                    options.plan.title_style = engine::PlanPolicy::TitleStyle::Chapter;
                else
                    return (usage("--titles must be printed or chapter"), -1);
                return 1;
            }
            if (arg == "--flat-outline") {
                options.plan.flat_outline_for_unknown_hierarchy = true;
                return 1;
            }
            if (arg == "--max-search-pages" || arg == "--max-evidence-pages") {
                const auto n = value(number_text) ? number(number_text) : std::nullopt;
                if (!n || (arg == "--max-search-pages" && *n == 0))
                    return (usage(arg + " needs a valid count"), -1);
                (arg == "--max-search-pages" ? options.limits.max_search_pages
                                             : options.limits.max_evidence_pages) = *n;
                return 1;
            }
            return 0;
        });
    if (parsed != kComplete) return parsed;
    if (plan_target == "-") return usage("--plan needs a file path");
    if (add && report_target == "-")
        return usage("add writes the PDF; give --report a file path");
    const std::optional<fs::path> report_path =
        report_target.empty() ? std::nullopt : target_of(report_target);
    const std::optional<fs::path> plan_path =
        plan_target.empty() ? std::nullopt
                            : std::optional<fs::path>(fs::u8path(plan_target));
    if (metadata_target == "-") return usage("--metadata needs a file path");
    const std::optional<fs::path> metadata_path =
        metadata_target.empty() ? std::nullopt
                                : std::optional<fs::path>(fs::u8path(metadata_target));
    std::optional<fs::path> output_path;
    if (add) {
        if (output_target.empty()) {
            fs::path out = common.input->parent_path();
            out /= common.input->stem();
            out += fs::u8path(" (bookmarked).pdf");
            output_path = out;
        } else {
            output_path = fs::u8path(output_target);
        }
    }
    if (!output_allowed(report_path, *common.input, common.force) ||
        !output_allowed(plan_path, *common.input, common.force) ||
        !output_allowed(output_path, *common.input, common.force) ||
        !output_allowed(metadata_path, *common.input, common.force))
        return kFailed;

    options.mode = common.mode;
    options.models = common.models;
    options.raster = common.raster;
    options.ocr_threads = common.ocr_threads;
    options.limits.ocr_budget = common.ocr_budget;
    std::signal(SIGINT, on_interrupt);
    const auto on_progress = [name](const engine::AnalysisProgress& p) {
        std::cerr << name << ": " << p.stage << ", " << p.pages_acquired << " pages read\n";
    };
    engine::AnalysisReport r;
#ifdef PDFBOOKMARK_WITH_METADATA
    if (metadata_path) {
        // One run, one session: pages read for the TOC are reused for the
        // metadata instead of being read (and OCR'd) again.
        engine::MetadataRunOptions meta;
        meta.mode = common.mode;
        meta.raster = common.raster;
        meta.ocr_budget = common.ocr_budget;
        auto book = engine::analyze_book(*common.input, options, meta, RunControl{&g_cancel},
                                         on_progress);
        if (!book) {
            std::cerr << "error: " << book.error().message << '\n';
            return kFailed;
        }
        if (!emit(metadata_path, engine::metadata_report_json(book.value().metadata)))
            return kFailed;
        std::cerr << name << ": metadata written (" << book.value().pages_reused
                  << " pages reused from the analysis)\n";
        r = std::move(book.value().analysis);
    } else
#endif
    {
        auto result = engine::analyze(*common.input, options, RunControl{&g_cancel}, on_progress);
        if (!result) {
            std::cerr << "error: " << result.error().message << '\n';
            return kFailed;
        }
        r = result.take();
    }
    if (report_path || !add)
        if (!emit(report_path, engine::analysis_report_json(r, options))) return kFailed;
    if (plan_path && r.plan.ready && r.plan.plan &&
        !write_output(*plan_path, engine::plan_json(*r.plan.plan)))
        return kFailed;

    std::cerr << name << ": outcome " << engine::outcome_name(r.outcome) << "; "
              << r.search_pages.size() << " pages searched, "
              << r.evidence_pages.size() << " evidence pages, OCR "
              << r.ocr_attempts_used << "/" << r.ocr_budget << '\n';
    if (r.candidate.chosen_id) {
        std::cerr << name << ": TOC " << *r.candidate.chosen_id << " ("
                  << r.candidate.reason << ")\n";
    } else if (!r.candidate.alternatives.empty()) {
        std::cerr << name << ": candidates:";
        for (const auto& id : r.candidate.alternatives) std::cerr << ' ' << id;
        std::cerr << " (choose with --candidate)\n";
    }
    if (r.mapping) {
        std::size_t resolved = 0;
        for (const auto& e : r.mapping->entries)
            resolved += e.status == mapping::MappingStatus::Resolved;
        std::cerr << name << ": " << resolved << "/" << r.mapping->entries.size()
                  << " entries resolved\n";
    }
    for (const auto& reason : r.stop_reasons)
        std::cerr << "  stop: " << reason << '\n';
    for (const auto& note : r.diagnostics)
        std::cerr << "  note: " << note << '\n';
    // Long books can produce hundreds of lines; show the first few.
    const auto list = [](const std::vector<std::string>& lines, const char* tag) {
        constexpr std::size_t kShown = 12;
        for (std::size_t i = 0; i < lines.size() && i < kShown; ++i)
            std::cerr << "  " << tag << ": " << lines[i] << '\n';
        if (lines.size() > kShown)
            std::cerr << "  ... and " << lines.size() - kShown << " more "
                      << tag << " lines (see the JSON report)\n";
    };
    list(r.plan.choices, "choice");
    list(r.plan.blockers, "blocked");
    if (r.outcome == engine::AnalysisOutcome::Cancelled) return kCancelled;
    if (r.outcome != engine::AnalysisOutcome::PlanReady) {
        std::cerr << name << ": no ready plan"
                  << (plan_path ? "; plan file not written" : "")
                  << (add ? "; no PDF written" : "") << '\n';
        if (!options.plan.allow_partial)
            std::cerr << "  tip: --allow-partial keeps the entries that can be "
                         "placed and leaves out the rest\n";
        return kPartial;
    }
    std::cerr << name << ": plan ready"
              << (plan_path ? " and written to " + plan_path->u8string() : "")
              << (r.plan.plan->omitted_entries.empty() ? "" : " (partial: see omissions)")
              << '\n';
    if (!add) return kComplete;

    engine::ApplyOptions apply_options;
    apply_options.replace_existing_output = common.force;
    const auto written = engine::apply(*common.input, *output_path, *r.plan.plan,
                                       apply_options, RunControl{&g_cancel});
    if (!written) {
        std::cerr << "error: " << written.error().message << '\n'
                  << "add: nothing written; input unchanged\n";
        return written.error().code == ErrorCode::Cancelled ? kCancelled : kFailed;
    }
    const auto& w = written.value();
    std::cerr << "add: wrote " << output_path->u8string() << " with "
              << w.verification.outline_items << " bookmarks (verified after reopening)"
              << (w.input_had_outline ? "; existing bookmarks replaced in the copy" : "")
              << '\n';
    return kComplete;
}

#ifdef PDFBOOKMARK_WITH_METADATA
std::string pages_one_based(const std::vector<pdfbookmark::metadata::Evidence>& evidence) {
    std::vector<int> pages;
    for (const auto& e : evidence)
        if (std::find(pages.begin(), pages.end(), e.source.page_index + 1) == pages.end())
            pages.push_back(e.source.page_index + 1);
    std::sort(pages.begin(), pages.end());
    std::string out;
    for (const int page : pages) out += (out.empty() ? "" : ", ") + std::to_string(page);
    return out;
}

template <typename T>
std::string field_status(const pdfbookmark::metadata::Field<T>& f) {
    using pdfbookmark::metadata::FieldStatus;
    if (f.status == FieldStatus::Resolved)
        return "[found on page " + pages_one_based(f.evidence) + "]";
    std::string text = f.status == FieldStatus::Ambiguous ? "ambiguous" : "not found";
    if (!f.reasons.empty()) text += " - " + f.reasons.back();
    return text;
}

int run_metadata(const std::vector<std::string>& args, const std::string& argv0) {
    using namespace pdfbookmark;
    Common common;
    std::string json_target, number_text;
    engine::MetadataRunOptions options;
    const int parsed = parse_common(args, argv0, common,
        [&](const std::string& arg, auto& value) {
            if (arg == "--json") return value(json_target) ? 1 : (usage("--json needs a value"), -1);
            if (arg == "--max-pages") {
                const auto n = value(number_text) ? number(number_text) : std::nullopt;
                if (!n || *n == 0) return (usage("--max-pages needs a positive count"), -1);
                options.max_pages = *n;
                return 1;
            }
            return 0;
        });
    if (parsed != kComplete) return parsed;
    const std::optional<fs::path> json_path =
        json_target.empty() || json_target == "-" ? std::nullopt
                                                  : std::optional<fs::path>(fs::u8path(json_target));
    if (!output_allowed(json_path, *common.input, common.force)) return kFailed;
    options.mode = common.mode;
    options.models = common.models;
    options.raster = common.raster;
    options.ocr_threads = common.ocr_threads;
    options.ocr_budget = std::min<std::size_t>(common.ocr_budget, 64);
    std::signal(SIGINT, on_interrupt);
    const auto result = engine::extract_metadata(*common.input, options, RunControl{&g_cancel});
    if (!result) {
        std::cerr << "error: " << result.error().message << '\n';
        return kFailed;
    }
    const auto& report = result.value();
    const auto& r = report.result;
    if (!json_target.empty()) {
        if (!emit(json_path, engine::metadata_report_json(report))) return kFailed;
    } else {
        std::string contributors;
        if (r.contributors.value)
            for (const auto& c : *r.contributors.value)
                contributors += (contributors.empty() ? "" : ", ") + c.name + " (" +
                                metadata::role_name(c.role) + ")";
        std::cout << "Title:            "
                  << (r.title.value ? r.title.value->title + "  " : "")
                  << field_status(r.title) << '\n'
                  << "Subtitle:         "
                  << (r.title.value && r.title.value->subtitle ? *r.title.value->subtitle
                                                                : "(none)")
                  << '\n'
                  << "Contributors:     " << (contributors.empty() ? "" : contributors + "  ")
                  << field_status(r.contributors) << '\n'
                  << "Edition:          "
                  << (r.edition.value ? r.edition.value->statement + "  " : "")
                  << field_status(r.edition) << '\n'
                  << "Publication year: "
                  << (r.publication_year.value
                          ? std::to_string(r.publication_year.value->year) + "  " : "")
                  << field_status(r.publication_year) << '\n'
                  << "Copyright year:   "
                  << (r.copyright_year.value
                          ? std::to_string(r.copyright_year.value->year) + "  " : "")
                  << field_status(r.copyright_year) << '\n'
                  << "Pages searched:   " << report.searched_pages.size() << " of "
                  << report.input.page_count
                  << (report.search_covered_document ? " (whole document)" : "") << '\n';
        std::cout << "(Page numbers count from 1 = first page of the file. Use --json\n"
                     " for the evidence and alternatives of every field.)\n";
    }
    if (report.cancelled) return kCancelled;
    const bool all = r.title.status == metadata::FieldStatus::Resolved &&
                     r.contributors.status == metadata::FieldStatus::Resolved &&
                     r.edition.status == metadata::FieldStatus::Resolved &&
                     r.publication_year.status == metadata::FieldStatus::Resolved;
    return all ? kComplete : kPartial;
}
#endif

int run_apply(const std::vector<std::string>& args) {
    using namespace pdfbookmark;
    std::optional<fs::path> input, plan, output;
    engine::ApplyOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--plan" || arg == "--output") {
            if (i + 1 >= args.size()) return usage(arg + " needs a path");
            (arg == "--plan" ? plan : output) = fs::u8path(args[++i]);
        } else if (arg == "--force") {
            options.replace_existing_output = true;
        } else if (!arg.empty() && arg[0] == '-') {
            return usage("Unknown option " + arg);
        } else if (input) {
            return usage("Only one input PDF is accepted");
        } else {
            input = fs::u8path(arg);
        }
    }
    if (input && !plan) {
        std::cerr << "error: apply needs a plan made by 'analyze' (--plan PATH).\n"
                     "To analyze and write the bookmarked PDF in one step, run:\n"
                     "  pdfbookmark add \"" << input->u8string() << "\"\n";
        return kUsage;
    }
    if (!input || !plan || !output)
        return usage("apply requires <input.pdf> --plan PATH --output PATH");
    std::signal(SIGINT, on_interrupt);
    std::cerr << "apply: writing " << output->u8string() << '\n';
    const auto result = engine::apply_plan_file(*input, *output, *plan, options,
                                                RunControl{&g_cancel});
    if (!result) {
        const auto& error = result.error();
        std::cerr << "error: " << error.message << '\n';
        if (error.code == ErrorCode::InputChanged)
            std::cerr << "  the plan was made for different input bytes; "
                         "re-run analyze on this file\n";
        std::cerr << "apply: nothing written; input unchanged\n";
        return error.code == ErrorCode::Cancelled ? kCancelled : kFailed;
    }
    const auto& r = result.value();
    std::cerr << "apply: committed " << r.verification.outline_items
              << " bookmarks on " << r.verification.page_count
              << " pages (verified after reopening)"
              << (r.input_had_outline ? "; existing outline replaced in the copy" : "")
              << (r.replaced_existing_output ? "; previous output file replaced" : "")
              << '\n';
    for (const auto& d : r.diagnostics) std::cerr << "  note: " << d << '\n';
    return kComplete;
}

int run(const std::vector<std::string>& args) {
    if (args.size() < 2) return usage({});
    const std::string& command = args[1];
    if (command == "--help" || command == "-h" || command == "help") {
        if (args.size() >= 3) {
            const char* text = command_help(args[2]);
            if (!text) return usage("Unknown command '" + args[2] + "'");
            std::cout << text;
        } else {
            std::cout << kOverview;
        }
        return kComplete;
    }
    if (command == "--version" || command == "-V" || command == "version") {
        std::cout << "pdfbookmark " << pdfbookmark::version() << '\n';
        return kComplete;
    }
    const std::vector<std::string> rest(args.begin() + 2, args.end());
    if (command_help(command)) {
        g_command = command;
        for (const auto& arg : rest)
            if (arg == "--help" || arg == "-h") {
                std::cout << command_help(command);
                return kComplete;
            }
    }
    if (command == "text") return run_text(rest, args[0]);
    if (command == "analyze") return run_analyze(rest, args[0], false);
    if (command == "add") return run_analyze(rest, args[0], true);
    if (command == "metadata") {
#ifdef PDFBOOKMARK_WITH_METADATA
        return run_metadata(rest, args[0]);
#else
        return usage("'metadata' is not available in this build");
#endif
    }
    if (command == "apply") return run_apply(rest);
    return usage("Unknown command '" + command + "'");
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);  // Exact UTF-8 JSON bytes.
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(fs::path(argv[i]).u8string());
    return run(args);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
