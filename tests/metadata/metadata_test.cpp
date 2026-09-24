// S6 pure-value tests: synthetic S1 pages, no PDF/OCR backend.
#include <pdfbookmark/metadata/metadata.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace pdfbookmark;
using namespace pdfbookmark::metadata;

void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct L {
    std::string text;
    double y, height, x = 60;
};

text::PageAcquisition page(PageIndex index, const std::vector<L>& lines,
                           std::size_t images = 0) {
    text::PageAcquisition p;
    p.page_index = index;
    p.outcome = text::Outcome::Ok;
    p.assessment.image_object_count = images;
    text::PageContent c;
    c.page_index = index;
    c.revision = static_cast<std::uint64_t>(index + 1);
    c.geometry.width_points = 500;
    c.geometry.height_points = 700;
    std::uint32_t id = 0;
    for (const auto& l : lines) {
        text::TextRegion r;
        r.id = id++;
        r.text = l.text;
        const double w = 0.5 * l.height * static_cast<double>(l.text.size());
        r.quad = Quad{{Point{l.x, l.y}, Point{l.x + w, l.y},
                       Point{l.x + w, l.y + l.height}, Point{l.x, l.y + l.height}}};
        c.regions.push_back(std::move(r));
    }
    p.selected = std::move(c);
    return p;
}

std::vector<std::string> names(const Field<std::vector<Contributor>>& f) {
    std::vector<std::string> out;
    if (f.value)
        for (const auto& c : *f.value) out.push_back(c.name);
    return out;
}

}  // namespace

