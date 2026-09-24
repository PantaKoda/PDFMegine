# S3 TOC Parsing handoff

**Subsystem and scope:** S3 under root `AGENTS.md` agent edition 2.0. Policy `s3-toc-parsing-v1`. Owned paths: `subsystems/toc_parsing/`, `include/pdfbookmark/parsing/`, `src/parsing/`, `tests/parsing/`, and its CMake package config. Root CMake adds only an opt-in `PDFBOOKMARK_BUILD_PARSING` target. No S1, S2, OCR, PDFium, or mapping implementation was changed.

**Public boundary:** `pdfbookmark::parsing::parse(const detection::TocCandidate&, const std::vector<text::PageAcquisition>&, const ParsingOptions&)` is pure. A caller can construct a candidate manually, so S2's runtime is not required. The returned `ParsedToc` contains ordered `TocEntry` values, exact printed-reference literals and optional syntax, `Root`/`KnownParent`/`Unknown` hierarchy with reasons, source references, unresolved fragments, missing pages, candidate boundaries, policy identity, and diagnostics. Revisions and candidate evidence are validated. No physical destination is inferred.

**Interpretation:** S3 assembles positioned regions into rows, detects two reference columns, merges supported wrapped titles, separates titles from trailing printed references, and infers hierarchy conservatively from indentation. The exact thresholds, supported reference forms, source-offset rule, and limitations are in `IMPLEMENTATION_DECISIONS.md` (S3-01 through S3-06). A `Complete` parse only means this candidate's supplied entry evidence was parsed without identified uncertainty; S4 must independently map every printed reference.

**Verification:** Windows x64, MSVC 14.51.36231, Visual Studio 18, Ninja, C++17. Standalone static, standalone shared, and root opt-in `parsing_pure` each passed 1/1. The installed pure consumer ran from an unrelated directory and printed `manual-toc entries=3` with only the S3 package. The installed S1 + S2 + S3 acquired consumer ran from the same unrelated working directory and produced 8/8 expected entries from the 3-page generated PDF, each with expected page/revision provenance and known-root hierarchy; `Complete`, zero unresolved fragments. Fixture digest and per-entry expectations are in `tests/parsing/fixtures/README.md`.

From an x64 Visual Studio developer shell:

```powershell
cmake -S subsystems/toc_parsing -B out/build/s3-parsing -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/s3-parsing
ctest --test-dir out/build/s3-parsing --output-on-failure
cmake --install out/build/s3-parsing --prefix out/install/s3-parsing
cmake -S tests/parsing/consumer -B out/parsing-consumer -G Ninja -DCMAKE_PREFIX_PATH=<repo>/out/install/s3-parsing
cmake --build out/parsing-consumer
cmake -S tests/parsing/acquired -B out/parsing-acquired -G Ninja -DpdfbookmarkText_DIR=<repo>/out/install/s1-structured/lib/cmake/pdfbookmarkText -DpdfbookmarkTocDetection_DIR=<repo>/out/install/s2-detection/lib/cmake/pdfbookmarkTocDetection -DpdfbookmarkTocParsing_DIR=<repo>/out/install/s3-parsing/lib/cmake/pdfbookmarkTocParsing -DPDFium_DIR=<PDFium package>
cmake --build out/parsing-acquired
cd <an unrelated directory>
<repo>/out/parsing-consumer/parsing_consumer.exe
<repo>/out/parsing-acquired/parsing_acquired.exe <repo>/tests/detection/fixtures/acquired_toc.pdf
```

Set `BUILD_SHARED_LIBS=ON` for S3's shared build. In the existing root build, configure with `PDFBOOKMARK_BUILD_PARSING=ON`, build `pdfbookmarkTocParsing` and `pdfbookmark_parsing_tests`, then run CTest `parsing_pure`.

**Handoff to S4 and Engine:** Use entry `id` and `sources` to preserve evidence. Treat `PrintedReference.literal` as the document's printed text; its parsed `ordinal` is syntax, never a physical index. S4 must establish numbering-section and destination evidence separately. Engine should retain `unparsed`, `missing_pages`, boundary states, and hierarchy uncertainty in its analysis and decide whether to request more S1 pages or apply an explicit partial-plan policy. S3 performs neither action.
