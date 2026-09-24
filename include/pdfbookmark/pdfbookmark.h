/* pdfbookmark C API: stable C interface of the pdfbookmark library.
 *
 * Use it from C, or from any language with a C foreign-function interface
 * (Python ctypes/cffi, C#, Rust, Go, Java, Node, ...). It works with every
 * compiler and with Debug or Release clients alike. C++ clients may prefer
 * <pdfbookmark/pdfbookmark.hpp>. The guide is docs/API.md, section "C API".
 *
 * Conventions
 *   - Strings in and out are UTF-8. Paths are UTF-8 as well, on every OS.
 *   - Every operation returns a pdfb_status. PDFB_OK (0) means success. On
 *     failure, pdfb_last_error() gives the message for the calling thread.
 *   - Options are a JSON object text (or NULL for the defaults). Unknown
 *     keys and wrong types are rejected (PDFB_INVALID_ARGUMENT).
 *   - Results are JSON text (schema_version 1; page indices are zero based)
 *     returned through `char** out_...` parameters. The caller frees each
 *     one with pdfb_free(). Outputs are set to NULL on failure.
 *   - Calls block. Run them off a UI thread. Cancel from any thread with a
 *     pdfb_cancel_token. Progress callbacks run on the calling thread.
 *   - The input PDF is never modified. Existing bookmarks are never used as
 *     evidence. pdfb_apply() writes a NEW file.
 */
#ifndef PDFBOOKMARK_PDFBOOKMARK_H
#define PDFBOOKMARK_PDFBOOKMARK_H

#include <stddef.h>

#if defined(_WIN32)
#  if defined(PDFBOOKMARK_BUILDING_LIBRARY)
#    define PDFB_API __declspec(dllexport)
#  else
#    define PDFB_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define PDFB_API __attribute__((visibility("default")))
#else
#  define PDFB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version of this C interface. It increases only when the C API changes
 * incompatibly; compare pdfb_c_api_version() with PDFB_C_API_VERSION. */
#define PDFB_C_API_VERSION 1

/* Status codes. The numbers are part of the ABI and never change. */
typedef enum pdfb_status {
    PDFB_OK = 0,
    PDFB_INVALID_ARGUMENT = 1,  /* Bad argument, options JSON or plan JSON. */
    PDFB_INPUT_OPEN = 2,        /* The input could not be opened or read. */
    PDFB_INPUT_CHANGED = 3,     /* The input changed, or the plan is for another file. */
    PDFB_PDF_BACKEND = 4,       /* The PDF could not be processed. */
    PDFB_OCR_CONFIGURATION = 5, /* OCR models missing or unusable. */
    PDFB_RESOURCE_LIMIT = 6,    /* A size or memory limit was exceeded. */
    PDFB_UNSUPPORTED = 7,       /* E.g. an encrypted or signed PDF for writing. */
    PDFB_OUTPUT_EXISTS = 8,     /* The output file exists (see replace_existing_output). */
    PDFB_OUTPUT_WRITE = 9,      /* The output could not be written or verified. */
    PDFB_CANCELLED = 10,        /* Cancelled through the token. */
    PDFB_INTERNAL = 11          /* Unexpected internal failure (a bug). */
} pdfb_status;

/* ------------------------------------------------------------ library */

/* Library version, "MAJOR.MINOR.PATCH". Static string; do not free. */
PDFB_API const char* pdfb_version(void);

/* PDFB_C_API_VERSION of the loaded library. */
PDFB_API int pdfb_c_api_version(void);

/* Stable name of a status, e.g. "input_changed". Static string. */
PDFB_API const char* pdfb_status_name(pdfb_status status);

/* Message of the last failed call on this thread ("" after a success).
 * Valid until the next pdfb_* call on the same thread. Do not free. */
PDFB_API const char* pdfb_last_error(void);

/* Frees a string returned by this library. NULL is ignored. */
PDFB_API void pdfb_free(char* text);

/* ------------------------------------------------------------ run control */

/* Cancellation token, shared between the thread running an operation and
 * the thread that cancels it. Cancellation is checked between pages. */
typedef struct pdfb_cancel_token pdfb_cancel_token;

PDFB_API pdfb_cancel_token* pdfb_cancel_token_new(void);  /* NULL if out of memory. */
PDFB_API void pdfb_cancel_token_cancel(pdfb_cancel_token* token);  /* Thread safe. */
PDFB_API void pdfb_cancel_token_free(pdfb_cancel_token* token);    /* NULL is ignored. */

