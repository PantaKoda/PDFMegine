# JSON formats

Every JSON document the library produces or accepts. C and other-language clients receive these from the C API (`docs/API.md` §4). The CLI writes the same documents. C++ clients get the same data as typed structs and need this page only to read or write files.

The structures below were taken from real output of `pdfbookmark` 0.1.0.

## 1. Conventions (all formats)

- **Encoding:** UTF-8, RFC 8259, with members in a fixed order. The same input and options give byte-identical output.
- **Versioning:** `schema_version` is `1`. A reader should reject other versions. New members may be added within version 1, so ignore members you don't know.
- **`kind`** identifies reports: `"pdfbookmark.text"`, `"pdfbookmark.analysis"` or `"pdfbookmark.metadata"`. Plans have no `kind`; they are recognised by `existing_outline_policy`.
- **Pages:** `page_index_base` is `0`. Every `page_index`, `pdf_page_index`, `first`/`end` and `pages[]` value is a **zero-based physical page** of the file; show `index + 1` to people. Ranges are half-open: `first` is included and `end` is not.
- **Printed references** (a TOC's "12", "iv", "A-3") are strings in `reference.literal`, never page indices.
- **Unknown or absent** values are `null`, never `0` or `""`.
- **Enum values** are lower-case `snake_case` strings; every possible value is listed in §8.
- **Reading plans is strict** (the only format the library reads back): unknown or missing members, wrong types, out-of-range numbers, bad hex and other schema versions are errors.

## 2. Shared objects

**`input`** identifies the exact PDF bytes that were read:
```json
{ "sha256": "<64 lowercase hex>", "page_count": 70, "display_path": "books/My Book.pdf" }
```
Plans carry only `sha256` and `page_count`. `apply` refuses a plan whose `input` doesn't match the file.

**Source reference** points at the text a decision is based on:
```json
{ "page_index": 39, "revision": 40, "region_id": 1, "utf8_begin": 0, "utf8_end": 132 }
```
`region_id` indexes `pages[].content.regions` of that page, and `revision` identifies which reading of the page. `utf8_begin`/`utf8_end` are optional byte offsets into the region's text.

**Page** (`pages[]` in text and analysis reports) is one page as read by text acquisition:
```jsonc
{
  "page_index": 39,
  "outcome": "ok",                     // §8 page outcome
  "configuration": 0,                  // index into acquisition.configurations
  "reasons": [],
  "assessment": { "readability": "acceptable", "coverage": "no_omission_indicated",
                  "policy_id": "s1-acquisition-v2", "unicode_scalars": 496, "visible_scalars": 486,
                  "replacement_count": 0, "control_count": 0, "image_object_count": 0, "reasons": [] },
  "attempts": [ { "source": "embedded_pdf", "state": "completed", "reason": "Native text available" } ],
  "content": {                         // null when no usable text
    "revision": 1,
    "source": "embedded_pdf",          // or "ocr"
    "reading_order": "estimated",      // or "uncertain"
    "geometry": { "frame": "canonical_top_left_points", "width_points": 600, "height_points": 800,
                  "rotation_quarters": 0, "user_unit": 1 },
    "text": "Contents\nPart One ........ 1\n…",   // all regions, in reading order
    "regions": [
      { "id": 0, "text": "Contents", "granularity": "pdf_text_run",
        "quad": [[40.9, 52.1], [120.3, 52.1], [120.3, 66.0], [40.9, 66.0]],   // points, top-left origin
        "ocr_confidence": null }        // OCR lines: mean character probability, 0..1
    ]
  }
}
```

**Acquisition summary** (`acquisition`): `policy_id`, `model_identity` (the OCR backend, the SHA-256 of each model file and `;threads=N`; `"not-used"` when no OCR ran), `configurations[]`, `ocr_budget` and `ocr_attempts_used`.

## 3. Text report (`kind: "pdfbookmark.text"`)

This is produced by `pdfb_extract_text`, `extract_text()` + `text_report_json()`, and `pdfbookmark text`.

| Member | Meaning |
| --- | --- |
| `schema_version`, `kind`, `page_index_base`, `input` | §1-2 |
| `status` | `complete`, `partial` (some page degraded or failed) or `cancelled` |
| `acquisition` | §2 |
| `diagnostics` | strings |
| `pages` | §2 page objects, in the order requested |

## 4. Analysis report (`kind: "pdfbookmark.analysis"`)

This is produced by `pdfb_analyze`, `analyze()` + `analysis_report_json()`, and `pdfbookmark analyze --report`. It is large (about 200 KB for a 70-page file), because it contains every page it read.

| Member | Meaning |
| --- | --- |
| `outcome` | **Start here.** `plan_ready`, `analysis_partial` (TOC found, plan blocked: read `plan.blockers`), `no_toc_found_in_search`, `search_incomplete`, `cancelled` |
| `stop_reasons`, `diagnostics` | strings, why the search stopped and notes |
| `options` | the options used: `limits{initial_pages, batch_pages, max_search_pages, max_evidence_pages, max_mapping_rounds, ocr_budget}`, `mode`, `ocr_models_loaded`, `candidate_id`, `candidate_tie_ratio`, `sections_supplied`, `override_count`, `allow_partial`, `flat_outline_for_unknown_hierarchy`, `title_style` (`printed`/`chapter`) |
| `acquisition` | §2 plus `search_pages[]`, `search_covered_document`, `evidence_pages[]` |
| `pages` | §2 page objects, every page read, in physical order |
| `detection` | `policy_id`, `diagnostics`, `candidates[]` (TOCs found: `id`, `score`, `start`/`end` boundary, `pages[]{page_index, revision, score, degraded}`, `interruptions[]{page_index, reason}`, `reasons`, `limitations`), `page_reviews[]{page_index, status, score, reasons}` |
| `candidate_choice` | `chosen_id`, `explicit_selection`, `reason`, `alternatives[]` (other candidate ids) |
| `parse` | `null`, or `candidate_id`, `policy_id`, `completeness` (`complete`/`incomplete`), `start`/`end`, `missing_pages[]`, `diagnostics`, `entries[]`, `unparsed[]{text, reason, sources}` |
| `mapping` | `null`, or `policy_id`, `rounds`, `sections[]`, `entries[]`, `unfulfilled_requests[]`, `diagnostics` |
| `plan` | `ready`, `blockers[]` (plain-language reasons it isn't ready), `choices[]` (policy decisions such as omissions), `validation_issues[]{node_id, field, message}`, `draft` (the §5 plan, **for review only**, `null` if none) |

**`parse.entries[]`** holds one TOC line each:
```jsonc
{ "id": "toc-p39-r40/p39r40e0", "title": "Part One", "order": 0,
  "reference": { "literal": "1", "numbering": "decimal", "prefix": "", "ordinal": 1,
                 "is_range": false, "range_end": null, "uncertain": false, "reasons": [] },  // null: no printed page
  "hierarchy": { "kind": "root", "parent_id": null, "reasons": ["Aligned to column root margin"] },
  "sources": [ /* source references */ ], "diagnostics": [] }
```

**`mapping.entries[]`** records where each entry points; `entry_id` matches `parse.entries[].id`:
```jsonc
{ "entry_id": "toc-p39-r40/p39r40e0", "status": "resolved", "pdf_page_index": 41,  // null unless resolved
  "method": "inferred_offset", "section_id": "body",
  "alternatives": [ { "pdf_page_index": 42, "method": "heading_match", "reason": "…", "sources": [] } ],
  "supporting_pages": [41, 45], "supporting_sources": [ /* … */ ],
  "reasons": ["Two independent agreeing anchors and target confirmation"] }
```

**`mapping.sections[]`** describes how printed numbers map to pages: `id`, `first`, `end`, `style`, `prefix`, `origin` (e.g. an engine default, recorded as an assumption), `viewer_labels_match_printed`.

## 5. Bookmark plan (input to `apply`)

This is produced by `pdfb_analyze` (its second output, only when ready), `plan_to_json()`, and `pdfbookmark analyze --plan`. It is consumed by `pdfb_apply`, `pdfb_validate_plan`, `plan_from_json()`, and `pdfbookmark apply --plan`. This is the one format clients edit.

```json
{
  "schema_version": 1,
  "page_index_base": 0,
  "input": { "sha256": "59401a0e…", "page_count": 70 },
  "existing_outline_policy": "replace_in_copy",
  "nodes": [
    { "id": "toc-p39-r40/p39r40e0", "parent_id": null, "title": "Part One",
      "destination": { "pdf_page_index": 41 } },
    { "id": "toc-p39-r40/p39r40e1", "parent_id": "toc-p39-r40/p39r40e0", "title": "Getting Started",
      "destination": { "pdf_page_index": 41 } }
  ],
  "omitted_entries": [ { "entry_id": "…", "reason": "…" } ],
  "promotions": [ { "node_id": "…", "original_parent_id": "…", "new_parent_id": null, "reason": "…" } ]
}
```

**Rules:**
- `nodes` order is the sibling order, and `parent_id` builds the tree (`null` = top level).
- `id`s must be unique and non-empty. Every parent must exist, and there must be no cycles.
- Titles must be non-empty UTF-8.
- `pdf_page_index` must be less than `page_count`. The same page may appear more than once.
- **Safe edits:** titles, `parent_id`, destinations, and adding, removing or reordering nodes. Run `pdfb_validate_plan` before applying.
- **Do not edit `input`:** it binds the plan to one exact file.
- `existing_outline_policy` is always `"replace_in_copy"`: the new file's outline becomes exactly `nodes`. The input file is never changed.
- `omitted_entries` and `promotions` are records of what `allow_partial` did, and are checked for consistency.

## 6. Metadata report (`kind: "pdfbookmark.metadata"`)

This is produced by `pdfb_extract_metadata`, `extract_metadata()` + `metadata_report_json()`, and `pdfbookmark metadata --json`. It is also produced by `pdfb_analyze_book` / `analyze_book()` and `pdfbookmark analyze --metadata`. The metadata stage runs first there, so its report is unchanged. In that run, three members of the **analysis** report's `acquisition` differ from a separate `analyze()`: `ocr_attempts_used` counts only the pages the analysis itself had to OCR (pages reused from the metadata stage count 0); `ocr_budget` is the analysis stage's effective allowance (the run-wide cap minus what the metadata stage used); and in `configurations[]`, a reused page keeps the metadata stage's configuration string, including that stage's `ocr_budget`. See `docs/API.md`, "Contents and metadata together".

| Member | Meaning |
| --- | --- |
| `input`, `policy_id` | §2; `"s6-document-metadata-v1"` |
| `fields` | `title`, `contributors`, `edition`, `publication_year`, `copyright_year`, each a **field** (below) |
| `pages[]` | `{page_index, role, reasons}` for each page examined |
| `search` | `pages[]` searched, `covered_document`, `stop_reasons[]`, `cancelled` |
| `acquisition`, `diagnostics` | §2 |

A **field** has this shape:
```jsonc
{ "status": "resolved",            // or "ambiguous" / "not_found_in_search"
  "value": …,                      // only when resolved, otherwise null
  "evidence": [ { "source": { /* source reference */ }, "text": "Second Edition", "reason": "Edition statement" } ],
  "alternatives": [ { "value": …, "score": 1, "evidence": [ … ], "reasons": [ … ] } ],
  "reasons": [ "…" ] }
```

| Field | `value` |
| --- | --- |
| `title` | `{ "title": "Parallel Worlds", "subtitle": "A Practical Guide" }` (`subtitle` may be `null`) |
| `contributors` | `[ { "name": "Jane Q. Doe", "role": "author" } ]` |
| `edition` | `{ "statement": "Second Edition", "ordinal": 2 }` (`ordinal` `null` for e.g. "Revised") |
| `publication_year`, `copyright_year` | `{ "year": 2012, "kind": "publication", "statement": "Second edition published 2012" }` |

A copyright year is never reported as the publication year. "Not found" means only that it wasn't found in the pages searched.

## 7. C API result objects

These are small results returned only by the C API:

| Function | Result |
| --- | --- |
| `pdfb_apply` | `{ "schema_version": 1, "output": "<path>", "output_sha256": "<hex>", "committed": true, "replaced_existing_output": false, "input_had_outline": false, "verification": { "page_count": 70, "outline_items": 8, "structure_matches": true, "input_unchanged": true }, "diagnostics": [] }` |
| `pdfb_validate_plan` | `{ "valid": false, "issues": [ { "code": "missing_parent", "node_index": 3, "node_id": "…", "field": "parent_id", "message": "…" } ] }` |
| `pdfb_read_pdf_identity` | `{ "sha256": "<hex>", "page_count": 70 }` |
| `pdfb_find_models` | `{ "detector": "<path>", "recognizer": "<path>", "charset": "<path>" }` or `null` |

## 8. Enum values

| Where | Values |
| --- | --- |
| text `status` | `complete`, `partial`, `cancelled` |
| page `outcome` | `ok`, `degraded`, `no_text_found`, `failed`, `cancelled` |
| `assessment.readability` | `acceptable`, `suspect`, `unknown` |
| `assessment.coverage` | `no_omission_indicated`, `suspected_incomplete`, `unknown` |
| `attempts[].state` | `completed`, `failed`, `skipped` |
| `source` | `embedded_pdf`, `ocr` |
| `reading_order` | `estimated`, `uncertain` |
| `regions[].granularity` | `pdf_text_run`, `ocr_line` |
| analysis `outcome` | `plan_ready`, `analysis_partial`, `no_toc_found_in_search`, `search_incomplete`, `cancelled` |
| candidate/parse `start`, `end` | `closed`, `may_continue`, `unknown` |
| `page_reviews[].status` | `candidate`, `rejected`, `skipped` |
| `parse.completeness` | `complete`, `incomplete` |
| `reference.numbering` | `decimal`, `roman`, `prefixed_decimal`, `unknown` |
| `hierarchy.kind` | `root`, `known_parent`, `unknown` |
| mapping `status` | `resolved`, `ambiguous`, `unresolved` |
| mapping `method` | `manual_entry`, `manual_offset`, `associated_local_link`, `viewer_label`, `inferred_offset`, `heading_match` |
| `options.title_style` | `printed`, `chapter` |
| metadata field `status` | `resolved`, `ambiguous`, `not_found_in_search` |
| metadata `pages[].role` | `cover`, `title`, `copyright`, `contents`, `other`, `unknown` |
| contributor `role` | `author`, `editor`, `translator`, `organization` |
| year `kind` | `publication`, `copyright`, `printing` |
| validation `code` | `unsupported_schema_version`, `unsupported_page_index_base`, `missing_input_digest`, `invalid_page_count`, `empty_plan`, `empty_node_id`, `duplicate_node_id`, `empty_title`, `invalid_utf8_title`, `missing_parent`, `self_parent`, `parent_cycle`, `destination_out_of_range`, `invalid_omission`, `invalid_promotion` |
