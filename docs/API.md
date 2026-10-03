# pdfbookmark library: API guide

pdfbookmark reads a PDF book's printed table of contents, works out which physical page each entry starts on, and writes a **new** PDF with matching bookmarks. It can also extract a book's title, authors, edition and years. The command-line tool `pdfbookmark` is one client of this library; a Qt app, a Python script or a C# program can be others.

There are two interfaces to the same library:

| Interface | Header | For |
| --- | --- | --- |
| **C++ API** | `<pdfbookmark/pdfbookmark.hpp>`, namespace `pdfbookmark` | C++ applications built with MSVC (e.g. Qt). Typed results, no JSON parsing. |
| **C API** | `<pdfbookmark/pdfbookmark.h>`, functions `pdfb_*` | C, and every language with a C FFI: Python, C#, Rust, Go, Java and more. Works with any compiler and any configuration. Results are JSON. |

- **CMake target:** `pdfbookmark::pdfbookmark` from package `pdfbookmark`. Both headers use the same target.
- **Version:** `pdfbookmark::version()` / `pdfb_version()` and `PDFBOOKMARK_VERSION_STRING`, following semantic versioning. The C API also has its own version number, `PDFB_C_API_VERSION` (currently 1).

Contents:
1. Using the SDK
2. Conventions
3. C++ API
4. C API
5. Python
6. Advanced API
7. Versioning

## 1. Using the SDK

```cmake
find_package(pdfbookmark 0.1 CONFIG REQUIRED)          # CMAKE_PREFIX_PATH=<sdk folder>
target_link_libraries(my_app PRIVATE pdfbookmark::pdfbookmark)
pdfbookmark_deploy_runtime(my_app)   # pdfbookmark.dll, dependency DLLs, OCR models
```

`pdfbookmark_deploy_runtime` runs on every build. It copies only changed files next to the executable, with the models in `models/`, where `find_models()` looks. When you ship your application, include those files. The library quiets OpenCV's INFO log output unless you set `OPENCV_LOG_LEVEL` yourself.

The SDK folder layout:

| Path | Contents |
| --- | --- |
| `include/pdfbookmark/` | Public headers: `pdfbookmark.hpp` (C++, plus the headers it includes) and `pdfbookmark.h` (C) |
| `lib/` | `pdfbookmark.lib` (Release) and `pdfbookmarkd.lib` (Debug) import libraries |
| `bin/` | `pdfbookmark.dll`, `pdfbookmarkd.dll`, and the Release runtime dependencies (PDFium, qpdf, zlib, libjpeg, ONNX Runtime, OpenCV) |
| `bin/debug/` | The Debug runtime dependencies |
| `share/pdfbookmark/models/` | OCR models (needed only for scanned pages) |
| `lib/cmake/pdfbookmark/` | CMake package, version file and deploy helper |
| `share/doc/pdfbookmark/` | This guide, `JSON_FORMATS.md`, `AGENTS.md` (a brief for coding agents working on client projects), the examples (`basic` in C++, `qt-quick` for Qt 6 QML, `python`) and licence notices |

**Compatibility.**
- **C API:** plain C types only, so any compiler (MSVC, clang, MinGW) and any language with a C FFI can use it. Debug and Release clients can both use the Release `pdfbookmark.dll`.
- **C++ API:** passes standard C++ types (`std::string`, `std::vector`, `std::filesystem::path`) between your program and the DLL. Their memory layout differs between compilers, and between Debug and Release builds, so:
  - build C++ clients with **MSVC x64** and **C++17 or later**, using the dynamic runtime (`/MD`, `/MDd`), which is the CMake and Qt default;
  - **Debug** clients link `pdfbookmarkd`, and Release, RelWithDebInfo and MinSizeRel clients link `pdfbookmark`. CMake selects the right one automatically.
  - For Qt 6, use Qt's "MSVC 2022 64-bit" kit.

**Qt Quick:** `examples/qt-quick` is a complete application. It shows the CMake setup, a `QML_ELEMENT` controller that runs the library on a worker thread, progress and cancellation, editable bookmark titles, and writing the new PDF. CI builds it against the installed SDK and runs it headless (`--selftest`).

The backends (PDFium, qpdf, ONNX Runtime, OpenCV) are private. No backend header or type appears in the API, and clients never link them directly.

## 2. Conventions

These apply to both interfaces. The C++ forms are shown here; section 4 gives the C forms.

