#include <pdfbookmark/mapping/mapping.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace pdfbookmark;
using namespace pdfbookmark::mapping;
using pdfbookmark::parsing::NumberingStyle;

void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
parsing::TocEntry entry(const std::string& id, const std::string& title,
                        const std::string& literal, NumberingStyle style,
                        std::uint32_t ordinal, const std::string& prefix = {},
                        std::optional<std::uint32_t> range_end = {}) {
    parsing::TocEntry result;
    result.id = id;
    result.title = title;
    result.sources.push_back({0, 1, 0, std::nullopt, std::nullopt});
    if (!literal.empty()) {
        parsing::PrintedReference ref;
        ref.literal = literal;
        ref.numbering = style;
        ref.ordinal = ordinal;
        ref.prefix = prefix;
        ref.is_range = range_end.has_value();
        ref.range_end = range_end;
        result.printed_reference = std::move(ref);
    }
    return result;
}
text::PageAcquisition page(PageIndex index, const std::string& footer = {},
                           const std::string& heading = {}) {
    text::PageAcquisition result;
    result.page_index = index;
    result.outcome = text::Outcome::Ok;
    text::PageContent content;
    content.page_index = index;
    content.revision = static_cast<std::uint64_t>(index + 2);
    content.geometry.width_points = 600;
    content.geometry.height_points = 800;
    if (!heading.empty()) {
        text::TextRegion region;
        region.id = 0;
        region.text = heading;
        region.quad = Quad{{Point{40, 40}, Point{450, 40},
                            Point{450, 56}, Point{40, 56}}};
        content.regions.push_back(std::move(region));
    }
    if (!footer.empty()) {
        text::TextRegion region;
        region.id = 1;
        region.text = footer;
        region.quad = Quad{{Point{280, 744}, Point{320, 744},
                            Point{320, 760}, Point{280, 760}}};
        content.regions.push_back(std::move(region));
    }
    result.selected = std::move(content);
    return result;
}
text::PdfPageFacts label(PageIndex index, const std::string& value) {
    text::PdfPageFacts result;
    result.page_index = index;
    result.viewer_label.availability = text::FactAvailability::Present;
    result.viewer_label.value = value;
    return result;
}
DocumentEvidence document(PageCount count = 80) {
    DocumentEvidence result;
    result.input.page_count = count;
    return result;
}
NumberingSection section(const std::string& id, PageIndex first,
                         PageIndex end, NumberingStyle style,
                         const std::string& prefix = {},
                         bool trust_labels = false) {
    return {id, first, end, style, prefix, "fixture",
            trust_labels};
}
bool has_destination(const EntryMapping& mapped, PageIndex destination) {
    return std::any_of(
        mapped.alternatives.begin(), mapped.alternatives.end(),
        [&](const auto& value) {
            return value.pdf_page_index == destination;
        });
}
}  // namespace

