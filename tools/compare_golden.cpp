#include "detector.hpp"
#include "geometry.hpp"
#include "npy.hpp"
#include "ort_util.hpp"
#include "recognizer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

namespace {

constexpr double kDetectorTensorTolerance = 1.0e-5;
constexpr double kDetectorMapTolerance = 1.0e-3;
constexpr double kDbCornerTolerance = 1.0;
constexpr double kDbScoreTolerance = 1.0e-5;
constexpr double kCropMeanTolerance = 2.0 / 255.0;
constexpr double kRecognitionTensorTolerance = 1.0e-5;
constexpr double kRecognitionLogitTolerance = 1.0e-3;
constexpr double kRecognitionConfidenceTolerance = 1.0e-3;

double json_number(const std::string& json, const std::string& key) {
    const std::regex expression(
        "\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(json, match, expression)) {
        throw std::runtime_error("missing JSON number: " + key);
    }
    return std::stod(match[1].str());
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open file: " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

std::string json_array(const std::string& json, const std::string& key) {
    const std::size_t key_position = json.find("\"" + key + "\"");
    if (key_position == std::string::npos) {
        throw std::runtime_error("missing JSON array: " + key);
    }
    const std::size_t begin = json.find('[', key_position);
    if (begin == std::string::npos) {
        throw std::runtime_error("missing JSON array opening bracket: " + key);
    }
    int depth = 0;
    for (std::size_t index = begin; index < json.size(); ++index) {
        if (json[index] == '[') {
            ++depth;
        } else if (json[index] == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(begin, index - begin + 1U);
            }
        }
    }
    throw std::runtime_error("unterminated JSON array: " + key);
}

std::vector<double> parse_numbers(const std::string& text) {
    const std::regex expression(
        R"(-?[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::vector<double> result;
    for (auto iterator = std::sregex_iterator(text.begin(), text.end(), expression);
         iterator != std::sregex_iterator();
         ++iterator) {
        result.push_back(std::stod((*iterator)[0].str()));
    }
    return result;
}

std::vector<ocr::Quad> json_quads(const std::string& json) {
    const auto numbers = parse_numbers(json_array(json, "boxes"));
    if (numbers.size() % 8U != 0U) {
        throw std::runtime_error("JSON boxes do not contain quads");
    }
    std::vector<ocr::Quad> result(numbers.size() / 8U);
    for (std::size_t box = 0U; box < result.size(); ++box) {
        for (std::size_t point = 0U; point < 4U; ++point) {
            result[box].p[point] = {
                static_cast<float>(numbers[box * 8U + point * 2U]),
                static_cast<float>(numbers[box * 8U + point * 2U + 1U]),
            };
        }
    }
    return result;
}

std::vector<ocr::Quad> json_named_quads(
    const std::string& json,
    const std::string& key) {
    std::vector<ocr::Quad> result;
    const std::string needle = "\"" + key + "\"";
    std::size_t position = 0U;
    while ((position = json.find(needle, position)) != std::string::npos) {
        const auto numbers = parse_numbers(json_array(json.substr(position), key));
        if (numbers.size() != 8U) {
            throw std::runtime_error("JSON field is not one quad: " + key);
        }
        ocr::Quad quad{};
        for (std::size_t point = 0U; point < 4U; ++point) {
            quad.p[point] = {
                static_cast<float>(numbers[point * 2U]),
                static_cast<float>(numbers[point * 2U + 1U]),
            };
        }
        result.push_back(quad);
        position += needle.size();
    }
    return result;
}

std::string parse_json_string(const std::string& json, std::size_t& position) {
    if (position >= json.size() || json[position] != '"') {
        throw std::runtime_error("expected JSON string");
    }
    ++position;
    std::string result;
    while (position < json.size()) {
        const char value = json[position++];
        if (value == '"') {
            return result;
        }
        if (value != '\\') {
            result.push_back(value);
            continue;
        }
        if (position >= json.size()) {
            throw std::runtime_error("unterminated JSON escape");
        }
        const char escaped = json[position++];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                throw std::runtime_error("unsupported JSON string escape");
        }
    }
    throw std::runtime_error("unterminated JSON string");
}

std::vector<std::string> json_strings(
    const std::string& json,
    const std::string& key) {
    std::vector<std::string> result;
    const std::string needle = "\"" + key + "\"";
    std::size_t position = 0U;
    while ((position = json.find(needle, position)) != std::string::npos) {
        position = json.find(':', position + needle.size());
        if (position == std::string::npos) {
            throw std::runtime_error("missing JSON string colon");
        }
        position = json.find('"', position + 1U);
        if (position == std::string::npos) {
            throw std::runtime_error("missing JSON string value");
        }
        result.push_back(parse_json_string(json, position));
    }
    return result;
}

std::vector<double> json_numbers_for_key(
    const std::string& json,
    const std::string& key) {
    const std::regex expression(
        "\"" + key +
        "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::vector<double> result;
    for (auto iterator = std::sregex_iterator(json.begin(), json.end(), expression);
         iterator != std::sregex_iterator();
         ++iterator) {
        result.push_back(std::stod((*iterator)[1].str()));
    }
    return result;
}

std::vector<std::string> load_charset(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open charset: " + path.string());
    }
    std::vector<std::string> result;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            throw std::runtime_error("charset contains a blank token");
        }
        result.push_back(line);
    }
    return result;
}

