#include <ocr/ocr.hpp>

#include "detector.hpp"
#include "geometry.hpp"
#include "image_io.hpp"
#include "ort_util.hpp"
#include "recognizer.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace ocr {

struct Engine::Impl {
    Impl(const std::filesystem::path& det_model,
         const std::filesystem::path& rec_model,
         const std::filesystem::path& charset_path,
         const Options& options)
        : opts(options),
          environment(ORT_LOGGING_LEVEL_WARNING, "ocr"),
          det_session(detail::create_session(environment, det_model, opts.threads)),
          rec_session(detail::create_session(environment, rec_model, opts.threads)) {
        std::ifstream stream(charset_path, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("charset file does not exist: " + charset_path.string());
        }
        std::string token;
        while (std::getline(stream, token)) {
            if (!token.empty() && token.back() == '\r') {
                token.pop_back();
            }
            if (token.empty()) {
                throw std::runtime_error("charset contains a blank token");
            }
            charset.push_back(token);
        }
        if (charset.empty()) {
            throw std::runtime_error("charset is empty");
        }
        const auto outputs = detail::describe_outputs(*rec_session);
        if (outputs.size() != 1 || outputs[0].shape.size() != 3) {
            throw std::runtime_error("recognizer must expose one rank-3 output");
        }
        const std::int64_t classes = outputs[0].shape.back();
        const auto expected = static_cast<std::int64_t>(charset.size()) + 2;
        if (classes != expected) {
            throw std::runtime_error(
                "recognizer class dimension does not match charset: C=" +
                std::to_string(classes) + ", expected " + std::to_string(expected));
        }
    }

    std::vector<Quad> detect(const cv::Mat& image) const {
        const auto resized = detail::resize_for_det(image, opts);
        auto tensor = detail::to_det_tensor(resized.image);
        const auto map = detail::run_detector(
            *det_session, tensor, resized.image.rows, resized.image.cols);
        const auto scored = detail::boxes_from_bitmap(
            map.values.data(),
            map.height,
            map.width,
            resized.source_height,
            resized.source_width,
            opts);
        std::vector<Quad> boxes;
        boxes.reserve(scored.size());
        for (const detail::ScoredQuad& item : scored) {
            boxes.push_back(item.box);
        }
        return detail::filter_tag_det_res(
            boxes, resized.source_height, resized.source_width);
    }

    std::vector<TextLine> run(const cv::Mat& image) const {
        auto boxes = detail::sorted_boxes(detect(image), opts.same_row_tol);
        std::vector<cv::Mat> crops;
        crops.reserve(boxes.size());
        for (const Quad& box : boxes) {
            crops.push_back(detail::get_rotate_crop_image(image, box));
        }
        const auto recognized =
            detail::recognize(*rec_session, crops, charset, opts);
        if (recognized.size() != boxes.size()) {
            throw std::runtime_error(
                "recognizer result count does not match detected boxes");
        }
        std::vector<TextLine> lines;
        lines.reserve(boxes.size());
        for (std::size_t index = 0U; index < boxes.size(); ++index) {
            if (recognized[index].confidence < opts.drop_score) {
                continue;
            }
            lines.push_back({
                boxes[index],
                recognized[index].text,
                recognized[index].confidence,
            });
        }
        return lines;
    }

    Options opts;
    Ort::Env environment;
    std::unique_ptr<Ort::Session> det_session;
    std::unique_ptr<Ort::Session> rec_session;
    std::vector<std::string> charset;
};

Engine::Engine(const std::filesystem::path& det_model,
               const std::filesystem::path& rec_model,
               const std::filesystem::path& charset,
               const Options& opts)
    : impl_(std::make_unique<Impl>(det_model, rec_model, charset, opts)) {}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

std::vector<TextLine> Engine::run(const ImageView& image) const {
    return impl_->run(detail::image_view(image));
}

std::vector<TextLine> Engine::run_encoded(
    const std::uint8_t* bytes,
    std::size_t size) const {
    return impl_->run(detail::decode_image(bytes, size));
}

std::vector<TextLine> Engine::run_file(const std::filesystem::path& path) const {
    return impl_->run(detail::load_image(path));
}

std::vector<Quad> Engine::detect(const ImageView& image) const {
    return impl_->detect(detail::image_view(image));
}

}  // namespace ocr
