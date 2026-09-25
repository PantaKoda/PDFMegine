#include "ocr_adapter.hpp"

#include <ocr/ocr.hpp>
#include <opencv2/core/utils/logger.hpp>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>

namespace pdfbookmark::text::detail {
namespace {

class PackageOcr final : public OcrBackend {
public:
    PackageOcr(const ModelResources& resources, int threads)
        : engine_(resources.detector, resources.recognizer, resources.charset,
                  options_with(threads)) {}

    std::vector<OcrLine> run(const Raster& raster) override {
        const ocr::ImageView view{raster.bgr.data(), raster.width, raster.height, raster.stride};
        const auto lines = engine_.run(view);
        std::vector<OcrLine> result;
        result.reserve(lines.size());
        for (const auto& line : lines) {
            OcrLine item;
            for (int i = 0; i < 4; ++i) {
                item.pixel_quad.points[static_cast<std::size_t>(i)] =
                    Point{line.box.p[i].x, line.box.p[i].y};
            }
            item.text = line.text;
            item.confidence = line.confidence;
            result.push_back(std::move(item));
        }
        return result;
    }

private:
    static ocr::Options options_with(int threads) {
        ocr::Options options;  // The validated PP-OCRv6 profile, unchanged.
        options.threads = threads;
        return options;
    }

    ocr::Engine engine_;
};

// OpenCV (inside the OCR package) prints optional-plugin probes at INFO
// level to stdout in Debug builds, which would corrupt clients' output. Quiet
// it unless the process chose a level through OPENCV_LOG_LEVEL. OpenCV reads
// that variable before our code runs, so set the level through its API.
void quiet_opencv_logging() {
#ifdef _WIN32
    std::size_t length = 0;
    const bool chosen = getenv_s(&length, nullptr, 0, "OPENCV_LOG_LEVEL") != 0 || length != 0;
#else
    const bool chosen = std::getenv("OPENCV_LOG_LEVEL") != nullptr;
#endif
    if (!chosen) cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
}

}  // namespace

std::unique_ptr<OcrBackend> make_ocr_backend(const ModelResources& resources, int threads) {
    if (auto fake = make_override_ocr_backend(resources)) return fake;
    quiet_opencv_logging();
    return std::make_unique<PackageOcr>(resources, threads);
}

const char* ocr_backend_identity() noexcept { return "PP-OCRv6 medium via ocr::Engine"; }
bool ocr_backend_available() noexcept { return true; }

}  // namespace pdfbookmark::text::detail
