#include "detector.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace ocr::detail {
namespace {

constexpr int kTinyDimensionSum = 64;
constexpr int kMinimumDetectorSide = 32;
constexpr int kDetectorStride = 32;
constexpr std::array<float, 3> kDetectorMean{0.485F, 0.456F, 0.406F};
constexpr std::array<float, 3> kDetectorStd{0.229F, 0.224F, 0.225F};
constexpr float kPixelScale = 1.0F / 255.0F;

int align_detector_side(int dimension) {
    const double units = static_cast<double>(dimension) /
                         static_cast<double>(kDetectorStride);
    return std::max(
        static_cast<int>(std::nearbyint(units) * kDetectorStride),
        kMinimumDetectorSide);
}

void validate_options(const Options& options) {
    if (options.limit_side_len <= 0) {
        throw std::invalid_argument("limit_side_len must be positive");
    }
    if (options.max_side_limit <= 0) {
        throw std::invalid_argument("max_side_limit must be positive");
    }
}

}  // namespace

DetectorResize resize_for_det(const cv::Mat& source, const Options& options) {
    validate_options(options);
    if (source.empty()) {
        throw std::invalid_argument("resize_for_det received an empty image");
    }
    if (source.type() != CV_8UC3) {
        throw std::invalid_argument("resize_for_det requires an 8-bit three-channel BGR image");
    }

    const int source_height = source.rows;
    const int source_width = source.cols;
    cv::Mat working = source;
    if (source_height + source_width < kTinyDimensionSum) {
        working = cv::Mat::zeros(
            std::max(kMinimumDetectorSide, source_height),
            std::max(kMinimumDetectorSide, source_width),
            source.type());
        source.copyTo(working(cv::Rect(0, 0, source_width, source_height)));
    }

    const int height = working.rows;
    const int width = working.cols;
    double ratio = 1.0;
    switch (options.limit_type) {
        case Options::LimitType::Min:
            if (std::min(height, width) < options.limit_side_len) {
                ratio = static_cast<double>(options.limit_side_len) /
                        static_cast<double>(std::min(height, width));
            }
            break;
        case Options::LimitType::Max:
            if (std::max(height, width) > options.limit_side_len) {
                ratio = static_cast<double>(options.limit_side_len) /
                        static_cast<double>(std::max(height, width));
            }
            break;
        case Options::LimitType::ResizeLong:
            ratio = static_cast<double>(options.limit_side_len) /
                    static_cast<double>(std::max(height, width));
            break;
    }

    int resize_height = static_cast<int>(static_cast<double>(height) * ratio);
    int resize_width = static_cast<int>(static_cast<double>(width) * ratio);
    const int longest = std::max(resize_height, resize_width);
    if (longest > options.max_side_limit) {
        const double cap_ratio = static_cast<double>(options.max_side_limit) /
                                 static_cast<double>(longest);
        resize_height = static_cast<int>(static_cast<double>(resize_height) * cap_ratio);
        resize_width = static_cast<int>(static_cast<double>(resize_width) * cap_ratio);
    }

    resize_height = align_detector_side(resize_height);
    resize_width = align_detector_side(resize_width);
    cv::Mat resized;
    cv::resize(working, resized, cv::Size(resize_width, resize_height));
    return {
        std::move(resized),
        source_height,
        source_width,
        static_cast<float>(resize_height) / static_cast<float>(height),
        static_cast<float>(resize_width) / static_cast<float>(width),
    };
}

std::vector<float> to_det_tensor(const cv::Mat& resized_bgr) {
    if (resized_bgr.empty() || resized_bgr.type() != CV_8UC3) {
        throw std::invalid_argument("to_det_tensor requires an 8-bit three-channel BGR image");
    }
    const int height = resized_bgr.rows;
    const int width = resized_bgr.cols;
    const std::size_t plane = static_cast<std::size_t>(height) *
                              static_cast<std::size_t>(width);
    std::vector<float> tensor(3U * plane);
    for (int y = 0; y < height; ++y) {
        const auto* row = resized_bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x);
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const float scaled =
                    static_cast<float>(row[x][static_cast<int>(channel)]) * kPixelScale;
                tensor[channel * plane + pixel] =
                    (scaled - kDetectorMean[channel]) / kDetectorStd[channel];
            }
        }
    }
    return tensor;
}

