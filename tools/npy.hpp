#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace ocr::tools {

class NpyArray {
public:
    static NpyArray load(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("cannot open npy file: " + path.string());
        }
        const std::string magic = read_string(stream, 6U);
        if (magic != std::string("\x93NUMPY", 6U)) {
            throw std::runtime_error("invalid npy magic: " + path.string());
        }
        const auto major = read_byte(stream);
        static_cast<void>(read_byte(stream));
        std::size_t header_length = 0;
        if (major == 1U) {
            header_length = read_little_endian(stream, 2U);
        } else if (major == 2U || major == 3U) {
            header_length = read_little_endian(stream, 4U);
        } else {
            throw std::runtime_error("unsupported npy version: " + path.string());
        }
        const std::string header = read_string(stream, header_length);
        std::smatch match;
        if (!std::regex_search(header, match, std::regex(R"('descr':\s*'([^']+)')"))) {
            throw std::runtime_error("npy header has no descr: " + path.string());
        }
        NpyArray result;
        result.descriptor_ = match[1].str();
        if (std::regex_search(header, match, std::regex(R"('fortran_order':\s*(True|False))")) &&
            match[1].str() != "False") {
            throw std::runtime_error("Fortran-order npy arrays are unsupported");
        }
        if (!std::regex_search(header, match, std::regex(R"('shape':\s*\(([^)]*)\))"))) {
            throw std::runtime_error("npy header has no shape: " + path.string());
        }
        const std::string shape_text = match[1].str();
        const std::regex dimension_regex(R"((\d+))");
        for (auto iterator = std::sregex_iterator(
                 shape_text.begin(), shape_text.end(), dimension_regex);
             iterator != std::sregex_iterator();
             ++iterator) {
            result.shape_.push_back(static_cast<std::size_t>(std::stoull((*iterator)[1].str())));
        }
        if (result.shape_.empty()) {
            throw std::runtime_error("scalar npy arrays are unsupported");
        }
        const std::size_t element_size = descriptor_size(result.descriptor_);
        std::size_t element_count = 1U;
        for (const std::size_t dimension : result.shape_) {
            element_count *= dimension;
        }
        result.bytes_.resize(element_count * element_size);
        stream.read(
            reinterpret_cast<char*>(result.bytes_.data()),
            static_cast<std::streamsize>(result.bytes_.size()));
        if (stream.gcount() != static_cast<std::streamsize>(result.bytes_.size())) {
            throw std::runtime_error("truncated npy data: " + path.string());
        }
        return result;
    }

    const std::vector<std::size_t>& shape() const {
        return shape_;
    }

    const std::string& descriptor() const {
        return descriptor_;
    }

    template <typename T>
    std::vector<T> values() const {
        if (bytes_.size() % sizeof(T) != 0U) {
            throw std::runtime_error("npy byte count does not match requested type");
        }
        std::vector<T> result(bytes_.size() / sizeof(T));
        std::memcpy(result.data(), bytes_.data(), bytes_.size());
        return result;
    }

private:
    static std::uint8_t read_byte(std::istream& stream) {
        char value = 0;
        stream.read(&value, 1);
        if (!stream) {
            throw std::runtime_error("truncated npy header");
        }
        return static_cast<std::uint8_t>(value);
    }

    static std::size_t read_little_endian(std::istream& stream, std::size_t bytes) {
        std::size_t value = 0U;
        for (std::size_t index = 0; index < bytes; ++index) {
            value |= static_cast<std::size_t>(read_byte(stream)) << (8U * index);
        }
        return value;
    }

    static std::string read_string(std::istream& stream, std::size_t length) {
        std::string result(length, '\0');
        stream.read(result.data(), static_cast<std::streamsize>(length));
        if (!stream) {
            throw std::runtime_error("truncated npy file");
        }
        return result;
    }

    static std::size_t descriptor_size(const std::string& descriptor) {
        if (descriptor == "|u1") {
            return 1U;
        }
        if (descriptor == "<f4" || descriptor == "=f4") {
            return 4U;
        }
        throw std::runtime_error("unsupported npy descriptor: " + descriptor);
    }

    std::string descriptor_;
    std::vector<std::size_t> shape_;
    std::vector<std::uint8_t> bytes_;
};

}  // namespace ocr::tools
