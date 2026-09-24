#include "codec.hpp"

#include <cstdio>

namespace pdfbookmark::engine::codec {
namespace {
using json::Value;

const char* name(text::Outcome v) {
    switch (v) {
        case text::Outcome::Ok: return "ok";
        case text::Outcome::Degraded: return "degraded";
        case text::Outcome::NoTextFound: return "no_text_found";
        case text::Outcome::Failed: return "failed";
        case text::Outcome::Cancelled: return "cancelled";
    }
    return "unknown";
}
const char* name(text::Source v) {
    return v == text::Source::Ocr ? "ocr" : "embedded_pdf";
}
const char* name(text::AttemptState v) {
    switch (v) {
        case text::AttemptState::Completed: return "completed";
        case text::AttemptState::Failed: return "failed";
        case text::AttemptState::Skipped: return "skipped";
    }
    return "unknown";
}
const char* name(text::Readability v) {
    switch (v) {
        case text::Readability::Acceptable: return "acceptable";
        case text::Readability::Suspect: return "suspect";
        case text::Readability::Unknown: return "unknown";
    }
    return "unknown";
}
const char* name(text::Coverage v) {
    switch (v) {
        case text::Coverage::NoOmissionIndicated: return "no_omission_indicated";
        case text::Coverage::SuspectedIncomplete: return "suspected_incomplete";
        case text::Coverage::Unknown: return "unknown";
    }
    return "unknown";
}
const char* name(text::ReadingOrder v) {
    return v == text::ReadingOrder::Estimated ? "estimated" : "uncertain";
}
const char* name(text::Granularity v) {
    return v == text::Granularity::OcrLine ? "ocr_line" : "pdf_text_run";
}
}  // namespace

std::string hex(const std::array<std::uint8_t, 32>& digest) {
    std::string out;
    char buffer[3];
    for (const auto byte : digest) {
        std::snprintf(buffer, sizeof buffer, "%02x", byte);
        out += buffer;
    }
    return out;
}

std::optional<std::array<std::uint8_t, 32>> unhex(const std::string& text) {
    if (text.size() != 64) return std::nullopt;
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 64; ++i) {
        const char c = text[i];
        int v = 0;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return std::nullopt;
        out[i / 2] = static_cast<std::uint8_t>(out[i / 2] | (i % 2 ? v : v << 4));
    }
    return out;
}

Value strings(const std::vector<std::string>& values) {
    auto array = Value::make_array();
    for (const auto& value : values) array.push(Value::of(value));
    return array;
}

Value count(std::size_t value) {
    return Value::of(static_cast<std::uint64_t>(value));
}

Value source_ref(const text::SourceReference& source) {
    auto out = Value::make_object();
    out.add("page_index", Value::of(source.page_index));
    out.add("revision", Value::of(static_cast<std::uint64_t>(source.revision)));
    out.add("region_id", Value::of(static_cast<std::uint64_t>(source.region_id)));
    out.add("utf8_begin", source.utf8_begin ? count(*source.utf8_begin) : Value::null());
    out.add("utf8_end", source.utf8_end ? count(*source.utf8_end) : Value::null());
    return out;
}

Value sources(const std::vector<text::SourceReference>& values) {
    auto array = Value::make_array();
    for (const auto& value : values) array.push(source_ref(value));
    return array;
}

Value page(const text::PageAcquisition& page,
           std::optional<std::size_t> configuration) {
    auto out = Value::make_object();
    out.add("page_index", Value::of(page.page_index));
    out.add("outcome", Value::of(name(page.outcome)));
    out.add("configuration", configuration ? count(*configuration) : Value::null());
    out.add("reasons", strings(page.reasons));
    auto& assessment = out.add("assessment", Value::make_object());
    assessment.add("readability", Value::of(name(page.assessment.readability)));
    assessment.add("coverage", Value::of(name(page.assessment.coverage)));
    assessment.add("policy_id", Value::of(page.assessment.policy_id));
    assessment.add("unicode_scalars", count(page.assessment.unicode_scalars));
    assessment.add("visible_scalars", count(page.assessment.visible_scalars));
    assessment.add("replacement_count", count(page.assessment.replacement_count));
    assessment.add("control_count", count(page.assessment.control_count));
    assessment.add("image_object_count", count(page.assessment.image_object_count));
    assessment.add("reasons", strings(page.assessment.reasons));
    auto& attempts = out.add("attempts", Value::make_array());
    for (const auto& attempt : page.attempts) {
        auto& a = attempts.push(Value::make_object());
        a.add("source", Value::of(name(attempt.source)));
        a.add("state", Value::of(name(attempt.state)));
        a.add("reason", Value::of(attempt.reason));
    }
    if (!page.selected) {
        out.add("content", Value::null());
        return out;
    }
    const auto& content = *page.selected;
    auto& c = out.add("content", Value::make_object());
    c.add("revision", Value::of(static_cast<std::uint64_t>(content.revision)));
    c.add("source", Value::of(name(content.source)));
    c.add("reading_order", Value::of(name(content.reading_order)));
    auto& geometry = c.add("geometry", Value::make_object());
    geometry.add("frame", Value::of("canonical_top_left_points"));
    geometry.add("width_points", Value::of(content.geometry.width_points, 3));
    geometry.add("height_points", Value::of(content.geometry.height_points, 3));
    geometry.add("rotation_quarters", Value::of(content.geometry.rotation_quarters));
    geometry.add("user_unit", content.geometry.user_unit
                                  ? Value::of(*content.geometry.user_unit, 6)
                                  : Value::null());
    c.add("text", Value::of(content.flat_text()));
    auto& regions = c.add("regions", Value::make_array());
    for (const auto& region : content.regions) {
        auto& r = regions.push(Value::make_object());
        r.add("id", Value::of(static_cast<std::uint64_t>(region.id)));
        r.add("text", Value::of(region.text));
        r.add("granularity", Value::of(name(region.granularity)));
        if (region.quad) {
            auto& quad = r.add("quad", Value::make_array());
            for (const auto& point : region.quad->points) {
                auto& p = quad.push(Value::make_array());
                p.push(Value::of(point.x, 3));
                p.push(Value::of(point.y, 3));
            }
        } else {
            r.add("quad", Value::null());
        }
        r.add("ocr_confidence",
              region.ocr_confidence
                  ? Value::of(static_cast<double>(*region.ocr_confidence), 6)
                  : Value::null());
    }
    return out;
}

}  // namespace pdfbookmark::engine::codec
