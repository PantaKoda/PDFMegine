# pdfbookmark SDK: brief for coding agents

<!-- Installed with the SDK as share/doc/pdfbookmark/AGENTS.md. Reference it from the client
     project's own AGENTS.md / CLAUDE.md, or copy it there. Paths below are relative to it. -->

You are writing an application that **uses** the pdfbookmark library. You do not modify it. The library reads a PDF book's printed table of contents, maps each entry to a physical page, and writes a **new** PDF with bookmarks. It also extracts title, authors, edition and years.

## Read, in this order

1. **This file**, for the rules below.
2. `API.md` (next to this file): the guide to the C++ API, C API, Python, conventions and SDK layout.
3. The headers they describe, which are authoritative for signatures: `<sdk>/include/pdfbookmark/pdfbookmark.hpp` (C++) or `<sdk>/include/pdfbookmark/pdfbookmark.h` (C and FFI).
4. `JSON_FORMATS.md`, only when you use the C API, another language, or read and write plan or report files.
5. `examples/`: `basic` (C++ console), `qt-quick` (Qt 6 QML app with a worker thread, progress, cancel and plan editing), `python` (ctypes over the C API).

Do not look for other documentation. The library's internal design files are not part of the SDK and are not needed to use it.

## CMake (the only supported integration)

```cmake
find_package(pdfbookmark 0.1 CONFIG REQUIRED)            # CMAKE_PREFIX_PATH must contain the SDK folder
target_link_libraries(<app> PRIVATE pdfbookmark::pdfbookmark)
pdfbookmark_deploy_runtime(<app>)                         # DLLs + OCR models next to the executable
```

- Don't link PDFium, qpdf, ONNX Runtime or OpenCV, and don't include their headers. They are private to the library.
- `pdfbookmark_deploy_runtime` is required. Without it the program won't start (missing DLLs), and OCR finds no models.

## Rules that are easy to get wrong

| Rule | Why |
| --- | --- |
| **Windows x64, MSVC only** for the C++ API: the Qt "MSVC 64-bit" kit, not MinGW. Other compilers and languages must use the C API. | The C++ API passes `std::string`/`std::vector` across the DLL. |
| Let CMake choose Debug/Release. Never link `pdfbookmarkd.lib` or `pdfbookmark.lib` by hand. | Debug apps must use the Debug DLL. The imported target handles it. |
| Build paths from wide strings: `std::filesystem::path(qstring.toStdWString())`, never `toStdString()` or `toLocal8Bit()`. | Non-ASCII file names would break. |
| Call `analyze`, `apply`, `extract_text` and `extract_metadata` on a **worker thread** (`QThreadPool`, `QtConcurrent`, `std::thread`), never on the GUI thread. | They take seconds to minutes, more with OCR on scanned books. |
| Progress callbacks run on that worker thread. Send them to the GUI with `QMetaObject::invokeMethod(obj, …, Qt::QueuedConnection)` or a queued signal. | Qt objects belong to their thread. |
| Need both the contents and the metadata of a book? Call **`analyze_book()`** (C API `pdfb_analyze_book`), not `analyze()` and then `extract_metadata()`. | It reads and OCRs each page once; two calls OCR the front pages twice. |
| Run one library operation at a time. | The PDF engine is shared process-wide and serializes calls anyway. |
| Cancel with `std::atomic_bool` and `pdfbookmark::RunControl{&flag}`. Keep the flag alive until the call returns. | Cancellation is cooperative and checked between pages. |
| Check `Result<T>` with `if (!r)`, then `r.error().message`. | Errors are values, not exceptions. |
| `AnalysisOutcome` other than `PlanReady` is **not an error**. Show `report.plan.blockers` (plain-language reasons). | A book without a usable TOC is a normal result. |
| Page indices are **zero based**. Show `index + 1` to users. | Same convention everywhere, including JSON. |
| Only write when `report.plan.ready`, or after the user edits a plan and `validate_plan(plan).valid` is true. | A draft plan is for review only. |
| Never pass the input path as the output. Choose a new file (e.g. `"<name> (bookmarked).pdf"`). An existing output is refused unless `ApplyOptions{true}`. | The library never modifies the input and refuses to overwrite it. |
| Don't edit `plan.input` (the SHA-256 and page count). Titles, parents, destinations and node order may be edited. | The plan is bound to one exact file. `apply` refuses others. |
| Set `options.models = pdfbookmark::find_models();`. `nullopt` is fine: scanned pages are then reported unreadable. | OCR models are found next to the executable (`models/`). |
| Expect an observed peak of about **2.3 GB** of memory and about **7 s per scanned page** for OCR at the default 300 DPI (about 1.1 GB and 3 s at `raster.dpi = 200`). These are measured on US Letter test pages, not guaranteed limits: larger pages need more. Set `options.ocr_threads` (0 = automatic, at most 8) lower if the UI needs CPU at the same time. Run one OCR job at a time. | OCR runs a neural network on each full page image. See `API.md` §2, "OCR cost". |
| Existing bookmarks in the PDF are ignored and replaced in the new copy. Don't try to merge them. | This is by design. |

## Minimal C++ flow

```cpp
#include <pdfbookmark/pdfbookmark.hpp>

pdfbookmark::AnalysisOptions options;
options.models = pdfbookmark::find_models();
options.plan.allow_partial = false;                     // true: leave out entries that can't be placed
auto report = pdfbookmark::analyze(pdf, options, pdfbookmark::RunControl{&cancel}, on_progress);
if (!report) { /* report.error().message */ }
else if (report.value().plan.ready) {
    pdfbookmark::BookmarkPlan plan = *report.value().plan.plan;   // optionally let the user edit titles
    if (pdfbookmark::validate_plan(plan).valid)
        auto written = pdfbookmark::apply(pdf, output, plan);     // written.value().verification.outline_items
} else { /* show report.value().plan.blockers */ }
```

## Shipping the application

The executable folder needs: your app, `windeployqt` output (for Qt apps), and everything `pdfbookmark_deploy_runtime` copied (`pdfbookmark.dll`, its dependency DLLs, `models/`). The library's licence notices are in the SDK's `share/doc/pdfbookmark/licenses/`; include them.
