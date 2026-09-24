#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <onnxruntime_cxx_api.h>

#include <ocr/ocr.hpp>

namespace ocr::detail {

struct RecognitionBatch {
    std::vector<std::size_t> crop_indices;
    int width;
    std::vector<float> input;
};

struct RecognitionLogits {
    std::vector<float> values;
    int batch;
    int timesteps;
    int classes;
};

struct RecognitionResult {
    std::string text;
    float confidence;
};

std::vector<RecognitionBatch> make_recognition_batches(
    const std::vector<cv::Mat>& crops,
    const Options& options);
RecognitionLogits run_recognizer_batch(
    Ort::Session& session,
    RecognitionBatch& batch,
    int recognition_height);
std::vector<RecognitionResult> decode_ctc(
    const RecognitionLogits& logits,
    const std::vector<std::string>& charset);
std::vector<RecognitionResult> recognize(
    Ort::Session& session,
    const std::vector<cv::Mat>& crops,
    const std::vector<std::string>& charset,
    const Options& options);

}  // namespace ocr::detail
