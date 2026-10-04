# Decision log

This is the single record of *why* the project is the way it is. Every change, addition or removal that is not self-evident from the code gets an entry here.

- **Sections 1-7** (subsystems and Engine) are the original decision tables. Each row gives the choice and its reason, and the last column its limits or uncertainty.
- **Section 8** (from 24 September 2026) records project-level changes in a fixed format: **Change**, **Why**, **Assumptions** (what must hold for the decision to be right; revisit it if one stops holding), and, where relevant, **Removed** and **Verified**.
- The OCR package keeps its own decisions in `docs/ocr/DECISIONS.md`.
- Implementation status is in `docs/IMPLEMENTATION_PROGRESS.md`.

## 1. S1 Text Acquisition

These decisions implement root `AGENTS.md` agent edition 2.0. They do not amend the existing OCR package's `docs/ocr/DECISIONS.md`. Policy identity in results: `s1-acquisition-v2` (v2 adds S1-12 and S1-13).

| ID | Choice and reason | Explicit limit or uncertainty |
| --- | --- | --- |
| S1-01 | Keep an immutable, capped in-memory copy of the PDF at `open()`; hash those exact bytes with SHA-256 and give PDFium the same buffer. This binds all subsequent reads to analyzed bytes even if the source path changes. | Default cap is 512 MiB; larger files get a resource error. A future Writer must compare the plan identity before output commit. |
| S1-02 | Serialize every PDFium call through one process-wide mutex, retain PDFium runtime by session reference count, and serialize the OCR adapter globally. A document holds its own acquisition mutex. | Parallel page acquisition is intentionally not implemented; cancellation is checked between pages, not inside one PDFium/ORT call. |
| S1-03 | Use PDFium's page transform to map PDF user coordinates to the displayed crop, top-left canonical frame. Fit an affine from three `FPDF_PageToDevice` samples on a 1,000,000-unit virtual device; retain the inverse. Scale canonical dimensions by parsed `/UserUnit` where needed. | The pinned PDFium SDK has no public `UserUnit` getter. The local resolver understands explicit uncompressed page-tree dictionaries; object streams or unresolved syntax return unknown physical scaling and degrade the page. Native region quads are axis-aligned bounds of the backend run, so rotated individual glyph outlines are approximate. |
| S1-04 | Decode PDFium character values as Unicode scalar values, join surrogate pairs, and replace malformed values with U+FFFD while counting replacements. Keep native text in PDFium run order, split at explicit newlines, and join flattened runs with newline separators. | PDFium run order is `Estimated`, not a guarantee for columns. No dehyphenation or case folding. No fabricated native confidence or word boxes. |
| S1-05 | Render to a packed, top-down BGR buffer with white background and no file round trip. Default 300 DPI; dimensions use `ceil(points*dpi/72)`. The OCR adapter hands those pixels to the existing `ocr::Engine::run(ImageView)`; OCR quads are scaled once from that exact bitmap to canonical coordinates. | Default caps: 20,000 per dimension, 80,000,000 pixels, 320,000,000 bytes. Oversize pages fail rather than silently downscale. Annotations/form values are initially excluded. |
| S1-06 | Auto accepts native text when it has visible Unicode scalars, fewer than half are replacements, fewer than half are unexpected controls, and no specific omission evidence is found. A page-covering image (at least half the area) plus native text in less than one quarter of page height is omission evidence and triggers OCR verification. | These thresholds are versioned heuristics from the generated mixed-page fixture, not calibrated probabilities. Sparse text alone does not force OCR. Image-object coverage misses some forms of graphics and cannot prove completeness. |
| S1-07 | With both sources usable, prefer native unless omission evidence exists and OCR has more than twice its visible **Unicode scalar** count. Preserve failed OCR/native fallback as `Degraded`; never concatenate candidates. | Material disagreement remains visible as a degraded outcome. No hybrid region merge. OCR confidence retains the backend's mean-CTC interpretation. |
| S1-08 | `EmbeddedOnly` never creates an OCR engine. `OcrOnly` requires model paths and OCR-enabled build. OCR starts lazily and is reused across batches; per-call attempt allowance counts started OCR calls, while Engine will own cross-call budgets. | Model/file absence is a top-level configuration error in OcrOnly. Runtime inference failure and raster limits remain per-page attempts; corrupt-but-present model files are currently detected when the first OCR attempt starts. |
| S1-09 | Read viewer page labels and supported local GoTo destinations mechanically through PDFium in request order. `Present`, `Absent`, `Unsupported`, and `Failed` are distinct fact states. | No printed-footer inference, external action interpretation, TOC logic, or outline inspection. |
| S1-10 | Build S1 as an opt-in target in the existing OCR root and as an independent `subsystems/text/` entry point for OCR-disabled native extraction. Preserve `text/` as a forwarding build entry. The installed package exports `pdfbookmark::Text`; no backend headers enter its public contract. | OCR-enabled static consumers still need transitive PDFium/OCR/ORT/OpenCV at final link. The standalone native build has no OCR/OpenCV/ORT requirement. No single-executable product packaging is claimed for this S1 library. |
| S1-12 | (v2) A character that PDFium flags with `FPDFText_IsHyphen` (a hyphen at a line break, which PDFium returns as a control code) is emitted as U+00AD SOFT HYPHEN and counted in an informational reason, not as an unexpected control character. | Found on a real TeX-produced book, where every page with a hyphenated line had been marked suspect. |
| S1-13 | (v2) An unknown UserUnit stays `null` with its informational reason, but no longer makes a page `Degraded`. The parser cannot read compressed object streams (common in modern PDFs) and PDFium has no getter, yet physical scale does not affect text, positions or page-relative geometry. | Physical size remains unknown for such PDFs. |

The root folder decision for all five domain ownership boundaries is recorded in `docs/ARCHITECTURE.md`. At the S1 gate, the four then-unimplemented folders contained scope/status manifests only; their later implementation is tracked in the S2–S4 sections and progress record.

The exact OCR model and charset SHA-256 values, PDFium SDK identity, fixture digests, and verification commands are in `docs/handoffs/S1_HANDOFF.md`.

## 2. S2 TOC Detection

Policy identity: `s2-toc-detection-v2` (v2, 23 Sep 2026, adds S2-08; v1 was the edition 2.0 policy). These numbers are conservative initial choices exercised against the pure-value counterexamples and the generated S1-acquired TOC PDF. They rank evidence; they are **not** calibrated probabilities.

| ID | Choice and reason | Explicit limit or uncertainty |
| --- | --- | --- |
| S2-01 | A page needs at least 3 title/reference-like rows, aligned-row density at least 0.45, and score at least 7. Reference right edges form a column when within 18 canonical points. Each column needs at least 2 rows. This prevents a heading or a few standalone numbers from qualifying. | Page score is `2*aligned_rows + min(dot_leader_rows,3) + 2*density + (Contents heading ? 2 : 0) - (excluded heading ? 8 : 0)`. An explicit Index, Glossary, List of Figures, or List of Tables heading is also a hard veto. A dense list without a recognizable heading can remain a false positive. |
| S2-02 | The detector recognizes a *pattern*, not an authoritative reference: a title followed by a decimal token of up to 4 digits, a one-to-three-letter prefix plus hyphen and digits, or an ASCII Roman token of up to 8 letters. A run of at least 3 dots is a leader cue. The title needs at least 2 letter-like Unicode scalars, counting non-ASCII UTF-8 leading bytes once. ASCII classification is locale-independent. | Range references, Unicode digits and other number syntaxes may be missed. Non-ASCII punctuation can still count as letter-like for this coarse cue. Reference meaning and final title splitting belong to S3/S4. |
| S2-03 | Inspect positioned regions directly, ignoring `flat_text()` order. Pair a reference-only region with the nearest left title region at a comparable y center (within the greater of 4 points or 0.6 of the larger region height; horizontal gap no more than 0.35 page width). Collapse duplicate row observations within 3 x-points and 2 y-points. This handles separate native runs and two columns. | When one S1 region contains multiple newline-separated lines, divide its box evenly to estimate line bands. That is a detection feature, not a claim of precise native per-line geometry. |
| S2-04 | Only short fragments (at most 48 bytes) in the top quarter of the page provide heading cues. `Contents` is optional positive evidence; Index/Glossary/Lists are negative only when the fragment is a heading rather than a row with a reference. | English heading words are recognized; non-English TOCs still qualify through repeated row geometry, but cannot gain a heading bonus. |
| S2-05 | Group adjacent candidate pages when a normalized reference-column right edge agrees within 0.08 of page width. Permit at most one intervening page only when that exact physical page was supplied but unassessable; record it as an interruption with its failure reason. Never bridge an unsupplied page or a supplied non-TOC page. | Adjacent TOCs with similar column geometry may still need downstream separation. Candidate IDs are `toc-p<first physical index>-r<first revision>`; ordering is physical-index deterministic. |
| S2-06 | Group score is mean page score plus 0.5 per additional candidate page minus 1 per recorded interruption. A missing neighbor at a supplied batch edge means `MayContinue`; an unassessable adjacent page means `Unknown`; an assessed noncandidate or separate candidate means `Closed`. Physical index zero closes the start. | S2 cannot know the total document page count from its supplied values, so the final supplied page never proves the end of a TOC. Engine decides whether to acquire neighbors. |
| S2-07 | Reject duplicate physical indices and selected-content index/revision mismatches before detection. Return every supplied page as `Candidate`, `Rejected`, or `Skipped`, and retain row region references with S1 revision IDs. | S2 never opens a PDF, invokes S1, or fabricates missing pages. A candidate is a hypothesis for S3, not parsed entries or resolved destinations. |
| S2-08 | (v2) **Wide-gap pairing:** a separate reference region more than 0.35 page width right of its nearest title may still pair, up to 0.8 page width, when (a) the title fragment is not itself a complete title-plus-reference row and (b) no other fragment on the same row band lies between them. **Density per visual line:** aligned rows are divided by visual lines, not raw fragments. A paired title and reference region, or duplicate observations of one row, count as one line. Found by Engine A9: right-aligned single-digit numbers after short leaders made S2 reject a real TOC page (title-number gap of about 400 points, and 4 rows / 9 fragments = 0.44 density). S2 then closed the neighbouring candidate's start, so Engine produced a confident plan with half the TOC. Regression fixture: `tests/engine/fixtures/boundary_short_leaders.pdf` (SHA-256 `1dbc1759…15439ae`). | Two-column counterexamples are covered: bare right-column numbers next to complete left rows, and a left row's own number intervening. A title with no number on its own row can still pair with a far number on the same row if nothing lies between them; real-book calibration remains A11. |

