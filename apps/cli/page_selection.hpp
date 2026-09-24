#pragma once

// CLI page selection: one-based inclusive user syntax, converted exactly once
// into zero-based library indices. Syntax: "all" | item ("," item)*, where an
// item is N or A-B (A <= B), e.g. "1-40", "3,7,10-12". Order is preserved.

#include <pdfbookmark/core/types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::cli {

struct PageSelection {
    bool all = false;
    std::vector<PageIndex> indices;  // Zero based; empty when `all`.
};

// Returns nullopt and sets `error` on malformed, zero, reversed, overflowing,
// or duplicate selections. Bounds against the document are checked later.
std::optional<PageSelection> parse_page_selection(const std::string& text,
                                                  std::string& error);

}  // namespace pdfbookmark::cli