/* Progress callback. `stage` is a UTF-8 label valid during the call.
 * `done` counts pages read so far. `total` is the page count when it is known, otherwise 0. */
typedef void (*pdfb_progress_fn)(void* user_data, const char* stage, size_t done,
                                 size_t total);

/* ------------------------------------------------------------ OCR models */

/* Default OCR model search: the PDFBOOKMARK_MODELS environment variable,
 * then "models" next to the library, then ../share/pdfbookmark/models.
 * *out_json is {"detector": path, "recognizer": path, "charset": path}, or
 * the JSON literal null when no models are found (OCR then unavailable). */
PDFB_API pdfb_status pdfb_find_models(char** out_json);

/* Reading options accepted by every operation that reads pages:
 *   "mode":       "auto" (default) | "embedded" (PDF text only) | "ocr"
 *   "models":     "<folder>" with det/ and rec/ inside; null disables OCR;
 *                 absent uses pdfb_find_models()
 *   "dpi":        50..1200, OCR image resolution (default 300)
 *   "ocr_budget": maximum pages to OCR in the run */

/* ------------------------------------------------------------ operations */

/* Positioned text of pages.
 * Options: reading options, plus "pages": [zero-based indices] (default: every page).
 * *out_report_json: the text report (same format as `pdfbookmark text`). */
PDFB_API pdfb_status pdfb_extract_text(const char* pdf_path, const char* options_json,
                                       pdfb_cancel_token* cancel,
                                       pdfb_progress_fn progress, void* user_data,
                                       char** out_report_json);

/* Finds the printed table of contents, maps entries to pages and builds a
 * bookmark plan. Nothing is written to disk.
 * Options: reading options, plus
 *   "allow_partial": bool, "flat_outline": bool,
 *   "titles": "printed" | "chapter", "candidate": "<candidate id>",
 *   "max_search_pages": N, "max_evidence_pages": N.
 * *out_report_json: the analysis report; its "outcome" is "plan_ready",
 *   "analysis_partial", "no_toc_found_in_search", "search_incomplete" or
 *   "cancelled", and "plan"."blockers" explains a plan that is not ready.
 * *out_plan_json (may be NULL if not wanted): the ready plan for
 *   pdfb_apply(), or NULL when the plan is not ready.
 * A document without a usable TOC is PDFB_OK with a non-ready outcome. */
PDFB_API pdfb_status pdfb_analyze(const char* pdf_path, const char* options_json,
                                  pdfb_cancel_token* cancel, pdfb_progress_fn progress,
                                  void* user_data, char** out_report_json,
                                  char** out_plan_json);

/* Writes a NEW PDF at output_path whose outline is exactly the plan. The
 * plan must belong to this exact input (SHA-256 and page count).
 * Options: "replace_existing_output": bool (never allows the input itself).
 * *out_result_json (may be NULL): {"output", "output_sha256", "committed",
 *   "replaced_existing_output", "input_had_outline", "verification": {...},
 *   "diagnostics": [...]}. */
PDFB_API pdfb_status pdfb_apply(const char* pdf_path, const char* output_path,
                                const char* plan_json, const char* options_json,
                                pdfb_cancel_token* cancel, char** out_result_json);

/* Title, contributors, edition, publication and copyright year.
 * Options: reading options, plus "max_pages": N (default 30).
 * *out_report_json: the metadata report (same format as `pdfbookmark metadata`). */
PDFB_API pdfb_status pdfb_extract_metadata(const char* pdf_path, const char* options_json,
                                           pdfb_cancel_token* cancel,
                                           char** out_report_json);

/* ------------------------------------------------------------ plans */

/* Structural check of a plan (ids, parents, cycles, titles, page bounds).
 * PDFB_OK means the JSON was decoded; validity is in the result:
 * {"valid": bool, "issues": [{"code", "node_index", "node_id", "field",
 * "message"}]}. Malformed JSON is PDFB_INVALID_ARGUMENT. */
PDFB_API pdfb_status pdfb_validate_plan(const char* plan_json, char** out_result_json);

/* SHA-256 and page count of a PDF, to bind a hand-made plan to it:
 * {"sha256": "<hex>", "page_count": N}. */
PDFB_API pdfb_status pdfb_read_pdf_identity(const char* pdf_path, char** out_json);

#ifdef __cplusplus
}
#endif

#endif /* PDFBOOKMARK_PDFBOOKMARK_H */
