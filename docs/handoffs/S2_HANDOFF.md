# S2 TOC Detection handoff

**Subsystem and guide:** S2, root `AGENTS.md` agent edition 2.0 (22 September 2026). Owner: this S2 implementation pass; no delegated agent. Contract/policy revision: `s2-toc-detection-v1`. Owned paths: `subsystems/toc_detection/`, `include/pdfbookmark/detection/`, `src/detection/`, `tests/detection/`, and the S2 package config. Root CMake adds only `PDFBOOKMARK_BUILD_DETECTION` as an opt-in target.

**Public operation:** `detect(const std::vector<text::PageAcquisition>&, DetectionOptions)` is pure and returns `Result<DetectionResult>`. `TocCandidate` carries an analysis-local ID, ordered explicit physical pages with S1 revision and row source references, rank score and reasons, supplied failed-page interruptions, limitations, and separate start/end continuation states. `DetectionResult.pages` reports every supplied physical index as Candidate, Rejected or Skipped; input request order does not affect candidate ordering. No PDF, OCR or S1 runtime linkage is needed by the pure detector.

**Ranking and boundaries:** Default thresholds and every numeric heuristic are in `IMPLEMENTATION_DECISIONS.md` (S2-01 through S2-07). Scores rank candidates under this policy and are not probabilities. Detection uses positioned title/reference patterns, right-edge columns, dot leaders and optional headings. It recognizes two columns without trusting flattened reading order. Only adjacent candidate pages, or pages separated by one *supplied but unassessable* page, may group. An unsupplied page remains a gap between distinct candidates. Batch edges stay `MayContinue`; failed adjacent evidence yields `Unknown`.

**Verification:** Windows x64, MSVC 14.51.36231, Visual Studio 18, Ninja, C++17. Static and shared standalone `detection_pure` each passed 1/1; root opt-in build passed 1/1. Static installed pure consumer ran from an unrelated directory, outputting `toc-p8-r3 pages=1 score=11`, with no backend package. A second installed consumer linked S1 + S2, acquired the deterministic PDF, and reported physical page scores `0:14.6`, `1:13`, `2:2`, candidate `toc-p0-r2` over `{0,1}` with score `14.3`. Both consumers ran from a working directory outside the repository. The PDF digest and exact expected structure are in `tests/detection/fixtures/README.md`.

From an x64 Visual Studio developer shell, the standalone S2 commands are:

```powershell
cmake -S subsystems/toc_detection -B out/build/s2-detection -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/s2-detection
ctest --test-dir out/build/s2-detection --output-on-failure
cmake --install out/build/s2-detection --prefix out/install/s2-detection
cmake -S tests/detection/consumer -B out/detection-consumer -G Ninja -DCMAKE_PREFIX_PATH=<repo>/out/install/s2-detection
cmake --build out/detection-consumer
python tests/detection/fixtures/make_toc.py
cmake -S tests/detection/acquired -B out/detection-acquired -G Ninja -DpdfbookmarkText_DIR=<repo>/out/install/s1-structured/lib/cmake/pdfbookmarkText -DpdfbookmarkTocDetection_DIR=<repo>/out/install/s2-detection/lib/cmake/pdfbookmarkTocDetection -DPDFium_DIR=<PDFium package>
cmake --build out/detection-acquired
cd <an unrelated directory>
<repo>/out/detection-consumer/detection_consumer.exe
<repo>/out/detection-acquired/detection_acquired.exe <repo>/tests/detection/fixtures/acquired_toc.pdf
```

Set `BUILD_SHARED_LIBS=ON` for the shared build. Root opt-in build: configure the existing root build with `PDFBOOKMARK_BUILD_DETECTION=ON`, build `pdfbookmarkTocDetection` and `pdfbookmark_detection_tests`, then run CTest `detection_pure`. The acquired-PDF consumer finds installed `pdfbookmarkText` and `pdfbookmarkTocDetection`; it requires PDFium only through S1. The installed S1 package used here came from the standalone OCR-disabled S1 build.

**Limits:** The suffix recognizer covers decimal, short prefixed decimal, and ASCII Roman patterns; it does not understand all printed reference forms. Mixed columns and multiline native runs use approximate geometry. An unlabeled index or figure list with TOC-like alignment can rank as a candidate. A true TOC with few entries or weak right alignment can be missed. The generated PDF verifies real S1 handoff but is not a broad book-layout calibration corpus. Scores and candidate IDs must not be treated as destination evidence.

**Handoff:** S3 should validate `candidate.pages[*].revision` against the supplied `PageContent` before parsing and use candidate row references as supporting evidence, while retaining unsupported fragments. Engine must supply any desired neighboring pages and choose among separate candidates; S2 never acquires them. No S1, S3, S4, S5 or Engine implementation change is required by this S2 library.

## Revision v2 (23 September 2026, agent edition 2.1)

Policy `s2-toc-detection-v2` adds decision S2-08: wide-gap title and reference pairing when the row between them is empty and the title is not already a complete row, and density counted per visual line.
- **Reason:** Engine A9 found that TOC pages with right-aligned numbers after short leaders were rejected, which silently truncated plans.
- **New pure tests:** the wide-gap positive case, bare right-column numbers next to complete left rows (no pairing), and a left row's own number blocking a cross-column pairing.
- **Results:** standalone static and shared `detection_pure` passed 1/1 each, and the root suite passed 15/15, including the Engine regression `boundary_short_leaders.pdf` (both TOC pages, full 8-entry plan).
- The installed S1+S2+S3+S4 acquired consumer, rebuilt against the reinstalled S2 package, still maps 3/3 and is outline-independent (candidate score 12.5, unchanged).
