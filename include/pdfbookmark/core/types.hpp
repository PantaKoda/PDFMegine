#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace pdfbookmark {

using PageIndex = std::int32_t;  // Physical, zero based.
using PageCount = std::int32_t;

struct Point {
    double x = 0;
    double y = 0;
};

struct Quad {
    std::array<Point, 4> points{};  // Top-left, top-right, bottom-right, bottom-left.
};

struct Affine {
    // x' = a*x + c*y + e; y' = b*x + d*y + f.
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Point map(Point p) const noexcept {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
};

enum class ErrorCode {
    InvalidArgument,
    InputOpen,
    InputChanged,
    PdfBackend,
    OcrConfiguration,
    ResourceLimit,
    Unsupported,   // A supported-feature boundary, e.g. encrypted/signed input.
    OutputExists,  // Refused to replace an existing output file.
    OutputWrite,   // Output could not be written, verified, or committed.
    Cancelled      // Cooperative cancellation before completion.
};

struct Error {
    ErrorCode code = ErrorCode::InvalidArgument;
    std::string message;
};

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}
    explicit operator bool() const noexcept { return value_.has_value(); }
    T& value() {
        if (!value_) throw std::logic_error("Result has no value");
        return *value_;
    }
    const T& value() const {
        if (!value_) throw std::logic_error("Result has no value");
        return *value_;
    }
    T take() { return std::move(value()); }
    const Error& error() const {
        if (!error_) throw std::logic_error("Result has no error");
        return *error_;
    }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

struct InputIdentity {
    std::array<std::uint8_t, 32> sha256{};
    PageCount page_count = 0;
    std::filesystem::path display_path;
};

struct RunControl {
    // Borrowed for the duration of a synchronous call.
    const std::atomic_bool* cancelled = nullptr;
    bool is_cancelled() const noexcept {
        return cancelled && cancelled->load(std::memory_order_relaxed);
    }
};

}  // namespace pdfbookmark
