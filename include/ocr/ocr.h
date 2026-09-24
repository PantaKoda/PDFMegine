#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct ocr_engine ocr_engine;

typedef enum ocr_status {
    OCR_STATUS_OK = 0,
    OCR_STATUS_INVALID_ARGUMENT = 1,
    OCR_STATUS_RUNTIME_ERROR = 2,
    OCR_STATUS_OUT_OF_MEMORY = 3,
    OCR_STATUS_INTERNAL_ERROR = 4
} ocr_status;

typedef enum ocr_limit_type {
    OCR_LIMIT_MIN = 0,
    OCR_LIMIT_MAX = 1,
    OCR_LIMIT_RESIZE_LONG = 2
} ocr_limit_type;

typedef struct ocr_image_view {
    const uint8_t* data;
    int width;
    int height;
    int stride;
} ocr_image_view;

typedef struct ocr_point {
    float x;
    float y;
} ocr_point;

typedef struct ocr_quad {
    ocr_point p[4];
} ocr_quad;

typedef struct ocr_text_line {
    ocr_quad box;
    /* UTF-8; owned by the returned array. */
    const char* text;
    float confidence;
} ocr_text_line;

typedef struct ocr_options {
    int limit_side_len;
    ocr_limit_type limit_type;
    int max_side_limit;

    float bin_thresh;
    float box_thresh;
    int max_candidates;
    float unclip_ratio;

    int rec_height;
    int rec_base_width;
    int rec_batch_size;

    float drop_score;
    int same_row_tol;
    int threads;
} ocr_options;

ocr_options ocr_default_options(void);

/* All path arguments are UTF-8. A null options pointer selects defaults. */
ocr_status ocr_engine_create(
    const char* det_model_utf8,
    const char* rec_model_utf8,
    const char* charset_utf8,
    const ocr_options* options,
    ocr_engine** out_engine,
    char** out_error);

void ocr_engine_destroy(ocr_engine* engine);

ocr_status ocr_engine_run(
    const ocr_engine* engine,
    const ocr_image_view* image,
    ocr_text_line** out_lines,
    size_t* out_count,
    char** out_error);

ocr_status ocr_engine_run_encoded(
    const ocr_engine* engine,
    const uint8_t* bytes,
    size_t size,
    ocr_text_line** out_lines,
    size_t* out_count,
    char** out_error);

ocr_status ocr_engine_run_file(
    const ocr_engine* engine,
    const char* path_utf8,
    ocr_text_line** out_lines,
    size_t* out_count,
    char** out_error);

ocr_status ocr_engine_detect(
    const ocr_engine* engine,
    const ocr_image_view* image,
    ocr_quad** out_quads,
    size_t* out_count,
    char** out_error);

void ocr_free_text_lines(ocr_text_line* lines, size_t count);
void ocr_free_quads(ocr_quad* quads);
void ocr_free_error(char* error);