double compare_quads(
    const std::vector<ocr::Quad>& actual,
    const std::vector<ocr::Quad>& expected) {
    if (actual.size() != expected.size()) {
        throw std::runtime_error(
            "box counts differ: actual=" + std::to_string(actual.size()) +
            " expected=" + std::to_string(expected.size()));
    }
    double maximum = 0.0;
    for (std::size_t box = 0U; box < actual.size(); ++box) {
        for (std::size_t point = 0U; point < 4U; ++point) {
            maximum = std::max(
                maximum,
                std::abs(static_cast<double>(actual[box].p[point].x) -
                         static_cast<double>(expected[box].p[point].x)));
            maximum = std::max(
                maximum,
                std::abs(static_cast<double>(actual[box].p[point].y) -
                         static_cast<double>(expected[box].p[point].y)));
        }
    }
    return maximum;
}

double crop_mean_absolute_difference(
    const cv::Mat& actual,
    const cv::Mat& expected) {
    if (actual.size() != expected.size() || actual.type() != expected.type()) {
        throw std::runtime_error("crop dimensions or types differ");
    }
    const std::size_t values =
        actual.total() * static_cast<std::size_t>(actual.channels());
    double absolute_sum = 0.0;
    for (int row = 0; row < actual.rows; ++row) {
        const auto* actual_row = actual.ptr<std::uint8_t>(row);
        const auto* expected_row = expected.ptr<std::uint8_t>(row);
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(actual.cols * actual.channels());
             ++index) {
            absolute_sum += std::abs(
                static_cast<double>(actual_row[index]) -
                static_cast<double>(expected_row[index]));
        }
    }
    return absolute_sum / static_cast<double>(values) / 255.0;
}

std::vector<std::filesystem::path> golden_directories(
    const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_directory()) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