DetectorMap run_detector(
    Ort::Session& session,
    std::vector<float>& input,
    int input_height,
    int input_width) {
    if (input_height <= 0 || input_width <= 0) {
        throw std::invalid_argument("detector input dimensions must be positive");
    }
    const std::size_t expected_size =
        3U * static_cast<std::size_t>(input_height) *
        static_cast<std::size_t>(input_width);
    if (input.size() != expected_size) {
        throw std::invalid_argument("detector input element count does not match shape");
    }

    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name = session.GetInputNameAllocated(0, allocator);
    auto output_name = session.GetOutputNameAllocated(0, allocator);
    const std::array<std::int64_t, 4> shape{
        1,
        3,
        static_cast<std::int64_t>(input_height),
        static_cast<std::int64_t>(input_width),
    };
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto input_value = Ort::Value::CreateTensor<float>(
        memory, input.data(), input.size(), shape.data(), shape.size());
    const char* input_names[] = {input_name.get()};
    const char* output_names[] = {output_name.get()};
    auto outputs = session.Run(
        Ort::RunOptions{nullptr}, input_names, &input_value, 1, output_names, 1);
    if (outputs.size() != 1U || !outputs[0].IsTensor()) {
        throw std::runtime_error("detector did not return one tensor");
    }
    const auto output_info = outputs[0].GetTensorTypeAndShapeInfo();
    const auto output_shape = output_info.GetShape();
    if (output_shape.size() != 4U || output_shape[0] != 1 || output_shape[1] != 1 ||
        output_shape[2] <= 0 || output_shape[3] <= 0) {
        throw std::runtime_error("detector output must have shape [1,1,H,W]");
    }
    const std::size_t output_size = output_info.GetElementCount();
    const float* output_data = outputs[0].GetTensorData<float>();
    return {
        std::vector<float>(output_data, output_data + output_size),
        static_cast<int>(output_shape[2]),
        static_cast<int>(output_shape[3]),
    };
}

std::vector<ScoredQuad> boxes_from_bitmap(
    const float* probability_map,
    int map_height,
    int map_width,
    int original_height,
    int original_width,
    const Options& options) {
    if (probability_map == nullptr || map_height <= 0 || map_width <= 0 ||
        original_height <= 0 || original_width <= 0) {
        throw std::invalid_argument("invalid DB post-processing input");
    }
    cv::Mat probabilities(
        map_height,
        map_width,
        CV_32FC1,
        const_cast<float*>(probability_map));
    cv::Mat bitmap;
    cv::compare(probabilities, options.bin_thresh, bitmap, cv::CMP_GT);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(
        bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    const std::size_t count = std::min(
        contours.size(),
        static_cast<std::size_t>(std::max(options.max_candidates, 0)));
    std::vector<ScoredQuad> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        std::vector<cv::Point2f> contour;
        contour.reserve(contours[index].size());
        for (const cv::Point& point : contours[index]) {
            contour.emplace_back(
                static_cast<float>(point.x),
                static_cast<float>(point.y));
        }
        const MiniBox first_box = get_mini_boxes(contour);
        if (first_box.shortest_side < 3.0F) {
            continue;
        }
        const float score = box_score_fast(probabilities, first_box.quad);
        if (options.box_thresh > score) {
            continue;
        }
        auto expanded = unclip(first_box.quad, options.unclip_ratio);
        if (expanded.size() != 1U || expanded[0].empty()) {
            continue;
        }
        const MiniBox expanded_box = get_mini_boxes(expanded[0]);
        if (expanded_box.shortest_side < 5.0F) {
            continue;
        }
        Quad mapped = expanded_box.quad;
        for (Point& point : mapped.p) {
            const double mapped_x = std::nearbyint(
                static_cast<double>(point.x) /
                static_cast<double>(map_width) *
                static_cast<double>(original_width));
            const double mapped_y = std::nearbyint(
                static_cast<double>(point.y) /
                static_cast<double>(map_height) *
                static_cast<double>(original_height));
            point.x = static_cast<float>(static_cast<int>(std::clamp(
                mapped_x, 0.0, static_cast<double>(original_width))));
            point.y = static_cast<float>(static_cast<int>(std::clamp(
                mapped_y, 0.0, static_cast<double>(original_height))));
        }
        result.push_back({mapped, score});
    }
    return result;
}

}  // namespace ocr::detail
