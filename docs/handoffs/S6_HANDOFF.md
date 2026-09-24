# S6 Document Metadata Extraction handoff

**Scope:** an owner-directed extension, requested on 24 September 2026, beyond the five subsystems in root `AGENTS.md` (edition 2.1). AGENTS.md itself is unchanged. S6 follows the same rules as the other subsystems: a pure library that consumes S1 contract values, never opens PDFs, runs OCR or fetches pages, and exposes no backend types. Engine decides which pages to read.

**Owned paths:** `include/pdfbookmark/metadata/`, `src/metadata/`, `tests/metadata/` (with `fixtures/make_front_matter.py`), `subsystems/document_metadata/` and `cmake/pdfbookmarkMetadataConfig.cmake.in`.

**Additive integration** (existing behavior is unchanged):
- `include/pdfbookmark/engine/metadata.hpp` and `src/engine/metadata.cpp`.
- The CLI `metadata` command.
- Tests `tests/engine/engine_metadata_test.cpp` and `tests/cli/cli_metadata_test.cmake`.
- The root option `PDFBOOKMARK_BUILD_METADATA`, OFF by default. With it OFF, the build is the previous system; this was verified with 15/15 suites passing.

## Public API

`metadata::extract(pages, hints, options)` returns `Result<MetadataResult>`, whose fields are:
- `title`, a `TitleValue` with `title` and an optional `subtitle`;
- `contributors`, a list of `{name, role}` where the role is author, editor, translator or organization;
- `edition`, an `EditionValue` holding the printed `statement` and an optional `ordinal`;
- `publication_year` and `copyright_year`, each a `YearValue` with `year`, `kind` and `statement`.

Each field has a status (`Resolved`, `Ambiguous` or `NotFoundInSearch`), a value only when resolved, evidence (page, revision, region and supporting text), alternatives with reasons, and decision reasons. The result also includes per-page roles (`cover`, `title`, `copyright`, `contents`, `other`, `unknown`) and diagnostics. `DocumentHints` (for example PDF Info title and author) can corroborate but never resolve a field on their own.

Engine: `engine::extract_metadata(input, MetadataRunOptions, RunControl)` reads the first 10 physical pages, then 10 more at a time up to 30 while any field is unresolved. It uses the shared run-wide OCR budget (default 16). Reaching the limit is reported as "not found in the searched pages". `metadata_report_json()` produces `kind: "pdfbookmark.metadata"`, schema 1, zero-based.

CLI: `pdfbookmark metadata <file> [--json PATH|-] [--max-pages N] [reading options]` prints a summary with **one-based** file page numbers. Exit codes: 0 when every field is found, 3 when some are not found or ambiguous, 1 on error, 2 on wrong usage.

## Decisions (policy `s6-document-metadata-v1`)

| ID | Choice | Limit |
| --- | --- | --- |
| S6-01 | **Visual lines:** regions are grouped into visual lines by vertical overlap (so "Computing" + "for", and "Georg" + "Hager", become one line). Line height is the font-size proxy. | Rotated or vertical cover text is not handled. |
| S6-02 | **Page roles:**<br>• contents: a "Contents" heading, or at least 2 lines (and at least half of all lines) ending in a page number;<br>• copyright: ©, "copyright", ISBN, "first published", "printed in", Library of Congress;<br>• cover: the first page, with an image or OCR text, and a prominent block;<br>• title: at most 25 lines with a prominent block. A title block must be at least 1.2× the median of the page's *other* lines, or at least 16 pt when there are none (which excludes dedications). | Heuristic, English keywords. |
| S6-03 | **Title:** the largest contiguous block of lines at least 0.7× the largest line, excluding publisher and series lines. It resolves when the same title appears on two or more cover/title pages, or on one page with prominence of at least 1.5 and no rival. Native text is preferred over OCR for the returned value. A subtitle comes from "Title: Subtitle" or a smaller line directly below the block. | A lone OCR'd cover without a title page resolves only by prominence. |
| S6-04 | **Contributors:** names from "by / Edited by / Translated by" statements and from name lines on cover and title pages, plus statements that name *this* title followed by names on any page (a series list: "TITLE, A and B"). A name resolves when two or more pages support it, or with an explicit responsibility phrase on a cover/title page. Other books' editors in a series list are never used. | Name detection targets Latin-script personal names. |
| S6-05 | **Edition:** only from cover, title and copyright pages. An explicit statement ("Second Edition", "Revised edition") wins, and conflicting statements are ambiguous. Otherwise, publication-history lines give the highest numbered edition. The printed statement is preserved. | English ordinals and qualifiers. |
| S6-06 | **Years:** kept apart as publication (published or publication statements, or a lone imprint year on a title page), copyright (©, "copyright", "(c)") and printing (printed, reprint, printing). The publication year is tied to the identified edition ("Second edition published 2012"); statements about other editions are excluded. Several unrelated years are ambiguous, and the largest is never assumed. A copyright year is never reported as the publication year; the reason says a copyright year exists. ISBN lines are ignored. | Only 1450–2100 counts as a year. |

## Verification (Windows x64, MSVC, C++17)

- **S6 standalone** (`cmake -S subsystems/document_metadata`): `metadata_pure` 1/1. The cases are:
  - a full second-edition book;
  - a real-book cover and half-title layout (a smaller lead-in line, words split across regions);
  - a copyright-only page;
  - a series list naming other books' editors;
  - an "Edited by" statement, conflicting titles, and body text only;
  - a hint not resolving anything alone, an unreadable page, and duplicate input.
- **Root development build with `PDFBOOKMARK_BUILD_METADATA=ON`: 18/18.** The 15 existing suites are unchanged; the new ones are `metadata_pure`, `engine_metadata` (generated `front_matter.pdf`: resolution, search limit, stepwise expansion, JSON, cancellation) and `cli_metadata`. With the option OFF: 15/15, and the command reports it is unavailable.
- **Real book** (the owner's *Introduction to High Performance Computing for Scientists and Engineers*, read-only):
  - **Title:** resolved from the OCR'd cover and the half-title.
  - **Authors:** Georg Hager and Gerhard Wellein, resolved from the cover and confirmed by the series pages. The editors of other series titles were ignored.
  - **Copyright year:** 2011.
  - **Edition and publication year:** not found in the 30 pages searched, which is correct because the book prints neither.
  - Two bugs were found and fixed on this book: block height used the first line instead of the largest, and the median included the title lines. TOC continuation pages were also being misread as title pages; they are now recognised as contents.

## Limitations and follow-ups

- **PDF Info metadata** (the file's Title/Author) is only accepted as a hint. Reading it needs a small addition to S1's facts facade, which was deliberately not done ("do not change the rest of the system"). Engine passes no hints yet.
- **English keywords only** (edition words, "published", "copyright", responsibility phrases).
- **Evaluation:** so far one real book plus synthetic fixtures. Evaluate more books (scanned, other publishers) before deciding whether a model is needed, as the request suggests.
