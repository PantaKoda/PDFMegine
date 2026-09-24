#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pdfbookmark::text::detail {

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size);
std::array<std::uint8_t, 32> sha256_file(const std::filesystem::path& path);
std::string sha256_hex(const std::array<std::uint8_t, 32>& digest);

}  // namespace pdfbookmark::text::detail
