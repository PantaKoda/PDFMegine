# Architecture

The full specification is `AGENTS.md` (agent edition 2.1). This page is the map: what the parts are, where each one lives, and how they depend on each other.

## Parts

| Part | Question it answers | Uses |
| --- | --- | --- |
| **S1 Text Acquisition** | What positioned text can we get from these PDF pages? (native text, or OCR for scanned pages) | PDFium and the OCR engine (private) |
| **S2 TOC Detection** | Which pages look like a table of contents? | S1's value types only |
| **S3 TOC Parsing** | What entries, printed page references and nesting does the TOC contain? | S1 and S2 value types |
| **S4 Page Mapping** | Which physical PDF page does each printed reference point to? | S1 and S3 value types |
| **S5 Bookmark Writing** | Is this bookmark plan valid, and can it be written to a new PDF? | qpdf (private) |
| **S6 Document Metadata** | Title, contributors, edition, publication and copyright year (an owner-directed extension) | S1 value types |
| **Engine** | Orchestration: which pages to read, in what order, under which budgets; builds the plan; JSON formats | the public APIs of S1-S6 |
| **Library** | One shared `pdfbookmark.dll` with the stable C++ API (`pdfbookmark.hpp`) and C API (`pdfbookmark.h`) | Engine |
| **CLI** | The `pdfbookmark` command: arguments, output, exit codes | the library |

Data flows S1 → S2 → S3 → S4 → Engine → S5. Only Engine calls the subsystems. S2-S4 are pure computations on values: they never open a PDF, and they return requests for more evidence instead of fetching it. Existing PDF bookmarks are never used as evidence, and the input PDF is never modified.

## Where things live

**Decision (23 September 2026):** `subsystems/` makes each subsystem visible at the root and holds its standalone build entry. Headers, implementation and tests stay in the `include/pdfbookmark/<area>/`, `src/<area>/` and `tests/<area>/` layout, so working code was not moved just to copy the specification's example tree. A subsystem folder never contains a second copy of its implementation.

```text
subsystems/
  text/                S1 Text Acquisition (standalone CMake entry, OCR-disabled)
  toc_detection/       S2 TOC Detection
  toc_parsing/         S3 TOC Parsing
  page_mapping/        S4 Page Mapping
  bookmark_writing/    S5 Bookmark Writing
  document_metadata/   S6 Document Metadata Extraction
include/pdfbookmark/   public contracts, one folder per owner, plus the stable
                       pdfbookmark.hpp (C++) and pdfbookmark.h (C) APIs
src/                   implementations, one folder per owner (engine/ includes the C API)
tests/                 tests, one folder per owner (library/ tests the public APIs)
library/               unified shared library target and SDK package
apps/cli/              command-line client
examples/              SDK clients (C++, Qt Quick, Python)
text/                  compatibility entry that forwards to subsystems/text
include/ocr/, src/*.cpp, tools/, corpus/, third_party/, models/
                       the OCR engine: a dependency of S1, not S1 code
```

| Owner | Visible folder | Public contract | Implementation and tests | Target |
| --- | --- | --- | --- | --- |
| Shared Core | none | `include/pdfbookmark/core/` | header-only | none |
| S1 Text Acquisition | `subsystems/text/` | `include/pdfbookmark/text/` | `src/text/`, `tests/text/` | `pdfbookmarkText` / `pdfbookmark::Text` |
| S2 TOC Detection | `subsystems/toc_detection/` | `include/pdfbookmark/detection/` | `src/detection/`, `tests/detection/` | `pdfbookmarkTocDetection` |
| S3 TOC Parsing | `subsystems/toc_parsing/` | `include/pdfbookmark/parsing/` | `src/parsing/`, `tests/parsing/` | `pdfbookmarkTocParsing` |
| S4 Page Mapping | `subsystems/page_mapping/` | `include/pdfbookmark/mapping/` | `src/mapping/`, `tests/mapping/` | `pdfbookmarkMapping` |
| S5 Bookmark Writing | `subsystems/bookmark_writing/` | `include/pdfbookmark/writer/` | `src/writer/`, `tests/writer/` | `pdfbookmarkWriter` (qpdf) and the backend-free `pdfbookmarkWriterPlan` |
| S6 Document Metadata | `subsystems/document_metadata/` | `include/pdfbookmark/metadata/` | `src/metadata/`, `tests/metadata/` | `pdfbookmarkMetadata` |
| Engine | none | `include/pdfbookmark/engine/` | `src/engine/`, `tests/engine/` | `pdfbookmarkEngine` |
| Library | `library/` | `pdfbookmark.hpp`, `pdfbookmark.h` | `src/engine/library.cpp`, `src/engine/c_api.cpp`, `tests/library/` | `pdfbookmark` / `pdfbookmark::pdfbookmark` |
| CLI | none | none | `apps/cli/`, `tests/cli/` | `pdfbookmarkCli` (`pdfbookmark.exe`) |

## Builds

- **Root build** (`CMakePresets.json`, `docs/BUILDING.md`): everything. The root `CMakeLists.txt` is the OCR engine's build, with the PDF Bookmark parts as options (`PDFBOOKMARK_BUILD_*`). `PDFBOOKMARK_BUILD_LIBRARY=ON`, which all presets set, turns them all on.
- **Standalone subsystem builds:** `cmake -S subsystems/<name>`. Each subsystem builds, tests and installs on its own. S2-S4 need no PDF or OCR library. S1 needs only PDFium. S5 needs qpdf, via the vcpkg toolchain.

The source of truth for each type is its owner's public header. Consumers use those headers instead of making copies.
