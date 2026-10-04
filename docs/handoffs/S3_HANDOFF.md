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

## Revision v3 (4 October 2026, issue #13)

Policy `s3-toc-parsing-v3` (decision E-40):
- **S3-10 Text-layer number repairs.** A page number in its own region at the end of a row with a stray space or the letter I/l for 1 ("21 1", "I I9", "33 I") is read as one number of at most 3 digits (4 without a space). The literal stays as printed, and the reference's `reasons` say how it was read. A leading section number with the same artifacts ("2. I", "8. I3", "I 8.6", and "8.1 1" when it is its own region) is corrected in the title, with an entry diagnostic. Inside a title a letter I/l must be among the repairs, so "3. 10 Things to Know" stays as printed. Such section-number regions are not counted as references when the parser looks for a second column.
- **S3-11 Chapter rows keep their page number.** A heading row ("Chapter 2. Cold Equation of State 17") is split into title and reference when at least two words remain and they are not only a label and its number ("Chapter 3").
- **S3-12 Hanging-indent wraps.** A row without a reference that starts with a label ("Chapter 1.", "4.6", "D.2", "C") continues on the next row when that row is indented, has a reference, does not start its own entry, and either its first word would not have fit on the full first line or the first line ends open (a comma, colon, hyphen or a word such as "and", "of").
- **S3-13 Running heads.** "xiv Contents" / "Contents xv" in the top 12% of a TOC page is ignored with a diagnostic.
- **S3-14 Labels.** "Appendix" is a heading label like "Chapter", "Part" and "Section", and numbered sections nest under "Chapter 8." or "8." as under "8".
- **Tests:** `parsing_pure` adds a noisy reprint page covering every rule above and a short heading that must not swallow its indented first entry.
