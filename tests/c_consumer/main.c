#include <ocr/ocr.h>

#include <stdio.h>
#include <stdlib.h>

static int report_error(const char* operation, ocr_status status, char* error) {
    fprintf(
        stderr,
        "%s failed (%d): %s\n",
        operation,
        (int)status,
        error == NULL ? "no error message" : error);
    ocr_free_error(error);
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(
            stderr,
            "usage: ocr_c_consumer DET_MODEL REC_MODEL CHARSET IMAGE\n");
        return 2;
    }

    ocr_engine* engine = NULL;
    char* error = NULL;
    ocr_options options = ocr_default_options();
    ocr_status status = ocr_engine_create(
        argv[1], argv[2], argv[3], &options, &engine, &error);
    if (status != OCR_STATUS_OK) {
        return report_error("ocr_engine_create", status, error);
    }

    ocr_text_line* lines = NULL;
    size_t count = 0;
    status = ocr_engine_run_file(
        engine, argv[4], &lines, &count, &error);
    if (status != OCR_STATUS_OK) {
        ocr_engine_destroy(engine);
        return report_error("ocr_engine_run_file", status, error);
    }

    printf("lines=%zu\n", count);
    if (count != 0U) {
        printf("first=%s\n", lines[0].text);
    }
    const size_t file_count = count;
    ocr_free_text_lines(lines, count);

    FILE* file = fopen(argv[4], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        ocr_engine_destroy(engine);
        fprintf(stderr, "could not open encoded image\n");
        return 1;
    }
    const long encoded_size = ftell(file);
    if (encoded_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        ocr_engine_destroy(engine);
        fprintf(stderr, "could not size encoded image\n");
        return 1;
    }
    uint8_t* encoded = (uint8_t*)malloc((size_t)encoded_size);
    if (encoded == NULL ||
        fread(encoded, 1U, (size_t)encoded_size, file) !=
            (size_t)encoded_size) {
        free(encoded);
        fclose(file);
        ocr_engine_destroy(engine);
        fprintf(stderr, "could not read encoded image\n");
        return 1;
    }
    fclose(file);
    lines = NULL;
    count = 0U;
    status = ocr_engine_run_encoded(
        engine,
        encoded,
        (size_t)encoded_size,
        &lines,
        &count,
        &error);
    free(encoded);
    if (status != OCR_STATUS_OK) {
        ocr_engine_destroy(engine);
        return report_error("ocr_engine_run_encoded", status, error);
    }
    if (count != file_count) {
        ocr_free_text_lines(lines, count);
        ocr_engine_destroy(engine);
        fprintf(stderr, "encoded and file result counts differ\n");
        return 1;
    }
    ocr_free_text_lines(lines, count);

    uint8_t blank_pixels[20U * 20U * 3U] = {0};
    const ocr_image_view blank = {blank_pixels, 20, 20, 0};
    ocr_quad* quads = NULL;
    count = 0U;
    status = ocr_engine_detect(engine, &blank, &quads, &count, &error);
    if (status != OCR_STATUS_OK) {
        ocr_engine_destroy(engine);
        return report_error("ocr_engine_detect", status, error);
    }
    printf("blank_quads=%zu\n", count);
    ocr_free_quads(quads);

    ocr_engine_destroy(engine);
    return 0;
}
