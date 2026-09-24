#include "recognizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

#include <opencv2/imgproc.hpp>

namespace ocr::detail {
namespace {

constexpr int kRecognitionChannels = 3;
constexpr float kRecognitionMean = 0.5F;
constexpr float kRecognitionStd = 0.5F;
constexpr float kPixelDenominator = 255.0F;
constexpr int kCtcBlank = 0;
constexpr std::size_t kNumpySmallSortLimit = 256U;

using SortItem = std::pair<double, std::size_t>;
using SortVector = std::array<SortItem, 4>;

SortVector compare_merge(
    const SortVector& first,
    const SortVector& second,
    unsigned int maximum_mask) {
    SortVector result{};
    for (std::size_t lane = 0U; lane < result.size(); ++lane) {
        const bool take_maximum =
            (maximum_mask & (1U << static_cast<unsigned int>(lane))) != 0U;
        if (take_maximum) {
            result[lane] =
                second[lane].first > first[lane].first ?
                    second[lane] : first[lane];
        } else {
            result[lane] =
                first[lane].first <= second[lane].first ?
                    first[lane] : second[lane];
        }
    }
    return result;
}

SortVector swap_adjacent(const SortVector& value) {
    return {value[1], value[0], value[3], value[2]};
}

SortVector swap_halves(const SortVector& value) {
    return {value[2], value[3], value[0], value[1]};
}

SortVector reverse(const SortVector& value) {
    return {value[3], value[2], value[1], value[0]};
}

SortVector sort_four(SortVector value) {
    value = compare_merge(value, swap_adjacent(value), 0xAU);
    value = compare_merge(value, reverse(value), 0xCU);
    return compare_merge(value, swap_adjacent(value), 0xAU);
}

SortVector merge_four(SortVector value) {
    value = compare_merge(value, swap_halves(value), 0xCU);
    return compare_merge(value, swap_adjacent(value), 0xAU);
}

void compare_exchange(SortVector& lower, SortVector& upper) {
    for (std::size_t lane = 0U; lane < lower.size(); ++lane) {
        if (upper[lane].first < lower[lane].first) {
            std::swap(lower[lane], upper[lane]);
        }
    }
}

void merge_vectors(
    std::vector<SortVector>& vectors,
    std::size_t begin,
    std::size_t count) {
    for (std::size_t offset = 0U; offset < count / 2U; ++offset) {
        const std::size_t upper_index = begin + count - offset - 1U;
        SortVector upper = reverse(vectors[upper_index]);
        compare_exchange(vectors[begin + offset], upper);
        vectors[upper_index] = reverse(upper);
    }

    for (std::size_t width = count / 2U; width >= 2U; width /= 2U) {
        for (std::size_t group = 0U; group < count; group += width) {
            for (std::size_t offset = 0U; offset < width / 2U; ++offset) {
                const std::size_t lower = begin + group + offset;
                compare_exchange(vectors[lower], vectors[lower + width / 2U]);
            }
        }
    }
    for (std::size_t index = begin; index < begin + count; ++index) {
        vectors[index] = merge_four(vectors[index]);
    }
}

std::vector<std::size_t> numpy_small_argsort(
    const std::vector<double>& values) {
    if (values.empty()) {
        return {};
    }
    std::size_t capacity = kNumpySmallSortLimit;
    while (capacity > 4U && values.size() * 2U <= capacity) {
        capacity /= 2U;
    }
    std::vector<SortItem> items;
    items.reserve(capacity);
    for (std::size_t index = 0U; index < values.size(); ++index) {
        items.emplace_back(values[index], index);
    }
    const SortItem padding{
        std::numeric_limits<double>::max(),
        std::numeric_limits<std::size_t>::max(),
    };
    items.resize(capacity, padding);

    std::vector<SortVector> vectors(capacity / 4U);
    for (std::size_t index = 0U; index < vectors.size(); ++index) {
        std::copy_n(
            items.begin() + static_cast<std::ptrdiff_t>(index * 4U),
            4,
            vectors[index].begin());
        vectors[index] = sort_four(vectors[index]);
    }
    for (std::size_t width = 2U; width <= vectors.size(); width *= 2U) {
        for (std::size_t begin = 0U; begin < vectors.size(); begin += width) {
            merge_vectors(vectors, begin, width);
        }
    }

    std::vector<std::size_t> indices;
    indices.reserve(values.size());
    for (const SortVector& vector : vectors) {
        for (const SortItem& item : vector) {
            if (indices.size() == values.size()) {
                return indices;
            }
            indices.push_back(item.second);
        }
    }
    return indices;
}

std::vector<std::size_t> recognition_order(
    const std::vector<cv::Mat>& crops) {
    std::vector<double> ratios;
    ratios.reserve(crops.size());
    for (const cv::Mat& crop : crops) {
        if (crop.empty() || crop.type() != CV_8UC3) {
            throw std::invalid_argument(
                "recognizer crops must be 8-bit three-channel BGR images");
        }
        ratios.push_back(
            static_cast<double>(crop.cols) / static_cast<double>(crop.rows));
    }
    if (ratios.size() <= kNumpySmallSortLimit) {
        return numpy_small_argsort(ratios);
    }
    std::vector<std::size_t> indices(crops.size());
    std::iota(indices.begin(), indices.end(), 0U);
    std::sort(
        indices.begin(),
        indices.end(),
        [&ratios](std::size_t left, std::size_t right) {
            return ratios[left] < ratios[right];
        });
    return indices;
}

void validate_recognition_options(const Options& options) {
    if (options.rec_height <= 0 || options.rec_base_width <= 0 ||
        options.rec_batch_size <= 0) {
        throw std::invalid_argument("recognition dimensions and batch size must be positive");
    }
}

}  // namespace

std::vector<RecognitionBatch> make_recognition_batches(
    const std::vector<cv::Mat>& crops,
    const Options& options) {
    validate_recognition_options(options);
    const std::vector<std::size_t> indices = recognition_order(crops);

    std::vector<RecognitionBatch> result;
    const std::size_t batch_size = static_cast<std::size_t>(options.rec_batch_size);
    for (std::size_t begin = 0U; begin < indices.size(); begin += batch_size) {
        const std::size_t end = std::min(indices.size(), begin + batch_size);
        double maximum_ratio =
            static_cast<double>(options.rec_base_width) /
            static_cast<double>(options.rec_height);
        for (std::size_t position = begin; position < end; ++position) {
            const cv::Mat& crop = crops[indices[position]];
            maximum_ratio = std::max(
                maximum_ratio,
                static_cast<double>(crop.cols) / static_cast<double>(crop.rows));
        }
        const int batch_width = static_cast<int>(
            static_cast<double>(options.rec_height) * maximum_ratio);
        const std::size_t samples = end - begin;
        const std::size_t plane =
            static_cast<std::size_t>(options.rec_height) *
            static_cast<std::size_t>(batch_width);
        RecognitionBatch batch;
        batch.width = batch_width;
        batch.crop_indices.assign(indices.begin() + static_cast<std::ptrdiff_t>(begin),
                                  indices.begin() + static_cast<std::ptrdiff_t>(end));
        batch.input.assign(
            samples * static_cast<std::size_t>(kRecognitionChannels) * plane,
            0.0F);

        for (std::size_t sample = 0U; sample < samples; ++sample) {
            const cv::Mat& crop = crops[batch.crop_indices[sample]];
            const double crop_ratio =
                static_cast<double>(crop.cols) / static_cast<double>(crop.rows);
            const int resized_width = std::min(
                batch_width,
                static_cast<int>(std::ceil(
                    static_cast<double>(options.rec_height) * crop_ratio)));
            cv::Mat resized;
            cv::resize(
                crop,
                resized,
                cv::Size(resized_width, options.rec_height),
                0.0,
                0.0,
                cv::INTER_LINEAR);
            const std::size_t sample_offset =
                sample * static_cast<std::size_t>(kRecognitionChannels) * plane;
            for (int y = 0; y < options.rec_height; ++y) {
                const auto* row = resized.ptr<cv::Vec3b>(y);
                for (int x = 0; x < resized_width; ++x) {
                    const std::size_t pixel =
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(batch_width) +
                        static_cast<std::size_t>(x);
                    for (int channel = 0; channel < kRecognitionChannels; ++channel) {
                        const float scaled =
                            static_cast<float>(row[x][channel]) / kPixelDenominator;
                        batch.input[
                            sample_offset +
                            static_cast<std::size_t>(channel) * plane +
                            pixel] =
                            (scaled - kRecognitionMean) / kRecognitionStd;
                    }
                }
            }
        }
        result.push_back(std::move(batch));
    }
    return result;
}

RecognitionLogits run_recognizer_batch(
    Ort::Session& session,
    RecognitionBatch& batch,
    int recognition_height) {
    if (batch.crop_indices.empty() || batch.width <= 0 || recognition_height <= 0) {
        throw std::invalid_argument("invalid recognition batch");
    }
    const std::array<std::int64_t, 4> shape{
        static_cast<std::int64_t>(batch.crop_indices.size()),
        kRecognitionChannels,
        static_cast<std::int64_t>(recognition_height),
        static_cast<std::int64_t>(batch.width),
    };
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto input_value = Ort::Value::CreateTensor<float>(
        memory,
        batch.input.data(),
        batch.input.size(),
        shape.data(),
        shape.size());
    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name = session.GetInputNameAllocated(0, allocator);
    auto output_name = session.GetOutputNameAllocated(0, allocator);
    const char* input_names[] = {input_name.get()};
    const char* output_names[] = {output_name.get()};
    auto outputs = session.Run(
        Ort::RunOptions{nullptr}, input_names, &input_value, 1, output_names, 1);
    if (outputs.size() != 1U || !outputs[0].IsTensor()) {
        throw std::runtime_error("recognizer did not return one tensor");
    }
    const auto info = outputs[0].GetTensorTypeAndShapeInfo();
    const auto output_shape = info.GetShape();
    if (output_shape.size() != 3U || output_shape[0] <= 0 ||
        output_shape[1] <= 0 || output_shape[2] <= 0) {
        throw std::runtime_error("recognizer output must have shape [N,T,C]");
    }
    const float* data = outputs[0].GetTensorData<float>();
    return {
        std::vector<float>(data, data + info.GetElementCount()),
        static_cast<int>(output_shape[0]),
        static_cast<int>(output_shape[1]),
        static_cast<int>(output_shape[2]),
    };
}

std::vector<RecognitionResult> decode_ctc(
    const RecognitionLogits& logits,
    const std::vector<std::string>& charset) {
    if (logits.batch < 0 || logits.timesteps < 0 || logits.classes <= 0) {
        throw std::invalid_argument("invalid recognition logits shape");
    }
    const std::size_t expected =
        static_cast<std::size_t>(logits.batch) *
        static_cast<std::size_t>(logits.timesteps) *
        static_cast<std::size_t>(logits.classes);
    if (logits.values.size() != expected ||
        logits.classes != static_cast<int>(charset.size()) + 2) {
        throw std::invalid_argument("recognition logits do not match charset");
    }
    std::vector<RecognitionResult> result;
    result.reserve(static_cast<std::size_t>(logits.batch));
    for (int sample = 0; sample < logits.batch; ++sample) {
        std::string text;
        float confidence_sum = 0.0F;
        int retained = 0;
        int previous_class = -1;
        for (int timestep = 0; timestep < logits.timesteps; ++timestep) {
            const std::size_t base =
                (static_cast<std::size_t>(sample) *
                     static_cast<std::size_t>(logits.timesteps) +
                 static_cast<std::size_t>(timestep)) *
                static_cast<std::size_t>(logits.classes);
            int maximum_class = 0;
            float maximum_value = logits.values[base];
            for (int character_class = 1;
                 character_class < logits.classes;
                 ++character_class) {
                const float value =
                    logits.values[base + static_cast<std::size_t>(character_class)];
                if (value > maximum_value) {
                    maximum_value = value;
                    maximum_class = character_class;
                }
            }
            const bool duplicate = maximum_class == previous_class;
            previous_class = maximum_class;
            if (duplicate || maximum_class == kCtcBlank) {
                continue;
            }
            if (maximum_class == logits.classes - 1) {
                text.push_back(' ');
            } else {
                text += charset[static_cast<std::size_t>(maximum_class - 1)];
            }
            confidence_sum += maximum_value;
            ++retained;
        }
        result.push_back({
            std::move(text),
            retained == 0 ? 0.0F :
                confidence_sum / static_cast<float>(retained),
        });
    }
    return result;
}

std::vector<RecognitionResult> recognize(
    Ort::Session& session,
    const std::vector<cv::Mat>& crops,
    const std::vector<std::string>& charset,
    const Options& options) {
    auto batches = make_recognition_batches(crops, options);
    std::vector<RecognitionResult> result(crops.size(), {"", 0.0F});
    for (RecognitionBatch& batch : batches) {
        const auto logits =
            run_recognizer_batch(session, batch, options.rec_height);
        const auto decoded = decode_ctc(logits, charset);
        if (decoded.size() != batch.crop_indices.size()) {
            throw std::runtime_error("recognizer batch result count differs");
        }
        for (std::size_t index = 0U; index < decoded.size(); ++index) {
            result[batch.crop_indices[index]] = decoded[index];
        }
    }
    return result;
}

}  // namespace ocr::detail
