// Installed S5 consumer: writes a PDF from a manually constructed plan,
// without S1-S4. Usage: writer_consumer <input.pdf> <output.pdf>
#include <pdfbookmark/writer/writer.hpp>

#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    using namespace pdfbookmark::writer;
    if (argc != 3) return 2;
    const auto identity = read_input_identity(argv[1]);
    if (!identity) {
        std::cerr << identity.error().message << '\n';
        return 3;
    }
    std::cout << "input sha256=";
    for (const auto byte : identity.value().sha256)
        std::printf("%02x", byte);
    std::cout << " pages=" << identity.value().page_count << '\n';

    BookmarkPlan plan;
    plan.input = identity.value();
    plan.nodes = {
        {"alpha", std::nullopt, "Alpha", {2}},
        {"beta", std::nullopt, "Beta", {3}},
        {"beta-note", std::string("beta"), "Beta \xE2\x80\x94 note", {3}},
        {"gamma", std::nullopt, "Gamma", {4}}};
    const auto checked = validate(plan);
    if (!checked.valid) return 4;
    const auto written = write_copy(argv[1], argv[2], plan);
    if (!written) {
        std::cerr << written.error().message << '\n';
        return 5;
    }
    const auto& result = written.value();
    std::cout << "committed=" << result.committed
              << " items=" << result.verification.outline_items
              << " pages=" << result.verification.page_count
              << " structure=" << result.verification.structure_matches
              << " input_unchanged=" << result.verification.input_unchanged
              << " replaced_input_outline_in_copy=" << result.input_had_outline
              << '\n';
    return result.committed ? 0 : 6;
}
