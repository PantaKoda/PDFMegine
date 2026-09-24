# S4 — Page Mapping

**Status: implemented, policy `s4-page-mapping-v2` (agent edition 2.1).** The public operation in `include/pdfbookmark/mapping/mapping.hpp` accepts S3 entries plus caller-supplied S1 page values, facts, numbering sections, and overrides. It returns resolved, ambiguous, or unresolved entries with evidence, reasons, alternatives, and bounded page requests. It neither opens a PDF nor chooses which entries to omit from a bookmark plan.

The implementation is in `src/mapping/`, with pure fixtures and installed consumers in `tests/mapping/`. Build the independent library with:

```powershell
cmake -S subsystems/page_mapping -B out/build/s4-mapping -G Ninja
cmake --build out/build/s4-mapping
ctest --test-dir out/build/s4-mapping --output-on-failure
```

The target is `pdfbookmarkMapping` / `pdfbookmark::Mapping`. The root build exposes it with `PDFBOOKMARK_BUILD_MAPPING=ON`. See `docs/handoffs/S4_HANDOFF.md` for methods, fixture expectations, verification, and limits.