## 3. S3 TOC Parsing

Policy identity: `s3-toc-parsing-v1`. These are deterministic parsing heuristics exercised by pure fixtures and one actually acquired generated TOC. They are not probabilities or physical-page mapping rules.

| ID | Choice and reason | Explicit limit or uncertainty |
| --- | --- | --- |
| S3-01 | Accept only a nonempty candidate with strictly increasing physical page indices and nonzero revisions. Validate each supplied page's selected index/revision, unique region IDs, and candidate row-evidence IDs. A missing selected candidate page produces `missing_pages` and an incomplete parse; a stale revision is an argument error. | Parsing does not acquire missing pages. S2 interruptions and limitations pass through to diagnostics; degraded evidence makes completeness incomplete. |
| S3-02 | Use canonical positioned S1 regions. Split explicit newlines and divide a multiline region's bounding height equally for row approximation. Group fragments whose y centers differ by at most 5 points, then order by x. Infer two columns only when two right-edge clusters each have at least 2 reference-like rows and are separated by at least 0.25 page width. Right edges within 18 points share a cluster; the right lane begins beyond the left cluster by 0.02 page width. | Native multiline bounding boxes do not establish precise per-line geometry. Narrow columns, irregular right edges, or interleaved layouts may remain a single column; those require additional fixtures before changing v1. |
| S3-03 | A trailing token is a reference when it is decimal (up to 8 digits), canonical ASCII Roman (up to 15 letters, ordinal 1–3999), or one-to-three ASCII letters plus hyphen plus decimal. Same-style/prefix ranges with a hyphen or en dash preserve the literal and expose start/end ordinals. Reversed ranges are marked uncertain. A trailing separator of at least 3 dots allows an unsupported final token to remain an uncertain literal; internal title ellipses do not. | Unicode digits, mixed-style ranges, bare page suffixes attached without a leader, and larger ordinals are unsupported. A standalone `PART`/`CHAPTER`/`SECTION` or all-capitals row of at most 38 bytes may be a no-reference heading. Recognition of a printed ordinal never identifies a PDF page. |
| S3-04 | Strip only a trailing run of at least 3 leader dots and its separator spaces; retain internal punctuation such as `Version 2.0`. A no-reference row at least 18 bytes long may join the immediately following referenced row when left margins differ by at most the 16-point child-indent threshold and the line gap is at most 1.8 line heights. Across adjacent pages, require the first row to end in the bottom quarter and the second to begin in the top quarter. | Byte length is a coarse wrap cue, not a Unicode character count or semantic proof. Cross-page continuation can still be ambiguous. Unsupported rows remain `UnparsedFragment` with source references rather than disappearing. |
| S3-05 | The minimum left margin in each column is its root baseline. An entry within 8 points is a known root. An indent of at least 16 points deeper than a preceding entry in the same column supports that entry as parent; 8–16 points or no qualifying predecessor leaves `Unknown`. Search backward within that column only. | Font, weight and explicit numbering hierarchy are not available in current S1 values. Indent-based parent inference can be wrong in elaborate layouts; unknown parentage makes the parse incomplete. No synthetic parent or flat-tree policy is applied. |
| S3-06 | IDs combine candidate ID, physical page/revision and entry order; duplicate titles therefore stay distinct. Entry sources retain region IDs and UTF-8 byte ranges. Preserve candidate boundary state and mark the parse complete only if both boundaries are `Closed`, all candidate pages are available, no interruptions/degraded evidence/unsupported fragments remain, and all entry references and hierarchy are certain. | IDs are analysis-local, not stable across reacquisition/revision changes. An entry with an exact printed reference can still be unmappable by S4; parse completeness says nothing about destination confidence. |
| S3-07 | (v2) A row that is only a page-number token (decimal of up to 4 digits, or Roman) in the top 10% or bottom 15% of a TOC page is that page's own printed number, not a TOC row. It is skipped with a diagnostic instead of becoming an unparsed fragment. | Found on a real book, where the TOC pages' "vii"–"xi" made every parse incomplete. |
| S3-08 | (v2) Dotted section numbers are hierarchy evidence: "2.1" is placed under the nearest preceding entry titled "2 …", and "A.2.1" under "A.2 …". This fills unknown parents and overrides a conflicting indentation guess, with the reason recorded. Plain numbers ("2 …") are not forced to top level, because a TOC may nest chapters under un-numbered parts. | Found on a real book, where 35 sections had indents between the root and child bands. |

## 4. S4 Page Mapping

Policy identity: `s4-page-mapping-v2` (agent edition 2.1; v1 under edition 2.0 let viewer labels and associated links resolve without content corroboration). All mapping inputs are caller-owned values. In particular, a supplied numbering section is explicit evidence or a user setting; S4 does not silently discover every restart. Methods and scores are not probabilities.

| ID | Choice and reason | Explicit limit or uncertainty |
| --- | --- | --- |
| S4-01 | Use zero-based physical indices and half-open, nonoverlapping `NumberingSection` ranges. An entry with repeated compatible numbering needs an explicit `EntrySection` association; otherwise the alternatives remain distinct. The offset equation is `pdf_index = printed_ordinal + offset`. | No automatic segmentation of restarts or inserted-page patterns. Reject stale S3 source revisions when the same physical page is supplied under a different S1 revision. |
| S4-02 | Interpret a standalone decimal, canonical Roman, or one-to-three-letter prefixed token from a positioned region in the bottom 15% of a page as a printed-footer observation. Decimal tokens are limited to 8 digits; Roman to 1–3999 and 15 letters. An exact title match in the top 25%, after ASCII case and whitespace normalization, is heading evidence; TOC source pages are excluded from target-heading matches. | These positional bands and token forms are conservative v1 fixtures, not a universal footer detector. Unicode case folding, decorated footers, mixed numbering within a region, and arbitrary heading variants remain unsupported. |
| S4-03 | An inferred offset needs at least two agreeing **distinct physical pages** within one known section, with no conflicting checked offset. Anchors come only from acquired content (printed footers, entry headings); a footer and heading on the same page count once, and viewer labels never count. By default the predicted target must itself have a matching printed footer or exact heading in acquired content (a viewer label does not confirm); otherwise return Unresolved and request that page if not already supplied. | Two anchors do not prove every intervening page sequential. `require_target_confirmation=false` is an explicit caller opt-out and is stated in the result reason. Conflicting observed offsets produce alternatives or an unresolved conflict, never a fabricated choice. |
| S4-04 | Viewer labels are observed separately from printed footers and are untrusted hints (edition 2.1). They never count as offset anchors. A label match is considered only when the section explicitly sets `viewer_labels_match_printed` and facts cover every page in that section, and it resolves only when the labelled page's acquired content corroborates the entry (exact heading, or a printed footer equal to the reference). Otherwise it remains an unverified alternative, with a one-page request when the page is not supplied. Repeated labels never auto-resolve. | PDF viewer labels can be unrelated to visible numbering. Missing, failed, and unsupported S1 facts are not treated as proof of label absence. A supplied page whose content does not match leaves the entry unresolved without a repeat request. |
| S4-05 | A local link is a candidate only when the caller supplies an `AssociatedLocalLink` tied to one of that entry's S3 source regions and S1 facts confirm that destination on the source page; destination bounds are validated. Per edition 2.1 it resolves only when the destination page's acquired content corroborates the entry (exact heading or matching printed footer); supporting sources then include both the link source and the confirming region. Uncorroborated links stay unverified alternatives and produce a priority-3 one-page request; if another method resolves elsewhere, the disagreement is kept in reasons. | S1 currently exposes page-level local destinations, not region-to-link geometry; S4 cannot invent an entry association from page-level facts. External actions are not local evidence. |
| S4-06 | Explicit per-entry destinations take precedence over all automatic evidence. Explicit section offsets resolve only inside their section. Both override forms require origin text; malformed, duplicate, or out-of-range choices fail the request. Contradictory observed anchors, links, and overlapping manual choices remain visible in reasons. | Manual choices are reported as `ManualEntry` or `ManualOffset`, never as inferred/verified. A valid range uses its start only if both start and end remain in the section; invalid ranges stay unresolved. |
| S4-07 | Evidence requests are one-page, half-open ranges with stable IDs and purpose/priority. Sort by priority and cap at 8 requests by default; no request widens work or executes acquisition. | If an already supplied target has no usable confirmation, repeating its acquisition is not automatically requested. Engine owns follow-up scheduling, deduplication and budgets. Some cases cannot identify a useful bounded next request. |
| S4-08 | Existing PDF outline bookmarks are excluded from mapping evidence (rule 13, section 1.4). `DocumentEvidence` has no outline field, S1's facts facade exposes only viewer labels and page-link destinations, and no S4 code reads outlines. Verified by the paired acquired fixture: identical page content with and without a misleading three-item outline yields identical S1 facts availability, S2 candidate, S3 entries and S4 mappings. | The paired check covers one generated native-text PDF; Engine still owns the full paired-PDF end-to-end gate (section 9.5). |
| S4-09 | (v3) Printed page numbers are read in both margin bands: the bottom 15% and the top 10%. A candidate is a region that is only the token, or, in the band's outermost line only, a running header or footer whose first or last word is the token ("Modern processors 3", "4 Introduction…"). If the word before a trailing number is a label (chapter, part, section, appendix, figure, table, page, …), it is not a page number. A band counts only if it has exactly one candidate (figure axes and code line numbers are ignored), except that a running-form outermost line wins over bare numbers deeper in the band. Printed 0 is never a page number. | Found on a real book with numbers in its running headers, whose footer-only evidence had produced false offsets from chart axes. |
| S4-10 | (v3) With per-entry target confirmation (the default), a dominant offset is accepted when at least 3 pages and at least 80% of the section's anchor pages support it; the minority pages are listed in the reasons as ignored outliers. Without confirmation, any contradiction still blocks resolution. The inserted-page fixture (3 against 1, 75%) stays unresolved. | Target confirmation is what makes this safe: a wrong offset cannot resolve an entry whose target does not show the expected number or heading. |
| S4-11 | (v3) Headings match across up to 3 nearby lines and may follow a "Chapter" label ("Chapter 9" / "Distributed-memory …" / "with MPI" matches "9 Distributed-memory … with MPI"). Titles that start with a number match exact heading lines anywhere on the page; others match only in the top band. | "Appendix A" labels are not a heading variant yet; they resolve through page numbers. |

