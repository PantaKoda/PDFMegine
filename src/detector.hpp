#pragma once

#include <vector>

#include <opencv2/core.hpp>
#include <onnxruntime_cxx_api.h>

#include <ocr/ocr.hpp>

namespace ocr::detail {

struct DetectorResize {
    cv::Mat image;
    int source_height;
    int source_width;
    float ratio_height;
    float ratio_width;
};

struct DetectorMap {
    std::vector<float> values;
    int height;
    int width;
};

struct ScoredQuad {
    Quad box;
    float score;
};

DetectorResize resize_for_det(const cv::Mat& source, const Options& options);
std::vector<float> to_det_tensor(const cv::Mat& resized_bgr);
DetectorMap run_detector(
    Ort::Session& session,
    std::vector<float>& input,
    int input_height,
    int input_width);
std::vector<ScoredQuad> boxes_from_bitmap(
    const float* probability_map,
    int map_height,
    int map_width,
    int original_height,
    int original_width,
    const Options& options);

}  // namespace ocr::detail
