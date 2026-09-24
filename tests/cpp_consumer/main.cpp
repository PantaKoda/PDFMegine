#include <ocr/ocr.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            throw std::invalid_argument(
                "usage: ocr_cpp_consumer DET_MODEL REC_MODEL CHARSET IMAGE");
        }
        ocr::Engine engine(
            std::filesystem::u8path(argv[1]),
            std::filesystem::u8path(argv[2]),
            std::filesystem::u8path(argv[3]));
        const auto lines =
            engine.run_file(std::filesystem::u8path(argv[4]));
        std::cout << "lines=" << lines.size() << '\n';
        if (!lines.empty()) {
            std::cout << "first=" << lines.front().text << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
