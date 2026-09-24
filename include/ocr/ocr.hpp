#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ocr {

/// A view over caller-owned BGR pixels. The engine does not own or copy them.
/// Pixels must have exactly three channels in B, G, R byte order.
struct ImageView {
    const std::uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;  ///< Bytes per row; 0 means width * 3.
};

struct Point {
    float x;
    float y;
};

/// Text quadrilateral, clockwise from top-left.
struct Quad {
    Point p[4];
};

struct TextLine {
    Quad box;
    std::string text;        ///< UTF-8.
    float confidence;        ///< Mean over retained CTC timesteps.
};

struct Options {
    int limit_side_len = 736;
    enum class LimitType { Min, Max, ResizeLong };
    LimitType limit_type = LimitType::Min;
    int max_side_limit = 4000;

    float bin_thresh = 0.2F;
    float box_thresh = 0.45F;
    int max_candidates = 3000;
    float unclip_ratio = 1.4F;

    int rec_height = 48;
    int rec_base_width = 320;
    int rec_batch_size = 6;

    float drop_score = 0.5F;
    int same_row_tol = 10;
    int threads = 1;
};

class Engine {
public:
    Engine(const std::filesystem::path& det_model,
           const std::filesystem::path& rec_model,
           const std::filesystem::path& charset,
           const Options& opts = {});
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    /// Runs detection and recognition on caller-owned BGR pixels.
    std::vector<TextLine> run(const ImageView& img) const;

    /// Decodes PNG, JPEG, or another OpenCV-supported encoded image.
    std::vector<TextLine> run_encoded(const std::uint8_t* bytes, std::size_t n) const;

    /// Loads and runs an image from disk.
    std::vector<TextLine> run_file(const std::filesystem::path& p) const;

    /// Runs detection only and returns filtered quads in contour order.
    std::vector<Quad> detect(const ImageView& img) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ocr
