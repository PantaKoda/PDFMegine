#pragma once

// S6 Document Metadata Extraction. Given positioned text of selected pages
// (S1 values), identify the document's title, contributors, edition and
// years, with evidence and uncertainty per field, and list every ISBN
// printed on those pages. Pure computation: never
// opens a PDF, runs OCR or fetches pages (Engine decides which pages).

#include <pdfbookmark/core/types.hpp>
#include <pdfbookmark/text/acquisition.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfbookmark::metadata {

enum class FieldStatus { Resolved, Ambiguous, NotFoundInSearch };

enum class PageRole {
    Cover,      // First page with prominent display text (often an image).
    TitlePage,  // Title set prominently on a text page (incl. half-title).
    Copyright,  // Copyright / publication / ISBN statements.
    Contents,   // Table of contents (not a metadata source).
    Other,
    Unknown     // No usable text.
};

struct Evidence {
    text::SourceReference source;  // Page, revision and region.
    std::string text;              // Supporting text as read.
    std::string reason;            // Why it supports the value.
};

struct TitleValue {
    std::string title;
    std::optional<std::string> subtitle;
};

enum class ContributorRole { Author, Editor, Translator, Organization };
struct Contributor {
    std::string name;
    ContributorRole role = ContributorRole::Author;
};

struct EditionValue {
    std::string statement;             // As printed, e.g. "Third Edition".
    std::optional<std::uint32_t> ordinal;  // 3; absent for e.g. "Revised".
};

enum class YearKind { Publication, Copyright, Printing };
struct YearValue {
    int year = 0;
    YearKind kind = YearKind::Publication;
    std::string statement;  // The line it was read from.
};

// One ISBN identifies one edition in one format (hardcover, paperback,
// ebook...), so a book usually prints several. All are listed; none is
// chosen as "the" ISBN.
enum class IsbnForm { Isbn10, Isbn13 };  // The form that was printed.
enum class IsbnFormat {
    Unknown,     // No recognised format label next to the number.
    Print,       // "print", binding not stated.
    Hardcover,
    Paperback,
    Electronic   // ebook, e-ISBN, PDF, EPUB, online.
};
struct IsbnValue {
    std::string isbn13;   // 13 digits, no separators; an ISBN-10 is converted.
    std::string printed;  // First printed form, e.g. "978-1-2345-6789-7".
    IsbnForm form = IsbnForm::Isbn13;
    IsbnFormat format = IsbnFormat::Unknown;
    std::optional<std::string> label;  // Printed qualifier, e.g. "hardback".
    std::vector<Evidence> evidence;    // Every line that prints this ISBN.
};

template <typename T>
struct Candidate {
    T value;
    double score = 0;  // Ranking only; not a probability.
    std::vector<Evidence> evidence;
    std::vector<std::string> reasons;
};

template <typename T>
struct Field {
    FieldStatus status = FieldStatus::NotFoundInSearch;
    std::optional<T> value;           // Only when Resolved.
    std::vector<Evidence> evidence;   // Support for `value`.
    std::vector<Candidate<T>> alternatives;  // Other or competing candidates.
    std::vector<std::string> reasons;
};

struct PageAssessment {
    PageIndex page_index = 0;
    PageRole role = PageRole::Unknown;
    std::vector<std::string> reasons;
};

// Optional hints (e.g. the PDF file's Info dictionary). A hint never
// resolves a field by itself; it can only corroborate page evidence.
struct DocumentHints {
    std::optional<std::string> title;
    std::optional<std::string> author;
};

struct MetadataOptions {
    double title_block_ratio = 0.7;   // Lines at >= ratio x largest height.
    double title_prominence = 1.5;    // Single-page title vs next largest.
    int min_year = 1450;
    int max_year = 2100;
};

struct MetadataResult {
    Field<TitleValue> title;
    Field<std::vector<Contributor>> contributors;
    Field<EditionValue> edition;
    Field<YearValue> publication_year;  // Publication statements only.
    Field<YearValue> copyright_year;    // "©" / "Copyright" statements.
    // Every ISBN with a valid check digit, in order of first appearance.
    // Empty means none was found in the supplied pages.
    std::vector<IsbnValue> isbns;
    std::vector<PageAssessment> pages;  // Physical order.
    std::vector<std::string> diagnostics;
    std::string policy_id;
};

// Supplied pages may be in any order; duplicates and stale selected-content
// indices are argument errors. Missing or failed pages are reported, not
// treated as empty.
Result<MetadataResult> extract(const std::vector<text::PageAcquisition>& pages,
                               const DocumentHints& hints = {},
                               const MetadataOptions& options = {});

const char* status_name(FieldStatus status);
const char* role_name(PageRole role);
const char* role_name(ContributorRole role);
const char* kind_name(YearKind kind);
const char* form_name(IsbnForm form);
const char* format_name(IsbnFormat format);

}  // namespace pdfbookmark::metadata
