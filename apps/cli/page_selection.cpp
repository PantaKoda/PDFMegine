#include "page_selection.hpp"

#include <cstdint>
#include <limits>
#include <set>

namespace pdfbookmark::cli {
namespace {

// Positive one-based page number within PageIndex range.
std::optional<std::int64_t> page_number(const std::string& text) {
    if (text.empty() || text.size() > 10) return std::nullopt;
    std::int64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    if (value < 1 || value > std::numeric_limits<PageIndex>::max())
        return std::nullopt;
    return value;
}

}  // namespace

std::optional<PageSelection> parse_page_selection(const std::string& text,
                                                  std::string& error) {
    PageSelection result;
    if (text == "all") {
        result.all = true;
        return result;
    }
    constexpr std::size_t kMaxPages = 1'000'000;
    std::set<PageIndex> seen;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::string item = text.substr(
            start, comma == std::string::npos ? std::string::npos
                                              : comma - start);
        const std::size_t dash = item.find('-');
        const auto first = page_number(item.substr(0, dash));
        const auto last = dash == std::string::npos
                              ? first
                              : page_number(item.substr(dash + 1));
        if (!first || !last) {
            error = "Invalid page item '" + item +
                    "' (use one-based N or A-B, e.g. 1-40)";
            return std::nullopt;
        }
        if (*last < *first) {
            error = "Reversed page range '" + item + "'";
            return std::nullopt;
        }
        if (static_cast<std::size_t>(*last - *first) >=
            kMaxPages - result.indices.size()) {
            error = "Page selection is too large";
            return std::nullopt;
        }
        for (std::int64_t page = *first; page <= *last; ++page) {
            const auto index = static_cast<PageIndex>(page - 1);  // Once.
            if (!seen.insert(index).second) {
                error = "Page " + std::to_string(page) + " is selected twice";
                return std::nullopt;
            }
            result.indices.push_back(index);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

}  // namespace pdfbookmark::cli
