# S5 — Bookmark Writing

**Status: implemented (agent edition 2.1).** S5 validates explicit, input-bound bookmark plans and writes a verified new PDF whose outline is exactly the plan tree (`replace_in_copy`). The input file is never modified. S5 never infers or repairs destinations.

- `pdfbookmark::WriterPlan` (`include/pdfbookmark/writer/plan.hpp`): the plan contract and structural `validate()`. It needs no qpdf headers, libraries or package.
- `pdfbookmark::Writer` (`include/pdfbookmark/writer/writer.hpp`): `read_input_identity()` and `write_copy()`, with qpdf as a private dependency.

```powershell
cmake -S subsystems/bookmark_writing -B out/build/s5-writer -G Ninja -DCMAKE_BUILD_TYPE=Debug -Dqpdf_DIR=<vcpkg_installed>/x64-windows/share/qpdf
cmake --build out/build/s5-writer
ctest --test-dir out/build/s5-writer --output-on-failure
```

The root build exposes it with `PDFBOOKMARK_BUILD_WRITER=ON`. See `docs/handoffs/S5_HANDOFF.md` for write semantics, verification evidence and limitations, and `docs/IMPLEMENTATION_DECISIONS.md` (S5-01…S5-07) for decisions.
