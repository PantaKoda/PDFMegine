#include <pdfbookmark/text/acquisition.hpp>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    pdfbookmark::text::TextAcquisition acquisition;
    auto opened = acquisition.open(argv[1]);
    if (!opened) return 3;
    auto document = opened.take();
    pdfbookmark::text::AcquisitionOptions options;
    options.mode = pdfbookmark::text::AcquisitionMode::EmbeddedOnly;
    auto result = document.acquire({2, 0}, options);
    if (!result || result.value().pages.size() != 2) return 4;
    if (!result.value().pages[0].selected ||
        !result.value().pages[1].selected) return 5;
    std::cout << result.value().pages[0].selected->flat_text() << '\n'
              << result.value().pages[1].selected->flat_text() << '\n';
    return 0;
}
