#include "ort_util.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_nodes(const char* kind, const std::vector<ocr::detail::TensorDescription>& nodes) {
    for (const auto& node : nodes) {
        std::cout << kind << ' ' << node.name << ' '
                  << ocr::detail::format_shape(node.shape) << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::invalid_argument("usage: ocr_model_info DET_MODEL REC_MODEL");
        }
        Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "ocr_model_info");
        auto det = ocr::detail::create_session(environment, std::filesystem::path(argv[1]), 1);
        auto rec = ocr::detail::create_session(environment, std::filesystem::path(argv[2]), 1);

        std::cout << "detector\n";
        print_nodes("input", ocr::detail::describe_inputs(*det));
        print_nodes("output", ocr::detail::describe_outputs(*det));
        std::cout << "recognizer\n";
        const auto rec_inputs = ocr::detail::describe_inputs(*rec);
        const auto rec_outputs = ocr::detail::describe_outputs(*rec);
        print_nodes("input", rec_inputs);
        print_nodes("output", rec_outputs);
        if (rec_inputs.size() != 1 || rec_outputs.size() != 1) {
            throw std::runtime_error("recognizer must have exactly one input and one output");
        }

        constexpr std::array<std::int64_t, 4> input_shape{1, 3, 48, 320};
        std::vector<float> input(1U * 3U * 48U * 320U, 0.0F);
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(
            memory,
            input.data(),
            input.size(),
            input_shape.data(),
            input_shape.size());
        const char* input_names[] = {rec_inputs[0].name.c_str()};
        const char* output_names[] = {rec_outputs[0].name.c_str()};
        auto outputs = rec->Run(
            Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
        const auto actual_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        std::cout << "dummy output " << ocr::detail::format_shape(actual_shape) << '\n';
        if (actual_shape.size() != 3 || actual_shape.back() != 18710) {
            throw std::runtime_error("recognizer class dimension is not 18710");
        }
        std::cout << "C=18710\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
