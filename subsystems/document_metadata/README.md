# S6 — Document Metadata Extraction

**Status: implemented (owner-directed extension), policy `s6-document-metadata-v2`.** Given positioned text from S1 for selected pages, it identifies the title (and subtitle), contributors (author, editor, translator or organization), edition, publication year and copyright year, with evidence, alternatives and a status of Resolved, Ambiguous or NotFoundInSearch for each. It also lists every ISBN printed on those pages (check digit validated, with the printed format label). It is pure computation with no PDF, OCR or page fetching.

```powershell
cmake -S subsystems/document_metadata -B out/build/s6-metadata -G Ninja
cmake --build out/build/s6-metadata
ctest --test-dir out/build/s6-metadata --output-on-failure
```

The root build exposes it with `PDFBOOKMARK_BUILD_METADATA=ON`, which also adds Engine's `extract_metadata()` and the CLI `metadata` command. See `docs/handoffs/S6_HANDOFF.md`.
