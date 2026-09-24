#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include <ocr/ocr.hpp>

namespace ocr::detail {

struct MiniBox {
    Quad quad;
    float shortest_side;
};

MiniBox get_mini_boxes(const std::vector<cv::Point2f>& contour);
float box_score_fast(const cv::Mat& probability_map, const Quad& box);
std::vector<std::vector<cv::Point2f>> unclip(
    const Quad& box,
    float unclip_ratio);
Quad order_points_clockwise(const Quad& box);
std::vector<Quad> filter_tag_det_res(
    const std::vector<Quad>& boxes,
    int image_height,
    int image_width);
std::vector<Quad> sorted_boxes(std::vector<Quad> boxes, int same_row_tolerance);
cv::Mat get_rotate_crop_image(const cv::Mat& original_bgr, const Quad& box);

}  // namespace ocr::detail