int main() {
    // A second-edition book: cover, title page, copyright page, TOC.
    const auto cover = page(0, {{"Example Press", 30, 12},
                                {"Parallel", 100, 44}, {"Worlds", 150, 44},
                                {"Jane Q. Doe", 500, 24}, {"John Smith", 530, 24}},
                            1);
    const auto title = page(2, {{"Parallel Worlds", 120, 30},
                                {"A Practical Guide", 160, 16},
                                {"Second Edition", 220, 12},
                                {"by Jane Q. Doe and John Smith", 280, 12},
                                {"Example Press", 620, 10}});
    const auto copyright = page(3, {
        {"Example Press, 1 Main Street, Springfield 55555", 60, 8},
        {"\xC2\xA9 2005, 2012 by Example Press", 80, 8},
        {"First edition published 2005", 95, 8},
        {"Second edition published 2012", 110, 8},
        {"Printed in the United States of America 10 9 8 7 6 5 4 3 2 1", 125, 8},
        {"ISBN 978-1-2345-6789-7", 140, 8}});
    const auto contents = page(4, {{"Contents", 60, 18},
                                   {"1 Introduction 1", 120, 10},
                                   {"2 Methods 12", 140, 10}});
    const auto result = extract({contents, copyright, title, cover});
    require(static_cast<bool>(result), "extract succeeds");
    const auto& r = result.value();
    require(r.policy_id == "s6-document-metadata-v1", "policy id");
    require(r.pages.size() == 4 && r.pages[0].role == PageRole::Cover &&
                r.pages[1].role == PageRole::TitlePage &&
                r.pages[2].role == PageRole::Copyright &&
                r.pages[3].role == PageRole::Contents,
            "page roles: cover, title, copyright, contents (physical order)");
    require(r.title.status == FieldStatus::Resolved && r.title.value &&
                r.title.value->title == "Parallel Worlds" &&
                r.title.value->subtitle == std::string("A Practical Guide") &&
                r.title.evidence.size() >= 3,
            "title agreed by cover and title page; subtitle kept separate");
    require(r.contributors.status == FieldStatus::Resolved &&
                names(r.contributors) ==
                    std::vector<std::string>{"Jane Q. Doe", "John Smith"} &&
                (*r.contributors.value)[0].role == ContributorRole::Author,
            "authors from the 'by' statement and cover agree");
    require(r.edition.status == FieldStatus::Resolved && r.edition.value &&
                r.edition.value->statement == "Second Edition" &&
                r.edition.value->ordinal == 2u,
            "edition statement preserved with its ordinal");
    require(r.publication_year.status == FieldStatus::Resolved &&
                r.publication_year.value->year == 2012 &&
                r.publication_year.value->kind == YearKind::Publication,
            "publication year tied to the identified (second) edition");
    require(r.copyright_year.status == FieldStatus::Ambiguous &&
                r.copyright_year.alternatives.size() == 2,
            "two copyright years stay ambiguous; the largest is not assumed");
    require(std::none_of(r.publication_year.alternatives.begin(),
                         r.publication_year.alternatives.end(),
                         [](const auto& c) { return c.value.year == 2012 &&
                                                    c.value.kind == YearKind::Publication; }),
            "the chosen year is not repeated among alternatives");

    // Real-book layout: a smaller lead-in line ("Introduction to") above
    // larger title lines, words split across regions on one line, and a
    // half-title page whose first line is smaller than the rest.
    const auto hpc_cover = page(0, {
        {"Chapman & Hall/CRC", 43, 18.5}, {"Computational Science Series", 63, 17.3},
        {"Introduction to", 87, 39.6}, {"High Performance", 129, 51.4},
        {"Computing", 177, 50.2}, {"for", 179, 39.4, 330},
        {"Scientists and Engineers", 222, 48},
        {"Georg", 534, 36.5}, {"Hager", 534, 36.2, 200}, {"Gerhard Wellein", 571, 33.6},
        {"CRC Press", 620, 15.6}, {"Taylor & Francis Group", 636, 9.8},
        {"A CHAPMAN & HALL BOOK", 653, 9.1}}, 1);
    const auto hpc_half = page(1, {{"Introduction to", 110, 16.8},
                                   {"High Performance", 140, 22.4},
                                   {"Computing for", 170, 22.4},
                                   {"Scientists and Engineers", 200, 22.4},
                                   {"K10600_FM.indd 1 6/1/10 11:51:56 AM", 676, 5.6}});
    const auto hpc = extract({hpc_cover, hpc_half});
    require(hpc && hpc.value().pages[0].role == PageRole::Cover &&
                hpc.value().pages[1].role == PageRole::TitlePage &&
                hpc.value().title.status == FieldStatus::Resolved &&
                hpc.value().title.value->title ==
                    "Introduction to High Performance Computing for Scientists and Engineers" &&
                hpc.value().contributors.status == FieldStatus::Ambiguous &&
                hpc.value().contributors.alternatives.size() == 1 &&
                hpc.value().contributors.alternatives[0].value.size() == 2 &&
                hpc.value().contributors.alternatives[0].value[0].name == "Georg Hager",
            "cover + half-title agree on the title; names seen only on the cover "
            "stay ambiguous until another page confirms them");

    // Copyright-only page (like the HPC book): publication stays not found.
    const auto only_copyright = page(4, {
        {"CRC Press", 130, 7}, {"\xC2\xA9 2011 by Taylor and Francis Group, LLC", 177, 7},
        {"Printed in the United States of America on acid-free paper", 220, 7},
        {"10 9 8 7 6 5 4 3 2 1", 229, 5},
        {"International Standard Book Number: 978-1-4398-1192-4 (Paperback)", 246, 7}});
    const auto crc = extract({cover, title, only_copyright});
    require(crc && crc.value().copyright_year.status == FieldStatus::Resolved &&
                crc.value().copyright_year.value->year == 2011 &&
                crc.value().copyright_year.value->kind == YearKind::Copyright &&
                crc.value().publication_year.status == FieldStatus::NotFoundInSearch &&
                crc.value().publication_year.reasons.back().find("copyright year (2011)") !=
                    std::string::npos,
            "copyright year is not reported as the publication year");

    // A series page lists other books with their editors; the title
    // statement ", Name and Name" confirms this book's authors.
    const auto series = page(1, {
        {"Example Series", 40, 12},
        {"PUBLISHED TITLES", 300, 7},
        {"GRID STUFF", 320, 6}, {"Edited by Other Person", 333, 9},
        {"PARALLEL WORLDS, Jane Q. Doe and John Smith", 360, 6},
        {"MORE THINGS", 380, 6}, {"Edited by Someone Else", 393, 9}});
    const auto with_series = extract({cover, series, title});
    require(with_series &&
                names(with_series.value().contributors) ==
                    std::vector<std::string>{"Jane Q. Doe", "John Smith"},
            "editors of other series titles are never attributed to this book");

    // Explicit editors.
    const auto edited = extract({page(0, {{"Collected Papers", 120, 30},
                                          {"Edited by Ann Lee", 300, 12}})});
    require(edited && edited.value().contributors.status == FieldStatus::Resolved &&
                (*edited.value().contributors.value)[0].role == ContributorRole::Editor,
            "'Edited by' yields an editor");

    // Conflicting single-page titles are ambiguous.
    const auto conflict = extract({page(0, {{"First Title", 100, 30}, {"small", 400, 8}}, 1),
                                   page(1, {{"Other Title", 100, 30}})});
    require(conflict && conflict.value().title.status == FieldStatus::Ambiguous &&
                !conflict.value().title.value &&
                conflict.value().title.alternatives.size() == 2,
            "competing titles stay ambiguous with both alternatives");

    // Body text only: nothing found, and a hint alone resolves nothing.
    DocumentHints hints;
    hints.title = "Hinted Title";
    const auto body = extract({page(5, {{"This is ordinary body text on a page.", 100, 10},
                                        {"It continues with more ordinary text.", 115, 10},
                                        {"And then some more text follows here.", 130, 10},
                                        {"With a fourth line of body text too.", 145, 10},
                                        {"The fifth line ends this paragraph.", 160, 10},
                                        {"Then a sixth line starts another one.", 175, 10},
                                        {"And a seventh finishes the page off.", 190, 10}})},
                              hints);
    require(body && body.value().title.status == FieldStatus::NotFoundInSearch &&
                body.value().contributors.status == FieldStatus::NotFoundInSearch &&
                body.value().edition.status == FieldStatus::NotFoundInSearch &&
                body.value().publication_year.status == FieldStatus::NotFoundInSearch,
            "body pages give not-found fields; a metadata hint never resolves alone");

    // Missing/failed pages are reported, duplicates rejected.
    text::PageAcquisition failed;
    failed.page_index = 9;
    failed.outcome = text::Outcome::Failed;
    const auto with_failed = extract({cover, failed});
    require(with_failed && with_failed.value().pages.back().role == PageRole::Unknown,
            "an unreadable page is reported as unknown, not as empty");
    require(!extract({cover, cover}), "duplicate pages are rejected");
    std::cout << "S6 metadata fixtures passed\n";
}