## 5. S5 Bookmark Writing

Guide: agent edition 2.1. The plan contract and validator are backend free (`pdfbookmark::WriterPlan`); qpdf 12.3.2 is private to `pdfbookmark::Writer`.

| ID | Choice and reason | Explicit limit or uncertainty |
| --- | --- | --- |
| S5-01 | Split into a backend-free plan/validator target and a qpdf writer target, so Engine and other clients can validate plans without qpdf. The installed config looks up qpdf with `QUIET`, so plan-only consumers need no qpdf package. | A static `Writer` consumer must still supply qpdf at the final link (`qpdf_DIR`). |
| S5-02 | Shared contract: append `Unsupported`, `OutputExists`, `OutputWrite`, `Cancelled` to Core `ErrorCode`. Structured plan issues come from `validate()`; `write_copy` summarizes up to five in its error message. | Error carries only code and message; callers wanting all issues call `validate()` themselves. |
| S5-03 | Read the input into memory once, hash it with qpdf's public `QPDFCryptoProvider` SHA-256, and parse the same bytes (`processMemoryFile`). This binds the write to the exact bytes the plan names, without copying S1's private hash code. Rehash the file immediately before commit. | 512 MiB default input cap. The final rehash narrows but cannot eliminate a race with external writers. |
| S5-04 | Protect the input: reject an output that `std::filesystem::equivalent` identifies as the input (path spellings, case, hard links), before writing and again just before commit, even with replacement enabled. Treat an unverifiable relationship as an alias. Existing outputs are refused unless `replace_existing_output` is set. | Symlink aliases rely on `equivalent` and are untested. Network or path-length edge cases are not tested. |
| S5-05 | Unsupported for writing: encrypted input (including empty-user-password), password-protected input, and signed input (`/Perms`, AcroForm `SigFlags` bit 1, or any `/Sig` field, including widget-less and inherited `/FT`, that has a dictionary `/V`). `QPDFAcroFormDocumentHelper::getFormFields` missed a widget-less signature field, so S5 walks `/Fields` and `/Kids` directly. | Structural detection only; no cryptographic validation. |
| S5-06 | `replace_in_copy`: set `/Outlines` in the copy to a new tree built from the plan and leave old items unreachable. Items are closed (item `Count` = −direct children, root `Count` = top-level items), use `[page /Fit]` destinations and `newUnicodeString` titles. Write with a deterministic `/ID` for reproducible output. | No open or expanded state, zoom or XYZ destinations, colors or styles. The output is a qpdf rewrite, not an incremental update. |
| S5-07 | Transaction: serialize in memory, exclusively create and flush a temporary sibling, reread it and require byte equality, reparse it and verify the page count and every item's title, links, counts and destination. Then check the input digest and cancellation, and commit with `MoveFileExW` (no-clobber unless replacing, `WRITE_THROUGH`). A temp guard removes the temporary file on any failure. Cancellation is honored only before commit; afterwards it is reported as a diagnostic. A private fault hook (`src/writer/writer_testing.hpp`) injects temp corruption, write failure and commit failure in tests. | POSIX commit (link+unlink/rename) is compiled only in principle and unverified. A killed process can leave a `*.pdfbookmark-*.tmp` file. |

## 6. Engine, CLI and library

