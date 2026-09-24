# S5 Bookmark Writing handoff

**Subsystem and guide:** S5 under root `AGENTS.md` agent edition 2.1. Owned paths: `subsystems/bookmark_writing/`, `include/pdfbookmark/writer/`, `src/writer/`, `tests/writer/`, `cmake/pdfbookmarkWriterConfig.cmake.in`, `cmake/pdfbookmarkCopyDlls.cmake`. The root build adds only opt-in `PDFBOOKMARK_BUILD_WRITER`. Shared contract change (integration role, serial mode): four appended `ErrorCode` values in `include/pdfbookmark/core/types.hpp` (`Unsupported`, `OutputExists`, `OutputWrite`, `Cancelled`). The existing values are unchanged, and no existing code switches over `ErrorCode`. S1–S4 and the OCR package were not otherwise changed.

**Public operations:**

| Target | Header | Operations |
| --- | --- | --- |
| `pdfbookmark::WriterPlan` (backend free) | `writer/plan.hpp` | `BookmarkNode`, `BookmarkPlan`, `PlanIssue`, `PlanValidation`, `validate(plan)` |
| `pdfbookmark::Writer` (private qpdf) | `writer/writer.hpp` | `read_input_identity(path)`, `write_copy(input, output, plan, options, control)` |

Plan semantics: `schema_version` 1, `page_index_base` 0, and input SHA-256/page count. Node array order is sibling order, and `parent_id` defines the hierarchy (a parent may appear after its child). Repeated destinations are valid. Duplicate or empty IDs, missing parents, self-parents, cycles, blank or invalid-UTF-8 titles, out-of-range destinations, empty plans, missing digests, inconsistent recorded omissions, and inconsistent promotions are structured issues tied to node index, ID and field. `existing_outline_policy` has one value, `ReplaceInCopy`. Engine's JSON codec must encode it as `"replace_in_copy"` and must not extend the schema independently.

