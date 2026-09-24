#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace ocr::detail {

struct TensorDescription {
    std::string name;
    std::vector<std::int64_t> shape;
    ONNXTensorElementDataType element_type;
};

std::unique_ptr<Ort::Session> create_session(
    Ort::Env& environment,
    const std::filesystem::path& model_path,
    int threads);

std::vector<TensorDescription> describe_inputs(const Ort::Session& session);
std::vector<TensorDescription> describe_outputs(const Ort::Session& session);
std::string format_shape(const std::vector<std::int64_t>& shape);

}  // namespace ocr::detail
