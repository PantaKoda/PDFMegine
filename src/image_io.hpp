#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <opencv2/core.hpp>

#include <ocr/ocr.hpp>

namespace ocr::detail {

cv::Mat image_view(const ImageView& image);
cv::Mat decode_image(const std::uint8_t* bytes, std::size_t size);
cv::Mat load_image(const std::filesystem::path& path);

}  // namespace ocr::detail
