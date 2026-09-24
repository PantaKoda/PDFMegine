# Engine and CLI handoff

Guide: root `AGENTS.md`, agent edition 2.1. Engine is orchestration and the CLI is a client; neither is a domain subsystem. Owned paths: `include/pdfbookmark/engine/`, `src/engine/`, `tests/engine/`, `apps/cli/`, `tests/cli/`, and the root `PDFBOOKMARK_BUILD_ENGINE` block. Completed steps: **A5** (text facade and `text` CLI) **A9** (analysis orchestration, analysis/plan JSON, `analyze` CLI) and **A10** (`apply` facade and CLI over S5).

## A5: migration decision

The old `inspect_file()` lives in the separate sibling repository `<old pdfbookmark repository>` (`pdfbookmarkEngine.cpp`). It opens PDFium and qpdf directly and uses its own text-status heuristic. That repository was **not modified**. In this repository its responsibilities are served by one session-based implementation: S1 acquisition and assessment behind `engine::extract_text`. Nothing from it was copied: no duplicate PDFium/qpdf access, and no second text-quality formula. The old executable's name (`pdfbookmarkCli`) is kept as the CMake target, which produces `pdfbookmark.exe`.

## Public operations

`include/pdfbookmark/engine/text.hpp`:
- `extract_text(input, pages?, TextOptions, RunControl, progress)` returns `Result<TextReport>`.
  - It opens one S1 session and acquires the pages in caller order, in batches (`batch_pages`, default 8).
  - Duplicates and out-of-range indices are rejected before any work; `nullopt` means all pages.
  - Engine owns the **run-wide OCR budget** (`ocr_budget`, default 64). It counts non-skipped OCR attempts and passes the remainder as each S1 call's `max_ocr_attempts`, so a budget consumed in one batch never resets.
  - Cancellation between batches marks unstarted pages `Cancelled`.
  - Status is `Complete` (all pages Ok or NoTextFound), `Partial` (any Degraded or Failed) or `Cancelled`.
- `text_report_json(report)` produces UTF-8 JSON with deterministic member order:

```json
{
  "schema_version": 1, "kind": "pdfbookmark.text", "page_index_base": 0,
  "input": {"sha256": "<64 hex>", "page_count": 6, "display_path": "..."},
  "status": "complete | partial | cancelled",
  "acquisition": {"policy_id": "...", "model_identity": "...",
                  "configurations": ["<S1 configuration_id>", "..."],
                  "ocr_budget": 64, "ocr_attempts_used": 0},
  "diagnostics": [],
  "pages": [{
    "page_index": 2, "outcome": "ok|degraded|no_text_found|failed|cancelled",
    "configuration": 0, "reasons": [],
    "assessment": {"readability": "...", "coverage": "...", "policy_id": "...",
                   "unicode_scalars": 0, "visible_scalars": 0,
                   "replacement_count": 0, "control_count": 0,
                   "image_object_count": 0, "reasons": []},
    "attempts": [{"source": "embedded_pdf|ocr", "state": "completed|failed|skipped", "reason": "..."}],
    "content": null | {"revision": 3, "source": "...", "reading_order": "estimated|uncertain",
      "geometry": {"frame": "canonical_top_left_points", "width_points": 600,
                   "height_points": 800, "rotation_quarters": 0, "user_unit": 1},
      "text": "<regions joined by \\n>",
      "regions": [{"id": 0, "text": "...", "granularity": "pdf_text_run|ocr_line",
                   "quad": [[x,y],[x,y],[x,y],[x,y]] | null, "ocr_confidence": null}]}
  }]
}
```

Geometry is rounded to 3 decimals and OCR confidence to 6; a non-finite value becomes `null`. `configuration` indexes `acquisition.configurations`. It is `null` for pages that were never started, because S1's configuration identity includes the per-call OCR allowance. Region IDs are interpretable because their text and geometry are serialized alongside them, with the page `revision`.

`src/engine/json.{hpp,cpp}` (private) is a strict RFC 8259 parser and deterministic writer. It rejects trailing commas, comments, duplicate keys, lone surrogates, invalid UTF-8, leading zeros and trailing content, and converts integers with range checks. No new external JSON dependency was introduced, because the repository had none. A9 reuses it for the plan and analysis codecs. Number formatting assumes the default "C" locale; the CLI never changes it.

