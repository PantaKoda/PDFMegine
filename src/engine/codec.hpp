#pragma once

// Engine-private JSON encoding helpers shared by the text and analysis
// reports and the plan codec.

#include "json.hpp"

#include <pdfbookmark/text/acquisition.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::engine::codec {

std::string hex(const std::array<std::uint8_t, 32>& digest);
std::optional<std::array<std::uint8_t, 32>> unhex(const std::string& text);
json::Value strings(const std::vector<std::string>& values);
json::Value count(std::size_t value);
json::Value source_ref(const text::SourceReference& source);
json::Value sources(const std::vector<text::SourceReference>& values);
// `configuration` is nullopt for pages with no producing S1 configuration.
json::Value page(const text::PageAcquisition& page,
                 std::optional<std::size_t> configuration);

}  // namespace pdfbookmark::engine::codec
