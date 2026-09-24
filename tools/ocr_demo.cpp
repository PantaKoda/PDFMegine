#include <ocr/ocr.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>

std::filesystem::path executable_path() {
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length == buffer.size()) {
        throw std::runtime_error("could not determine executable path");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}
#else
#include <unistd.h>

std::filesystem::path executable_path() {
    std::string buffer(4096U, '\0');
    const ssize_t length =
        readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length <= 0 || static_cast<std::size_t>(length) == buffer.size()) {
        throw std::runtime_error("could not determine executable path");
    }
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer);
}
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 1 && argc != 2 && argc != 5) {
            throw std::invalid_argument(
                "usage: ocr_demo [IMAGE] or "
                "ocr_demo IMAGE DET_MODEL REC_MODEL CHARSET");
        }
        const std::filesystem::path executable_directory =
            executable_path().parent_path();
        std::filesystem::path image =
            executable_directory / "assets/sample.png";
        std::filesystem::path detector =
            executable_directory / "assets/models/det/inference.onnx";
        std::filesystem::path recognizer =
            executable_directory / "assets/models/rec/inference.onnx";
        std::filesystem::path charset =
            executable_directory / "assets/models/rec/charset.txt";
        if (argc >= 2) {
            image = std::filesystem::u8path(argv[1]);
        }
        if (argc == 5) {
            detector = std::filesystem::u8path(argv[2]);
            recognizer = std::filesystem::u8path(argv[3]);
            charset = std::filesystem::u8path(argv[4]);
        }

        ocr::Engine engine(detector, recognizer, charset);
        const auto lines = engine.run_file(image);
        for (const ocr::TextLine& line : lines) {
            std::cout << line.confidence << '\t' << line.text << '\t';
            for (const ocr::Point& point : line.box.p) {
                std::cout << point.x << ',' << point.y << ' ';
            }
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
