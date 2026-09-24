#include "page_selection.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main() {
    using pdfbookmark::cli::parse_page_selection;
    std::string error;
    const auto list = parse_page_selection("7,1-3,10", error);
    require(list && !list->all &&
                list->indices == std::vector<pdfbookmark::PageIndex>{6, 0, 1, 2, 9},
            "one-based inclusive items convert once to zero-based, order kept");
    const auto all = parse_page_selection("all", error);
    require(all && all->all && all->indices.empty(), "all");
    const auto one = parse_page_selection("1", error);
    require(one && one->indices == std::vector<pdfbookmark::PageIndex>{0},
            "page 1 is index 0");
    for (const char* bad : {"", "0", "0-3", "3-1", "1,1", "1-3,2", "a", "1-",
                            "-2", "1,,2", "1 - 2", "99999999999",
                            "1-2000000"}) {
        error.clear();
        require(!parse_page_selection(bad, error) && !error.empty(),
                std::string("rejects '") + bad + "'");
    }
    std::cout << "CLI page selection fixtures passed\n";
}