int main() {
    const auto target = entry("e12", "Transport", "12",
                              NumberingStyle::Decimal, 12);
    auto base = document();
    base.sections.push_back(
        section("body", 12, 60, NumberingStyle::Decimal));
    base.pages = {page(12, "1"), page(14, "3"), page(23, "12")};
    const auto inferred = map({target}, base);
    require(inferred && inferred.value().entries.size() == 1 &&
            inferred.value().entries[0].status == MappingStatus::Resolved &&
            inferred.value().entries[0].pdf_page_index == 23 &&
            inferred.value().entries[0].method ==
                ResolutionMethod::InferredOffset &&
            inferred.value().entries[0].supporting_pages.size() == 3 &&
            inferred.value().entries[0].supporting_sources.size() >= 2 &&
            inferred.value().policy_id == "s4-page-mapping-v4",
            "printed ordinal 1 at index 12 implies offset 11, with two anchors and target confirmation");

    // Issue #13: a target heading that differs from the TOC entry only by
    // text-layer errors (one wrong letter, "1" for "I", a chapter label the
    // page omits) confirms an offset that two anchors already give.
    auto layer_errors = base;
    layer_errors.pages = {page(12, "1"), page(14, "3"),
                   page(41, {}, "13.4 Hercules X-I: A Protolype Binary X-Ray Pulsar"),
                   page(51, {}, "Appendix 1 Radiative Transport"),
                   page(55, {}, "Star Deaths and the Formation of Compact Objects"),
                   page(56, {}, "Pulsers")};
    const auto approximate = map(
        {entry("e30", "13.4 Hercules X-I: A Prototype Binary X-Ray Pulsar", "30",
               NumberingStyle::Decimal, 30),
         entry("e40", "Appendix I. Radiative Transport", "40", NumberingStyle::Decimal, 40),
         entry("e44", "Chapter 1. Star Deaths and the Formation of Compact Objects", "44",
               NumberingStyle::Decimal, 44),
         entry("e45", "Pulsars", "45", NumberingStyle::Decimal, 45)},
        layer_errors);
    require(approximate && approximate.value().entries.size() == 4,
            "approximate confirmation fixture maps");
    const auto& loose = approximate.value().entries;
    for (std::size_t i = 0; i < 3; ++i)
        require(loose[i].status == MappingStatus::Resolved &&
                    loose[i].pdf_page_index == static_cast<PageIndex>(
                        std::vector<int>{41, 51, 55}[i]) &&
                    std::any_of(loose[i].supporting_sources.begin(),
                                loose[i].supporting_sources.end(),
                                [&](const text::SourceReference& s) {
                                    return s.page_index == *loose[i].pdf_page_index;
                                }),
                "a heading with text-layer errors confirms the offset target");
    require(loose[3].status == MappingStatus::Unresolved,
            "a short title is never matched loosely ('Pulsers' is not 'Pulsars')");

    auto insufficient = base;
    insufficient.pages = {page(12, "1")};
    const auto one_anchor = map({target}, insufficient);
    const auto again = map({target}, insufficient);
    require(one_anchor && again &&
            one_anchor.value().entries[0].status ==
                MappingStatus::Unresolved &&
            !one_anchor.value().entries[0].pdf_page_index &&
            !one_anchor.value().requests.empty() &&
            one_anchor.value().requests.size() <= 8 &&
            one_anchor.value().requests.size() ==
                again.value().requests.size() &&
            one_anchor.value().requests[0].id ==
                again.value().requests[0].id,
            "one anchor stays unresolved with stable bounded requests");
    auto same_page_twice = insufficient;
    same_page_twice.sections[0].viewer_labels_match_printed = true;
    same_page_twice.facts.push_back(label(12, "1"));
    const auto duplicate_observation = map({target}, same_page_twice);
    require(duplicate_observation &&
            duplicate_observation.value().entries[0].status ==
                MappingStatus::Unresolved,
            "footer and viewer label on one physical page count as one anchor");
    for (const auto& request : one_anchor.value().requests)
        require(request.first >= 0 && request.end <= 80 &&
                request.end > request.first &&
                request.end - request.first <= 3,
                "every evidence request is bounded");
    MappingOptions one_request;
    one_request.max_requests = 1;
    const auto prioritized = map({target}, insufficient, {}, one_request);
    require(prioritized && prioritized.value().requests.size() == 1 &&
            prioritized.value().requests[0].first == 23 &&
            prioritized.value().requests[0].priority == 2,
            "the request cap keeps the highest-priority target check");

    auto absent = base;
    absent.pages.pop_back();
    const auto missing_target = map({target}, absent);
    require(missing_target &&
            missing_target.value().entries[0].status ==
                MappingStatus::Unresolved &&
            std::any_of(missing_target.value().requests.begin(),
                        missing_target.value().requests.end(),
                        [](const auto& request) {
                            return request.first == 23 &&
                                   request.end == 24;
                        }),
            "two anchors do not silently resolve an unobserved target");
    MappingOptions waived_confirmation;
    waived_confirmation.require_target_confirmation = false;
    const auto waived =
        map({target}, absent, {}, waived_confirmation);
    require(waived && waived.value().entries[0].pdf_page_index == 23 &&
            waived.value().entries[0].reasons[0].find("waived") !=
                std::string::npos,
            "explicit opt-out is recorded when target confirmation is waived");

    auto conflict = base;
    conflict.pages.push_back(page(20, "8"));
    const auto inserted = map({target}, conflict);
    require(inserted &&
            inserted.value().entries[0].status ==
                MappingStatus::Ambiguous &&
            has_destination(inserted.value().entries[0], 23) &&
            has_destination(inserted.value().entries[0], 24) &&
            !inserted.value().entries[0].pdf_page_index,
            "inserted-page discontinuity yields both offset alternatives");

    auto roman_doc = document();
    roman_doc.sections.push_back(
        section("front", 0, 10, NumberingStyle::Roman));
    roman_doc.pages = {page(2, "i"), page(3, "ii"), page(5, "iv")};
    const auto front = map(
        {entry("front-iv", "Preface", "iv",
               NumberingStyle::Roman, 4)}, roman_doc);
    require(front && front.value().entries[0].pdf_page_index == 5,
            "Roman front-matter ordinal is section scoped");

    auto appendix_doc = document();
    appendix_doc.sections.push_back(section(
        "appendix-a", 40, 70, NumberingStyle::PrefixedDecimal, "A"));
    appendix_doc.pages = {
        page(40, "A-1"), page(41, "A-2"), page(51, "A-12")};
    const auto appendix = map(
        {entry("a12", "Appendix topic", "A-12",
               NumberingStyle::PrefixedDecimal, 12, "A")},
        appendix_doc);
    require(appendix && appendix.value().entries[0].pdf_page_index == 51,
            "appendix prefix is retained as section identity");

    const auto range = map(
        {entry("range", "Methods", "12–15",
               NumberingStyle::Decimal, 12, {}, 15)}, base);
    require(range && range.value().entries[0].pdf_page_index == 23,
            "valid printed range uses its start for page-level destination");
    const auto outside_range = map(
        {entry("wide", "Methods", "12–65",
               NumberingStyle::Decimal, 12, {}, 65)}, base);
    require(outside_range &&
            outside_range.value().entries[0].status ==
                MappingStatus::Unresolved,
            "range end outside known section is not silently truncated");

    const auto no_ref = entry("heading", "PART I", "",
                              NumberingStyle::Unknown, 0);
    const auto missing_ref = map({no_ref}, base);
    require(missing_ref && missing_ref.value().entries[0].status ==
                MappingStatus::Unresolved &&
            !missing_ref.value().entries[0].pdf_page_index,
            "missing reference has no invented destination");
    const MappingOverride manual{OverrideKind::EntryDestination,
                                 "heading", 25, std::nullopt, "user"};
    const auto manually_resolved = map({no_ref}, base, {manual});
    require(manually_resolved &&
            manually_resolved.value().entries[0].pdf_page_index == 25 &&
            manually_resolved.value().entries[0].method ==
                ResolutionMethod::ManualEntry,
            "manual entry choice remains separate from inference");

    const MappingOverride offset{OverrideKind::SectionOffset,
                                 "body", std::nullopt, 11, "user"};
    const auto manual_offset = map({target}, insufficient, {offset});
    require(manual_offset &&
            manual_offset.value().entries[0].pdf_page_index == 23 &&
            manual_offset.value().entries[0].method ==
                ResolutionMethod::ManualOffset,
            "explicit section offset needs no inferred anchors");
    require(!map({target}, base, {offset, offset}) &&
            !map({target}, base,
                 {{OverrideKind::EntryDestination, "e12", 80,
                   std::nullopt, "user"}}),
            "duplicate or out-of-range manual overrides are rejected");

    const MappingOverride deliberate{
        OverrideKind::EntryDestination, "e12", 24, std::nullopt, "reviewer"};
    const auto disagreement = map({target}, base, {deliberate});
    require(disagreement &&
            disagreement.value().entries[0].pdf_page_index == 24 &&
            disagreement.value().entries[0].method ==
                ResolutionMethod::ManualEntry &&
            std::any_of(disagreement.value().entries[0].reasons.begin(),
                        disagreement.value().entries[0].reasons.end(),
                        [](const auto& reason) {
                            return reason.find("contradicts observed anchors") !=
                                   std::string::npos;
                        }),
            "manual entry decision wins but contradictory anchors remain visible");

    auto labels_doc = document();
    labels_doc.sections.push_back(
        section("labels", 12, 16, NumberingStyle::Decimal, {}, true));
    labels_doc.facts = {
        label(12, "1"), label(13, "2"),
        label(14, "2"), label(15, "4")};
    const auto label_entry =
        entry("label2", "Duplicate label", "2",
              NumberingStyle::Decimal, 2);
    const auto repeated = map({label_entry}, labels_doc);
    require(repeated &&
            repeated.value().entries[0].status ==
                MappingStatus::Ambiguous &&
            has_destination(repeated.value().entries[0], 13) &&
            has_destination(repeated.value().entries[0], 14),
            "repeated trusted viewer labels keep both plausible destinations");
    labels_doc.sections[0].viewer_labels_match_printed = false;
    const auto untrusted = map({label_entry}, labels_doc);
    require(untrusted &&
            untrusted.value().entries[0].status ==
                MappingStatus::Ambiguous &&
            has_destination(untrusted.value().entries[0], 13) &&
            has_destination(untrusted.value().entries[0], 14),
            "untrusted repeated viewer labels remain alternatives, never authority");

    auto unique_labels = document();
    unique_labels.sections.push_back(
        section("labels", 12, 16, NumberingStyle::Decimal, {}, true));
    unique_labels.facts = {
        label(12, "1"), label(13, "2"), label(14, "3"), label(15, "4")};
    const auto label3 = entry("label3", "Labelled chapter", "3",
                              NumberingStyle::Decimal, 3);
    const auto bare_label = map({label3}, unique_labels);
    require(bare_label &&
            bare_label.value().entries[0].status ==
                MappingStatus::Unresolved &&
            !bare_label.value().entries[0].pdf_page_index &&
            has_destination(bare_label.value().entries[0], 14) &&
            std::any_of(bare_label.value().requests.begin(),
                        bare_label.value().requests.end(),
                        [](const auto& request) {
                            return request.first == 14;
                        }),
            "a trusted-section viewer label alone does not resolve without page content");
    unique_labels.pages = {page(14, {}, "Labelled chapter")};
    const auto corroborated_label = map({label3}, unique_labels);
    require(corroborated_label &&
            corroborated_label.value().entries[0].pdf_page_index == 14 &&
            corroborated_label.value().entries[0].method ==
                ResolutionMethod::ViewerLabel &&
            !corroborated_label.value().entries[0]
                 .supporting_sources.empty(),
            "a viewer label resolves once acquired target content corroborates it");
    unique_labels.pages = {page(14, "7")};
    const auto contradicted_label = map({label3}, unique_labels);
    require(contradicted_label &&
            contradicted_label.value().entries[0].status ==
                MappingStatus::Unresolved,
            "a viewer label contradicted by the printed footer stays unresolved");

    // One footer page plus a label on another page is still one anchor.
    auto label_anchor = document();
    label_anchor.sections.push_back(
        section("body", 12, 60, NumberingStyle::Decimal, {}, true));
    label_anchor.pages = {page(12, "1")};
    label_anchor.facts = {label(13, "2")};
    const auto label_only_anchor = map({target}, label_anchor);
    require(label_only_anchor &&
            label_only_anchor.value().entries[0].status ==
                MappingStatus::Unresolved &&
            std::any_of(label_only_anchor.value().entries[0].reasons.begin(),
                        label_only_anchor.value().entries[0].reasons.end(),
                        [](const auto& reason) {
                            return reason.find("Fewer than two") !=
                                   std::string::npos;
                        }),
            "viewer labels never count as independent offset anchors");

    auto link_doc = document();
    text::PdfPageFacts toc_fact;
    toc_fact.page_index = 0;
    toc_fact.local_link_destinations.availability =
        text::FactAvailability::Present;
    toc_fact.local_link_destinations.value =
        std::vector<PageIndex>{30};
    link_doc.facts.push_back(toc_fact);
    link_doc.associated_links.push_back(
        {"e12", {0, 1, 0, std::nullopt, std::nullopt}, 30});
    const auto uncorroborated_link = map({target}, link_doc);
    require(uncorroborated_link &&
            uncorroborated_link.value().entries[0].status ==
                MappingStatus::Unresolved &&
            !uncorroborated_link.value().entries[0].pdf_page_index &&
            has_destination(uncorroborated_link.value().entries[0], 30) &&
            std::any_of(uncorroborated_link.value().requests.begin(),
                        uncorroborated_link.value().requests.end(),
                        [](const auto& request) {
                            return request.first == 30 && request.end == 31;
                        }),
            "a local link is an untrusted hint until target content corroborates it");
    link_doc.pages = {page(30, {}, "Transport")};
    const auto linked = map({target}, link_doc);
    require(linked &&
            linked.value().entries[0].pdf_page_index == 30 &&
            linked.value().entries[0].method ==
                ResolutionMethod::AssociatedLocalLink &&
            linked.value().entries[0].supporting_sources.size() == 2 &&
            linked.value().requests.empty(),
            "an associated local link resolves once its target heading is acquired");
    link_doc.pages = {page(30, "99", "Unrelated heading")};
    const auto contradicted_link = map({target}, link_doc);
    require(contradicted_link &&
            contradicted_link.value().entries[0].status ==
                MappingStatus::Unresolved &&
            !contradicted_link.value().entries[0].pdf_page_index,
            "a local link whose acquired target does not match stays unresolved");
    link_doc.pages.clear();
    link_doc.associated_links[0].destination = 31;
    require(!map({target}, link_doc),
            "a page-level link fact alone cannot be assigned to an entry");

    auto heading_doc = document();
    heading_doc.sections.push_back(
        section("short", 12, 15, NumberingStyle::Decimal));
    heading_doc.entry_sections.push_back(
        {"heading", "short", "reviewer"});
    heading_doc.pages = {
        page(12), page(13, {}, "PART I"), page(14)};
    const auto heading_match = map({no_ref}, heading_doc);
    require(heading_match &&
            heading_match.value().entries[0].pdf_page_index == 13 &&
            heading_match.value().entries[0].method ==
                ResolutionMethod::HeadingMatch,
            "exact heading within a fully supplied explicit section can map a no-reference entry");

    auto repeated_sections = document();
    repeated_sections.sections = {
        section("first", 12, 20, NumberingStyle::Decimal),
        section("second", 30, 40, NumberingStyle::Decimal)};
    const auto repeated_entry =
        entry("restart", "Introduction", "1",
              NumberingStyle::Decimal, 1);
    const std::vector<MappingOverride> restart_offsets = {
        {OverrideKind::SectionOffset, "first", std::nullopt, 11, "user"},
        {OverrideKind::SectionOffset, "second", std::nullopt, 29, "user"}};
    const auto restart = map({repeated_entry}, repeated_sections,
                             restart_offsets);
    require(restart &&
            restart.value().entries[0].status ==
                MappingStatus::Ambiguous &&
            has_destination(restart.value().entries[0], 12) &&
            has_destination(restart.value().entries[0], 30),
            "restarted decimal numbering needs entry-section association");
    repeated_sections.entry_sections.push_back(
        {"restart", "second", "reviewer"});
    const auto associated_restart =
        map({repeated_entry}, repeated_sections, restart_offsets);
    require(associated_restart &&
            associated_restart.value().entries[0].pdf_page_index == 30 &&
            associated_restart.value().entries[0].method ==
                ResolutionMethod::ManualOffset,
            "explicit section association selects one scoped offset");

    const auto beyond = map(
        {entry("far", "Far chapter", "70",
               NumberingStyle::Decimal, 70)}, base);
    require(beyond && beyond.value().entries[0].status ==
                MappingStatus::Unresolved &&
            !beyond.value().entries[0].pdf_page_index,
            "out-of-range inferred arithmetic remains unresolved");
    const MappingOverride impossible{
        OverrideKind::SectionOffset, "body", std::nullopt,
        std::numeric_limits<std::int64_t>::max(), "user"};
    require(!map({target}, base, {impossible}),
            "overflowing explicit arithmetic is rejected");
    auto stale = base;
    stale.pages.push_back(page(0));
    require(!map({target}, stale),
            "entry references cannot silently attach to a reacquired revision");

    // S4-09: page numbers in running headers ("Modern processors 3").
    auto headers = document();
    headers.sections.push_back(section("body", 12, 60, NumberingStyle::Decimal));
    headers.pages = {page(12, {}, "Modern processors 1"),
                     page(14, {}, "3 Modern processors"),
                     page(23, {}, "Transport 12"),
                     // Labels ("Chapter 9") are not page numbers.
                     page(30, {}, "Chapter 9"), page(31, {}, "Part 2")};
    const auto from_headers = map({target}, headers);
    require(from_headers &&
            from_headers.value().entries[0].status == MappingStatus::Resolved &&
            from_headers.value().entries[0].pdf_page_index == 23,
            "running-header page numbers anchor and confirm like footers");

    // S4-10: one stray number (e.g. a code line "50") among many agreeing
    // anchor pages is an outlier when each target is confirmed.
    auto stray = base;
    stray.pages = {page(12, "1"), page(13, "2"), page(14, "3"), page(15, "4"),
                   page(23, "12"), page(30, "50")};
    const auto dominant = map({target}, stray);
    require(dominant &&
            dominant.value().entries[0].status == MappingStatus::Resolved &&
            dominant.value().entries[0].pdf_page_index == 23 &&
            std::any_of(dominant.value().entries[0].reasons.begin(),
                        dominant.value().entries[0].reasons.end(),
                        [](const auto& r) { return r.find("outlier") != std::string::npos; }),
            "a single outlier does not veto a dominant, confirmed offset");
    MappingOptions strict;
    strict.require_target_confirmation = false;
    const auto strict_result = map({target}, stray, {}, strict);
    require(strict_result &&
            strict_result.value().entries[0].status != MappingStatus::Resolved &&
            !strict_result.value().entries[0].pdf_page_index,
            "without target confirmation any contradiction blocks resolution");

    // S4-09: several numbers in one band (figure axis) are not evidence, and
    // a printed 0 is never a page number.
    auto noisy = base;
    auto axis = page(20);
    for (std::uint32_t i = 0; i < 3; ++i) {
        text::TextRegion label;
        label.id = 10 + i;
        label.text = i == 0 ? "0" : std::to_string(100 + i);
        const double x = 100.0 + 60.0 * i;
        label.quad = Quad{{Point{x, 740}, Point{x + 20, 740},
                           Point{x + 20, 752}, Point{x, 752}}};
        axis.selected->regions.push_back(std::move(label));
    }
    noisy.pages.push_back(axis);
    auto zero = page(21, "0");
    noisy.pages.push_back(zero);
    const auto denoised = map({target}, noisy);
    require(denoised &&
            denoised.value().entries[0].status == MappingStatus::Resolved &&
            denoised.value().entries[0].pdf_page_index == 23,
            "figure-axis numbers and a printed 0 do not contradict the offset");

    std::cout << "S4 mapping fixtures: offset, overrides, Roman, appendix, "
                 "ranges, ambiguity and requests passed\n";
}