| ID | Choice and reason | Explicit limit or uncertainty |
| --- | --- | --- |
| E-01 | The old sibling-repo `inspect_file()` is superseded, not ported. Its text extraction and quality heuristics are S1's responsibility; Engine only calls S1 through one persistent session. The sibling repository is left untouched. | The old tool's per-page image-coverage printout has no CLI equivalent beyond S1's `image_object_count` and assessment reasons. |
| E-02 | Engine owns a run-wide OCR budget (default 64) across S1 calls in 8-page batches, debiting non-skipped OCR attempts. Per-page configuration indices keep S1 identities traceable, because they embed the per-call allowance. | Defaults are provisional until A9 fixes finite analysis-wide budgets. |
| E-03 | Engine carries its own strict JSON parser and writer instead of adding a dependency (the repository has none). Numbers keep their lexeme for checked conversion; output is deterministic in insertion order, with fixed rounding for geometry. | Assumes the "C" numeric locale. Not a general-purpose JSON library. |
| E-04 | CLI: one-based page selection converted once. JSON goes to stdout or a file (no clobbering without `--force`; never the input); progress and summaries go to stderr; `wmain` for Unicode paths. Exit codes are 0/1/2/3/4 = complete/failed/usage/partial/cancelled. | `analyze` and `apply` are not implemented yet (exit 2). Engine and CLI have no install rules until A11. |
| E-05 | Finite defaults: first 40 pages, then batches of 20, up to 200 searched pages; up to 60 evidence pages and 6 S4 rounds; 64 OCR attempts; all configurable. Search extends only when there is no candidate yet or a candidate may continue at the searched edge. Pages already acquired are kept and never re-acquired, so revisions stay stable. | Not calibrated on real books. A TOC after page 200 is reported as not found in the search. |
| E-06 | Automatic candidate choice requires the best score to exceed the runner-up by more than the 0.9 tie ratio; otherwise explicit selection is required. An explicit ID must match a detected candidate. | Scores are S2 rankings, not probabilities. A short TOC and a detailed TOC with similar scores always need a person to choose. |
| E-07 | Without caller sections, Engine assumes one decimal `body` section after the selected TOC (or before it when the TOC is last) and records that assumption everywhere. S4's two-anchor plus target-confirmation rule still guards every inferred destination. | Roman, prefixed and restarted numbering stay unresolved without explicit sections or overrides; the CLI does not yet accept them. |
| E-08 | Readiness separates S3's single completeness flag into entry-level causes (unknown hierarchy, uncertain reference), which are handled by the hierarchy policy and mapping, and document-level causes (unparsed fragments, missing pages, unclosed boundaries, unexplained), which need `allow_partial`. Without this split, `flat_outline` could never succeed (found by test 7). | Depends on S3's documented completeness rules (S3-06). |
| E-09 | Plan files are written only when ready. The analysis report embeds a draft for inspection, and strict plan decoding rejects a report passed as a plan, so a draft cannot be mistaken for a ready plan. | No separate draft-plan export. |
| E-10 | `apply` is a thin Engine facade: strict plan decoding, then S5 `write_copy`. Engine adds only a plan-file size cap and the rule that the output is never the plan file. Engine links `pdfbookmarkWriter` (qpdf stays private to S5); the root Engine block finds qpdf again only to stage runtime DLLs and for the qpdf-based verification test. | End-to-end correctness is proven on generated native-text fixtures only; real books remain A11. |
| E-11 | The run's OCR model identity is the first real identity S1 reports. S1 returns `not-used` for batches without OCR (models load lazily), which is neutral; a warning is emitted only if two different real identities appear. Found on a real book, where the report said `not-used` although OCR had run. | Relies on S1's `not-used` sentinel (src/text/acquisition.cpp). |
| E-12 | Test-only seams (the S1 fake-OCR factory and S5 write fault injection) compile only with `PDFBOOKMARK_TEST_HOOKS=ON` (default for development, and needed by `text_policy` and the writer fault tests). Release packages build with it OFF. Removed the never-produced `AnalysisOutcome::Failed`. Validation issues of an already-blocked draft are no longer repeated as blockers but are serialized as `plan.validation_issues`. | Developers must keep the option ON to run the full suite. |
| E-13 | The end-user package is an allow-list install component (`app`): the executable, the runtime DLLs it actually resolves, only the imported MSVC runtime DLLs, the 3 model files OCR reads, the drag-and-drop scripts, the user README and licence notices. OpenCV INFO logging is quieted unless the user set `OPENCV_LOG_LEVEL` (moved into the library by E-19). | It is a portable folder, not a single executable (the §10.3 goal is still open). |
| E-14 | Distribution is a CPack ZIP (`pdfbookmark-<version>-win64.zip`) of the `app` component, with a per-user PowerShell installer that puts the program on the user `PATH` (no admin rights, registry value type preserved) and a matching uninstaller. `pdfbookmark --version` reports `PDFBOOKMARK_VERSION`. There is no MSI/NSIS installer because no installer toolchain is installed; WiX could be added later without changing the package contents. | Not code-signed; SmartScreen may warn on first run. Windows x64 only. |
| E-15 | Default sections for real books: after a first mapping pass with the assumed decimal body, if the TOC cites Roman numbers and exactly one decimal offset was inferred, Engine assumes Roman front matter on physical pages [0, printed 1) and the decimal body from printed 1, then maps again (recorded). Engine now lets S4 request up to 500 confirmation pages (`max_requests` 500, `max_evidence_pages` 500), because every entry's target must be confirmed. | Mixed or restarted numbering still needs explicit sections. |
| E-16 | CLI `add` = analyze plus apply in one run (default output `<name> (bookmarked).pdf` next to the input; optional `--plan`/`--report`). `apply` without `--plan` explains that it needs a plan and suggests `add`. Long blocker and choice lists are capped at 12 lines, with the rest in the JSON report. The drag-and-drop script now calls `add`. | Found when a user ran `apply` on a book without a plan. |
| E-17 | When a parse is incomplete only because of degraded TOC pages, the blocker names those pages and S1's reasons instead of "other evidence limits". | |
| E-18 | Bookmark title style is a plan-assembly policy (`--titles printed|chapter`, default `printed`). `chapter` renames only top-level nodes: "N Title" becomes "Chapter N: Title", and "X Title" becomes "Appendix X: Title" only when X has "X.n" child sections or directly follows a recognised appendix letter, so a chapter titled "A Short History" is never renamed. Parsed entries and evidence keep the printed text; the change is recorded as a plan choice. | English labels only. |
| E-19 | The project is also a library. One shared `pdfbookmark.dll` (all subsystems plus Engine; backends private; `WINDOWS_EXPORT_ALL_SYMBOLS`) is behind `PDFBOOKMARK_BUILD_LIBRARY`. Its stable API is the single header `<pdfbookmark/pdfbookmark.hpp>` (namespace `pdfbookmark`: `version`, `find_models`/`models_in`, text, analyze, apply, plan editing, validation and JSON, metadata), documented in `docs/API.md`. The CLI includes only that header and links only that target. The SDK is a CMake package (component `sdk`) with Release and Debug (`d` postfix) builds, the dependency DLLs per configuration, the models, and a `pdfbookmark_deploy_runtime()` helper that runs on every client build. The library, not the CLI, quiets OpenCV's INFO logging through OpenCV's API when OCR first starts, unless `OPENCV_LOG_LEVEL` is set; setting the environment variable from our code came too late for Debug OpenCV. `onnxruntime.dll` is installed explicitly (Windows 11 ships an unrelated one in System32). A shared library was chosen over a static one: the static closure (PDFium, qpdf, ONNX Runtime, OpenCV) cannot be shipped as one archive, and those backends are available only as DLLs here. | The C++ ABI (std types across the DLL) requires MSVC x64 with `/MD` and a matching configuration. The C API (E-20) has no such restriction. Windows x64 only. |
| E-20 | C API `<pdfbookmark/pdfbookmark.h>` (`pdfb_*`, `PDFB_C_API_VERSION` 1) in the same DLL, implemented in `src/engine/c_api.cpp` as a thin layer over the C++ API. It has fixed-number status codes, a thread-local `pdfb_last_error()`, UTF-8 strings and paths, JSON options with strict keys (named like the CLI flags), and JSON results in the CLI's formats, freed with `pdfb_free()`. It also has opaque cancel tokens and a plain progress callback. No C++ exception crosses the boundary: every entry point converts failures to a status and sets outputs to NULL. Detailed data travels as the existing versioned JSON instead of C structs, to keep the ABI small and stable. `examples/python/pdfbookmark.py` is a single-file ctypes wrapper. Tests: `library_c_api` (compiled as C11) and `library_python`. | Verified with MSVC-built C and with Python. No other C compiler was available to test. |

## 7. S6 Document Metadata Extraction

S6 decisions are recorded with its contract in `docs/handoffs/S6_HANDOFF.md`.

## 8. Project and repository changes (from 24 September 2026)

### E-21 Git repository and reproducible dependencies (24 Sep 2026)

- **Change:**
  - The project is now a Git repository, pushed to the private GitHub repository `PantaKoda/PDFMegine`.
  - Dependencies are pinned in `cmake/PdfbookmarkDependencies.cmake`. PDFium 155.0.8057 (`bblanchon/pdfium-binaries`), ONNX Runtime 1.30.0 and OpenCV 5.0.0 are downloaded from their official releases, with SHA-256 checks, into `.deps/`.
  - qpdf 12.3.2 comes from a vcpkg manifest (`vcpkg.json`, with the baseline in `vcpkg-configuration.json`).
  - The OCR models are one ZIP on this repository's `models-v1` release, downloaded into `models/` with a SHA-256 check.
- **Why:**
  - The build used to depend on folders scattered over one PC (a Downloads folder, a sibling repository, a Desktop folder), so nobody else and no CI machine could build it.
  - Prebuilt packages were chosen over building PDFium, ONNX Runtime and OpenCV from source. The downloads are **byte-identical** to the binaries every test and the OCR parity were verified with, so nothing about behaviour changes. OpenCV 5.0 is also not available in vcpkg.
  - qpdf uses vcpkg because the same vcpkg baseline reproduces the exact build used so far, and vcpkg also works on Linux and macOS.
  - The models (139 MB) cannot go into plain Git: GitHub warns at 50 MB and blocks files over 100 MB. Release assets have no bandwidth quota, unlike Git LFS's free tier.
- **Assumptions:**
  - Developers use Windows x64 with Visual Studio 2026 and its bundled vcpkg (`VCPKG_ROOT` set by the developer shell).
  - The upstream release files stay available at their URLs. If one disappears, the checksum still identifies the right file, which can be passed as a local copy.
  - The repository stays private, so the model download needs credentials (GitHub CLI login or `GH_TOKEN`). If it becomes public, the token becomes optional, and the code already works without one for public releases.
- **Removed:** the dependency paths into the old `pdfbookmark` repository, `Downloads/opencv` and `Desktop/Dev/onnxruntime`.
- **Verified:**
  - The downloaded PDFium and ONNX Runtime trees and the extracted OpenCV tree are identical to the old local copies (recursive `diff`). The OpenCV installer's SHA-256 matched the official release.
  - A clean `dev` build passed all 22 tests, including `golden_parity` (the OCR parity against the PaddleOCR reference).

### E-22 Build presets (24 Sep 2026)

- **Change:** `CMakePresets.json` now has three presets, `dev`, `release` and `sdk-debug`, plus matching build and test presets. They use the vcpkg toolchain and `VCPKG_APPLOCAL_DEPS=OFF`.
- **Why:**
  - Builds needed about 20 hand-typed `-D` options.
  - Presets make the documented commands, CI and local builds identical.
  - vcpkg's per-executable DLL copying is off because the project copies runtime DLLs itself (E-23). Two mechanisms would duplicate work and race.
- **Assumptions:** CMake 3.25 or newer (Visual Studio 2026 ships 4.x).
- **Removed:**
  - The Visual Studio template presets `x64-debug`, `x64-release`, `x86-debug` and `x86-release`. The dependencies exist only for x64, so the x86 presets could never build.
  - The template's `SegmentHeap.cmake` include. It depends on a Visual Studio IDE environment variable (`VSINSTALLDIR`) and is not needed by the project.
  - The test filter that excluded `model_info`. That test passes and had no recorded reason to be excluded.

