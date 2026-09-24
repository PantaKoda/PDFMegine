#include "image_io.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <opencv2/imgcodecs.hpp>

namespace ocr::detail {

cv::Mat image_view(const ImageView& image) {
    if (image.data == nullptr) {
        throw std::invalid_argument("image data is null");
    }
    if (image.width <= 0 || image.height <= 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    if (image.width > std::numeric_limits<int>::max() / 3) {
        throw std::invalid_argument("image width is too large");
    }
    const int packed_stride = image.width * 3;
    const int stride = image.stride == 0 ? packed_stride : image.stride;
    if (stride < packed_stride) {
        throw std::invalid_argument("image stride is smaller than width * 3");
    }
    return cv::Mat(
        image.height,
        image.width,
        CV_8UC3,
        const_cast<std::uint8_t*>(image.data),
        static_cast<std::size_t>(stride));
}

cv::Mat decode_image(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size == 0U) {
        throw std::invalid_argument("encoded image is empty");
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("encoded image exceeds OpenCV's size limit");
    }
    const cv::Mat encoded(
        1,
        static_cast<int>(size),
        CV_8UC1,
        const_cast<std::uint8_t*>(bytes));
    cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (decoded.empty()) {
        throw std::runtime_error("OpenCV could not decode the image");
    }
    return decoded;
}

cv::Mat load_image(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("image file does not exist or cannot be read: " +
                                 path.string());
    }
    const std::streampos end = stream.tellg();
    if (end <= 0) {
        throw std::runtime_error("image file is empty: " + path.string());
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("image file is too large: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("failed to read image file: " + path.string());
    }
    return decode_image(bytes.data(), bytes.size());
}

}  // namespace ocr::detail
