# PDF Bookmark

Adds bookmarks (the PDF outline) to PDF books from their **printed table of contents**. It reads the contents pages, including scanned pages through its built-in OCR, works out which physical page each entry starts on, and writes a **new** PDF with the bookmarks. The original file is never modified. It can also extract a book's title, authors, edition and years.

It is a C++ library with a command-line tool as one of its clients:

| You want to… | Use | Read |
| --- | --- | --- |
| Bookmark PDFs (no programming) | `pdfbookmark` command or drag-and-drop scripts from the release ZIP | `packaging/README.txt` |
| Use it from C++ (e.g. a Qt app) | `<pdfbookmark/pdfbookmark.hpp>` from the SDK | `docs/API.md` |
| Use it from C, Python, C#, Rust… | `<pdfbookmark/pdfbookmark.h>` (C API) | `docs/API.md` §4-5 |
| Build, test or package it | CMake presets | `docs/BUILDING.md` |
| Understand or change the design | Five subsystems plus Engine | `docs/ARCHITECTURE.md`, `AGENTS.md` |

```powershell
pdfbookmark add "My Book.pdf"                 # writes "My Book (bookmarked).pdf"
pdfbookmark add "My Book.pdf" --allow-partial --titles chapter
pdfbookmark help
```

## Quick start for developers (Windows x64)

From a Visual Studio 2026 x64 developer shell, logged in with the GitHub CLI (`gh auth login`):

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The first configure downloads the pinned dependencies (PDFium, ONNX Runtime, OpenCV, OCR models) and builds qpdf with vcpkg. Details are in `docs/BUILDING.md`.

## Repository map

```text
include/pdfbookmark/   public headers: pdfbookmark.hpp (C++ API), pdfbookmark.h (C API),
                       and one folder per subsystem
src/                   implementation, one folder per subsystem (text, detection, parsing,
                       mapping, writer, metadata, engine)
apps/cli/              the pdfbookmark command-line tool
library/               the unified shared library and its SDK package
subsystems/            standalone build entry points, one per subsystem
tests/                 tests and generated fixture PDFs, one folder per subsystem
examples/              SDK clients: basic (C++), python (C API via ctypes)
packaging/             end-user scripts and guide shipped in the release ZIP
cmake/                 pinned dependencies, package configuration, helpers
include/ocr/, src/*.cpp, tools/, corpus/, third_party/
                       the OCR engine (PP-OCRv6 on ONNX Runtime), used by text acquisition
docs/                  all documentation (below)
```

## Documentation

| Document | Contents |
| --- | --- |
| `docs/API.md` | Library guide: C++ API, C API, Python, SDK use, conventions, versioning |
| `docs/BUILDING.md` | Requirements, presets, dependencies, tests, packages |
| `docs/ARCHITECTURE.md` | Subsystems, ownership and repository layout |
| `docs/IMPLEMENTATION_DECISIONS.md` | Decision log: every change with its reasons and assumptions |
| `docs/IMPLEMENTATION_PROGRESS.md` | Status of each implementation step and what was verified |
| `docs/handoffs/` | Detailed contract and verification record per subsystem (S1-S6, Engine) |
| `docs/ocr/` | The OCR engine's own README and decisions |
| `AGENTS.md` | The implementation specification (agent edition 2.1) |
| `packaging/README.txt` | User guide shipped with the program |

## Status

- Windows x64 is verified.
- Linux and macOS are not yet supported (planned: CI builds on all three).
- The repository is private and has no licence file yet (`docs/IMPLEMENTATION_DECISIONS.md` E-26).
- Third-party licences are listed in `docs/BUILDING.md` and shipped in each package's `licenses/` folder.
