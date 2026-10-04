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
    require(r.policy_id == "s6-document-metadata-v3", "policy id");
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
    require(r.isbns.size() == 1 && r.isbns[0].isbn13 == "9781234567897" &&
                r.isbns[0].printed == "978-1-2345-6789-7" &&
                r.isbns[0].form == IsbnForm::Isbn13 &&
                r.isbns[0].format == IsbnFormat::Unknown && !r.isbns[0].label &&
                r.isbns[0].evidence.size() == 1 &&
                r.isbns[0].evidence[0].source.page_index == 3,
            "the ISBN on the copyright page is listed with its source line");

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
    require(crc.value().isbns.size() == 1 &&
                crc.value().isbns[0].isbn13 == "9781439811924" &&
                crc.value().isbns[0].format == IsbnFormat::Paperback &&
                crc.value().isbns[0].label == std::string("Paperback"),
            "'International Standard Book Number' with its printed format label");

    // Every ISBN is listed: one per format, ISBN-10 converted, repeats merged.
    const auto isbn_page = page(4, {
        {"\xC2\xA9 2011 by Example Press", 60, 8},
        {"ISBN 978-1-4398-1192-4 (hardback : alk. paper)", 80, 8},
        {"978-1-4398-1193-1 (ebook)", 95, 8},                // Line below: no keyword.
        {"ISBN-10: 1-4398-1192-X", 110, 8},                  // Same book as line 1.
        {"ISBN-10 0-306-40615-2 ISBN-13 978-0-306-40615-7", 125, 8},  // One ISBN twice.
        {"e-ISBN 978\xE2\x80\x93" "3\xE2\x80\x93" "642\xE2\x80\x93" "11111\xE2\x80\x93" "2",
         140, 8},                                            // En dashes.
        {"ISBN 0 19 853453 1 pbk", 155, 8},
        {"ISBN 978-1-4398-1192-5", 170, 8},                  // Wrong check digit.
        {"Printed in the United States of America", 185, 8},
        {"Order number 9781234567897", 200, 8}});            // Not an ISBN statement.
    const auto listed = extract({cover, title, isbn_page});
    require(static_cast<bool>(listed), "extract with an ISBN page succeeds");
    const auto& isbns = listed.value().isbns;
    require(isbns.size() == 5, "five distinct ISBNs; the invalid and the unlabelled "
                               "number are not listed");
    require(isbns[0].isbn13 == "9781439811924" && isbns[0].format == IsbnFormat::Hardcover &&
                isbns[0].label == std::string("hardback : alk. paper") &&
                isbns[0].form == IsbnForm::Isbn13 && isbns[0].evidence.size() == 2,
            "hardback ISBN; its ISBN-10 form (check digit X) is merged as evidence");
    require(isbns[1].isbn13 == "9781439811931" && isbns[1].format == IsbnFormat::Electronic &&
                isbns[1].evidence[0].reason == "Line directly below an ISBN statement",
            "an ISBN on the line below an ISBN statement is listed");
    require(isbns[2].isbn13 == "9780306406157" && isbns[2].form == IsbnForm::Isbn10 &&
                isbns[2].printed == "0-306-40615-2" && isbns[2].evidence.size() == 1,
            "ISBN-10 and ISBN-13 of one book on one line are one entry; the "
            "'ISBN-10'/'ISBN-13' tags are not read as digits");
    require(isbns[3].isbn13 == "9783642111112" && isbns[3].format == IsbnFormat::Electronic &&
                isbns[3].label == std::string("e-ISBN") &&
                isbns[3].printed == "978-3-642-11111-2",
            "e-ISBN with typographic dashes");
    require(isbns[4].isbn13 == "9780198534532" && isbns[4].format == IsbnFormat::Paperback &&
                isbns[4].label == std::string("pbk") && isbns[4].printed == "0 19 853453 1",
            "space-separated ISBN-10 with a bare format word");
    const auto& diagnostics = listed.value().diagnostics;
    require(std::any_of(diagnostics.begin(), diagnostics.end(), [](const std::string& d) {
                return d.find("978-1-4398-1192-5") != std::string::npos &&
                       d.find("check digit") != std::string::npos;
            }),
            "an ISBN with a wrong check digit is reported, not listed");

    // A misread ISBN-13 is not rescued as an ISBN-10, a qualifier in front
    // of the next ISBN is not this ISBN's label, and a ten-digit number
    // below an ISBN line is not an ISBN.
    const auto strict_page = page(4, {
        {"ISBN 978-0-306-40615-2", 60, 8},  // Tail "0-306-40615-2" is a valid ISBN-10.
        {"Hardback ISBN 978-1-4398-1192-4 Paperback ISBN 978-1-4398-1193-1", 80, 8},
        {"9 8 7 6 5 4 3 2 1 0", 95, 8},     // Printer's key: passes the ISBN-10 check.
        {"ISBN 0-19-853453-1 (ISBN-13 978-0-19-853453-2) ISBN 978-3-642-11111-3", 110, 8}});
    const auto strict = extract({cover, title, strict_page});
    require(static_cast<bool>(strict), "extract with the strict ISBN page succeeds");
    const auto& s_isbns = strict.value().isbns;
    require(s_isbns.size() == 3 && s_isbns[0].isbn13 == "9781439811924" &&
                s_isbns[0].format == IsbnFormat::Hardcover &&
                s_isbns[1].isbn13 == "9781439811931" &&
                s_isbns[1].format == IsbnFormat::Paperback &&
                s_isbns[2].isbn13 == "9780198534532" && !s_isbns[2].label,
            "misread ISBN-13 and printer's key are not listed; each ISBN keeps its "
            "own qualifier; a parenthesis holding the next ISBN is not a label");
    const auto& s_diag = strict.value().diagnostics;
    const auto reported = [&](const char* number) {
        return std::any_of(s_diag.begin(), s_diag.end(), [&](const std::string& d) {
            return d.find(number) != std::string::npos;
        });
    };
    require(reported("978-0-306-40615-2") && reported("978-3-642-11111-3"),
            "a failing ISBN is reported even when its line also has a valid one");

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

    // Issue #13: a reprint whose text layer was made by OCR. The half-title
    // sets the subtitle smaller and apart; the title page sets it in capitals
    // over two lines, then the authors one per line, an affiliation, a
    // letter-spaced publisher logo with a soft hyphen and the imprint. The
    // copyright page reads the copyright sign as "0".
    const auto half_title = page(0, {{"Black Holes,", 72.3, 16.8},
                                     {"White Dwarfs,", 92.7, 16.8},
                                     {"and Neutron Stars", 113.0, 13.7},
                                     {"The Physics of Compact Objects", 141.6, 11.8}});
    const auto title_page = [&](const std::vector<std::string>& authors, bool affiliation) {
        std::vector<L> lines = {{"Black Holes,", 73.8, 27.2},
                                {"White Dwarfs,", 105.3, 28.0},
                                {"and Neutron Stars", 138.5, 22.2},
                                {"THE PHYSICS OF", 191.5, 13.5},
                                {"COMPACT OBJECTS", 211.8, 13.5}};
        double y = 286.2;
        for (const auto& a : authors) {
            lines.push_back({a, y, 17.1});
            y += 26.1;
        }
        if (affiliation) lines.push_back({"Cornell University, Ithaca, New York", y - 6, 9.5});
        lines.push_back({"W I LEY\xC2\xAD" "VCH", 480.4, 19.4});
        lines.push_back({"WILEY-VCH Verlag GmbH & Co. KGaA", 511.7, 13.2});
        return page(1, lines);
    };
    const auto reprint_copyright = page(2, {
        {"Library of Congress Card No.: applied for", 246.7, 8.3},
        {"0 1983 by A & Sons, Inc.", 386.6, 8.3},
        {"0 2004 B Verlag GmbH & Co. KGaA, Weinheim", 399.6, 8.4},
        {"All rights reserved (including those of translation into other languages).", 424.6, 8.3}});
    std::vector<L> body_lines = {{"Copyright 0 2004 B Verlag GmbH & Co. KGaA", 20, 6.9}};
    for (int i = 0; i < 30; ++i)
        body_lines.push_back({"A line of ordinary body text in the first chapter.", 100.0 + 12 * i, 9.6});
    const auto chapter_page = page(15, body_lines);
    const std::vector<std::vector<std::string>> author_sets = {
        {"Stuart L. Shapiro"},
        {"Stuart L. Shapiro", "Saul A. Teukolsky"},
        {"Stuart L. Shapiro", "Saul A. Teukolsky", "Ann B. Author"}};
    for (const auto& authors : author_sets)
        for (const bool affiliation : {true, false}) {
            const auto r = extract(
                {half_title, title_page(authors, affiliation), reprint_copyright, chapter_page});
            const std::string variant = std::to_string(authors.size()) + " author(s)" +
                                        (affiliation ? " with affiliation" : "");
            require(r && r.value().title.status == FieldStatus::Resolved &&
                        r.value().title.value->title ==
                            "Black Holes, White Dwarfs, and Neutron Stars" &&
                        r.value().title.value->subtitle == "The Physics of Compact Objects" &&
                        r.value().title.alternatives.empty(),
                    "one title read on two pages, in two layouts, agrees (" + variant + ")");
            require(r.value().contributors.status == FieldStatus::Resolved &&
                        names(r.value().contributors) == authors,
                    "title-page author block gives every author in order, no subtitle or "
                    "logo line (" + variant + ")");
        }
    const auto reprint = extract({half_title, title_page(author_sets[1], true),
                                  reprint_copyright, chapter_page});
    const auto& cy = reprint.value().copyright_year;
    require(cy.status == FieldStatus::Resolved && cy.value->year == 1983 &&
                cy.evidence.size() == 1 && cy.evidence[0].source.page_index == 2 &&
                cy.alternatives.size() == 1 && cy.alternatives[0].value.year == 2004,
            "'0 1983 by A' is the original copyright; the 2004 reprint is an alternative");
    require(reprint.value().publication_year.status == FieldStatus::NotFoundInSearch &&
                reprint.value().pages[3].role == PageRole::Other,
            "a running 'Copyright 0 2004' head does not make a copyright page");
    // A running foot is weaker evidence: it counts only for a kind of year
    // that no copyright page states.
    std::vector<L> footed = {{"\xC2\xA9 2024 B GmbH. Published 2024 by B", 680, 6.9}};
    for (int i = 0; i < 30; ++i)
        footed.push_back({"A line of ordinary body text in the first chapter.", 100.0 + 12 * i, 9.6});
    const auto with_foot = extract({reprint_copyright, page(20, footed)});
    require(with_foot && with_foot.value().copyright_year.value->year == 1983 &&
                with_foot.value().publication_year.status == FieldStatus::Resolved &&
                with_foot.value().publication_year.value->year == 2024 &&
                with_foot.value().publication_year.evidence[0].source.page_index == 20,
            "a running foot gives the publication year no copyright page states, "
            "not the copyright year the copyright page states");

    // Counterexamples found on real books: a law's year is not a copyright
    // year, a figure axis "0 2000 4000" is not a copyright sign and year.
    const auto legal = extract({page(4, {
        {"Copyright \xC2\xA9 2005, Howard D. Curtis. All rights reserved", 100, 8},
        {"provisions of the Copyright, Designs and Patents Act 1988 or under the terms", 115, 8},
        {"permitted under Section 107 or 108 of the 1976 United States Copyright Act", 130, 8},
        {"0 2000 4000 6000 8000 10000", 145, 8}})});
    require(legal && legal.value().copyright_year.status == FieldStatus::Resolved &&
                legal.value().copyright_year.value->year == 2005,
            "years of copyright laws and figure axes are not copyright years");
    require(!extract({page(1, {{"Black Holes,", 73.8, 27.2},
                               {"F IFTH E DITION", 200, 17.1},
                               {"\xE2\x80\x94 Jean-Claude Brantschen", 230, 17.1}})})
                 .value().contributors.value,
            "small capitals ('F IFTH E DITION') and a dash-led attribution are not names");
    // A smaller line of names below the title is the authors, not a subtitle.
    const auto names_below = extract({page(3, {{"Applied Computational Physics", 92.5, 16.8},
                                               {"Joseph F. Boudreau and Eric S. Swanson", 132.6, 11.5},
                                               {"with contributions from Riccardo Maria Bianchi", 159, 9.7}})});
    require(names_below &&
                (!names_below.value().title.value || !names_below.value().title.value->subtitle) &&
                !names_below.value().contributors.alternatives.empty() &&
                names_below.value().contributors.alternatives[0].value.size() == 2,
            "a line listing names is read as contributors, not as the subtitle");
    // "by ..." confirms the names it gives, not other name-like lines on the
    // page (a subtitle fragment, the publisher's city).
    const auto stated = extract({page(4, {{"How Linux Works", 100, 30},
                                          {"Should\xC2\xA0Know", 200, 12},
                                          {"by Brian Ward", 250, 12},
                                          {"San Francisco", 600, 9}})});
    require(stated && names(stated.value().contributors) == std::vector<std::string>{"Brian Ward"},
            "a responsibility statement confirms only its own names");
    // Names whose words are separated by no-break spaces are still names.
    const auto nbsp = extract({page(1, {{"Pro Cryptography and", 156, 37.5},
                                        {"Cryptanalysis", 198, 37.5},
                                        {"Marius\xC2\xA0Iulian\xC2\xA0Mihailescu", 527, 13.1},
                                        {"Stefania\xC2\xA0Loredana\xC2\xA0Nita", 547, 13.3}})});
    require(nbsp && !nbsp.value().contributors.alternatives.empty() &&
                nbsp.value().contributors.alternatives[0].value.size() == 2 &&
                nbsp.value().contributors.alternatives[0].value[0].name == "Marius Iulian Mihailescu",
            "no-break spaces inside names are read as spaces");
    // A chapter opening set large ("1 Mathematical Preliminaries") is not a title.
    const auto chapter_open = extract({page(18, {{"1 Mathematical Preliminaries", 120, 30},
                                                 {"and Error Analysis", 160, 30},
                                                 {"Introduction", 300, 10}})});
    require(chapter_open && chapter_open.value().title.status == FieldStatus::NotFoundInSearch,
            "a numbered chapter heading is not the book title");
    // An author and, far below in another size, the publisher's cities are
    // two blocks: the layout rule does not resolve either.
    const auto cities = extract({page(2, {{"Think Bayes", 152, 28.5},
                                          {"Bayesian Statistics in Python", 183, 17.2},
                                          {"Allen B. Downey", 370, 15.3},
                                          {"Beijing Boston Farnham Sebastopol Tokyo", 595, 11.1}})});
    require(cities && cities.value().contributors.status == FieldStatus::Ambiguous,
            "name lines far apart in different sizes are not one author block");
    // A soft hyphen or letter-spaced logo is never a name, even on its own.
    const auto logo_only = extract({page(1, {{"Black Holes,", 73.8, 27.2},
                                             {"W I LEY\xC2\xAD" "VCH", 300, 17.1}})});
    require(logo_only && logo_only.value().contributors.status == FieldStatus::NotFoundInSearch,
            "a letter-spaced logo with a soft hyphen is not a contributor");

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
                body.value().publication_year.status == FieldStatus::NotFoundInSearch &&
                body.value().isbns.empty(),
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
