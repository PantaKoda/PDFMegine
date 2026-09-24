# S1 Text Acquisition handoff

**Subsystem and guide:** S1 Text Acquisition, root `AGENTS.md` agent edition 2.0 (22 September 2026). Owner: this S1 implementation pass; no concurrent agent. Contract revision: `s1-acquisition-v1`. Owned paths: `subsystems/text/`, `include/pdfbookmark/text/`, `src/text/`, `tests/text/`, S1 CMake package and documentation. `text/CMakeLists.txt` remains a compatibility forwarder. Minimal Core types live in `include/pdfbookmark/core/types.hpp`; root CMake only gains the opt-in Text target. Root `ARCHITECTURE.md` records the five visible domain folders and their status.

**Public operations:** `TextAcquisition::open(path, OpenOptions)` binds an immutable PDF snapshot and returns a move-only `TextDocument`. `TextDocument::acquire(indices, options, control)` returns request-ordered, owned page evidence; `read_facts(request)` returns neutral geometry, labels and local-link destinations. Indices are zero-based; duplicate or out-of-range requests fail before work. Empty requests succeed. Session closure does not invalidate returned values.

**Acquisition behavior:** EmbeddedOnly, OcrOnly, and Auto; positioned native PDF text and existing PP-OCRv6 package output; per-source attempts, readability, coverage and outcome; page revisions and source references; bounded 300-DPI BGR rendering; page-boundary cancellation. Auto stays native on clean/sparse pages, verifies a large-image/sparse-native case, selects a substantially fuller OCR candidate when supported, and preserves native text after OCR failure. Policy rationale and limits are in `IMPLEMENTATION_DECISIONS.md`. No other subsystem consumes a backend type.

**Exact backend inspected:** OCR `include/ocr/ocr.hpp` accepts caller-owned BGR `ImageView` pixels; `ocr::Engine::run` returns ordered UTF-8 text, boxes in input-image pixel coordinates, and confidence. PDFium SDK at `<PDFium package>`: version 155.0.8057.0. OCR model resource SHA-256:

| Resource | SHA-256 |
| --- | --- |
| `models/det/inference.onnx` | `EB13B44B25BB36F89528B68720AF8A61D9CF381176107F465DB1757B65D086E1` |
| `models/rec/inference.onnx` | `9C09ABF0957F7968C7586464B7397B84AD2387A0497A351AF40E9ACC71B673BA` |
| `models/rec/charset.txt` | `B5F2BFE2BDD9448429E3E82B51C789775D9B42F2403D082B00662EB77E401C5D` |

**Build and test:** Windows x64, MSVC 14.51.36231, Visual Studio 18, Ninja, C++17. Existing root OCR golden CTest passed 30 images before S1 changes: all reported stages exactly matched except confidence maximum absolute `2.980232e-07`. S1 does not modify that backend. In an x64 Visual Studio developer shell:

```powershell
cmake -S subsystems/text -B out/build/s1-structured -G Ninja -DPDFium_DIR=<PDFium package>
cmake --build out/build/s1-structured
ctest --test-dir out/build/s1-structured --output-on-failure
cmake --install out/build/s1-structured --prefix out/install/s1-structured

cmake --build out/build/s1-debug --target pdfbookmark_text_smoke pdfbookmark_text_policy pdfbookmark_text_ocr_integration
ctest --test-dir out/build/s1-debug --output-on-failure -R '^text_'
cmake --build out/build/s1-shared --target pdfbookmark_text_smoke pdfbookmark_text_policy pdfbookmark_text_ocr_integration
ctest --test-dir out/build/s1-shared --output-on-failure -R '^text_'
```

The root builds were configured with `PDFBOOKMARK_BUILD_TEXT=ON`, `PDFBOOKMARK_TEXT_WITH_OCR=ON`, `PDFium_DIR` above and the repository's existing OCR dependency variables. `s1-debug` is static; `s1-shared` uses `BUILD_SHARED_LIBS=ON`. A separate OCR-disabled root build and the standalone `text/` build work without invoking OCR.

Final focused results after the last S1 code change: static 3/3 passed in 18.20 s, shared 3/3 passed in 18.33 s, standalone OCR-disabled 1/1 passed in 0.08 s. The OCR integration test confirmed 4 lines, identical text, confidence differences below 1e-6, and canonical-to-raster box corner errors below 0.01 pixel.

After the root-folder decision, the canonical `subsystems/text` entry configured, built, and passed `text_smoke` (1/1, 0.14 s). Its installed package built a separate consumer that ran from an unrelated directory, printing `Second page` then `Hello World`. The former `text` entry also reconfigured, built, and passed `text_smoke` (1/1, 0.12 s). The change relocates only the standalone CMake entry; public headers, implementation, fixtures, and the root OCR build remain in place.

**Independent consumer:** `tests/text/consumer` includes only `pdfbookmark/text/acquisition.hpp`, finds installed `pdfbookmarkText`, links `pdfbookmark::Text`, and acquires indices `{2,0}` without Engine. The standalone OCR-disabled package was installed and this separate project built against it with only PDFium as an external package; run from an unrelated directory printed `Second page` then `Hello World`. OCR-enabled installed consumer also ran from that unrelated working directory. Static S1 consumers need the package's backend libraries at link time, but no backend header in consumer source. No cross-OS or single-executable distribution claim is made.

Exact separate-consumer commands (after the package installs above, in an x64 Visual Studio developer shell):

```powershell
cmake -S tests/text/consumer -B out/text-consumer-structured -G Ninja -DCMAKE_PREFIX_PATH=<repo>/out/install/s1-structured -DPDFium_DIR=<PDFium package>
cmake --build out/text-consumer-structured
Copy-Item out/install/s1-structured/bin/pdfium.dll out/text-consumer-structured/pdfium.dll
cd <an unrelated directory>
<repo>/out/text-consumer-structured/text_consumer.exe <repo>/tests/text/fixtures/basic.pdf
```

**Fixture evidence:** `tests/text/fixtures/README.md` lists exact generated PDF digests and zero-based expected pages. `text_smoke` covers session/facts/native geometry/Unicode/lifetime; `text_policy` uses a fake at the real OCR seam for Auto and failure choices; `text_ocr_integration` checks same-bitmap direct-package parity. These values can be handed to a future contract-only consumer without linking PDFium or OCR.

**Known limits:** Native quads are axis-aligned bounds of PDFium runs, not precise rotated-glyph quads; reading order is estimated and two-column behavior has not been independently calibrated. The `/UserUnit` fallback supports explicit uncompressed page trees; compressed object streams produce unknown physical scale and a degraded page. Coverage only detects the documented large-image/narrow-text pattern and does not certify absence of omissions. No page auto-downscaling, native/OCR merge, printed-page inference, annotation/form capture, or outline inspection. A corrupt existing OCR model is detected at first attempt rather than fully validated during `open()`.

**Adjacent contract need:** No S2/S3/S4 implementation change is required. They may consume `PageAcquisition` and `PdfFactsResult` values without backend linkage. Source references remain valid only for the corresponding page revision. Engine must own page-search scheduling and cross-call OCR budgets. S5 must check `InputIdentity` before committing an output. These are future integration actions, outside this S1 assignment.
