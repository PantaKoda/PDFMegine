extern "C" {
#include <ocr/ocr.h>
}

#include <ocr/ocr.hpp>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ocr_engine {
    ocr_engine(
        const std::filesystem::path& det_model,
        const std::filesystem::path& rec_model,
        const std::filesystem::path& charset,
        const ocr::Options& options)
        : value(det_model, rec_model, charset, options) {}

    ocr::Engine value;
};

namespace {

void set_error(char** output, const char* message) noexcept {
    if (output == nullptr) {
        return;
    }
    *output = nullptr;
    const std::size_t length = std::strlen(message);
    auto* copy = static_cast<char*>(std::malloc(length + 1U));
    if (copy == nullptr) {
        return;
    }
    std::memcpy(copy, message, length + 1U);
    *output = copy;
}

template <typename Function>
ocr_status translate_exceptions(char** error, Function&& function) noexcept {
    if (error != nullptr) {
        *error = nullptr;
    }
    try {
        std::forward<Function>(function)();
        return OCR_STATUS_OK;
    } catch (const std::invalid_argument& exception) {
        set_error(error, exception.what());
        return OCR_STATUS_INVALID_ARGUMENT;
    } catch (const std::bad_alloc& exception) {
        set_error(error, exception.what());
        return OCR_STATUS_OUT_OF_MEMORY;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return OCR_STATUS_RUNTIME_ERROR;
    } catch (...) {
        set_error(error, "unknown C++ exception");
        return OCR_STATUS_INTERNAL_ERROR;
    }
}

std::filesystem::path utf8_path(const char* value, const char* name) {
    if (value == nullptr || value[0] == '\0') {
        throw std::invalid_argument(std::string(name) + " path is empty");
    }
    return std::filesystem::u8path(value);
}

ocr::Options to_cpp_options(const ocr_options* input) {
    if (input == nullptr) {
        return {};
    }
    ocr::Options result;
    result.limit_side_len = input->limit_side_len;
    switch (input->limit_type) {
        case OCR_LIMIT_MIN:
            result.limit_type = ocr::Options::LimitType::Min;
            break;
        case OCR_LIMIT_MAX:
            result.limit_type = ocr::Options::LimitType::Max;
            break;
        case OCR_LIMIT_RESIZE_LONG:
            result.limit_type = ocr::Options::LimitType::ResizeLong;
            break;
        default:
            throw std::invalid_argument("unknown detector limit type");
    }
    result.max_side_limit = input->max_side_limit;
    result.bin_thresh = input->bin_thresh;
    result.box_thresh = input->box_thresh;
    result.max_candidates = input->max_candidates;
    result.unclip_ratio = input->unclip_ratio;
    result.rec_height = input->rec_height;
    result.rec_base_width = input->rec_base_width;
    result.rec_batch_size = input->rec_batch_size;
    result.drop_score = input->drop_score;
    result.same_row_tol = input->same_row_tol;
    result.threads = input->threads;
    return result;
}

ocr::ImageView to_cpp_image(const ocr_image_view* image) {
    if (image == nullptr) {
        throw std::invalid_argument("image view is null");
    }
    return {image->data, image->width, image->height, image->stride};
}

ocr_quad to_c_quad(const ocr::Quad& input) {
    ocr_quad result{};
    for (std::size_t index = 0U; index < 4U; ++index) {
        result.p[index] = {input.p[index].x, input.p[index].y};
    }
    return result;
}

void free_lines(ocr_text_line* lines, std::size_t count) noexcept {
    if (lines == nullptr) {
        return;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        std::free(const_cast<char*>(lines[index].text));
    }
    std::free(lines);
}

ocr_text_line* copy_lines(const std::vector<ocr::TextLine>& input) {
    if (input.empty()) {
        return nullptr;
    }
    auto* result = static_cast<ocr_text_line*>(
        std::calloc(input.size(), sizeof(ocr_text_line)));
    if (result == nullptr) {
        throw std::bad_alloc();
    }
    try {
        for (std::size_t index = 0U; index < input.size(); ++index) {
            result[index].box = to_c_quad(input[index].box);
            result[index].confidence = input[index].confidence;
            auto* text = static_cast<char*>(
                std::malloc(input[index].text.size() + 1U));
            if (text == nullptr) {
                throw std::bad_alloc();
            }
            std::memcpy(
                text,
                input[index].text.c_str(),
                input[index].text.size() + 1U);
            result[index].text = text;
        }
    } catch (...) {
        free_lines(result, input.size());
        throw;
    }
    return result;
}

ocr_quad* copy_quads(const std::vector<ocr::Quad>& input) {
    if (input.empty()) {
        return nullptr;
    }
    auto* result = static_cast<ocr_quad*>(
        std::malloc(input.size() * sizeof(ocr_quad)));
    if (result == nullptr) {
        throw std::bad_alloc();
    }
    for (std::size_t index = 0U; index < input.size(); ++index) {
        result[index] = to_c_quad(input[index]);
    }
    return result;
}

void validate_line_outputs(
    ocr_text_line** lines,
    std::size_t* count) {
    if (lines == nullptr || count == nullptr) {
        throw std::invalid_argument("line output pointers are null");
    }
    *lines = nullptr;
    *count = 0U;
}

}  // namespace

