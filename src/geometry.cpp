#include "geometry.hpp"

#include "opencv_geometry_compat.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <clipper2/clipper.offset.h>
#include <opencv2/imgproc.hpp>

namespace ocr::detail {
namespace {

cv::Point2f to_cv(const Point& point) {
    return {point.x, point.y};
}

Point from_cv(const cv::Point2f& point) {
    return {point.x, point.y};
}

float distance(const Point& first, const Point& second) {
    return std::hypot(first.x - second.x, first.y - second.y);
}

double decimal_value(float value) {
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general);
    if (converted.ec != std::errc{}) {
        throw std::runtime_error("could not convert float option to decimal");
    }
    return std::stod(std::string(buffer.data(), converted.ptr));
}

}  // namespace

MiniBox get_mini_boxes(const std::vector<cv::Point2f>& contour) {
    if (contour.empty()) {
        throw std::invalid_argument("get_mini_boxes received an empty contour");
    }
    const cv::RotatedRect rectangle = cv::minAreaRect(contour);
    std::array<cv::Point2f, 4> points{};
    rectangle.points(points.data());
    std::stable_sort(
        points.begin(),
        points.end(),
        [](const cv::Point2f& left, const cv::Point2f& right) {
            return left.x < right.x;
        });

    std::size_t top_left = 0U;
    std::size_t bottom_left = 1U;
    if (points[1].y <= points[0].y) {
        top_left = 1U;
        bottom_left = 0U;
    }
    std::size_t top_right = 2U;
    std::size_t bottom_right = 3U;
    if (points[3].y <= points[2].y) {
        top_right = 3U;
        bottom_right = 2U;
    }
    return {
        {{
            from_cv(points[top_left]),
            from_cv(points[top_right]),
            from_cv(points[bottom_right]),
            from_cv(points[bottom_left]),
        }},
        std::min(rectangle.size.width, rectangle.size.height),
    };
}

float box_score_fast(const cv::Mat& probability_map, const Quad& box) {
    if (probability_map.empty() || probability_map.type() != CV_32FC1) {
        throw std::invalid_argument("box_score_fast requires a CV_32FC1 map");
    }
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const Point& point : box.p) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }
    const int xmin = std::clamp(
        static_cast<int>(std::floor(min_x)), 0, probability_map.cols - 1);
    const int xmax = std::clamp(
        static_cast<int>(std::ceil(max_x)), 0, probability_map.cols - 1);
    const int ymin = std::clamp(
        static_cast<int>(std::floor(min_y)), 0, probability_map.rows - 1);
    const int ymax = std::clamp(
        static_cast<int>(std::ceil(max_y)), 0, probability_map.rows - 1);
    cv::Mat mask = cv::Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8UC1);
    std::vector<cv::Point> shifted;
    shifted.reserve(4U);
    for (const Point& point : box.p) {
        shifted.emplace_back(
            static_cast<int>(point.x - static_cast<float>(xmin)),
            static_cast<int>(point.y - static_cast<float>(ymin)));
    }
    const std::vector<std::vector<cv::Point>> polygons{shifted};
    cv::fillPoly(mask, polygons, cv::Scalar(1));
    const cv::Mat region = probability_map(
        cv::Rect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1));
    return static_cast<float>(cv::mean(region, mask)[0]);
}

std::vector<std::vector<cv::Point2f>> unclip(
    const Quad& box,
    float unclip_ratio) {
    double twice_signed_area = 0.0;
    double perimeter = 0.0;
    Clipper2Lib::Path64 path;
    path.reserve(4U);
    for (std::size_t index = 0; index < 4U; ++index) {
        const Point& current = box.p[index];
        const Point& next = box.p[(index + 1U) % 4U];
        twice_signed_area +=
            static_cast<double>(current.x) * static_cast<double>(next.y) -
            static_cast<double>(next.x) * static_cast<double>(current.y);
        perimeter += static_cast<double>(distance(current, next));
        path.emplace_back(
            static_cast<std::int64_t>(current.x),
            static_cast<std::int64_t>(current.y));
    }
    if (perimeter == 0.0) {
        return {};
    }
    const double area = std::abs(twice_signed_area) * 0.5;
    const double offset_distance =
        area * decimal_value(unclip_ratio) / perimeter;
    Clipper2Lib::ClipperOffset offset(2.0, 0.25);
    offset.AddPath(path, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon);
    Clipper2Lib::Paths64 expanded;
    offset.Execute(offset_distance, expanded);

    std::vector<std::vector<cv::Point2f>> result;
    result.reserve(expanded.size());
    for (const auto& expanded_path : expanded) {
        std::vector<cv::Point2f> points;
        points.reserve(expanded_path.size());
        for (const auto& point : expanded_path) {
            points.emplace_back(
                static_cast<float>(point.x),
                static_cast<float>(point.y));
        }
        result.push_back(std::move(points));
    }
    return result;
}