- **Pages.** Page indices in the API and in JSON are **zero based**: the first page of the file is 0. Show `index + 1` to users, the way PDF viewers do.
- **Text** is UTF-8. On Windows, build paths from wide strings (`std::filesystem::path(L"…")`, `wmain`, or Qt's `QString::toStdWString()`) so that non-ASCII file names work.
- **Results.** Every operation returns `Result<T>`:

  ```cpp
  const auto report = pdfbookmark::analyze(pdf);
  if (!report) { std::cerr << report.error().message; return; }   // ErrorCode + message
  use(report.value());
  ```

  Errors describe the whole call: a bad argument, an unreadable file, an unsupported (encrypted or signed) PDF, an existing output, cancellation. Expected per-page or per-entry problems are **part of the value** (outcomes, blockers, reasons), not errors.
- **Threads.** Operations block, from milliseconds up to minutes for large scanned books, so call them from a worker thread in GUI applications. Progress callbacks run on that worker thread; marshal them to the UI thread (in Qt, `QMetaObject::invokeMethod(…, Qt::QueuedConnection)`). The PDF engine is shared by the whole process and concurrent calls are serialized, so run one operation at a time.
- **OCR cost.** Only scanned (image-only) pages need OCR, and it is the expensive part. Typical figures for US-letter pages on a 12-core desktop CPU (Ryzen 9 5900X):

  | Render resolution (`dpi`) | Time per page | Observed peak memory of the process |
  | --- | --- | --- |
  | 300 (default) | about 7.4 s | about 2.3 GB |
  | 200 | about 2.8 s | about 1.1 GB |
  | 150 | about 1.6 s | about 0.6 GB |

  - These are **observed peaks, not limits.** They were stable across the tested fixtures (1, 4 and 20 US Letter pages). Larger pages, other content and other recognition batches can need more.
  - Lower resolutions may reduce accuracy on small print. Evaluate on your own scans before lowering `dpi` by default.
  - `ocr_threads` (default automatic: half the processors, at most 8) trades speed against CPU left for your UI. Results are the same for any thread count, apart from OCR confidence differences of about 1e-6.
  - Pages with a real text layer need no OCR and take milliseconds.
- **Cancellation.** Pass `RunControl{&flag}` with a `std::atomic_bool flag`, and set `flag = true` from any thread. Cancellation is checked between pages.
- **Safety.** The input PDF is never modified. Existing bookmarks are never used as evidence. `apply()` writes a new file whose outline is exactly the plan, verifies it by reopening it, and only then commits it. The output can never be the input or the plan file.
- **Ownership.** Results own all their data and stay valid after the call returns.

## 3. C++ API

### Finding OCR models
```cpp
std::optional<ModelResources> find_models();                   // PDFBOOKMARK_MODELS, next to the DLL, ../share/pdfbookmark/models
std::optional<ModelResources> models_in(const std::filesystem::path& dir);
```
Pass the result as `options.models`. Without models, PDFs with real text still work, and scanned pages are reported as unreadable.

### Adding bookmarks (analyze, then apply)
```cpp
pdfbookmark::AnalysisOptions options;
options.models = pdfbookmark::find_models();
options.plan.allow_partial = true;                              // optional, see below
options.plan.title_style = pdfbookmark::PlanPolicy::TitleStyle::Chapter;  // "Chapter 1: …"
auto report = pdfbookmark::analyze(pdf, options, control, on_progress);
if (report && report.value().plan.ready)
    pdfbookmark::apply(pdf, output, *report.value().plan.plan);
```

`AnalysisReport` contains:
- **`outcome`:** `PlanReady`, `AnalysisPartial` (a TOC was found but the plan is blocked; see `plan.blockers`), `NoTocFoundInSearch`, `SearchIncomplete` or `Cancelled`.
- **`plan`:**
  - `ready`;
  - `plan`, a `BookmarkPlan` that is present even when not ready, for review;
  - `blockers`, the reasons it isn't ready, in plain language;
  - `choices`, policy decisions such as omissions and promotions;
  - `validation`.
- **Evidence:** the detected TOC `candidate`, the parsed `entries`, the page `mapping` of each entry (resolved, ambiguous or unresolved, with reasons and alternatives), the acquired `pages`, `stop_reasons` and `diagnostics`.

`PlanPolicy` options:
- **`allow_partial`** leaves out entries that can't be placed reliably, and records them in `omitted_entries`.
- **`flat_outline_for_unknown_hierarchy`** puts entries with unclear nesting at the top level.
- **`title_style`** keeps titles as printed (default) or uses "Chapter N: …".

`SearchLimits` bounds the work: the first 40 pages, then 20 at a time up to 200; up to 500 extra pages to confirm page numbers; and up to 64 OCR attempts.

### Editing a plan (review screens)
`BookmarkPlan` is plain data. Each `nodes` entry has `id`, `parent_id` (none for top level), `title` (UTF-8) and `destination.pdf_page_index`; array order is sibling order. Clients may edit titles, parents and destinations, then:
```cpp
PlanValidation v = pdfbookmark::validate_plan(plan);   // structure: ids, parents, cycles, titles, page bounds
std::string json = pdfbookmark::plan_to_json(plan);    // save for later
auto again = pdfbookmark::plan_from_json(json);        // strict decoding
auto identity = pdfbookmark::read_pdf_identity(pdf);   // SHA-256 + page count
```
A plan is bound to the exact PDF bytes (SHA-256 and page count), and `apply()` refuses a plan made for a different file.

### Writing
```cpp
Result<WriteResult> apply(pdf, output, plan, ApplyOptions{/*replace_existing_output*/ false}, control);
Result<WriteResult> apply_plan_file(pdf, output, plan_json_path, options, control);
```
Encrypted and signed PDFs are refused (`ErrorCode::Unsupported`). An existing output file is refused unless `replace_existing_output` is set.

### Book metadata
```cpp
auto meta = pdfbookmark::extract_metadata(pdf, MetadataRunOptions{});  // first 10 pages, then up to 30
```
`result.title`, `contributors`, `edition`, `publication_year` and `copyright_year` each have:
- a `status`: `Resolved`, `Ambiguous` or `NotFoundInSearch`;
- a `value`, only when resolved;
- `evidence` (page, region and text), `alternatives` and `reasons`.

A copyright year is never reported as the publication year.

`result.isbns` lists every ISBN printed in the pages searched (empty when none was found). Each entry has the normalised `isbn13`, the `printed` form, the `format` (`Hardcover`, `Paperback`, `Electronic`, `Print` or `Unknown`), the printed `label` and its `evidence`. An ISBN identifies one edition in one format, so a book usually has several; none is singled out.

### Contents and metadata together
```cpp
auto book = pdfbookmark::analyze_book(pdf, analysis_options, metadata_options, control, on_progress,
    [&](const pdfbookmark::MetadataReport& m) { show_title(m); });   // early, optional
// book.value().metadata        - as extract_metadata(), always present on success
// book.value().analysis        - as analyze(); empty if the analysis failed ...
// book.value().analysis_error  - ... and then this says why
// book.value().pages_reused    - analysis pages taken from what the metadata stage read
```
- **Use this when you need both.** The PDF is opened once, and the pages read for the metadata are reused by the TOC analysis instead of being read and OCR'd again. On a scanned book this halves the work: for 4 scanned pages, 61 s becomes 31 s.
- **Metadata first, and early.** The metadata stage runs first, and `on_metadata` receives its report before the (much longer) TOC analysis starts, so a client can show the title within about a minute on a scanned book instead of after the whole analysis.
- **Metadata survives a failed analysis.** An analysis error is returned in `analysis_error`, and the metadata is still returned. Only errors in opening the file or in the metadata stage fail the whole call.
- **Same results:** with enough OCR budget, results equal the two separate calls. Only the analysis report's `ocr_attempts_used` is lower.
- **Settings:** `models` and `ocr_threads` come from the analysis options. Pages are reused only when both option sets use the same `mode` and `raster` settings, which the defaults do. With the defaults the metadata pages (at most 30) lie inside the analysis's first 40-page batch, so every page is OCR'd once.
- **OCR budget:** `analysis.limits.ocr_budget` caps OCR for the whole run. The metadata stage may use at most `metadata.ocr_budget` of it, and the analysis gets what is left. Reused pages cost nothing. Each report's `ocr_budget` is its stage's effective allowance. In the C API and CLI, the single `ocr_budget` / `--ocr-budget` is that run-wide cap.
- **Progress** reports stage `"metadata"` first, then the analysis stages. `pages_acquired` counts pages of the current stage, so it restarts when the analysis begins.
- **Cancellation** applies to both stages. The CLI exits with 4 if either stage was cancelled; completed outputs are still written, but no PDF is.
- **C API:** `pdfb_analyze_book(…, progress, on_metadata, user_data, &report, &plan, &metadata)`. `*out_metadata_json` is set whenever the metadata stage completed, **even when the call then returns an analysis error**. Free it in both cases.

### Text
```cpp
auto text = pdfbookmark::extract_text(pdf, std::vector<PageIndex>{0, 1, 2}, TextOptions{});
```
This returns positioned text regions per page, with the source (PDF text or OCR) and quality assessment.

### JSON
`text_report_json`, `analysis_report_json`, `metadata_report_json` and `plan_to_json` produce schema-versioned JSON (`schema_version: 1`, `page_index_base: 0`), the same formats the CLI writes. Every member and enum value is listed in `JSON_FORMATS.md`.

## 4. C API

`#include <pdfbookmark/pdfbookmark.h>`. The header itself documents every function. The design:
- **Status codes.** Every operation returns a `pdfb_status`: `PDFB_OK` (0) or an error such as `PDFB_OUTPUT_EXISTS`. The numbers never change. `pdfb_status_name()` gives a stable name, and `pdfb_last_error()` gives the message for the calling thread.
- **Strings** are UTF-8, including file paths on Windows.
- **Options** are a JSON object text, or `NULL` for the defaults. Unknown keys are rejected, so a typo is an error, not a silently ignored option.
- **Results** are JSON text returned through `char** out` parameters, in the same formats the CLI writes (reference: `JSON_FORMATS.md`). Free each one with `pdfb_free()`. On failure, outputs are `NULL`.
- **Cancellation:** create a `pdfb_cancel_token` and call `pdfb_cancel_token_cancel()` from any thread. **Progress:** an optional callback, `(user_data, stage, done, total)`.

| Function | Does | Options (JSON keys) |
| --- | --- | --- |
| `pdfb_extract_text` | Positioned text of pages | reading options, `pages` (zero-based array; default all) |
| `pdfb_analyze` | Find the TOC and build a plan; also returns the ready plan's JSON, or `NULL` | reading options, `allow_partial`, `flat_outline`, `titles` (`"printed"`/`"chapter"`), `candidate`, `max_search_pages`, `max_evidence_pages` |
| `pdfb_apply` | Write a NEW PDF with a plan's bookmarks | `replace_existing_output` |
| `pdfb_extract_metadata` | Title, contributors, edition, years | reading options, `max_pages` |
| `pdfb_analyze_book` | Both of the above in one run; pages are read and OCR'd once. Outputs: report, plan, metadata | the `pdfb_analyze` options plus `max_pages` |
| `pdfb_validate_plan` | Structural check of an edited plan | none |
| `pdfb_read_pdf_identity` | SHA-256 and page count | none |
| `pdfb_find_models` | The OCR models found by the default search | none |
| `pdfb_version`, `pdfb_c_api_version`, `pdfb_status_name`, `pdfb_last_error`, `pdfb_free`, `pdfb_cancel_token_*` | Library services | none |

**Reading options:**
- `mode`: `"auto"`, `"embedded"` or `"ocr"`;
- `models`: a folder path, or `null` for no OCR (default: `pdfb_find_models`);
- `dpi`: 50 to 1200;
- `ocr_budget`;
- `ocr_threads`: 0 to 64 (0 = automatic).

These match the CLI flags.

```c
char *report = NULL, *plan = NULL, *result = NULL;
if (pdfb_analyze(u8"C:/Books/Book.pdf", "{\"allow_partial\": true}", NULL, NULL, NULL,
                 &report, &plan) != PDFB_OK) {
    fprintf(stderr, "%s
", pdfb_last_error());
} else if (plan) {  /* NULL: not ready; see "plan"."blockers" in report */
    pdfb_apply(u8"C:/Books/Book.pdf", u8"C:/Books/Book (bookmarked).pdf", plan, NULL, NULL, &result);
}
pdfb_free(report); pdfb_free(plan); pdfb_free(result);
```

A book without a usable table of contents is not an error. `pdfb_analyze` returns `PDFB_OK`, and the report's `outcome` explains the result.

## 5. Python

`examples/python/pdfbookmark.py` is a single-file wrapper over the C API that uses `ctypes`. It needs no build step and no packages. Copy it next to your script:

```python
import pdfbookmark
lib = pdfbookmark.Library("C:/pdfbookmark-sdk/bin/pdfbookmark.dll")  # or PDFBOOKMARK_LIBRARY
report, plan = lib.analyze("book.pdf", allow_partial=True, titles="chapter")
if plan:
    lib.apply("book.pdf", "book (bookmarked).pdf", plan)
else:
    print(report["plan"]["blockers"])
```

- Keyword arguments are the C API options.
- Results are dicts, and failures raise `PdfBookmarkError` (with `.name`, e.g. `"output_exists"`).
- `CancelToken(lib).cancel()` works from another thread.
- `examples/python/add_bookmarks.py` is a complete command-line example, and `test_pdfbookmark.py` is the wrapper's test (run by CTest as `library_python`).

The same pattern works in other languages: declare the `pdfb_*` functions with the FFI and parse the JSON results.

## 6. Advanced API

The subsystem headers are installed and usable:
- `pdfbookmark/text` (S1, text acquisition);
- `detection` (S2), `parsing` (S3) and `mapping` (S4);
- `writer` (S5);
- `metadata` (S6);
- `engine` (the implementations behind the stable API).

They may change between minor versions; `pdfbookmark.hpp` is the stable surface. The design of each subsystem is described in `AGENTS.md` and the `docs/handoffs/S*_HANDOFF.md` files in the source repository.

## 7. Versioning

`MAJOR.MINOR.PATCH`. Within a major version, the stable APIs stay source compatible, and the package is found with `find_package(pdfbookmark <major>.<minor>)`.
- **C API:** binary compatible while `PDFB_C_API_VERSION` stays the same. A newer DLL can replace an older one without rebuilding, and new functions and option keys may be added.
- **C++ API:** rebuild C++ clients when upgrading, because its binary interface is not guaranteed between versions.
