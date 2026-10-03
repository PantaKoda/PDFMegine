// Engine metadata facade on a generated front-matter PDF.
// Usage: pdfbookmark_engine_metadata_tests <front_matter.pdf>
#include <pdfbookmark/engine/metadata.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace pdfbookmark;
void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: front_matter.pdf");
    const auto pdf = std::filesystem::u8path(argv[1]);
    engine::MetadataRunOptions options;
    options.mode = text::AcquisitionMode::EmbeddedOnly;
    const auto report = engine::extract_metadata(pdf, options);
    require(static_cast<bool>(report), "extract_metadata succeeds");
    const auto& r = report.value().result;
    require(r.title.status == metadata::FieldStatus::Resolved &&
                r.title.value->title == "Parallel Worlds" &&
                r.title.value->subtitle == std::string("A Practical Guide"),
            "title and subtitle from cover + title page");
    require(r.contributors.status == metadata::FieldStatus::Resolved &&
                r.contributors.value->size() == 2 &&
                (*r.contributors.value)[0].name == "Jane Q. Doe",
            "authors resolved");
    require(r.edition.status == metadata::FieldStatus::Resolved &&
                r.edition.value->ordinal == 2u,
            "second edition");
    require(r.publication_year.status == metadata::FieldStatus::Resolved &&
                r.publication_year.value->year == 2012,
            "publication year of the second edition (not the largest year)");
    require(r.copyright_year.status == metadata::FieldStatus::Ambiguous,
            "two copyright years stay ambiguous");
    require(report.value().searched_pages.size() == 10 &&
                !report.value().search_covered_document &&
                report.value().stop_reasons.back() == "All fields resolved",
            "stops after the first 10 pages once every field is resolved");

    // Page limit: with only 2 pages the copyright page is never read.
    engine::MetadataRunOptions narrow = options;
    narrow.initial_pages = 2;
    narrow.batch_pages = 1;
    narrow.max_pages = 2;
    const auto limited = engine::extract_metadata(pdf, narrow);
    require(limited && limited.value().searched_pages.size() == 2 &&
                limited.value().result.publication_year.status ==
                    metadata::FieldStatus::NotFoundInSearch &&
                limited.value().stop_reasons.back().find("page limit") != std::string::npos,
            "reaching the limit is 'not found in the searched pages'");

    // Expansion: start with 1 page, grow in steps until resolved.
    engine::MetadataRunOptions grow = options;
    grow.initial_pages = 1;
    grow.batch_pages = 1;
    const auto grown = engine::extract_metadata(pdf, grow);
    require(grown && grown.value().result.publication_year.status ==
                         metadata::FieldStatus::Resolved &&
                grown.value().searched_pages.size() == 3,
            "search expands only as far as needed");

    const auto json = engine::metadata_report_json(report.value());
    require(json.find("\"kind\": \"pdfbookmark.metadata\"") != std::string::npos &&
                json.find("\"publication_year\"") != std::string::npos &&
                json.find("\"copyright_year\"") != std::string::npos &&
                r.isbns.size() == 1 &&
                json.find("\"isbn13\": \"9781234567897\"") != std::string::npos,
            "JSON report carries all fields and the ISBN list");
    std::atomic_bool cancel{true};
    const auto cancelled = engine::extract_metadata(pdf, options, RunControl{&cancel});
    require(cancelled && cancelled.value().cancelled, "cancellation reported");
    std::cout << "Engine metadata passed\n";
}