Quad order_points_clockwise(const Quad& box) {
    std::size_t minimum_sum = 0U;
    std::size_t maximum_sum = 0U;
    float minimum = box.p[0].x + box.p[0].y;
    float maximum = minimum;
    for (std::size_t index = 1U; index < 4U; ++index) {
        const float sum = box.p[index].x + box.p[index].y;
        if (sum < minimum) {
            minimum = sum;
            minimum_sum = index;
        }
        if (sum > maximum) {
            maximum = sum;
            maximum_sum = index;
        }
    }
    std::array<std::size_t, 2> remaining{};
    std::size_t remaining_index = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (index != minimum_sum && index != maximum_sum) {
            remaining[remaining_index++] = index;
        }
    }
    const float first_difference =
        box.p[remaining[0]].y - box.p[remaining[0]].x;
    const float second_difference =
        box.p[remaining[1]].y - box.p[remaining[1]].x;
    const std::size_t top_right =
        first_difference < second_difference ? remaining[0] : remaining[1];
    const std::size_t bottom_left =
        first_difference < second_difference ? remaining[1] : remaining[0];
    return {{
        box.p[minimum_sum],
        box.p[top_right],
        box.p[maximum_sum],
        box.p[bottom_left],
    }};
}

std::vector<Quad> filter_tag_det_res(
    const std::vector<Quad>& boxes,
    int image_height,
    int image_width) {
    if (image_height <= 0 || image_width <= 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    std::vector<Quad> result;
    result.reserve(boxes.size());
    for (const Quad& input : boxes) {
        Quad box = order_points_clockwise(input);
        for (Point& point : box.p) {
            point.x = static_cast<float>(static_cast<int>(std::clamp(
                point.x, 0.0F, static_cast<float>(image_width - 1))));
            point.y = static_cast<float>(static_cast<int>(std::clamp(
                point.y, 0.0F, static_cast<float>(image_height - 1))));
        }
        const int width = static_cast<int>(distance(box.p[0], box.p[1]));
        const int height = static_cast<int>(distance(box.p[0], box.p[3]));
        if (width <= 3 || height <= 3) {
            continue;
        }
        result.push_back(box);
    }
    return result;
}

std::vector<Quad> sorted_boxes(std::vector<Quad> boxes, int same_row_tolerance) {
    if (same_row_tolerance < 0) {
        throw std::invalid_argument("same-row tolerance cannot be negative");
    }
    std::stable_sort(
        boxes.begin(),
        boxes.end(),
        [](const Quad& left, const Quad& right) {
            if (left.p[0].y != right.p[0].y) {
                return left.p[0].y < right.p[0].y;
            }
            return left.p[0].x < right.p[0].x;
        });
    for (std::size_t index = 0U; index + 1U < boxes.size(); ++index) {
        for (std::size_t current = index + 1U; current > 0U; --current) {
            Quad& left = boxes[current - 1U];
            Quad& right = boxes[current];
            if (std::abs(right.p[0].y - left.p[0].y) <
                    static_cast<float>(same_row_tolerance) &&
                right.p[0].x < left.p[0].x) {
                std::swap(left, right);
            } else {
                break;
            }
        }
    }
    return boxes;
}

cv::Mat get_rotate_crop_image(const cv::Mat& original_bgr, const Quad& box) {
    if (original_bgr.empty() || original_bgr.type() != CV_8UC3) {
        throw std::invalid_argument(
            "get_rotate_crop_image requires an 8-bit three-channel BGR image");
    }
    const int crop_width = static_cast<int>(std::max(
        distance(box.p[0], box.p[1]),
        distance(box.p[2], box.p[3])));
    const int crop_height = static_cast<int>(std::max(
        distance(box.p[0], box.p[3]),
        distance(box.p[1], box.p[2])));
    if (crop_width <= 0 || crop_height <= 0) {
        throw std::invalid_argument("crop dimensions must be positive");
    }
    const std::array<cv::Point2f, 4> source{
        to_cv(box.p[0]),
        to_cv(box.p[1]),
        to_cv(box.p[2]),
        to_cv(box.p[3]),
    };
    const std::array<cv::Point2f, 4> destination{
        cv::Point2f(0.0F, 0.0F),
        cv::Point2f(static_cast<float>(crop_width), 0.0F),
        cv::Point2f(
            static_cast<float>(crop_width),
            static_cast<float>(crop_height)),
        cv::Point2f(0.0F, static_cast<float>(crop_height)),
    };
    const cv::Mat transform = cv::getPerspectiveTransform(
        source.data(), destination.data(), cv::DECOMP_LU);
    cv::Mat crop;
    cv::warpPerspective(
        original_bgr,
        crop,
        transform,
        cv::Size(crop_width, crop_height),
        cv::INTER_CUBIC,
        cv::BORDER_REPLICATE);
    if (static_cast<float>(crop_height) / static_cast<float>(crop_width) >= 1.5F) {
        cv::Mat rotated;
        cv::rotate(crop, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
        crop = std::move(rotated);
    }
    return crop;
}

}  // namespace ocr::detail
