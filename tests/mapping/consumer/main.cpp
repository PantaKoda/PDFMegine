#include <pdfbookmark/mapping/mapping.hpp>

#include <iostream>

int main() {
    using namespace pdfbookmark;
    mapping::DocumentEvidence evidence;
    evidence.input.page_count = 40;
    evidence.sections.push_back({
        "body", 12, 40, parsing::NumberingStyle::Decimal,
        {}, "caller-defined section", false});
    parsing::TocEntry entry;
    entry.id = "transport";
    entry.title = "Transport";
    parsing::PrintedReference reference;
    reference.literal = "12";
    reference.numbering = parsing::NumberingStyle::Decimal;
    reference.ordinal = 12;
    entry.printed_reference = reference;
    mapping::MappingOverride offset{
        mapping::OverrideKind::SectionOffset, "body",
        std::nullopt, 11, "caller decision"};
    const auto result = mapping::map({entry}, evidence, {offset});
    if (!result || result.value().entries.size() != 1 ||
        result.value().entries[0].status !=
            mapping::MappingStatus::Resolved ||
        result.value().entries[0].pdf_page_index != 23 ||
        result.value().entries[0].method !=
            mapping::ResolutionMethod::ManualOffset)
        return 1;
    std::cout << "transport pdf_index="
              << *result.value().entries[0].pdf_page_index << '\n';
}