extern "C" {

ocr_options ocr_default_options(void) {
    const ocr::Options source;
    return {
        source.limit_side_len,
        OCR_LIMIT_MIN,
        source.max_side_limit,
        source.bin_thresh,
        source.box_thresh,
        source.max_candidates,
        source.unclip_ratio,
        source.rec_height,
        source.rec_base_width,
        source.rec_batch_size,
        source.drop_score,
        source.same_row_tol,
        source.threads,
    };
}

ocr_status ocr_engine_create(
    const char* det_model_utf8,
    const char* rec_model_utf8,
    const char* charset_utf8,
    const ocr_options* options,
    ocr_engine** out_engine,
    char** out_error) {
    if (out_engine != nullptr) {
        *out_engine = nullptr;
    }
    return translate_exceptions(out_error, [&]() {
        if (out_engine == nullptr) {
            throw std::invalid_argument("engine output pointer is null");
        }
        auto* engine = new ocr_engine(
            utf8_path(det_model_utf8, "detector model"),
            utf8_path(rec_model_utf8, "recognizer model"),
            utf8_path(charset_utf8, "charset"),
            to_cpp_options(options));
        *out_engine = engine;
    });
}

void ocr_engine_destroy(ocr_engine* engine) {
    delete engine;
}

ocr_status ocr_engine_run(
    const ocr_engine* engine,
    const ocr_image_view* image,
    ocr_text_line** out_lines,
    size_t* out_count,
    char** out_error) {
    return translate_exceptions(out_error, [&]() {
        validate_line_outputs(out_lines, out_count);
        if (engine == nullptr) {
            throw std::invalid_argument("engine is null");
        }
        const auto lines = engine->value.run(to_cpp_image(image));
        *out_lines = copy_lines(lines);
        *out_count = lines.size();
    });
}

ocr_status ocr_engine_run_encoded(
    const ocr_engine* engine,
    const uint8_t* bytes,
    size_t size,
    ocr_text_line** out_lines,
    size_t* out_count,
    char** out_error) {
    return translate_exceptions(out_error, [&]() {
        validate_line_outputs(out_lines, out_count);
        if (engine == nullptr) {
            throw std::invalid_argument("engine is null");
        }
        const auto lines = engine->value.run_encoded(bytes, size);
        *out_lines = copy_lines(lines);
        *out_count = lines.size();
    });
}

ocr_status ocr_engine_run_file(
    const ocr_engine* engine,
    const char* path_utf8,
    ocr_text_line** out_lines,
    size_t* out_count,
    char** out_error) {
    return translate_exceptions(out_error, [&]() {
        validate_line_outputs(out_lines, out_count);
        if (engine == nullptr) {
            throw std::invalid_argument("engine is null");
        }
        const auto lines =
            engine->value.run_file(utf8_path(path_utf8, "image"));
        *out_lines = copy_lines(lines);
        *out_count = lines.size();
    });
}

ocr_status ocr_engine_detect(
    const ocr_engine* engine,
    const ocr_image_view* image,
    ocr_quad** out_quads,
    size_t* out_count,
    char** out_error) {
    return translate_exceptions(out_error, [&]() {
        if (out_quads == nullptr || out_count == nullptr) {
            throw std::invalid_argument("quad output pointers are null");
        }
        *out_quads = nullptr;
        *out_count = 0U;
        if (engine == nullptr) {
            throw std::invalid_argument("engine is null");
        }
        const auto quads = engine->value.detect(to_cpp_image(image));
        *out_quads = copy_quads(quads);
        *out_count = quads.size();
    });
}

void ocr_free_text_lines(ocr_text_line* lines, size_t count) {
    free_lines(lines, count);
}

void ocr_free_quads(ocr_quad* quads) {
    std::free(quads);
}

void ocr_free_error(char* error) {
    std::free(error);
}

}  // extern "C"