## CLI

`pdfbookmark text <input.pdf> [--pages SPEC] [--json PATH|-] [--force] [--mode auto|embedded|ocr] [--models DIR | --no-ocr-models] [--dpi N] [--ocr-budget N]`

- `--pages` is one-based inclusive (`all`, `N`, `A-B`, or comma lists, with order kept). It is converted to zero-based exactly once, in `apps/cli/page_selection.cpp`. Page 0, reversed ranges, duplicates, malformed items and oversized selections are usage errors.
- JSON goes to stdout by default. Progress, summaries and per-page failure reasons go to stderr. Windows stdout is binary, so the UTF-8 bytes are exact.
- An existing `--json` file is refused unless `--force` is given. The input PDF can never be the JSON target. Output is written to `<path>.partial` and then renamed.
- Default models are `<executable dir>/models/{det,rec}` when present; otherwise OCR is unavailable and reported per page.
- Windows uses `wmain`, so non-ASCII paths work (verified with `tëst 日本.pdf`).
- Ctrl+C requests cooperative cancellation.
- Exit codes: 0 complete, 1 failed, 2 usage, 3 partial (results written), 4 cancelled. `analyze` and `apply` are described below.

## Verification (Windows x64, MSVC 14.51, VS 18, Ninja, C++17, root build `out/build/s1-debug` with S1 OCR enabled)

`cmake -S . -B out/build/s1-debug -DPDFBOOKMARK_BUILD_ENGINE=ON` (with the existing S1–S5 options), then `cmake --build out/build/s1-debug` and `ctest --test-dir out/build/s1-debug -E "golden|model_info"`: **11/11 passed**, including the new tests below.

- **`engine_text`** (5.7 s):
  - JSON strictness: 14 rejected inputs, surrogate pairs, checked integers, and a deterministic round trip.
  - Caller order `{4,0,2,5,3,1}` in 2-page batches with one configuration and cumulative progress; all pages; up-front rejection of duplicates and out-of-range indices.
  - Pre-set cancellation gives 6 explicitly cancelled pages.
  - Identical runs produce byte-identical JSON; the JSON's identity, indices, order and regions match the report.
  - OcrOnly without models is a configuration error.
  - **Real-model run-wide budget:** OcrOnly at 100 DPI with budget 1 over pages `{2,3,4}` in 1-page batches. OCR read index 2 as "Alpha / 1", indices 3 and 4 failed with the budget exhausted, and there were two configurations.
- **`cli_page_selection`**: conversion and 13 malformed selections.
- **`cli_text`** (CMake script):
  - CLI page 3 is index 2, and stdout stays empty when writing to a file.
  - Existing JSON is unchanged without `--force` and replaced with it; stdout JSON works, with progress on stderr.
  - The input-as-output case exits 1, and the usage cases exit 2.
  - Out-of-range and missing inputs exit 1; `ocr` mode with `--no-ocr-models` exits 1.
- **Manual runs from an unrelated directory:**
  - A Unicode path with embedded text was complete.
  - The scanned `tests/text/fixtures/image_only.pdf` in Auto mode with models used OCR ("Settings / Account overview / Email address").
  - Without models it exited 3, with "page 1 (index 0): failed - OCR model resources unavailable".

OpenCV (an existing OCR-package dependency) prints `[ INFO ] … plugin … FAILED` lines to stderr while probing optional parallel back ends. stdout JSON is unaffected.

## Limitations and next actions

- Engine and CLI are not installed or packaged yet. Single-executable packaging with embedded models belongs to A11.

## A9: analysis and plan workflow

`include/pdfbookmark/engine/analysis.hpp`:
- `analyze(input, AnalysisOptions, RunControl, progress)` returns `Result<AnalysisReport>`. It uses one S1 session and never modifies the PDF or reads outlines.
- `analysis_report_json(report, options)` produces the analysis JSON.
- `plan_json(plan)` and `parse_plan_json(text)` are the S5-schema codec.

