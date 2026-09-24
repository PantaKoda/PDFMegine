/* C API test: plain C, uses ONLY <pdfbookmark/pdfbookmark.h>.
 * Usage: pdfbookmark_c_api_tests <engine fixtures dir> <front_matter.pdf> <work dir>
 * (paths in UTF-8; the work dir must exist). */
#include <pdfbookmark/pdfbookmark.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s (last error: %s)\n", what, pdfb_last_error());
        exit(1);
    }
}

static int contains(const char* text, const char* part) {
    return text && strstr(text, part) != NULL;
}

static void join(char* out, size_t size, const char* dir, const char* name) {
    snprintf(out, size, "%s/%s", dir, name);
}

static size_t g_progress_calls = 0;
static void on_progress(void* user_data, const char* stage, size_t done, size_t total) {
    (void)stage; (void)done; (void)total;
    require(user_data == &g_progress_calls, "progress receives user_data");
    ++g_progress_calls;
}

int main(int argc, char** argv) {
    char input[4096], boundary[4096], output[4096];
    char *report = NULL, *plan = NULL, *result = NULL;
    pdfb_status status;
    pdfb_cancel_token* token;

    require(argc == 4, "usage: fixtures-dir front_matter.pdf work-dir");
    join(input, sizeof input, argv[1], "boundary_outlined.pdf");
    join(boundary, sizeof boundary, argv[1], "boundary.pdf");
    join(output, sizeof output, argv[3], "c_api_out.pdf");
    remove(output);

    /* Library info. */
    require(pdfb_c_api_version() == PDFB_C_API_VERSION, "C API version matches");
    require(strlen(pdfb_version()) >= 5, "version string");
    require(strcmp(pdfb_status_name(PDFB_INPUT_CHANGED), "input_changed") == 0, "status names");
    require(pdfb_find_models(&result) == PDFB_OK && result != NULL, "find_models");
    pdfb_free(result);

    /* Text of one page, PDF text only. */
    status = pdfb_extract_text(boundary, "{\"mode\":\"embedded\",\"pages\":[39]}", NULL, NULL,
                               NULL, &report);
    require(status == PDFB_OK && contains(report, "\"page_index\": 39"), "extract_text");
    pdfb_free(report);

    /* Analyze -> plan -> validate -> apply. */
    status = pdfb_analyze(input, "{\"mode\":\"embedded\",\"titles\":\"printed\"}", NULL,
                          on_progress, &g_progress_calls, &report, &plan);
    require(status == PDFB_OK && contains(report, "\"outcome\": \"plan_ready\"") && plan,
            "analyze returns a ready plan");
    require(g_progress_calls > 0, "progress callback called");
    pdfb_free(report);

    require(pdfb_validate_plan(plan, &result) == PDFB_OK && contains(result, "\"valid\": true"),
            "validate_plan");
    pdfb_free(result);

    require(pdfb_read_pdf_identity(input, &result) == PDFB_OK &&
                contains(result, "\"page_count\""),
            "read_pdf_identity");
    pdfb_free(result);

    status = pdfb_apply(input, output, plan, NULL, NULL, &result);
    require(status == PDFB_OK && contains(result, "\"committed\": true") &&
                contains(result, "\"structure_matches\": true"),
            "apply writes a verified copy");
    pdfb_free(result);

    /* Safety and errors are status codes with messages. */
    status = pdfb_apply(input, output, plan, NULL, NULL, &result);
    require(status == PDFB_OUTPUT_EXISTS && result == NULL && *pdfb_last_error(),
            "existing output refused");
    status = pdfb_apply(input, input, plan, "{\"replace_existing_output\":true}", NULL, NULL);
    require(status == PDFB_INVALID_ARGUMENT, "the input can never be the output");
    status = pdfb_apply(boundary, output, plan, "{\"replace_existing_output\":true}", NULL, NULL);
    require(status == PDFB_INPUT_CHANGED, "a plan for another PDF is refused");
    pdfb_free(plan);

    require(pdfb_analyze(input, "{\"colour\":1}", NULL, NULL, NULL, &report, NULL) ==
                    PDFB_INVALID_ARGUMENT &&
                report == NULL && contains(pdfb_last_error(), "colour"),
            "unknown option rejected");
    require(pdfb_analyze(input, "{\"mode\":\"fast\"}", NULL, NULL, NULL, &report, NULL) ==
                PDFB_INVALID_ARGUMENT,
            "bad option value rejected");
    require(pdfb_validate_plan("{\"nodes\":", &result) == PDFB_INVALID_ARGUMENT,
            "malformed plan JSON rejected");
    join(output, sizeof output, argv[3], "missing.pdf");
    require(pdfb_analyze(output, NULL, NULL, NULL, NULL, &report, NULL) != PDFB_OK &&
                *pdfb_last_error(),
            "missing input is an error");
    require(pdfb_analyze(input, NULL, NULL, NULL, NULL, NULL, NULL) == PDFB_INVALID_ARGUMENT,
            "NULL output pointer rejected");

    /* Cancellation. */
    token = pdfb_cancel_token_new();
    require(token != NULL, "cancel token");
    pdfb_cancel_token_cancel(token);
    status = pdfb_analyze(input, NULL, token, NULL, NULL, &report, &plan);
    require(status == PDFB_OK && contains(report, "\"outcome\": \"cancelled\"") && plan == NULL,
            "cooperative cancellation");
    pdfb_free(report);
    pdfb_cancel_token_free(token);

    /* Metadata. */
    status = pdfb_extract_metadata(argv[2], "{\"mode\":\"embedded\"}", NULL, &report);
    require(status == PDFB_OK && contains(report, "Parallel Worlds"), "extract_metadata");
    pdfb_free(report);

    printf("pdfbookmark %s C API passed\n", pdfb_version());
    return 0;
}