### E-23 One runtime-DLL copy step (24 Sep 2026)

- **Change:** the backend DLLs (PDFium, ONNX Runtime, OpenCV, qpdf and its dependencies) are copied into the build root by one target, `pdfbookmark_runtime_dlls`. Every executable that runs from the build root depends on it through `pdfbookmark_use_runtime()`.
- **Why:** the first clean build from scratch failed. Several test executables copied the same `opencv_world500d.dll` in parallel `POST_BUILD` steps, and Windows refused the concurrent writes ("permission denied"). Builds had only worked before because the DLLs were already in place.
- **Assumptions:** all such executables share the build root as their output directory (true today; the unified library sets `RUNTIME_OUTPUT_DIRECTORY` to it).
- **Removed:** eleven per-executable `POST_BUILD` DLL copies.

### E-24 Local-only OCR reference data (24 Sep 2026)

- **Change:** `golden/` (2.3 GB of PaddleOCR reference outputs), `PaddleOCR/` (2.1 GB upstream checkout) and `.venv/` are not in Git. `golden_parity` is registered only when `golden/` exists.
- **Why:** they are far too large for Git and can be regenerated with `tools/generate_golden.py` (see `docs/ocr/README.md`). The parity test is still run locally whenever the OCR path or its dependencies change.
- **Assumptions:** OCR code and dependencies change rarely. When they do, the change is verified on a machine that has `golden/`, and CI runs every other test.

### E-25 Documentation layout (24 Sep 2026)

- **Change:**
  - The root `README.md` is now the project's front page and documentation index.
  - The OCR package's `README.md` and `DECISIONS.md` moved unchanged to `docs/ocr/`, with relative links fixed.
  - `ARCHITECTURE.md` moved to `docs/`.
  - The S1-S6 and Engine handoff records moved to `docs/handoffs/`.
  - `docs/PACKAGING.md` became `docs/BUILDING.md`, which now also covers requirements, presets, dependencies and tests.
  - This file got a title and a reading guide.
  - Personal paths (user folders, the local book folder) in historical verification records were replaced by placeholders such as `<repo>` and `<PDFium package>`.
- **Why:** the owner asked for documentation that is organised, not scattered.
  - The root had four documents, two of which described only the OCR package.
  - `docs/` mixed guides with seven handoff records.
  - Personal paths should not be in a shared repository.