double compare_values(
    const std::vector<float>& actual,
    const std::vector<float>& expected) {
    if (actual.size() != expected.size()) {
        throw std::runtime_error("tensor element counts differ");
    }
    double maximum = 0.0;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        maximum = std::max(
            maximum,
            std::abs(static_cast<double>(actual[index]) -
                     static_cast<double>(expected[index])));
    }
    return maximum;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5 && argc != 6) {
            throw std::invalid_argument(
                "usage: ocr_compare_golden GOLDEN_ROOT DET_MODEL REC_MODEL CHARSET [THREADS]");
        }
        // Optional ONNX Runtime thread count (default 1, the validated
        // baseline); used to check multi-threaded inference against the
        // same tolerances (PDFMegine issue #1).
        const int threads = argc == 6 ? std::stoi(argv[5]) : 1;
        const std::filesystem::path golden_root = argv[1];
        Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "ocr_compare_golden");
        auto detector_session =
            ocr::detail::create_session(environment, std::filesystem::path(argv[2]), threads);
        auto recognizer_session =
            ocr::detail::create_session(environment, std::filesystem::path(argv[3]), threads);
        const auto charset = load_charset(std::filesystem::path(argv[4]));
        const auto directories = golden_directories(golden_root);
        if (directories.empty()) {
            throw std::runtime_error("golden root has no image directories");
        }

        ocr::Options options;
        options.threads = threads;
        double overall_tensor_maximum = 0.0;
        double overall_map_maximum = 0.0;
        double overall_raw_corner_maximum = 0.0;
        double overall_final_corner_maximum = 0.0;
        double overall_sorted_corner_maximum = 0.0;
        double overall_score_maximum = 0.0;
        double overall_crop_mean_maximum = 0.0;
        double overall_rec_tensor_maximum = 0.0;
        double overall_rec_logit_maximum = 0.0;
        double overall_confidence_maximum = 0.0;
        for (const auto& directory : directories) {
            const auto decoded = ocr::tools::NpyArray::load(directory / "01_decoded.npy");
            if (decoded.descriptor() != "|u1" || decoded.shape().size() != 3U ||
                decoded.shape()[2] != 3U) {
                throw std::runtime_error("decoded image must be uint8 HWC BGR");
            }
            auto pixels = decoded.values<std::uint8_t>();
            cv::Mat image(
                static_cast<int>(decoded.shape()[0]),
                static_cast<int>(decoded.shape()[1]),
                CV_8UC3,
                pixels.data());
            const auto resized = ocr::detail::resize_for_det(image, options);
            const auto tensor = ocr::detail::to_det_tensor(resized.image);
            const auto expected_array =
                ocr::tools::NpyArray::load(directory / "02_det_input.npy");
            if (expected_array.descriptor() != "<f4" ||
                expected_array.shape().size() != 4U) {
                throw std::runtime_error("detector input must be float32 NCHW");
            }
            const auto expected = expected_array.values<float>();
            const double difference = compare_values(tensor, expected);
            overall_tensor_maximum = std::max(overall_tensor_maximum, difference);

            const std::string metadata = read_text(directory / "02_det_meta.json");
            const int expected_source_height =
                static_cast<int>(json_number(metadata, "src_h"));
            const int expected_source_width =
                static_cast<int>(json_number(metadata, "src_w"));
            const int expected_height =
                static_cast<int>(json_number(metadata, "resize_h"));
            const int expected_width =
                static_cast<int>(json_number(metadata, "resize_w"));
            const double expected_ratio_height = json_number(metadata, "ratio_h");
            const double expected_ratio_width = json_number(metadata, "ratio_w");

            const bool dimensions_match =
                resized.source_height == expected_source_height &&
                resized.source_width == expected_source_width &&
                resized.image.rows == expected_height &&
                resized.image.cols == expected_width;
            const bool ratios_match =
                resized.ratio_height == static_cast<float>(expected_ratio_height) &&
                resized.ratio_width == static_cast<float>(expected_ratio_width);
            auto mutable_tensor = tensor;
            const auto detector_map = ocr::detail::run_detector(
                *detector_session,
                mutable_tensor,
                resized.image.rows,
                resized.image.cols);
            const auto expected_map_array =
                ocr::tools::NpyArray::load(directory / "03_det_map.npy");
            if (expected_map_array.descriptor() != "<f4" ||
                expected_map_array.shape().size() != 4U ||
                expected_map_array.shape()[0] != 1U ||
                expected_map_array.shape()[1] != 1U ||
                detector_map.height !=
                    static_cast<int>(expected_map_array.shape()[2]) ||
                detector_map.width !=
                    static_cast<int>(expected_map_array.shape()[3])) {
                throw std::runtime_error("detector output shapes differ");
            }
            const double map_difference = compare_values(
                detector_map.values, expected_map_array.values<float>());
            overall_map_maximum = std::max(overall_map_maximum, map_difference);

            auto golden_map = expected_map_array.values<float>();
            const auto raw = ocr::detail::boxes_from_bitmap(
                golden_map.data(),
                static_cast<int>(expected_map_array.shape()[2]),
                static_cast<int>(expected_map_array.shape()[3]),
                resized.source_height,
                resized.source_width,
                options);
            std::vector<ocr::Quad> raw_boxes;
            std::vector<float> raw_scores;
            raw_boxes.reserve(raw.size());
            raw_scores.reserve(raw.size());
            for (const auto& item : raw) {
                raw_boxes.push_back(item.box);
                raw_scores.push_back(item.score);
            }
            const std::string raw_json =
                read_text(directory / "04_boxes_raw.json");
            const auto expected_raw = json_quads(raw_json);
            const double raw_corner_difference =
                compare_quads(raw_boxes, expected_raw);
            const auto expected_scores =
                parse_numbers(json_array(raw_json, "scores"));
            if (raw_scores.size() != expected_scores.size()) {
                throw std::runtime_error("DB score counts differ");
            }
            double score_difference = 0.0;
            for (std::size_t index = 0U; index < raw_scores.size(); ++index) {
                score_difference = std::max(
                    score_difference,
                    std::abs(static_cast<double>(raw_scores[index]) -
                             expected_scores[index]));
            }
            const auto filtered = ocr::detail::filter_tag_det_res(
                raw_boxes, resized.source_height, resized.source_width);
            const auto expected_filtered = json_quads(
                read_text(directory / "05_boxes_final.json"));
            const double final_corner_difference =
                compare_quads(filtered, expected_filtered);
            const auto ordered =
                ocr::detail::sorted_boxes(filtered, options.same_row_tol);
            const auto expected_ordered = json_quads(
                read_text(directory / "06_boxes_sorted.json"));
            const double sorted_corner_difference =
                compare_quads(ordered, expected_ordered);
            double crop_mean_maximum = 0.0;
            std::vector<cv::Mat> crops;
            crops.reserve(expected_ordered.size());
            for (std::size_t crop_index = 0U;
                 crop_index < expected_ordered.size();
                 ++crop_index) {
                const cv::Mat crop = ocr::detail::get_rotate_crop_image(
                    image, expected_ordered[crop_index]);
                std::ostringstream crop_name;
                crop_name << std::setfill('0') << std::setw(3) << crop_index << ".png";
                const cv::Mat expected_crop = cv::imread(
                    (directory / "07_crops" / crop_name.str()).string(),
                    cv::IMREAD_COLOR);
                if (expected_crop.empty()) {
                    throw std::runtime_error("missing golden crop: " + crop_name.str());
                }
                crops.push_back(expected_crop);
                crop_mean_maximum = std::max(
                    crop_mean_maximum,
                    crop_mean_absolute_difference(crop, expected_crop));
            }
            auto recognition_batches =
                ocr::detail::make_recognition_batches(crops, options);
            std::vector<ocr::detail::RecognitionResult> recognition_results(
                crops.size(), {"", 0.0F});
            double rec_tensor_maximum = 0.0;
            double rec_logit_maximum = 0.0;
            for (std::size_t batch_index = 0U;
                 batch_index < recognition_batches.size();
                 ++batch_index) {
                std::ostringstream stem;
                stem << std::setfill('0') << std::setw(3) << batch_index;
                auto& batch = recognition_batches[batch_index];
                const auto expected_rec_input = ocr::tools::NpyArray::load(
                    directory / ("08_rec_input_" + stem.str() + ".npy"));
                const std::vector<std::size_t> expected_input_shape{
                    batch.crop_indices.size(),
                    3U,
                    static_cast<std::size_t>(options.rec_height),
                    static_cast<std::size_t>(batch.width),
                };
                if (expected_rec_input.descriptor() != "<f4" ||
                    expected_rec_input.shape() != expected_input_shape) {
                    throw std::runtime_error("recognition input shapes differ");
                }
                rec_tensor_maximum = std::max(
                    rec_tensor_maximum,
                    compare_values(
                        batch.input,
                        expected_rec_input.values<float>()));
                const auto expected_indices = parse_numbers(read_text(
                    directory /
                    ("08_rec_input_" + stem.str() + "_indices.json")));
                if (expected_indices.size() != batch.crop_indices.size()) {
                    throw std::runtime_error("recognition batch index counts differ");
                }
                for (std::size_t index = 0U; index < expected_indices.size(); ++index) {
                    if (batch.crop_indices[index] !=
                        static_cast<std::size_t>(expected_indices[index])) {
                        throw std::runtime_error("recognition batch index order differs");
                    }
                }
                const auto logits = ocr::detail::run_recognizer_batch(
                    *recognizer_session, batch, options.rec_height);
                const auto expected_logits = ocr::tools::NpyArray::load(
                    directory / ("09_rec_logits_" + stem.str() + ".npy"));
                const std::vector<std::size_t> expected_logit_shape{
                    static_cast<std::size_t>(logits.batch),
                    static_cast<std::size_t>(logits.timesteps),
                    static_cast<std::size_t>(logits.classes),
                };
                if (expected_logits.descriptor() != "<f4" ||
                    expected_logits.shape() != expected_logit_shape) {
                    throw std::runtime_error("recognition logit shapes differ");
                }
                rec_logit_maximum = std::max(
                    rec_logit_maximum,
                    compare_values(
                        logits.values,
                        expected_logits.values<float>()));
                const auto decoded_results =
                    ocr::detail::decode_ctc(logits, charset);
                for (std::size_t index = 0U;
                     index < decoded_results.size();
                     ++index) {
                    recognition_results[batch.crop_indices[index]] =
                        decoded_results[index];
                }
            }
            const std::filesystem::path extra_batch =
                directory /
                ("08_rec_input_" +
                 [&recognition_batches]() {
                     std::ostringstream value;
                     value << std::setfill('0') << std::setw(3)
                           << recognition_batches.size();
                     return value.str();
                 }() +
                 ".npy");
            if (std::filesystem::exists(extra_batch)) {
                throw std::runtime_error("C++ produced fewer recognition batches");
            }

            std::vector<ocr::Quad> retained_boxes;
            std::vector<std::string> retained_texts;
            std::vector<float> retained_confidences;
            for (std::size_t index = 0U; index < recognition_results.size(); ++index) {
                if (recognition_results[index].confidence >= options.drop_score) {
                    retained_boxes.push_back(expected_ordered[index]);
                    retained_texts.push_back(recognition_results[index].text);
                    retained_confidences.push_back(
                        recognition_results[index].confidence);
                }
            }
            const std::string final_json = read_text(directory / "10_final.json");
            const auto expected_retained_boxes =
                json_named_quads(final_json, "quad");
            const auto expected_texts = json_strings(final_json, "text");
            const auto expected_confidences =
                json_numbers_for_key(final_json, "confidence");
            if (retained_texts != expected_texts ||
                retained_confidences.size() != expected_confidences.size()) {
                throw std::runtime_error("decoded recognition text differs");
            }
            const double retained_corner_difference =
                compare_quads(retained_boxes, expected_retained_boxes);
            double confidence_difference = 0.0;
            for (std::size_t index = 0U; index < retained_confidences.size(); ++index) {
                confidence_difference = std::max(
                    confidence_difference,
                    std::abs(static_cast<double>(retained_confidences[index]) -
                             expected_confidences[index]));
            }
            overall_raw_corner_maximum = std::max(
                overall_raw_corner_maximum, raw_corner_difference);
            overall_final_corner_maximum = std::max(
                overall_final_corner_maximum, final_corner_difference);
            overall_sorted_corner_maximum = std::max(
                overall_sorted_corner_maximum, sorted_corner_difference);
            overall_score_maximum =
                std::max(overall_score_maximum, score_difference);
            overall_crop_mean_maximum = std::max(
                overall_crop_mean_maximum, crop_mean_maximum);
            overall_rec_tensor_maximum = std::max(
                overall_rec_tensor_maximum, rec_tensor_maximum);
            overall_rec_logit_maximum = std::max(
                overall_rec_logit_maximum, rec_logit_maximum);
            overall_confidence_maximum = std::max(
                overall_confidence_maximum, confidence_difference);

            std::cout << directory.filename().string()
                      << " size=" << resized.image.cols << 'x' << resized.image.rows
                      << " tensor_max_abs=" << std::scientific << difference
                      << " map_max_abs=" << map_difference
                      << " boxes=" << raw_boxes.size()
                      << " raw_corner_max=" << raw_corner_difference
                      << " final_corner_max=" << final_corner_difference
                      << " sorted_corner_max=" << sorted_corner_difference
                      << " score_max_abs=" << score_difference
                      << " crop_mean_abs=" << crop_mean_maximum
                      << " rec_tensor_max_abs=" << rec_tensor_maximum
                      << " rec_logit_max_abs=" << rec_logit_maximum
                      << " retained=" << retained_texts.size()
                      << " confidence_max_abs=" << confidence_difference << '\n';
            if (!dimensions_match || !ratios_match ||
                difference >= kDetectorTensorTolerance ||
                map_difference >= kDetectorMapTolerance ||
                raw_corner_difference > kDbCornerTolerance ||
                final_corner_difference > kDbCornerTolerance ||
                sorted_corner_difference > kDbCornerTolerance ||
                score_difference >= kDbScoreTolerance ||
                crop_mean_maximum >= kCropMeanTolerance ||
                rec_tensor_maximum >= kRecognitionTensorTolerance ||
                rec_logit_maximum >= kRecognitionLogitTolerance ||
                retained_corner_difference > 0.0 ||
                confidence_difference >= kRecognitionConfidenceTolerance) {
                throw std::runtime_error(
                    "detector parity failed for " +
                    directory.filename().string());
            }
        }
        detector_session.reset();
        recognizer_session.reset();

        ocr::Engine engine(
            std::filesystem::path(argv[2]),
            std::filesystem::path(argv[3]),
            std::filesystem::path(argv[4]),
            options);
        double engine_corner_maximum = 0.0;
        double engine_confidence_maximum = 0.0;
        for (const auto& directory : directories) {
            const auto decoded =
                ocr::tools::NpyArray::load(directory / "01_decoded.npy");
            auto pixels = decoded.values<std::uint8_t>();
            const ocr::ImageView image{
                pixels.data(),
                static_cast<int>(decoded.shape()[1]),
                static_cast<int>(decoded.shape()[0]),
                0,
            };
            const auto lines = engine.run(image);
            std::vector<ocr::Quad> actual_boxes;
            std::vector<std::string> actual_texts;
            std::vector<float> actual_confidences;
            actual_boxes.reserve(lines.size());
            actual_texts.reserve(lines.size());
            actual_confidences.reserve(lines.size());
            for (const ocr::TextLine& line : lines) {
                actual_boxes.push_back(line.box);
                actual_texts.push_back(line.text);
                actual_confidences.push_back(line.confidence);
            }
            const std::string final_json =
                read_text(directory / "10_final.json");
            const auto expected_boxes =
                json_named_quads(final_json, "quad");
            const auto expected_texts = json_strings(final_json, "text");
            const auto expected_confidences =
                json_numbers_for_key(final_json, "confidence");
            if (actual_texts != expected_texts ||
                actual_confidences.size() != expected_confidences.size()) {
                std::cerr << "end-to-end line counts actual=" << lines.size()
                          << " expected=" << expected_texts.size() << '\n';
                const std::size_t common =
                    std::min(actual_texts.size(), expected_texts.size());
                for (std::size_t index = 0U; index < common; ++index) {
                    if (actual_texts[index] != expected_texts[index]) {
                        std::cerr << "text[" << index << "] actual=["
                                  << actual_texts[index] << "] expected=["
                                  << expected_texts[index] << "] actual_box";
                        for (const ocr::Point& point : actual_boxes[index].p) {
                            std::cerr << ' ' << point.x << ',' << point.y;
                        }
                        std::cerr << " expected_box";
                        for (const ocr::Point& point : expected_boxes[index].p) {
                            std::cerr << ' ' << point.x << ',' << point.y;
                        }
                        std::cerr << '\n';
                    }
                }
                throw std::runtime_error(
                    "end-to-end Engine text differs for " +
                    directory.filename().string());
            }
            const double corner_difference =
                compare_quads(actual_boxes, expected_boxes);
            double confidence_difference = 0.0;
            for (std::size_t index = 0U;
                 index < actual_confidences.size();
                 ++index) {
                confidence_difference = std::max(
                    confidence_difference,
                    std::abs(static_cast<double>(actual_confidences[index]) -
                             expected_confidences[index]));
            }
            engine_corner_maximum =
                std::max(engine_corner_maximum, corner_difference);
            engine_confidence_maximum =
                std::max(engine_confidence_maximum, confidence_difference);
            std::cout << directory.filename().string()
                      << " engine_lines=" << lines.size()
                      << " engine_corner_max=" << corner_difference
                      << " engine_confidence_max_abs="
                      << confidence_difference << '\n';
            if (corner_difference > kDbCornerTolerance ||
                confidence_difference >= kRecognitionConfidenceTolerance) {
                for (std::size_t index = 0U;
                     index < actual_boxes.size();
                     ++index) {
                    std::cerr << "actual_box[" << index << "]=";
                    for (const ocr::Point& point : actual_boxes[index].p) {
                        std::cerr << ' ' << point.x << ',' << point.y;
                    }
                    std::cerr << " expected=";
                    for (const ocr::Point& point : expected_boxes[index].p) {
                        std::cerr << ' ' << point.x << ',' << point.y;
                    }
                    std::cerr << " confidence=" << actual_confidences[index]
                              << " expected_confidence="
                              << expected_confidences[index] << '\n';
                }
                throw std::runtime_error(
                    "end-to-end Engine parity failed for " +
                    directory.filename().string());
            }
        }
        std::cout << "images=" << directories.size()
                  << " tensor_overall_max_abs=" << std::scientific
                  << overall_tensor_maximum
                  << " map_overall_max_abs=" << overall_map_maximum
                  << " raw_corner_overall_max=" << overall_raw_corner_maximum
                  << " final_corner_overall_max=" << overall_final_corner_maximum
                  << " sorted_corner_overall_max=" << overall_sorted_corner_maximum
                  << " score_overall_max_abs=" << overall_score_maximum
                  << " crop_mean_overall_max=" << overall_crop_mean_maximum
                  << " rec_tensor_overall_max_abs=" << overall_rec_tensor_maximum
                  << " rec_logit_overall_max_abs=" << overall_rec_logit_maximum
                  << " confidence_overall_max_abs="
                  << overall_confidence_maximum
                  << " engine_corner_overall_max=" << engine_corner_maximum
                  << " engine_confidence_overall_max_abs="
                  << engine_confidence_maximum << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
