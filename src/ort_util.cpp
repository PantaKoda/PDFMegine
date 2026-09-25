#include "ort_util.hpp"

#include <sstream>
#include <stdexcept>

namespace ocr::detail {
namespace {

std::vector<TensorDescription> describe_nodes(
    const Ort::Session& session,
    bool inputs) {
    Ort::AllocatorWithDefaultOptions allocator;
    const std::size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
    std::vector<TensorDescription> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto name = inputs ? session.GetInputNameAllocated(index, allocator)
                           : session.GetOutputNameAllocated(index, allocator);
        const auto info = inputs ? session.GetInputTypeInfo(index)
                                 : session.GetOutputTypeInfo(index);
        const auto tensor = info.GetTensorTypeAndShapeInfo();
        result.push_back({name.get(), tensor.GetShape(), tensor.GetElementType()});
    }
    return result;
}

}  // namespace

std::unique_ptr<Ort::Session> create_session(
    Ort::Env& environment,
    const std::filesystem::path& model_path,
    int threads) {
    if (threads < 1) {
        throw std::invalid_argument("ONNX Runtime thread count must be positive");
    }
    if (!std::filesystem::is_regular_file(model_path)) {
        throw std::runtime_error("model file does not exist: " + model_path.string());
    }
    Ort::SessionOptions options;
    // Free tensor memory after each run instead of keeping ONNX Runtime's
    // CPU arena, which grows to the largest shapes seen (variable-width
    // recognition batches, full-page detection) and never shrinks: 8.7 GB
    // peak for four 300-DPI pages versus 2.3 GB without it, identical
    // outputs (PDFMegine issue #1; docs/ocr/DECISIONS.md).
    options.DisableCpuMemArena();
    options.DisableMemPattern();
    options.SetIntraOpNumThreads(threads);
    options.SetInterOpNumThreads(1);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    const std::wstring native_path = model_path.wstring();
    return std::make_unique<Ort::Session>(environment, native_path.c_str(), options);
#else
    const std::string native_path = model_path.string();
    return std::make_unique<Ort::Session>(environment, native_path.c_str(), options);
#endif
}

std::vector<TensorDescription> describe_inputs(const Ort::Session& session) {
    return describe_nodes(session, true);
}

std::vector<TensorDescription> describe_outputs(const Ort::Session& session) {
    return describe_nodes(session, false);
}

std::string format_shape(const std::vector<std::int64_t>& shape) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < shape.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        stream << shape[index];
    }
    stream << ']';
    return stream.str();
}

}  // namespace ocr::detail
