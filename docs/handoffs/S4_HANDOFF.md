# S4 Page Mapping handoff

**Subsystem and scope:** S4 under root `AGENTS.md` agent edition 2.1; policy `s4-page-mapping-v2`. Owned paths are `subsystems/page_mapping/`, `include/pdfbookmark/mapping/`, `src/mapping/`, `tests/mapping/`, and the S4 package config. The root build adds only opt-in `PDFBOOKMARK_BUILD_MAPPING`. S1–S3 implementations and the OCR package were not changed.

**Public boundary:** `pdfbookmark::mapping::map(entries, evidence, overrides, options)` is pure and usable without S1–S3 runtime libraries. `DocumentEvidence` supplies the input page count/identity, S1 pages/facts, explicit numbering sections, optional entry-section and local-link associations, and limitations. `EntryMapping` retains an in-range destination only for `Resolved`; `Ambiguous` keeps alternatives and `Unresolved` explains missing evidence. `EvidenceRequest` is a bounded value for Engine, never a callback or acquisition side effect.

**Supported methods:** Explicit per-entry destination and section offset overrides; fact-verified entry-associated local links and fully observed viewer labels in explicitly trusted sections, each only when the target page's acquired content corroborates the entry (edition 2.1: labels/links are untrusted hints; otherwise they remain unverified alternatives with a bounded page request); an inferred section offset from two independent agreeing physical-page anchors with target confirmation; and an exact heading in a fully supplied explicit section. Automatic resolution never equates a printed ordinal with a physical index. The precise observation bands, syntax, override precedence, section rules, and limitations are in `IMPLEMENTATION_DECISIONS.md` (S4-01 through S4-08).

**Verification:** Windows x64, MSVC 14.51.36231, Visual Studio 18, Ninja, C++17. Standalone static, standalone shared, and root opt-in `mapping_pure` each passed 1/1. An installed backend-free consumer linked only the S4 package and ran from an unrelated directory, printing `transport pdf_index=23`. A separate installed S1 + S2 + S3 + S4 consumer ran from the same unrelated directory on `acquired_mapping.pdf`, producing `Alpha→2`, `Beta→3`, `Gamma→4` (3/3) with inferred offset 1, and on its paired `acquired_mapping_outlined.pdf` (same pages plus a misleading three-item outline, confirmed present through PDFium) produced identical normalized facts, candidate, entries and mappings: `outline-independent: identical S1-S4 semantics with 3 misleading bookmarks present`. S4 has no outline input and never reads existing bookmarks (S4-08). The PDF's SHA-256 and exact expected evidence are in `tests/mapping/fixtures/README.md`.

From an x64 Visual Studio developer shell:

```powershell
cmake -S subsystems/page_mapping -B out/build/s4-mapping -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/s4-mapping
ctest --test-dir out/build/s4-mapping --output-on-failure
cmake --install out/build/s4-mapping --prefix out/install/s4-mapping
cmake -S tests/mapping/consumer -B out/mapping-consumer -G Ninja -DCMAKE_PREFIX_PATH=<repo>/out/install/s4-mapping
cmake --build out/mapping-consumer
python tests/mapping/fixtures/make_mapping.py
cmake -S tests/mapping/acquired -B out/mapping-acquired -G Ninja -DpdfbookmarkText_DIR=<repo>/out/install/s1-structured/lib/cmake/pdfbookmarkText -DpdfbookmarkTocDetection_DIR=<repo>/out/install/s2-detection/lib/cmake/pdfbookmarkTocDetection -DpdfbookmarkTocParsing_DIR=<repo>/out/install/s3-parsing/lib/cmake/pdfbookmarkTocParsing -DpdfbookmarkMapping_DIR=<repo>/out/install/s4-mapping/lib/cmake/pdfbookmarkMapping -DPDFium_DIR=<PDFium package>
cmake --build out/mapping-acquired
cd <an unrelated directory>
<repo>/out/mapping-consumer/mapping_consumer.exe
<repo>/out/mapping-acquired/mapping_acquired.exe <repo>/tests/mapping/fixtures/acquired_mapping.pdf <repo>/tests/mapping/fixtures/acquired_mapping_outlined.pdf
```

Set `BUILD_SHARED_LIBS=ON` for the shared S4 build. The existing root build uses `PDFBOOKMARK_BUILD_MAPPING=ON`, target `pdfbookmarkMapping`/`pdfbookmark_mapping_tests`, and CTest `mapping_pure`.

**Handoff to Engine/S5:** Engine must never add outline-derived evidence to `DocumentEvidence`. Engine should pass S3 entry IDs and sources unchanged, supply numbering sections only with documented provenance, retain every alternative/unresolved reason, and decide which bounded S1 page requests to satisfy. Engine applies any explicit omission or manual-choice policy when assembling a bookmark plan. S5 receives only explicit physical destinations; it must not reinterpret S4's printed references or infer a missing page.

## Revision v4 (4 October 2026, issue #13)

Policy `s4-page-mapping-v4` (decision E-40) adds **S4-12 approximate target confirmation**. When two agreeing anchors give a target but no exact heading or printed number confirms it, the target's own text may confirm it if a heading there matches the entry up to text-layer errors: case and punctuation, a soft hyphen, I/l for 1 and O for 0 in numbers and one-letter words ("Appendix I" / "Appendix 1"), a chapter/appendix/part/section label the page omits, and at most one wrong character in 20 ("Protolype"). Keys shorter than 12 characters never match loosely, and a page holding TOC entries never confirms (its row names the entry but says nothing about where it starts). The rule is used only for this confirmation: exact headings remain the only heading anchors, and labels and local links still need exact confirmation. The mapping reason names the matched heading, and its region is among the supporting sources. `mapping_pure` covers the three real cases and a short-title counterexample.