**`write_copy` behavior** (decisions S5-01…S5-07):
1. Re-validates the plan structurally; an earlier validation is never trusted.
2. Rejects an output that is the input, including `.\`, case and hard-link aliases, even when replacement is enabled.
3. Refuses an existing output unless `replace_existing_output` is set, and rejects a non-regular output.
4. Reads the input bytes once and requires the SHA-256 and page count to match the plan (`InputChanged` otherwise). qpdf parses those same bytes.
5. Rejects encrypted and password-protected input, and signed input (`/Perms`, `SigFlags` bit 1, or any `/Sig` field with a value), with `Unsupported`.
6. Replaces the copy's `/Outlines` with exactly the plan tree. Items are closed (`Count` = −children; root `Count` = number of top-level items), destinations are `[page /Fit]`, and titles use `newUnicodeString`. Other objects, such as named destinations and link annotations, are untouched. Old outline items become unreachable and are not written.
7. Serializes in memory with a deterministic `/ID`, then writes a temporary sibling (`<output>.pdfbookmark-<random>.tmp`, exclusive create, flushed, closed).
8. Rereads the temporary file and requires byte equality with the serialized copy. It then reparses it and verifies the page count and every outline item's title, `/Parent`, `/Prev`, `/Next`, `/First`, `/Last`, `/Count` and destination page against the plan.
9. Rehashes the input immediately before commit and checks cancellation.
10. Rechecks aliasing and the output policy, then commits with `MoveFileExW` (no-clobber unless replacing, `WRITE_THROUGH`). On POSIX it uses link+unlink or rename. Any failure before commit removes the temporary file and leaves the input and any existing output intact. Cancellation observed after commit is reported as a diagnostic on a committed result.

**Verification (Windows x64, MSVC 14.51.36231, VS 18, Ninja, C++17, CMake 4.3.1-msvc1, qpdf 12.3.2 shared from vcpkg baseline `1460b31b08c42cc2e9ac2c79f45ec8707e2675e2` via `<old pdfbookmark repository>/vcpkg_installed/x64-windows`):**
- `writer_plan` (links only `WriterPlan`) and `writer_qpdf` passed 2/2 in the standalone static, standalone shared, and root opt-in builds.
- `writer_qpdf` builds its fixtures with qpdf at runtime: plain, outlined with named destination and link annotation, R6-encrypted, and widget-less signature. It independently reads the outputs with `QPDFOutlineDocumentHelper` and covers:
  - valid hierarchy, Unicode (`Ünïcødé — 日本語 😀`) and repeated destinations;
  - page-content preservation, and preserved named destinations and link annotations;
  - exact replacement of the old outline in the copy, while the original keeps its bytes and 2 bookmarks;
  - stale digest and page count, cyclic and empty plans;
  - output collision, explicit replacement, and `.\`, case and hard-link aliases (both directions);
  - encrypted and signed inputs;
  - pre-commit cancellation, injected temp corruption, temp-write failure, and commit failure that preserves the existing output;
  - post-commit cancellation;
  - input bytes compared after success, failure and cancellation, and no leftover temporary files.
- The output of the first case is reproducible: SHA-256 `cb2795c3…8a1ca4` on repeated runs.
- A temporary mutation, sending all destinations to page 0, made the writer refuse to commit ("valid plan commits a verified output" failed). The source was restored.
- Installed consumers ran from an unrelated directory:
  - `writer_plan_consumer` was configured without qpdf, and its `build.ninja` has no qpdf reference. Output: `valid=1 cycle_rejected=1`.
  - `writer_consumer` (static and shared packages) wrote a manual 4-item plan onto `tests/mapping/fixtures/acquired_mapping_outlined.pdf`. It reported input SHA-256 `3c5cb133…3d444c1e`, which equals `sha256sum`, and `committed=1 items=4 pages=6 structure=1 input_unchanged=1 replaced_input_outline_in_copy=1`. A second run refused the existing output. The input hash was identical before and after.
- A separate PDFium probe read the input outline as `Alpha→5, Beta→1, Zeta→3` (unchanged) and the output as `Alpha→2; Beta→3 (count −1) › Beta — note→3; Gamma→4`.

From an x64 Visual Studio developer shell (`Q` = the qpdf package directory above):

```powershell
cmake -S subsystems/bookmark_writing -B out/build/s5-writer -G Ninja -DCMAKE_BUILD_TYPE=Debug -Dqpdf_DIR=$Q
cmake --build out/build/s5-writer
ctest --test-dir out/build/s5-writer --output-on-failure
cmake --install out/build/s5-writer --prefix out/install/s5-writer
cmake -S tests/writer/consumer -B out/writer-consumer -G Ninja -DCMAKE_BUILD_TYPE=Debug -DpdfbookmarkWriter_DIR=<repo>/out/install/s5-writer/lib/cmake/pdfbookmarkWriter -Dqpdf_DIR=$Q
cmake --build out/writer-consumer
cmake -S tests/writer/plan_consumer -B out/writer-plan-consumer -G Ninja -DpdfbookmarkWriter_DIR=<repo>/out/install/s5-writer/lib/cmake/pdfbookmarkWriter
cmake --build out/writer-plan-consumer
```

Add `-DBUILD_SHARED_LIBS=ON` for the shared build. For the root build, use `-DPDFBOOKMARK_BUILD_WRITER=ON -Dqpdf_DIR=$Q`.

**Limitations:**
- Only page-level `/Fit` destinations are written, and all items are closed.
- Signature detection is structural; it does not validate signatures.
- Encrypted, password-protected and signed PDFs are unsupported for writing.
- The output is rewritten by qpdf, so it is not byte-identical to the input apart from the outline, and it is not an incremental update.
- The input is held in memory (512 MiB default cap).
- Temporary-file cleanup cannot run if the process is killed.
- Package installs copy every DLL beside the selected qpdf, including the unneeded `fmt` and `turbojpeg`.
- Only Windows is verified; the POSIX commit path is untested.

**Handoff:** Engine assembles `BookmarkPlan` from S4 `Resolved` results, never from existing bookmarks, and runs `validate()` before offering a plan. Engine owns plan JSON, including `"replace_in_copy"` and hex SHA-256, and the `apply` facade that calls `write_copy`. The CLI `apply` command (A10) and correct-chapter end-to-end checks (A11) remain. Correct serialization here does not prove that S4 chose the right page.
