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

It also has `isbns`, a plain list of `IsbnValue` (not a field with a status, because no single ISBN is "the" value): `isbn13`, `printed`, `form` (ISBN-10 or ISBN-13 as printed), `format`, an optional `label`, and `evidence`.

Each field has a status (`Resolved`, `Ambiguous` or `NotFoundInSearch`), a value only when resolved, evidence (page, revision, region and supporting text), alternatives with reasons, and decision reasons. The result also includes per-page roles (`cover`, `title`, `copyright`, `contents`, `other`, `unknown`) and diagnostics. `DocumentHints` (for example PDF Info title and author) can corroborate but never resolve a field on their own.

Engine: `engine::extract_metadata(input, MetadataRunOptions, RunControl)` reads the first 10 physical pages, then 10 more at a time up to 30 while any field is unresolved. It uses the shared run-wide OCR budget (default 16). Reaching the limit is reported as "not found in the searched pages". `metadata_report_json()` produces `kind: "pdfbookmark.metadata"`, schema 1, zero-based.

CLI: `pdfbookmark metadata <file> [--json PATH|-] [--max-pages N] [reading options]` prints a summary with **one-based** file page numbers. Exit codes: 0 when every field is found, 3 when some are not found or ambiguous, 1 on error, 2 on wrong usage.

## Decisions (policy `s6-document-metadata-v2`; v2, 3 Oct 2026, adds S6-07 and leaves S6-01 to S6-06 unchanged)

| ID | Choice | Limit |
| --- | --- | --- |
| S6-01 | **Visual lines:** regions are grouped into visual lines by vertical overlap (so "Computing" + "for", and "Georg" + "Hager", become one line). Line height is the font-size proxy. | Rotated or vertical cover text is not handled. |
| S6-02 | **Page roles:**<br>• contents: a "Contents" heading, or at least 2 lines (and at least half of all lines) ending in a page number;<br>• copyright: ©, "copyright", ISBN, "first published", "printed in", Library of Congress;<br>• cover: the first page, with an image or OCR text, and a prominent block;<br>• title: at most 25 lines with a prominent block. A title block must be at least 1.2× the median of the page's *other* lines, or at least 16 pt when there are none (which excludes dedications). | Heuristic, English keywords. |
| S6-03 | **Title:** the largest contiguous block of lines at least 0.7× the largest line, excluding publisher and series lines. It resolves when the same title appears on two or more cover/title pages, or on one page with prominence of at least 1.5 and no rival. Native text is preferred over OCR for the returned value. A subtitle comes from "Title: Subtitle" or a smaller line directly below the block. | A lone OCR'd cover without a title page resolves only by prominence. |
| S6-04 | **Contributors:** names from "by / Edited by / Translated by" statements and from name lines on cover and title pages, plus statements that name *this* title followed by names on any page (a series list: "TITLE, A and B"). A name resolves when two or more pages support it, or with an explicit responsibility phrase on a cover/title page. Other books' editors in a series list are never used. | Name detection targets Latin-script personal names. |
| S6-05 | **Edition:** only from cover, title and copyright pages. An explicit statement ("Second Edition", "Revised edition") wins, and conflicting statements are ambiguous. Otherwise, publication-history lines give the highest numbered edition. The printed statement is preserved. | English ordinals and qualifiers. |
| S6-06 | **Years:** kept apart as publication (published or publication statements, or a lone imprint year on a title page), copyright (©, "copyright", "(c)") and printing (printed, reprint, printing). The publication year is tied to the identified edition ("Second edition published 2012"); statements about other editions are excluded. Several unrelated years are ambiguous, and the largest is never assumed. A copyright year is never reported as the publication year; the reason says a copyright year exists. ISBN lines are ignored. | Only 1450–2100 counts as a year. |
| S6-07 | **ISBNs:** every ISBN on a line that says "ISBN" or "International Standard Book Number", on any supplied page, and on the lines directly below such a line while they keep giving ISBNs (one format per line). Digits may be separated by hyphens, typographic dashes or spaces. Only numbers that pass the ISBN-10 or ISBN-13 check digit are listed; a failing number on an ISBN line goes to the diagnostics. An ISBN-10 is converted to ISBN-13 (978 prefix), and repeats are merged into one entry. The format comes from the printed qualifier: parentheses or a bare word after the number ("(hardback)", "pbk"), or words around the keyword ("e-ISBN", "Print ISBN", "ISBN (eBook)"). All ISBNs are listed; none is chosen. They do not affect the search stop or the exit status. | English format words. A number without an ISBN keyword on or above its line is not read. ISBNs of other books listed on the same pages (a series list, a multi-volume set) are listed too. |

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

- **ISBNs (S6-07, 3 Oct 2026):** root `dev` build 26/26. `metadata_pure` adds: the copyright-page ISBN with its source line; "International Standard Book Number … (Paperback)"; one page with a hardback label, an ebook ISBN on the next line without a keyword, an ISBN-10 with check digit X merged into its ISBN-13, both forms on one line, an e-ISBN with en dashes, a space-separated ISBN-10 with "pbk", a wrong check digit (diagnostic, not listed) and a valid number on a line that is not an ISBN statement (not listed). `engine_metadata` and `cli_metadata` check the JSON `isbns` and the summary line. Real books (3 Oct 2026, read-only, `tests/books/`): *Computational Physics* (Wiley, 4th ed.) lists 3 ISBNs from its copyright page (Print, ePDF, ePub); *Numerical Recipes* (Cambridge, 3rd ed.) lists 2 (hardback, eBook), each printed as ISBN-13 and ISBN-10 and merged into one entry. Both match the printed pages, and the input hashes are unchanged. Both files have embedded text (0 OCR attempts). *Black Holes, White Dwarfs and Neutron Stars* (`shapiro1983.pdf`, a scan with an embedded OCR layer) lists its one ISBN, 9780471873167, both from the embedded layer (printed there with stray spaces, "978-0-47 1-873 16-7") and from our own OCR (`--mode ocr --max-pages 4`, 4 OCR attempts); the ISBN-10 line is merged into the same entry.

## Limitations and follow-ups

- **ISBNs are listed, not interpreted.** There is no book+edition key yet: an edition has one ISBN per format, and joining them needs either the printed set or an external catalogue. How the list is used is an open owner decision.
- **The search does not look for ISBNs.** It stops when title, contributors, edition and publication year are resolved, so a copyright page beyond the pages read is not reached for its ISBN alone.

- **PDF Info metadata** (the file's Title/Author) is only accepted as a hint. Reading it needs a small addition to S1's facts facade, which was deliberately not done ("do not change the rest of the system"). Engine passes no hints yet.
- **English keywords only** (edition words, "published", "copyright", responsibility phrases).
- **Evaluation:** so far one real book plus synthetic fixtures. Evaluate more books (scanned, other publishers) before deciding whether a model is needed, as the request suggests.
