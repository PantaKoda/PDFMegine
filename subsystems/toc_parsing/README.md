# S3 — TOC Parsing

**Status: implemented, policy `s3-toc-parsing-v1`.** The public operation is `pdfbookmark::parsing::parse(candidate, supplied_pages, options)` in `include/pdfbookmark/parsing/parsing.hpp`. It accepts a chosen S2-compatible candidate and caller-owned S1 values. It never opens a document or resolves physical destinations. Entries retain titles, literal printed references, source revisions/regions, order, and explicit hierarchy evidence; unresolved fragments and boundaries remain visible.

Implementation and tests live in `src/parsing/` and `tests/parsing/`. This folder is the independent CMake entry point:

```powershell
cmake -S subsystems/toc_parsing -B out/build/s3-parsing -G Ninja
cmake --build out/build/s3-parsing
ctest --test-dir out/build/s3-parsing --output-on-failure
```

The target is `pdfbookmarkTocParsing` / `pdfbookmark::TocParsing`. The root build exposes it with `PDFBOOKMARK_BUILD_PARSING=ON`. See `docs/handoffs/S3_HANDOFF.md` for the fixture, installed consumers, supported syntax, and limits.