**Sequence (§9.2):**
1. **Search.** Acquire physical pages `[0, 40)`, then 20 at a time, clamped to the document and to `max_search_pages` (200). Keep every acquired page. Rerun S2 over the whole contiguous searched range after each batch.
2. **Extend the search** only while no candidate exists, or while a candidate is not `Closed` and ends on the last searched page (S2's "may continue").
3. **Choose a candidate.** An explicit `candidate_id` wins. Otherwise take the highest score, unless the runner-up reaches 90% of it (`candidate_tie_ratio`), in which case the result is `analysis_partial` with every candidate ID listed.
4. **Parse** with S3 over the searched pages.
5. **Sections.** Caller-supplied sections are used as given. Otherwise there is one **assumed** decimal `body` section from the page after the TOC to the end of the document, or the pages before the TOC if the TOC is last. That assumption is recorded in the report's diagnostics, the section `origin` and CLI notes. S4 still needs two independent agreeing anchors plus target confirmation inside it.
6. **Mapping loop.** Call S4, gather facts (viewer labels) for acquired pages, and take the pages from S4's bounded requests that are not yet acquired or requested. Fetch them within `max_evidence_pages` (60) and `max_mapping_rounds` (6), and call S4 again. Stop when there are no requests, nothing new can be learned, a budget runs out, or on cancellation; the stop reason is recorded. Requests still unmet are reported.
7. **Plan assembly** (§9.4):
   - Entries S4 resolved become nodes; destinations come only from S4.
   - By default, any unresolved or ambiguous entry, unknown hierarchy, or document-level parse incompleteness blocks readiness. Document-level incompleteness means unparsed fragments, missing pages, an unclosed boundary, or an unexplained cause.
   - `allow_partial` records omissions, promotes resolved children of known ancestry to the nearest retained ancestor or to top level (recorded), and accepts document-level incompleteness (recorded).
   - `flat_outline_for_unknown_hierarchy` makes unknown parents top level (recorded).
   - An empty node set is never ready. S5 `validate()` always runs.
   - A draft is embedded in the report for inspection; `ready` only when there are no blockers.

**Outcomes:**
- `plan_ready`
- `analysis_partial`: a TOC was found but the plan is blocked, a candidate choice is needed, or the requested candidate is missing.
- `no_toc_found_in_search`: every searched page was assessed. It is not proof of absence; `search_covered_document` says whether the whole document was searched.
- `search_incomplete`: no candidate, and some searched page failed or was cancelled.
- `cancelled`
- Subsystem or document errors are `Result` errors and exit 1.

**Plan JSON:** exactly the §10.2 shape: `schema_version` 1, `page_index_base` 0, `input` (`sha256` hex and `page_count`), `existing_outline_policy` `"replace_in_copy"`, `nodes` (`id`, `parent_id`, `title`, `destination.pdf_page_index`), `omitted_entries` and `promotions`. Decoding rejects unknown or missing members, wrong types, out-of-range integers, bad hex, other policies, other schema versions, and an analysis report passed as a plan. S5 semantics stay in `writer::validate()`. Engine writes a plan file only when ready; there is no draft-plan file.

**Analysis JSON (`kind: "pdfbookmark.analysis"`)** contains:
- input identity, outcome, stop reasons, diagnostics, and an options summary (limits and policies);
- acquisition identities, the OCR budget, and the search and evidence pages;
- every acquired page's evidence (regions with revisions), so source references stay interpretable;
- S2 candidates and page reviews, the candidate choice, and S3 entries (references, hierarchy, sources) with unparsed fragments;
- S4 sections, entry results (alternatives, supporting sources, reasons), and unmet requests;
- the plan's `ready` flag, blockers, choices and draft.

**CLI:** `pdfbookmark analyze <input.pdf> [--report PATH|-] [--plan PATH] [--candidate ID] [--allow-partial] [--flat-outline] [--max-search-pages N] [--max-evidence-pages N] [--force] [--mode/--models/--no-ocr-models/--dpi/--ocr-budget]`.
- The report goes to stdout unless `--report` is given. The plan file is written only when ready.
- stderr shows the outcome, the chosen TOC or the candidate IDs to choose from, resolved counts, stop reasons, notes (including the assumed section and search extensions), policy choices and blockers.
- Exit codes: 0 plan ready, 3 no ready plan (report still written), 4 cancelled, 1 error, 2 usage.

**Fixtures** (`tests/engine/fixtures/make_engine_fixtures.py`, standard library only, deterministic):

| File | SHA-256 | Covers |
| --- | --- | --- |
| `boundary.pdf` | `59401a0e…97d48a8` | 70 pages; the TOC spans physical indices 39/40, and 40 has no heading. Printed n is at index 40+n. Known hierarchy; target index 60 lies beyond the search. |
| `boundary_outlined.pdf` | `8df353f8…0e0138` | The same pages plus a misleading 3-item outline. |
| `multi.pdf` | `6315edff…64c1` | Two tied TOCs (indices 1 and 3) with prose between. |
| `unknown_hierarchy.pdf` | `1d3d868e…a2b26` | A 12-point indent, giving unknown parentage. |
| `unresolved.pdf` | `a492a9ab…ee52da` | An entry citing printed page 99, with a resolvable child. |
| `no_toc.pdf` | `e1df09cd…61f` | 50 prose pages. |

**Verification:** `ctest --test-dir out/build/s1-debug -E "golden|model_info"` passed **13/13**. Scenarios:
- **`engine_analysis`, 12 scenarios:**
  - The 39/40 candidate is kept across batches: 60 pages searched, and the evidence request for index 60 is fulfilled.
  - Exact expected nodes: Part One→41 with 3 children at 41/43/46; Part Two→50 with children at 51/55; Appendix Notes→60.
  - The paired-outline reports are identical after normalizing input identity.
  - The plan codec round-trips and has 8 strictness rejections; a decoded out-of-range destination is caught by S5.
  - An evidence budget of 0 gives `analysis_partial`, with unmet requests and no guessed destination. The partial policy then gives 7 nodes and 1 omission.
  - A search limit of 30 gives `no_toc_found_in_search`, marked as not covering the document.
  - Tied candidates give `analysis_partial`; explicit selection maps Intro, Methods, Results and Summary to indices 5, 8, 12 and 16; an unknown ID is reported.
  - Unknown hierarchy is blocked by default and flattened only with the flat policy.
  - The unresolved entry is blocked by default; the partial policy omits it and promotes its child to top level.
  - The no-TOC document searches all 50 pages (40 + 10).
  - Cancellation, invalid limits and a missing input are handled.
  - The analysis JSON is deterministic.
- **`cli_analyze`:**
  - Ready plan and report files are written with stdout empty.
  - Existing outputs are refused without `--force`.
  - An unready analysis writes no plan file and shows blockers.
  - The partial plan records its omission and promotion.
  - The tied case lists IDs, and a second run with `--candidate <id from report>` succeeds.
  - `no_toc_found_in_search`; the usage and safety errors.

**Findings and limitations:**
- **S2 pairing limit: fixed in S2 v2 (decision S2-08).** A separate page number far to the right of its title was not paired, and density counted split rows twice. A TOC page was rejected, S2 closed the neighbouring candidate, and Engine produced a confident half-TOC plan. Engine cannot detect a page S2 rejects, so the fix belongs in S2. The regression fixture `boundary_short_leaders.pdf` (the original failing layout, byte-identical) now yields both TOC pages and the full plan (`engine_analysis` case 1b).
- The default numbering section is an assumption. Roman front matter, appendix prefixes and restarts need caller-supplied sections or overrides; the CLI does not expose those yet.
- No real scanned or native book has been run through `analyze`; correct destinations on real books are A11's gate.

## A10: apply

`include/pdfbookmark/engine/apply.hpp`:
- `apply(input, output, plan, ApplyOptions, RunControl)` delegates directly to S5 `write_copy`. Engine adds no writing logic.
- `apply_plan_file(...)` reads the plan file (64 MiB cap), refuses an output that is the plan file itself, decodes it strictly with `parse_plan_json`, and applies it.

S5 alone performs:
- structural revalidation and the input SHA-256 and page-count check (a stale plan gives `InputChanged`);
- input/output alias protection, and no-clobber unless `replace_existing_output`;
- the encrypted and signed-input refusals;
- the verified temporary file, commit, and cancellation before commit.

**CLI:** `pdfbookmark apply <input.pdf> --plan PATH --output PATH [--force]`.
- stderr reports the committed bookmark and page counts (verified after reopening), whether an existing outline was replaced in the copy, and whether a previous output file was replaced.
- On failure it prints the reason, "re-run analyze" for stale plans, and "nothing written; input unchanged".
- Exit codes: 0 committed, 1 failed, 2 usage, 4 cancelled.
- `--force` only permits replacing an existing *output*. The input and the plan file can never be the output.

**Verification:** `ctest --test-dir out/build/s1-debug -E "golden|model_info"` passed **15/15** (S1–S5, Engine, CLI).
- **`engine_apply`** (analyze, plan file, apply on `boundary_outlined.pdf`) checks the two §9.5 properties separately:
  - **(a) Written outline structure:** qpdf's `QPDFOutlineDocumentHelper`, independent of S5's verifier, reads exactly the plan tree (8 items, titles, depth, order, pages) with no old bookmark retained.
  - **(b) Correct chapter destinations:** S1 reads each destination page *of the output*, and each page begins with or contains its entry's heading.
  - The input bytes and its misleading 3-item outline are unchanged.
  - A manual Unicode title edit is applied.
  - Refusals: a stale plan (another input) gives `InputChanged`; an analysis report used as a plan gives `InvalidArgument`; output as plan file, output as input (even with replacement), existing output without replacement, and pre-commit cancellation are all refused, and none leaves output.
- **`cli_apply`:**
  - `analyze` then `apply` exits 0 with "committed 8 bookmarks on 70 pages" and "existing outline replaced in the copy".
  - Existing output is refused and left unchanged; `--force` replaces it, and identical runs are byte-identical.
  - A stale plan exits 1 with "re-run analyze" and no file; a report used as a plan exits 1; input as output exits 1.
  - Four usage errors exit 2.
  - The input SHA-256 is unchanged.
- **Independent PDFium read** of the CLI output: `Part One→41 (−3) › Getting Started→41, Installation→43, Configuration→46; Part Two→50 (−2) › Networking→51, Storage→55; Appendix Notes→60`. The input still reads `Part One→5, Networking→2, Zeta Chapter→66`.

## A11 (in progress): first real-book check

A read-only `analyze` of the owner's local `CCNA_Certification_Guide_2024_V8 final.pdf` (41 pages, SHA-256 prefix `fe3014626eba5d85`; not copied into the repository, outputs kept in the session scratchpad):
- **Result:** `no_toc_found_in_search`, all 41 pages searched, 1 OCR attempt, no plan written. The input SHA-256 was identical before and after. Runtime was 23 s (Debug build).
- **Why:** the document's table of contents (physical index 1) prints the **page number before the title** ("03 Overview", "08 CCNA study guide", … "43 Opportunities"). S2 and S3 only recognize title-then-reference rows, so the page scored 2 and was rejected. That is an unsupported layout reported honestly, not a wrong plan. The document is a 41-page brochure whose last entry cites printed page 43, beyond its end.
- **Engine bug found and fixed (E-11):** the report named the OCR model identity `not-used` although OCR had run in a later batch. It now reports the real identity, and the `engine_text` budget test asserts it.
- All pages were `degraded` in S1's assessment (not investigated further; detection treats degraded pages as usable-with-provenance).

Remaining for A11:
- a real book with a supported TOC layout (title, then leader or gap, then number), native and scanned, with manually checked destinations;
- a decision on supporting number-first TOC rows (an S2 and S3 feature);
- Engine and CLI install rules and single-executable packaging with models.

## Packaging and audit follow-ups

- `AnalysisOutcome::Failed` was removed; failures are `Result` errors (exit 1).
- The analysis JSON `plan` object now includes `validation_issues`. Validation issues of a draft that is already blocked are not repeated in `blockers`.
- The library quiets OpenCV's INFO logging when OCR first starts, unless `OPENCV_LOG_LEVEL` is set (E-19).
- The CLI is a client of the unified library: it includes only `<pdfbookmark/pdfbookmark.hpp>` and links `pdfbookmark::pdfbookmark` when `PDFBOOKMARK_BUILD_LIBRARY=ON` (see `docs/API.md`).
- End-user package, drag-and-drop scripts and user guide: see `docs/BUILDING.md` and `packaging/README.txt`.