- **Assumptions:**
  - `AGENTS.md` (the owner's specification) is left unchanged, although it names the OCR package's `README.md` and `DECISIONS.md` as root files. Their new location under `docs/ocr/` is treated as an "established equivalent" in the sense of `AGENTS.md` §12.5.
  - `docs/IMPLEMENTATION_PROGRESS.md` and this file keep their names because `AGENTS.md` refers to them.

### E-26 What is not published (24 Sep 2026)

- **Change:** `.gitignore` keeps out:
  - build output, `dist/`, IDE state, downloads and models;
  - local OCR reference material (E-24);
  - personal test books (`tests/books/**/*.pdf`);
  - the owner's design document `pdfbookmark-final-human-design.docx`.

  The repository has no licence file yet.
- **Why:**
  - Books are copyrighted personal files.
  - The design document is superseded by `AGENTS.md`, and the owner had not asked for it to be published.
  - A licence is the owner's decision.
  - Anything pushed stays in the Git history even if deleted later, so leaving out is the safe default.
- **Assumptions:** the repository is private. Before it is made public, the owner chooses a licence (Apache-2.0 fits all dependency licences) and reviews the history.

### E-27 Line endings and binary files in Git (24 Sep 2026)

- **Change:** `.gitattributes` normalises text files to LF in the repository. It marks PDFs, images, models and archives as binary (never converted) and keeps `*.cmd`/`*.bat` as CRLF.
- **Why:**
  - Git on this machine converts line endings (`core.autocrlf=true`), and the generated test PDFs are ASCII-only, so Git would treat them as text. Converting their LF to CRLF on checkout would shift every byte offset in the PDF (the cross-reference table) and change their recorded SHA-256 digests.
  - `cmd.exe` mis-parses batch labels in files with LF line endings.
- **Assumptions:** no test compares a text file (source, JSON, CMake) byte for byte across line-ending styles. A fresh clone's full test run checks this.

### E-28 GitHub Actions CI and release workflow (24 Sep 2026)

- **Change:**
  - `ci.yml` builds the `dev` preset and runs all tests on `windows-2025-vs2026` for pushes to `main`, pull requests, and on demand.
  - `release.yml` builds both packages on a `vX.Y.Z` tag, smoke-tests the packaged CLI, and publishes a GitHub Release. A manual run is a dry run that keeps the ZIPs as 1-day artifacts.
- **Why:** the owner asked for CI/CD that produces the libraries and packages. It also proves that every commit builds from a clean machine, which a developer PC cannot show.
  - Windows only: the pinned dependencies exist only for Windows x64 so far. Linux and macOS jobs come with the port.
  - Documentation-only pushes skip CI, and a newer push cancels an older run, because the private repository's free minutes are limited (Windows counts double).
  - The runner with Visual Studio 2026 matches local builds, so `/W4 /WX` sees the same compiler warnings.
  - The only third-party action, `ilammy/msvc-dev-cmd` (sets up the MSVC environment), is pinned to a full commit SHA, so a moved tag cannot change what runs. GitHub's own actions use major-version tags.
  - Release ZIPs go to GitHub Releases, not workflow artifacts. Private repositories get only 500 MB of artifact storage, and the two ZIPs take about 330 MB.
- **Assumptions:**
  - The runner label `windows-2025-vs2026` keeps existing, with Visual Studio 2026.
  - The workflow `GITHUB_TOKEN` can read this repository's release assets (true for `contents: read`).
  - The tag is the release trigger, and `PDFBOOKMARK_VERSION` in `CMakeLists.txt` stays the single source of the version number. The workflow fails if they differ.

### E-29 Client documentation bundle and Qt Quick example (24 Sep 2026)

- **Change:** the SDK now ships, next to `API.md`:
  - `AGENTS.md`, a one-page brief for coding agents that build *client* applications (source `docs/AGENTS_SDK.md`);
  - `JSON_FORMATS.md`, every JSON document with all enum values;
  - `examples/qt-quick`, a Qt 6 QML application with a `QML_ELEMENT` controller, worker thread, progress, cancellation, editable titles, writing the new PDF, and a headless `--selftest` mode.

  CI installs the SDK from its `dev` build, installs Qt 6.9.3 (`jurplel/install-qt-action`, pinned to a commit), builds the example against the installed SDK, and runs the self-test.
- **Why:**
  - The owner will build a Qt Quick front end and wants agents working on it to have the right documentation bundled.
  - Client agents need how-to-use rules, not the library's internal design. `AGENTS.md` (root), the handoffs and this log would mislead them, so they are not shipped.
  - The JSON formats were spread over handoff records. The new reference was generated from real output, and every enum value comes from the encoder source.
  - Building the example in CI against the *installed* package proves the package works for a real Qt project, which a build-tree test cannot. The example is Debug, like a Qt Creator default, and it runs headless in the same job, so it adds only a few minutes and no second build of the library.
- **Assumptions:**
  - Qt clients use the MSVC 64-bit Qt kit (a C++ API requirement, E-19/E-20).
  - Qt `win64_msvc2022_64` binaries are compatible with the Visual Studio 2026 compiler (same MSVC v14x ABI).
  - **Amended the same day:** the first CI run used Qt 6.11.2 and failed. aqtinstall 3.3.0, the newest release (June 2025), lists 6.11.2 but cannot download it, because Qt's online repository moved 6.10 and later out of the layout it reads (`qtsdkrepository/windows_x86/desktop/` ends at `qt6_693`). CI therefore uses **Qt 6.9.3**, the newest version aqtinstall can install. The example needs only Qt 6.5 or newer, so clients may use 6.10/6.11. Move CI to a newer Qt when aqtinstall supports the new layout.
  - The SDK installed from the `dev` build contains test hooks. That only matters in CI; released SDKs come from the `release` and `sdk-debug` presets without hooks.
  - The example was not compiled locally, because Qt is not installed on the development PC and was deliberately not downloaded. CI is its only build verification.

### E-30 Quieter install scripts (24 Sep 2026)

- **Change:** the two install scripts that collect runtime DLLs (the `app` and `sdk` components) set CMake policy CMP0207 to NEW, so paths are normalised before the exclude patterns are applied.
- **Why:** with CMake 4.x every system DLL printed a multi-line policy warning (about 50 per install). That buried real messages in local and CI logs.
- **Assumptions:** the exclude patterns (`system32`, `winsxs` and `syswow64`, case-insensitive) match both path forms, so the set of installed DLLs is unchanged. The next packaging run confirms this: it should produce the same file list.

### E-31 OCR memory and throughput (issue #1, 25 Sep 2026)

- **Change:**
  1. The OCR sessions no longer use ONNX Runtime's CPU memory arena or memory patterns (a small backend fix, recorded in `docs/ocr/DECISIONS.md`).
  2. OCR inference uses several CPU threads. The new option `text::OpenOptions::ocr_threads` is exposed as `ocr_threads` in `TextOptions`, `AnalysisOptions` and `MetadataRunOptions`, as the C API reading option `ocr_threads`, and as the CLI `--ocr-threads N`.
     - 0 means automatic: half the logical processors, clamped to 1-8. The resolved value is part of `model_identity` (`;threads=N`).
  3. `ocr_compare_golden` accepts an optional thread count.
  4. `tools/bench/` (`make_scan_fixture.py`, `ocr_resources.ps1`, documented in `docs/BUILDING.md` §8) makes the measurements reproducible. The generated 4-page fixture is byte-identical to the reader harness's.
- **Why:** issue #1 reported a Qt harness at 8.9 GB private memory and about 18 s per scanned page. It was reproduced here (8.7 GB, 18.8 s/page) and traced to two causes:
  - **Memory:** ONNX Runtime's arena kept the largest allocations ever seen, so one page peaked at 4.45 GB and four at 8.7 GB. Memory-pattern planning added more on top.
    - Attribution on 4 pages: arena off alone gives 2.82 GB; patterns off alone gives 4.56 GB; both off gives 2.32 GB.
    - Output is identical in every combination.
    - 2.3 GB is an **observed peak, stable across the tested fixtures** (1, 4 and 20 US Letter pages), not a guaranteed limit.
  - **Time:** inference ran on **one thread** (`ocr::Options::threads` defaulted to 1, and S1 never set it). Speed scales to about 8 threads (84 s → 30 s for 4 pages) and then levels off (24 threads: 33 s). Capping at 8, or half the processors, leaves cores for the client's UI.
  - The remaining 2.3 GB scales with the render size: 1.1 GB at 200 DPI, 0.6 GB at 150. That is the detector running on the full page image.
- **Assumptions:**
  - Multi-threaded results are acceptable as equivalent. Golden parity at 8 threads has identical boxes and text, and confidence within 6e-7.
  - Clients that need exact single-thread reproducibility can set `ocr_threads = 1`.
  - The default DPI stays 300 until accuracy at 200 DPI has been evaluated on real scanned books. 200 DPI would halve memory and speed OCR up 2.6×, but small print may suffer, and the fixture used here is synthetic.
- **Verified (Release, Ryzen 9 5900X, 4-page issue fixture at 300 DPI):**
  - metadata 75 s → 30 s and analysis 72 s → 29 s, both 8.7 GB → 2.3 GB;
  - 20 pages 376 s → 147 s (7.4 s/page steady), 8.7 GB → 2.3 GB, with system commit up about 2 GB instead of about 10 GB;
  - text and boxes identical to the pre-fix output;
  - full `dev` suite 23/23, including `golden_parity`.
- **Not done (follow-ups):**
  - Metadata extraction and TOC analysis each OCR the same front pages in separate sessions, so the reader's metadata-plus-analysis workload OCRs them twice.
    - Sharing a session alone would only avoid reloading the models, because `TextDocument::acquire()` processes the requested pages again.
    - The follow-up is for Engine to **reuse compatible `PageAcquisition` results** (same page, revision and acquisition configuration) between metadata extraction and TOC analysis, while S1 keeps ownership of acquisition. This was a review point on issue #1.
  - The `0xE0000008` Qt PDF exits seen in the reader harness are not reproduced or explained here, and **their cause remains unresolved**. The lower memory use reduces the risk of memory pressure in future runs. It says nothing about what caused the earlier crashes.

### E-32 Version 0.2.0 (25 Sep 2026)

- **Change:** `PDFBOOKMARK_VERSION` is now 0.2.0, released as tag `v0.2.0` (SDK and CLI ZIPs on GitHub Releases).
- **Why:**
  - It contains the issue #1 fix (E-31, PR #2).
  - The C++ option structs `TextOptions`, `AnalysisOptions`, `MetadataRunOptions` and `text::OpenOptions` gained a field (`ocr_threads`), which changes their memory layout. C++ clients must therefore rebuild against the new headers, which calls for a minor version bump, not a patch.
  - The C API is unchanged apart from a new optional option key, so `PDFB_C_API_VERSION` stays 1 and C and FFI clients need no rebuild.
- **Assumptions:**
  - The CMake package keeps `SameMajorVersion` compatibility, so clients asking for `find_package(pdfbookmark 0.1)` also accept 0.2.0.
  - Clients rebuild fully when switching SDKs; the SDK brief says so.
  - The standalone subsystem packages (`subsystems/*`) and the OCR package keep their own 0.1.0 versions. They are not released separately.

### E-33 Reuse acquired pages across metadata and TOC analysis (issue #3, 27 Sep 2026)

- **Change:** new operation `analyze_book(pdf, AnalysisOptions, MetadataRunOptions, control, progress)` returns a `BookReport` (both reports plus `pages_reused`). It is also available as C API `pdfb_analyze_book`, Python `Library.analyze_book` and CLI `analyze`/`add --metadata PATH`.
  - It opens one S1 session and runs analysis, then metadata.
  - A per-session `PageCache` in the Engine ledger serves pages the analysis already acquired.
  - `analyze()` and `extract_metadata()` are unchanged. Internally they became "open, then run on the session" (`src/engine/runs.hpp`).
- **Why:**
  - MyBooksLibrary imports every book with both calls, so each scanned page was OCR'd twice. The re-verification on #1 measured this at 57.7 s for 4 pages.
  - Following the #1 review, a shared session alone is not enough, because `acquire()` would process the pages again. Engine therefore reuses `PageAcquisition` results, while S1 still performs every acquisition.
  - Analysis runs first because it reads the most pages (TOC search, up to 40). The metadata stage (at most 30 pages) is then normally served entirely from the cache.
- **Reuse rules:**
  - Same session only, which means the same input bytes, OCR models and OCR threads.
  - Same `mode` and raster limits, otherwise the page is read again.
  - Never pages that were cancelled, or whose OCR was skipped (e.g. for budget), so the metadata stage can still OCR them.
  - Reused pages keep their revision, configuration and model identity (no repointing, `AGENTS.md` §3.3). They cost no OCR budget.
- **Assumptions:**
  - `models` and `ocr_threads` are session settings and come from the analysis options; the metadata options' values are not used (documented in `book.hpp` and `API.md`).
  - No new C API version: adding a function is compatible (`PDFB_C_API_VERSION` stays 1).
  - Existing C++ structs are unchanged, so this is an additive minor release (0.3.0).
- **Verified:**
  - `engine_book` test with a counting fake OCR:
    - 4 scanned pages are OCR'd exactly once (8 times with separate calls);
    - both reports equal the separate calls' (metadata apart from `ocr_attempts_used`);
    - same input identity;
    - no reuse at a different DPI;
    - cancellation reaches both stages without reusing cancelled pages;
    - budget-skipped pages are OCR'd by the metadata stage;
    - text-layer pages are reused with no OCR.
  - The C API, Python and CLI (`cli_metadata`) checks pass. The `dev` suite is 22/22.
  - Real OCR (Release, Ryzen 9 5900X, `tools/bench`):
    - 4-page scan: 61.3 s → 31.2 s;
    - 20-page scan: 305.1 s → 147.0 s;
    - peak 2.32 GB unchanged;
    - analysis reports identical, and metadata reports identical except `ocr_attempts_used`.
- **Amended after the PR #4 review (27 Sep 2026)**, which found three P2 problems. All are fixed, each with regressions at the Engine, C API and CLI levels:
  1. **The OCR budget reset between stages.** Each stage had its own ledger with a full allowance, so a single `ocr_budget` of 1 allowed 2 attempts.
     - Now `analysis.limits.ocr_budget` caps the whole run. The metadata stage gets `min(metadata.ocr_budget, what the analysis left)`, reported as its `ocr_budget`. Cache hits stay free.
     - **Assumption:** a sequential two-stage run needs no shared counter object; handing the remainder to the second stage is exact.
     - **Tests:** `engine_book` (fake OCR: exactly 1 call at cap 1; 2 left for metadata at cap 6), `library_c_api` and `cli_book` (real OCR, 4 scanned pages, `ocr_budget` 1: 1 attempt in total).
  2. **CLI cancellation during the metadata stage exited 0.**
     - The exit status now comes from `cli::analysis_exit_code(analysis_cancelled, metadata_cancelled, plan_ready)`. Completed outputs are still written; the run exits 4 and `add` writes no PDF.
     - **Tests:** `engine_book` (cancellation during metadata: analysis complete, metadata cancelled) and `cli_outputs` (exit-status rule).
  3. **Output destinations could collide.** For example `--report x --metadata x` overwrote one with the other.
     - All requested outputs (`--report`, `--plan`, `--metadata`, `add --output`) are now compared pairwise **before any work**, by `cli::output_collision`.
     - The comparison uses normalised absolute paths (`weakly_canonical`, case-insensitive on Windows) and `equivalent()` for existing files, which covers hard links. A collision exits 2 (usage) and nothing is written.
     - **Tests:** `cli_outputs` (the spellings `.` and `..`, case, a hard link, pairs) and `cli_book`, which checks exit 2 and that no file is created or changed, even with `--force`.
- **Amended after the second PR #4 review (consumer side, 27 Sep 2026): metadata first.**
  - **Change:**
    - `analyze_book` now runs the metadata stage **before** the TOC analysis, and hands the metadata to an optional `on_metadata` callback (C: `pdfb_metadata_fn`; Python: `on_metadata`; CLI: `--metadata` is written at that moment).
    - An analysis error no longer fails the call: `BookReport::analysis` is optional, and `analysis_error` says why. In the C API, `*out_metadata_json` survives an analysis-only failure, and the function then returns the analysis error status.
    - The OCR budget stays one run-wide cap (`analysis.limits.ocr_budget`). The metadata stage may use at most its own budget of it; the analysis gets the rest.
    - Progress counts are per stage (documented).
  - **Why:** analysis-first delivered the title only after the whole TOC analysis, about 5-8 minutes on a scanned book, and lost the metadata when the analysis failed. MyBooksLibrary requires metadata first, published independently (its AGENTS.md §8), and kept when the TOC analysis fails (§6). With analysis-first, the SDK brief was steering its agent into violating both.
  - **Why the reversed order costs no OCR:** with the default limits, the metadata pages (at most 30) lie inside the analysis's first 40-page batch, so the pages OCR'd are the same union in either order. Measured: still one OCR per page (`engine_book` case 1).
  - **Assumptions:**
    - Under a tight run-wide budget, the metadata stage now spends first. That is the right priority for clients that publish metadata first; the analysis gets the remainder.
    - Errors in opening the input or in the metadata stage still fail the call, because they indicate problems at the session level.
  - **Tests:** `engine_book` checks:
    - metadata delivered before any analysis stage ran, and equal to the returned report;
    - the analysis reuses the metadata's 4 pages (4 OCR calls in total);
    - a cancellation during the analysis keeps the metadata complete;
    - an S4 error after acquisition (an invalid numbering section) returns `analysis_error` with the metadata delivered and returned;
    - results equal separate calls.

    `library_c_api` checks the callback and the real-OCR run-wide cap (the metadata stage now does the 1 OCR), and `library_python` checks the callback.


### E-34 Version 0.3.0 (27 Sep 2026)

- **Change:** `PDFBOOKMARK_VERSION` is now 0.3.0, released as tag `v0.3.0`.
- **Why:** it contains issue #3 (PR #4): `analyze_book()` / `pdfb_analyze_book`, metadata first, one OCR per page, and the run-wide OCR budget, plus the CLI's `--metadata`, output-collision and exit-status fixes. This is a minor bump because the change is additive: new functions and types, and no existing struct or function changed. The consumer review asked for the bump in the release commit.
- **Assumptions:**
  - Existing 0.2.0 clients keep working after a rebuild. `SameMajorVersion` lets `find_package(pdfbookmark 0.2)` accept 0.3.0.
  - `PDFB_C_API_VERSION` stays 1 because the C change is additive. `pdfb_analyze_book` is new in this release, so its signature was changed before any client used it.


### E-35 ISBN list in the metadata result (3 Oct 2026)

- **Change:**
  - `metadata::MetadataResult` has a new member `isbns`, a list of `IsbnValue` (`isbn13`, `printed`, `form`, `format`, `label`, `evidence`). The rules are S6-07 in `docs/handoffs/S6_HANDOFF.md`.
  - The metadata report JSON has a new top-level array `isbns` (`docs/JSON_FORMATS.md` §6). The C API and Python return it unchanged, because they pass this JSON through.
  - `pdfbookmark metadata` prints one `ISBN:` line per ISBN.
  - The S6 policy identity is now `s6-document-metadata-v2`.
- **Why:** the owner asked for the ISBN as an identifier of a book and its edition. An ISBN identifies one edition in one format, so an edition has several and there is no single ISBN per book+edition. The owner decided (3 Oct 2026) to store every ISBN found and decide later how to use the list.
- **Assumptions:**
  - `isbns` is a plain list, not a field with a status. No ISBN is "the" value, so Resolved/Ambiguous does not apply; an empty list means none was found in the pages searched.
  - Only numbers that pass the check digit are listed. A failing number on an ISBN line is reported in `diagnostics`, so an OCR misread is visible but never stored as an ISBN.
  - ISBNs do not take part in the search stop or in the CLI exit status. Otherwise every book without a printed ISBN would be searched to the page limit and reported as partial.
  - The JSON stays `schema_version` 1: the change adds a member and changes none. A client that rejects unknown members of this report would need updating; the reports are outputs, and strict member checks apply only to plan input.
  - C++ clients must rebuild, because `MetadataResult` gained a member. `PDFBOOKMARK_VERSION` (0.3.0) and `PDFB_C_API_VERSION` (1) are unchanged here; the version bump belongs to the next release commit.
- **Verified:** root `dev` build, 26/26 suites (`metadata_pure`, `engine_metadata` and `cli_metadata` cover the list). Real books (3 Oct 2026, read-only, `tests/books/`): *Computational Physics* (Wiley, 4th ed.) lists 3 ISBNs from its copyright page (Print, ePDF, ePub); *Numerical Recipes* (Cambridge, 3rd ed.) lists 2 (hardback, eBook), each printed as ISBN-13 and ISBN-10 and merged into one entry. Both match the printed pages, and the input hashes are unchanged. Both files have embedded text (0 OCR attempts). *Black Holes, White Dwarfs and Neutron Stars* (`shapiro1983.pdf`, a scan with an embedded OCR layer) lists its one ISBN, 9780471873167, both from the embedded layer (printed there with stray spaces, "978-0-47 1-873 16-7") and from our own OCR (`--mode ocr --max-pages 4`, 4 OCR attempts); the ISBN-10 line is merged into the same entry.


### E-36 ISBN list: review fixes for PR 8 (3 Oct 2026)

- **Change** (`src/metadata/metadata.cpp`, `tests/metadata/metadata_test.cpp`):
  - A 978/979 number that fails the ISBN-13 check is skipped whole. Its last ten digits are no longer read as an ISBN-10.
  - On a line below an ISBN statement that has no ISBN keyword, only an ISBN-13 is listed.
  - A failing number on an ISBN line is reported in `diagnostics` even when the same line also has a valid ISBN (the first failing number per line).
  - A bare format word after an ISBN is not its label when that ISBN has its own qualifier in front and the word is followed by the ISBN keyword ("Hardback ISBN … Paperback ISBN …").
  - A parenthesis after an ISBN is a label only when it closes before the next ISBN on the line.
  - Repeat evidence is compared by line text as well as page and region, because one region can hold several lines.
  - The year rules skip every line the ISBN list reads from (`isbn_line`), not only lines that contain "isbn".
- **Why:** code review of PR 8 found that an OCR-misread ISBN-13 could be stored as a different ISBN, and that unrelated ten-digit numbers (a printer's key "9 8 7 6 5 4 3 2 1 0") passed the ISBN-10 check on continuation lines.
- **Assumptions:**
  - An ISBN-10 printed without the keyword on a continuation line is rare; it is no longer listed. Any ten-digit number passes the ISBN-10 check 1 time in 11, an ISBN-13 needs the 978/979 prefix as well.
  - The policy identity stays `s6-document-metadata-v2`, because v2 has not been released.
  - The C++ ABI change of `MetadataResult` is not addressed here; the version bump stays with the next release commit (E-35).
- **Verified:** `dev` build; `metadata_pure` (new cases), `engine_metadata` and `cli_metadata` pass. The three real books in `tests/books/` list the same ISBNs and labels as in E-35 (embedded mode).


### E-37 Version 0.4.0 for the `MetadataResult` change (3 Oct 2026)

- **Change:** `PDFBOOKMARK_VERSION` is 0.4.0 in `CMakeLists.txt` and `vcpkg.json`; the package names in `docs/BUILDING.md` follow. `PDFB_C_API_VERSION` stays 1.
- **Why:** E-35 added `isbns` to the exported struct `metadata::MetadataResult`, which changes the C++ layout. With the version still 0.3.0, a client built against the released 0.3.0 headers could not tell the new DLL from the old one.
- **Assumptions:**
  - A minor bump is the signal for a C++ layout change while the major version is 0. `SameMajorVersion` still lets `find_package(pdfbookmark 0.3)` accept 0.4.0, so C++ clients must rebuild against the 0.4.0 headers; the C API and Python are unaffected (they pass JSON through).
  - `PDFBOOKMARK_VERSION` is a cache variable: an existing build folder keeps 0.3.0 until it is reconfigured with `-DPDFBOOKMARK_VERSION=0.4.0` or a fresh cache.
  - The package sizes quoted in `docs/BUILDING.md` are those of 0.3.0 and were not re-measured. No 0.4.0 package or tag has been made.
- **Verified:** not built; version strings only.


### E-38 The OCR models' licence ships with every package (issue #6, 3 Oct 2026)

- **Change:**
  - The PaddleOCR `LICENSE` (Apache-2.0) is now in the repository as `packaging/licenses/PaddleOCR-PP-OCR-models.txt`. The SDK installs it to `share/doc/pdfbookmark/licenses/`, and the end-user package to `licenses/`.
  - The two copies of the notice list (root `CMakeLists.txt` for the app, `library/CMakeLists.txt` for the SDK) are now one function, `pdfbookmark_install_notices` in `cmake/PdfbookmarkDependencies.cmake`.
  - A missing notice is a configure error. Before, the SDK skipped it silently and the app package only warned.
  - CI checks that the installed SDK contains the models' licence.
- **Why:** SDK 0.3.0 shipped the models without their licence. Apache-2.0 section 4(a) requires giving recipients a copy. The install took the file from a local `PaddleOCR/` checkout, which is not in Git and not in the `models-v1` archive, so every fresh clone and every CI build left it out without any message.
- **Assumptions:**
  - The models are PaddleOCR PP-OCR models under Apache-2.0, as `models/README.md` states. The committed file is byte-identical to the `LICENSE` of the local PaddleOCR checkout.
  - PaddleOCR has no `NOTICE` file, so the licence text alone satisfies section 4.
  - Every configuration that defines these packages has all eight notices available, because the root project always finds PDFium, qpdf (with zlib and libjpeg-turbo from vcpkg), ONNX Runtime and OpenCV.
  - The already published SDK 0.3.0 is not changed; the fix ships with the next release.
- **Removed:** the dependency on the local-only `PaddleOCR/` folder for packaging.
- **Verified:** a `dev` configure, build and `cmake --install --component sdk` from a fresh worktree without `PaddleOCR/` installs all eight notices, and the models' licence equals the PaddleOCR `LICENSE`. With the file removed, the configure stops with an error naming it.


### E-39 OpenCV built from source without Media Foundation (issue #7, 3 Oct 2026)

- **Change:**
  - OpenCV is no longer used as the prebuilt `opencv_world500.dll` from the official Windows package. The configure step builds the **same 5.0.0 sources** (the `sources/` folder inside that pinned package) with `BUILD_LIST=core,imgproc,imgcodecs` as one `opencv_world` DLL, into `.deps/opencv-min-<key>` (`pdfbookmark_build_opencv` in `cmake/PdfbookmarkDependencies.cmake`).
  - Video I/O, the GUI module, Media Foundation, DirectShow and FFmpeg are off. Everything else keeps OpenCV's defaults, as in the official build: Intel IPP, the Concurrency parallel backend, and the bundled JPEG, PNG, TIFF, WebP, JPEG 2000 and GIF codecs.
  - A dependency path that points into `.deps/` is now re-evaluated on every configure, so existing build trees pick up a changed pin. A path outside `.deps/` is still the caller's own copy and is left alone.
  - CI and the release workflow cache `.deps/opencv-min-*`.
- **Why:** the official DLL imports `MFPlat.DLL`, `MF.dll` and `MFReadWrite.dll` in its normal import table. They exist on Windows N and KN editions only with the Media Feature Pack, so on those editions any program using the SDK (`app.exe` → `pdfbookmark.dll` → `opencv_world500.dll`) fails at start. The OCR code uses only `core`, `imgproc` and `imgcodecs`. This amends E-21, which chose prebuilt packages for all three backends.
- **Assumptions:**
  - Building the same sources with the same options for the modules used gives the same OCR results. This was checked, not assumed (see Verified).
  - The import-table problem was confirmed with `dumpbin`; the failure itself has **not** been reproduced on an N edition, and the fix has not been run on one.
  - The DLL keeps its name `opencv_world500.dll` (`opencv_world500d.dll` for Debug), so client packaging scripts need no change.
  - OpenCV's own configure downloads `ippicv` from GitHub, checked against the hash in the OpenCV sources. This is a second download host besides the release pages.
  - Each configuration is built on first use: about 2.5 minutes each on the development PC.
- **Removed:** the use of the prebuilt `opencv_world500(d).dll` and, with it, the imports of Media Foundation, `GDI32`, `USER32`, `COMDLG32`, `ADVAPI32`, `OLEAUT32` and `SHLWAPI`.
- **Verified:**
  - `dumpbin /dependents` on the new Release and Debug DLLs: `KERNEL32`, `ole32` and the C++ runtime only. No shipped binary of the Release build imports a Media Foundation DLL.
  - `dev` build: 26/26 suites, including `golden_parity` (the OCR parity against the PaddleOCR reference on 30 images) with the new Debug DLL.
  - Release build: OCR of 3 scanned pages gives the same 41 regions (text, quads and confidences) as the previous build with the official DLL.
  - Sizes: Release DLL 54 MB (was 80 MB), Debug 88 MB (was 140 MB).


### E-40 Issue #13: one title in two layouts, title-page authors, "0" for ©, a split contents (4 Oct 2026)

- **Change** (policies `s2-toc-detection-v3`, `s3-toc-parsing-v3`, `s4-page-mapping-v4`, `s6-document-metadata-v3`; rules in the S2, S3, S4 and S6 handoffs):
  - **S2** (S2-09 to S2-11): row density is counted per visual line on pages whose rows end in one or two reference columns; "xiv Contents" is a running head of a continued page, not a new heading; one sparse page between two compatible TOC pages joins their candidate.
  - **S3** (S3-10 to S3-14): numbers with text-layer artifacts ("21 1", "I I9", "8. I3", "I 8.6") are read as numbers, keeping the printed literal; chapter rows keep their page number; hanging-indent wraps join; the TOC page's running head is ignored; "Appendix" is a heading label and sections nest under "Chapter 8.".
  - **S4** (S4-12): an offset target that two anchors already give may be confirmed by a heading that differs from the entry only by text-layer errors. Exact headings stay the only heading anchors.
  - **S6** (S6-08 to S6-10): title and subtitle split at a typographic gap, multi-line subtitles, agreeing readings on two pages resolve the title (mixed case shown); a title-page author block resolves its names; "0 1983 by …" is a copyright statement, the earliest of several holders' years is the copyright year, law citations and figure axes give no year, and a running "Copyright" head or foot counts only when no copyright page states that year.
- **Why:** issue #13. *Black Holes, White Dwarfs, and Neutron Stars* (`shapiro1983.pdf`, a reprint with an OCR-made text layer) gave an ambiguous title, wrong authors, copyright 2004 instead of 1983, and no contents (two "competing" candidates around an undetected middle page). The issue asked for general rules, since these patterns recur in reprints.
- **Assumptions:**
  - Text-layer repairs apply to matching and to numbers only: titles keep their text, except a section number at the start of a TOC title, which is corrected with an entry diagnostic. A spaced number is joined only when it is its own region at the end of a row and has at most 3 digits.
  - The approximate heading match allows one wrong character in 20 and never applies to keys shorter than 12 characters; it only confirms a destination the offset already gives.
  - The earliest copyright year is the work's copyright only when each statement names one year and different years name different holders; one holder with several years stays ambiguous.
  - A cover's name block alone still does not resolve contributors (an existing test), and the title-page block counts only when that page's title block reads as the resolved title.
  - The JSON members are unchanged (schema 1); only `policy_id` values and results change. No public C++ type changed.
- **Removed:** nothing. A cluster guard tried during this work (ignore reference clusters in the first quarter of a page) was dropped: on *Higham* it turned dot-leader titles into a false left column.
- **Verified:**
  - `dev` build: 25/25 suites (golden parity not run). New pure fixtures: the issue's five-page contents and bridged page (S2); a noisy reprint TOC page (S3); three target headings with text-layer errors and a short-title counterexample (S4); the issue's title and author variants (1-3 authors, with and without affiliation), "0 1983" copyright with a reprint, running heads and feet, and counterexamples found on real books (S6).
  - `shapiro1983.pdf` (SHA-256 `2507b2af...5018`, unchanged after every run), default options, 0 OCR attempts: one candidate on physical pages 10-14, 160/160 entries resolved, `plan_ready`; `apply` wrote 160 bookmarks, verified after reopening. 158 titles appear on their target pages; the other two follow the book's own TOC (8.12 is listed at 227 but printed on 226). Metadata: title and subtitle resolved from pages 0 and 1, both authors, copyright 1983 with 2004 as the reprint alternative, the ISBN list unchanged (9780471873167, from the ISBN-13 and ISBN-10 lines).
  - Regression corpus (owner's 84 local PDFs, `--mode embedded`, read-only), against `v0.4.0`: resolved TOC entries 9,286 -> 9,625 and unparsed rows 1,104 -> 884. 14 books resolve 1 to 6 fewer entries; every lost entry is a "Contents" running head parsed as an entry or a wrapped-title fragment now joined to its full title. Chosen candidates are unchanged except `shapiro1983`; *Concrete Mathematics* gains a one-page alternative (its "A Note on Notation" list), still choosing its TOC. Metadata: 25 books change, all checked by hand; most are now-correct copyright years (law citations no longer count), complete subtitles and resolved authors. Remaining wrong results were wrong before (a slide deck, a series-editor list).
  - Regressions found by the corpus and fixed before this entry: a far-left reference cluster guard that broke two-column TOCs and *Higham*; stricter visual-line clustering that rejected TOC pages of *Evaluating Derivatives*; a references page counted as a TOC (*JRM093*); small capitals, dash attributions, city lines and law years in metadata.
