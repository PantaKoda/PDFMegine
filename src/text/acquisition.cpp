#include <pdfbookmark/text/acquisition.hpp>

#include "ocr_adapter.hpp"
#include "pdf_session.hpp"
#include "sha256.hpp"

#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <variant>

namespace pdfbookmark::text {
namespace {

constexpr const char* kPolicyId = "s1-acquisition-v2";
std::mutex global_ocr_mutex;

std::string configuration_id(const AcquisitionOptions& options) {
    return "mode=" + std::to_string(static_cast<int>(options.mode)) +
           ";dpi=" + std::to_string(options.raster.dpi) +
           ";max_pixels=" + std::to_string(options.raster.max_pixels) +
           ";max_bytes=" + std::to_string(options.raster.max_bytes) +
           ";max_dimension=" + std::to_string(options.raster.max_dimension) +
           ";ocr_budget=" + std::to_string(options.max_ocr_attempts);
}

bool is_unicode_space(std::uint32_t cp) {
    return cp == 0x09 || cp == 0x20 || cp == 0xa0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
           cp == 0x2029 || cp == 0x202f || cp == 0x205f ||
           cp == 0x3000;
}

struct Utf8Counts {
    std::size_t scalars = 0;
    std::size_t visible = 0;
    std::size_t replacements = 0;
};

Utf8Counts count_utf8(const std::string& value) {
    Utf8Counts counts;
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[i]);
        std::uint32_t cp = 0xfffd;
        std::size_t length = 1;
        if (first < 0x80) {
            cp = first;
        } else {
            std::size_t expected = 0;
            if (first >= 0xc2 && first <= 0xdf) {
                cp = first & 0x1f; expected = 2;
            } else if (first >= 0xe0 && first <= 0xef) {
                cp = first & 0x0f; expected = 3;
            } else if (first >= 0xf0 && first <= 0xf4) {
                cp = first & 0x07; expected = 4;
            }
            if (expected != 0 && i + expected <= value.size()) {
                bool valid = true;
                for (std::size_t j = 1; j < expected; ++j) {
                    const auto next = static_cast<std::uint8_t>(value[i + j]);
                    if ((next & 0xc0) != 0x80) { valid = false; break; }
                    cp = (cp << 6) | (next & 0x3f);
                }
                if (valid && cp <= 0x10ffff &&
                    !(cp >= 0xd800 && cp <= 0xdfff) &&
                    !(expected == 3 && cp < 0x800) &&
                    !(expected == 4 && cp < 0x10000))
                    length = expected;
                else
                    cp = 0xfffd;
            } else {
                cp = 0xfffd;
            }
        }
        if (cp == 0xfffd) ++counts.replacements;
        ++counts.scalars;
        if (!is_unicode_space(cp)) ++counts.visible;
        i += length;
    }
    return counts;
}

void append_scalar(std::string& out, std::uint32_t cp, std::size_t& replaced) {
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        cp = 0xfffd;
        ++replaced;
    }
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::string decode_utf16le(const std::vector<std::uint8_t>& bytes,
                           std::size_t& replaced) {
    std::string text;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const std::uint32_t first = static_cast<std::uint32_t>(bytes[i]) |
                                    (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        if (first == 0) break;
        std::uint32_t cp = first;
        if (first >= 0xd800 && first <= 0xdbff) {
            if (i + 3 < bytes.size()) {
                const std::uint32_t second = static_cast<std::uint32_t>(bytes[i + 2]) |
                                             (static_cast<std::uint32_t>(bytes[i + 3]) << 8);
                if (second >= 0xdc00 && second <= 0xdfff) {
                    cp = 0x10000 + ((first - 0xd800) << 10) + second - 0xdc00;
                    i += 2;
                }
            }
        }
        append_scalar(text, cp, replaced);
    }
    if (bytes.size() % 2 != 0) {
        append_scalar(text, 0xfffd, replaced);
        ++replaced;
    }
    return text;
}

struct PageGuard {
    explicit PageGuard(FPDF_PAGE value) : value(value) {}
    ~PageGuard() { if (value) FPDF_ClosePage(value); }
    FPDF_PAGE value;
};
struct TextGuard {
    explicit TextGuard(FPDF_TEXTPAGE value) : value(value) {}
    ~TextGuard() { if (value) FPDFText_ClosePage(value); }
    FPDF_TEXTPAGE value;
};
struct BitmapGuard {
    explicit BitmapGuard(FPDF_BITMAP value) : value(value) {}
    ~BitmapGuard() { if (value) FPDFBitmap_Destroy(value); }
    FPDF_BITMAP value;
};

PageGeometry page_geometry(FPDF_PAGE page,
                           std::optional<double> parsed_user_unit) {
    PageGeometry geometry;
    geometry.width_points = FPDF_GetPageWidthF(page);
    geometry.height_points = FPDF_GetPageHeightF(page);
    geometry.rotation_quarters = FPDFPage_GetRotation(page);
    geometry.user_unit = parsed_user_unit;
    if (!std::isfinite(geometry.width_points) || !std::isfinite(geometry.height_points) ||
        geometry.width_points <= 0 || geometry.height_points <= 0)
        throw std::runtime_error("Invalid PDF page dimensions");

    // The pinned PDFium reports nominal dimensions even when /UserUnit differs
    // from one. Account for the parsed physical scale before fitting.
    float left = 0, bottom = 0, right = 0, top = 0;
    const bool has_box =
        FPDFPage_GetCropBox(page, &left, &bottom, &right, &top) ||
        FPDFPage_GetMediaBox(page, &left, &bottom, &right, &top);
    if (parsed_user_unit && has_box) {
        const double box_width = std::abs(static_cast<double>(right) - left);
        const double box_height = std::abs(static_cast<double>(top) - bottom);
        const double nominal = (geometry.rotation_quarters % 2 == 0) ?
                                   box_width : box_height;
        const double reported_ratio = nominal > 0 ?
            geometry.width_points / nominal : 1.0;
        if (std::abs(reported_ratio - *parsed_user_unit) > 0.01) {
            geometry.width_points *= *parsed_user_unit;
            geometry.height_points *= *parsed_user_unit;
        }
    }
    // A large virtual device yields a sub-0.001-point affine fit.
    constexpr int kVirtualSize = 1'000'000;
    const auto map = [page, &geometry](double x, double y) {
        int dx = 0, dy = 0;
        if (!FPDF_PageToDevice(page, 0, 0, kVirtualSize, kVirtualSize, 0,
                               x, y, &dx, &dy))
            throw std::runtime_error("PDFium page transform failed");
        return Point{geometry.width_points * dx / kVirtualSize,
                     geometry.height_points * dy / kVirtualSize};
    };
    const Point origin = map(0, 0), unit_x = map(1, 0), unit_y = map(0, 1);
    geometry.pdf_to_canonical = {
        unit_x.x - origin.x, unit_x.y - origin.y,
        unit_y.x - origin.x, unit_y.y - origin.y,
        origin.x, origin.y};
    const auto& transform = geometry.pdf_to_canonical;
    const double det = transform.a * transform.d -
                       transform.b * transform.c;
    if (!std::isfinite(det) || std::abs(det) < 1e-12)
        throw std::runtime_error("Degenerate PDF page transform");
    geometry.canonical_to_pdf = {
        transform.d / det, -transform.b / det,
        -transform.c / det, transform.a / det,
        (transform.c * transform.f - transform.d * transform.e) / det,
        (transform.b * transform.e - transform.a * transform.f) / det};
    return geometry;
}

Quad quad_from_pdf_rect(const PageGeometry& geometry,
                        double left, double right, double bottom, double top) {
    const auto& transform = geometry.pdf_to_canonical;
    std::array<Point, 4> corners = {
        transform.map({left, top}), transform.map({right, top}),
        transform.map({right, bottom}), transform.map({left, bottom})};
    double min_x = corners[0].x, max_x = corners[0].x;
    double min_y = corners[0].y, max_y = corners[0].y;
    for (const Point& p : corners) {
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
    }
    Quad quad;
    quad.points = {Point{min_x, min_y}, Point{max_x, min_y},
                   Point{max_x, max_y}, Point{min_x, max_y}};
    return quad;
}

struct NativeCandidate {
    PageContent content;
    TextAssessment assessment;
    bool usable = false;
};

NativeCandidate extract_native(FPDF_PAGE page, PageIndex index,
                               std::uint64_t revision,
                               std::optional<double> user_unit) {
    NativeCandidate candidate;
    candidate.content.page_index = index;
    candidate.content.revision = revision;
    candidate.content.source = Source::EmbeddedPdf;
    candidate.content.geometry = page_geometry(page, user_unit);
    candidate.assessment.policy_id = kPolicyId;
    if (!user_unit)
        candidate.assessment.reasons.push_back(
            "Physical UserUnit is unknown for this PDF structure");

    TextGuard text_page(FPDFText_LoadPage(page));
    if (!text_page.value) throw std::runtime_error("Cannot load PDF text page");
    const int count = FPDFText_CountChars(text_page.value);
    if (count < 0) throw std::runtime_error("PDFium returned invalid character count");
    TextRegion current;
    current.granularity = Granularity::PdfTextRun;
    int run_begin = 0;
    std::size_t line_end_hyphens = 0;
    bool has_box = false;
    double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    const auto flush = [&](int end, TextRegion& region, bool& boxed,
                           double& left, double& top, double& right, double& bottom) {
        if (region.text.empty()) return;
        region.id = static_cast<std::uint32_t>(candidate.content.regions.size());
        region.pdf_char_begin = run_begin;
        region.pdf_char_end = end;
        if (boxed) {
            Quad quad;
            quad.points = {Point{left, top}, Point{right, top},
                           Point{right, bottom}, Point{left, bottom}};
            region.quad = quad;
        }
        candidate.content.regions.push_back(std::move(region));
        region = TextRegion{};
        region.granularity = Granularity::PdfTextRun;
        boxed = false;
        left = top = right = bottom = 0;
    };
    for (int i = 0; i < count; ++i) {
        const int char_index = i;
        auto cp = static_cast<std::uint32_t>(FPDFText_GetUnicode(text_page.value, i));
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < count) {
            const auto low = static_cast<std::uint32_t>(
                FPDFText_GetUnicode(text_page.value, i + 1));
            if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                ++i;
            }
        }
        if (cp == '\r' || cp == '\n') {
            flush(i, current, has_box, min_x, min_y, max_x, max_y);
            run_begin = i + 1;
            continue;
        }
        // PDFium reports a hyphen at a line break (e.g. "differ-" / "ent")
        // as a control code and flags it via FPDFText_IsHyphen. Represent it
        // as U+00AD SOFT HYPHEN, a hyphen shown only where a word breaks
        // (S1-12), instead of counting it as an unexpected control character.
        if (cp < 0x20 && cp != '\t' &&
            FPDFText_IsHyphen(text_page.value, char_index) == 1) {
            cp = 0x00AD;
            ++line_end_hyphens;
        }
        append_scalar(current.text, cp, candidate.assessment.replacement_count);
        ++candidate.assessment.unicode_scalars;
        if (!is_unicode_space(cp)) ++candidate.assessment.visible_scalars;
        if (cp < 0x20 && cp != '\t') ++candidate.assessment.control_count;
        double left = 0, right = 0, bottom = 0, top = 0;
        if (FPDFText_GetCharBox(text_page.value, char_index,
                                &left, &right, &bottom, &top) &&
            std::isfinite(left) && std::isfinite(right) &&
            std::isfinite(bottom) && std::isfinite(top) &&
            right > left && top > bottom) {
            const auto quad = quad_from_pdf_rect(candidate.content.geometry,
                                                 left, right, bottom, top);
            const Point a = quad.points[0], b = quad.points[2];
            if (!has_box) {
                min_x = a.x; min_y = a.y; max_x = b.x; max_y = b.y;
                has_box = true;
            } else {
                min_x = std::min(min_x, a.x); min_y = std::min(min_y, a.y);
                max_x = std::max(max_x, b.x); max_y = std::max(max_y, b.y);
            }
        }
    }
    flush(count, current, has_box, min_x, min_y, max_x, max_y);

    double largest_image_fraction = 0;
    const int objects = FPDFPage_CountObjects(page);
    const double page_area = candidate.content.geometry.width_points *
                             candidate.content.geometry.height_points;
    for (int i = 0; i < objects; ++i) {
        const auto object = FPDFPage_GetObject(page, i);
        if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE) continue;
        ++candidate.assessment.image_object_count;
        float left = 0, bottom = 0, right = 0, top = 0;
        if (FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top) &&
            page_area > 0) {
            const auto quad = quad_from_pdf_rect(candidate.content.geometry,
                                                 left, right, bottom, top);
            const double area = (quad.points[2].x - quad.points[0].x) *
                                (quad.points[2].y - quad.points[0].y);
            largest_image_fraction = std::max(largest_image_fraction,
                                              std::clamp(area / page_area, 0.0, 1.0));
        }
    }

    candidate.usable = candidate.assessment.visible_scalars != 0 &&
        candidate.assessment.replacement_count * 2 < candidate.assessment.unicode_scalars &&
        candidate.assessment.control_count * 2 < candidate.assessment.unicode_scalars;
    candidate.assessment.readability = candidate.assessment.visible_scalars == 0 ?
        Readability::Unknown :
        (candidate.usable && candidate.assessment.replacement_count == 0 &&
         candidate.assessment.control_count == 0 ? Readability::Acceptable :
                                                     Readability::Suspect);
    if (candidate.assessment.replacement_count)
        candidate.assessment.reasons.push_back("Unmapped or invalid Unicode characters");
    if (candidate.assessment.control_count)
        candidate.assessment.reasons.push_back("Unexpected control characters");
    if (line_end_hyphens)
        candidate.assessment.reasons.push_back(
            std::to_string(line_end_hyphens) +
            " line-break hyphen(s) reported by PDFium kept as U+00AD");
    double text_top = candidate.content.geometry.height_points, text_bottom = 0;
    bool any_positioned = false;
    for (const auto& region : candidate.content.regions) {
        if (!region.quad) continue;
        any_positioned = true;
        text_top = std::min(text_top, region.quad->points[0].y);
        text_bottom = std::max(text_bottom, region.quad->points[2].y);
    }
    // A near-page image plus text confined to a thin strip is specific omission
    // evidence. Sparse text alone is not.
    if (largest_image_fraction >= 0.5 && any_positioned &&
        (text_bottom - text_top) <
            candidate.content.geometry.height_points * 0.25) {
        candidate.assessment.coverage = Coverage::SuspectedIncomplete;
        candidate.assessment.reasons.push_back(
            "Large image with native text confined to a narrow page band");
    } else if (candidate.usable) {
        candidate.assessment.coverage = Coverage::NoOmissionIndicated;
    }
    return candidate;
}

detail::Raster render_bgr(FPDF_PAGE page, PageGeometry& geometry,
                          const RasterLimits& limits) {
    if (limits.dpi <= 0 || limits.max_dimension <= 0 ||
        limits.max_pixels == 0 || limits.max_bytes == 0)
        throw std::invalid_argument("Invalid raster limits");
    const double width = std::ceil(geometry.width_points * limits.dpi / 72.0);
    const double height = std::ceil(geometry.height_points * limits.dpi / 72.0);
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width < 1 || height < 1 ||
        width > limits.max_dimension || height > limits.max_dimension)
        throw std::length_error("Raster dimensions exceed limit");
    detail::Raster raster;
    raster.width = static_cast<int>(width);
    raster.height = static_cast<int>(height);
    const auto pixels = static_cast<std::uint64_t>(raster.width) * raster.height;
    if (pixels > limits.max_pixels || pixels > limits.max_bytes / 3 ||
        raster.width > std::numeric_limits<int>::max() / 3)
        throw std::length_error("Raster pixel or byte limit exceeded");
    raster.stride = raster.width * 3;
    raster.bgr.resize(static_cast<std::size_t>(pixels * 3));
    BitmapGuard bitmap(FPDFBitmap_CreateEx(raster.width, raster.height,
                                          FPDFBitmap_BGR, raster.bgr.data(),
                                          raster.stride));
    if (!bitmap.value) throw std::runtime_error("PDFium cannot create BGR bitmap");
    FPDFBitmap_FillRect(bitmap.value, 0, 0, raster.width, raster.height, 0xffffffff);
    FPDF_RenderPageBitmap(bitmap.value, page, 0, 0,
                          raster.width, raster.height, 0, 0);
    geometry.raster_width = raster.width;
    geometry.raster_height = raster.height;
    geometry.effective_dpi_x = raster.width * 72.0 / geometry.width_points;
    geometry.effective_dpi_y = raster.height * 72.0 / geometry.height_points;
    geometry.canonical_to_raster = Affine{
        raster.width / geometry.width_points, 0, 0,
        raster.height / geometry.height_points, 0, 0};
    geometry.raster_to_canonical = Affine{
        geometry.width_points / raster.width, 0, 0,
        geometry.height_points / raster.height, 0, 0};
    return raster;
}

void add_ocr_lines(PageContent& content,
                   const std::vector<detail::OcrLine>& lines) {
    content.regions.reserve(lines.size());
    for (const auto& line : lines) {
        TextRegion region;
        region.id = static_cast<std::uint32_t>(content.regions.size());
        region.text = line.text;
        region.granularity = Granularity::OcrLine;
        region.ocr_confidence = line.confidence;
        Quad quad;
        for (std::size_t i = 0; i < 4; ++i)
            quad.points[i] = content.geometry.raster_to_canonical->map(
                line.pixel_quad.points[i]);
        region.quad = quad;
        content.regions.push_back(std::move(region));
    }
}

Result<std::monostate> validate_pages(const std::vector<PageIndex>& pages,
                                      PageCount count) {
    std::set<PageIndex> seen;
    for (const auto index : pages) {
        if (index < 0 || index >= count)
            return Error{ErrorCode::InvalidArgument, "Page index out of range"};
        if (!seen.insert(index).second)
            return Error{ErrorCode::InvalidArgument, "Duplicate page index"};
    }
    return std::monostate{};
}

}  // namespace

std::string PageContent::flat_text() const {
    std::string flat;
    for (const auto& region : regions) {
        if (!flat.empty()) flat.push_back('\n');
        flat += region.text;
    }
    return flat;
}

struct TextDocument::Impl {
    explicit Impl(const std::filesystem::path& path, const OpenOptions& opts)
        : session(path, opts.max_pdf_bytes), resources(opts.ocr_models) {}
    detail::PdfSession session;
    std::optional<ModelResources> resources;
    std::unique_ptr<detail::OcrBackend> ocr;
    std::string model_identity;
    std::uint64_t next_revision = 1;
    std::mutex acquire_mutex;
};

TextDocument::TextDocument(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
TextDocument::~TextDocument() = default;
TextDocument::TextDocument(TextDocument&&) noexcept = default;
TextDocument& TextDocument::operator=(TextDocument&&) noexcept = default;
PageCount TextDocument::page_count() const noexcept { return impl_->session.page_count(); }
const InputIdentity& TextDocument::identity() const noexcept {
    return impl_->session.identity();
}

Result<TextDocument> TextAcquisition::open(const std::filesystem::path& path,
                                           const OpenOptions& options) const {
    if (options.max_pdf_bytes == 0)
        return Error{ErrorCode::InvalidArgument, "max_pdf_bytes must be positive"};
    try {
        return TextDocument(std::make_unique<TextDocument::Impl>(path, options));
    } catch (const std::length_error& e) {
        return Error{ErrorCode::ResourceLimit, e.what()};
    } catch (const std::exception& e) {
        return Error{ErrorCode::InputOpen, e.what()};
    }
}

Result<AcquisitionBatch> TextDocument::acquire(
    const std::vector<PageIndex>& pages, const AcquisitionOptions& options,
    const RunControl& control) {
    if (const auto validation = validate_pages(pages, page_count()); !validation)
        return validation.error();
    if (options.raster.dpi <= 0 || options.raster.max_dimension <= 0 ||
        options.raster.max_pixels == 0 || options.raster.max_bytes == 0)
        return Error{ErrorCode::InvalidArgument, "Invalid raster limits"};
    if (pages.empty()) {
        AcquisitionBatch empty;
        empty.policy_id = kPolicyId;
        empty.configuration_id = configuration_id(options);
        empty.model_identity = "not-used";
        return empty;
    }
    if (options.mode == AcquisitionMode::OcrOnly && !impl_->resources)
        return Error{ErrorCode::OcrConfiguration, "OcrOnly needs model resources"};
    if (options.mode == AcquisitionMode::OcrOnly) {
        if (!detail::ocr_backend_available())
            return Error{ErrorCode::OcrConfiguration, "S1 was built without OCR"};
        const auto& models = *impl_->resources;
        std::error_code file_error;
        const bool det_exists =
            std::filesystem::is_regular_file(models.detector, file_error);
        const bool rec_exists = !file_error &&
            std::filesystem::is_regular_file(models.recognizer, file_error);
        const bool charset_exists = !file_error &&
            std::filesystem::is_regular_file(models.charset, file_error);
        if (file_error || !det_exists || !rec_exists || !charset_exists)
            return Error{ErrorCode::OcrConfiguration,
                         "One or more OCR model resources are missing"};
    }

    std::lock_guard<std::mutex> document_lock(impl_->acquire_mutex);
    AcquisitionBatch batch;
    batch.policy_id = kPolicyId;
    batch.configuration_id = configuration_id(options);
    batch.model_identity = "not-used";
    batch.pages.reserve(pages.size());
    std::size_t ocr_attempts = 0;
    for (const PageIndex index : pages) {
        PageAcquisition result;
        result.page_index = index;
        result.assessment.policy_id = kPolicyId;
        if (control.is_cancelled()) {
            result.outcome = Outcome::Cancelled;
            result.reasons.push_back("Cancelled before this page");
            batch.complete = false;
            batch.pages.push_back(std::move(result));
            continue;
        }
        const auto revision = impl_->next_revision++;
        std::optional<NativeCandidate> native;
        std::optional<PageGeometry> geometry;
        detail::Raster raster;
        bool need_ocr = options.mode == AcquisitionMode::OcrOnly;
        try {
            {
                std::lock_guard<std::mutex> pdf_lock(detail::pdfium_mutex());
                PageGuard page(FPDF_LoadPage(impl_->session.handle(), index));
                if (!page.value) throw std::runtime_error("PDFium cannot load page");
                geometry = page_geometry(page.value,
                                         impl_->session.user_unit(index));
                if (options.mode != AcquisitionMode::OcrOnly) {
                    try {
                        native = extract_native(page.value, index, revision,
                                                impl_->session.user_unit(index));
                        result.assessment = native->assessment;
                        result.attempts.push_back({
                            Source::EmbeddedPdf, AttemptState::Completed,
                            native->usable ? "Native text available" : "No usable native text"});
                    } catch (const std::exception& e) {
                        result.attempts.push_back({
                            Source::EmbeddedPdf, AttemptState::Failed, e.what()});
                    }
                    need_ocr = options.mode == AcquisitionMode::Auto &&
                        (!native || !native->usable ||
                         native->assessment.coverage == Coverage::SuspectedIncomplete);
                }
                if (need_ocr && impl_->resources && ocr_attempts < options.max_ocr_attempts)
                    raster = render_bgr(page.value, *geometry, options.raster);
            }
        } catch (const std::length_error& e) {
            result.attempts.push_back({Source::Ocr, AttemptState::Failed, e.what()});
            result.reasons.push_back("Raster resource limit");
        } catch (const std::exception& e) {
            result.reasons.push_back(e.what());
        }
        std::optional<PageContent> recognized;
        if (need_ocr) {
            if (!impl_->resources) {
                result.attempts.push_back({
                    Source::Ocr, AttemptState::Skipped, "OCR model resources unavailable"});
            } else if (ocr_attempts >= options.max_ocr_attempts) {
                result.attempts.push_back({
                    Source::Ocr, AttemptState::Skipped, "Per-call OCR budget exhausted"});
            } else if (!raster.bgr.empty() && geometry) {
                ++ocr_attempts;
                try {
                    std::lock_guard<std::mutex> ocr_lock(global_ocr_mutex);
                    if (impl_->model_identity.empty()) {
                        const auto& models = *impl_->resources;
                        impl_->model_identity =
                            std::string(detail::ocr_backend_identity()) +
                            ";det=" + detail::sha256_hex(
                                detail::sha256_file(models.detector)) +
                            ";rec=" + detail::sha256_hex(
                                detail::sha256_file(models.recognizer)) +
                            ";charset=" + detail::sha256_hex(
                                detail::sha256_file(models.charset));
                    }
                    if (!impl_->ocr) impl_->ocr = detail::make_ocr_backend(*impl_->resources);
                    const auto lines = impl_->ocr->run(raster);
                    recognized.emplace();
                    recognized->page_index = index;
                    recognized->revision = revision;
                    recognized->source = Source::Ocr;
                    recognized->geometry = *geometry;
                    add_ocr_lines(*recognized, lines);
                    result.attempts.push_back({
                        Source::Ocr, AttemptState::Completed,
                        lines.empty() ? "OCR found no text" : "OCR completed"});
                } catch (const std::exception& e) {
                    result.attempts.push_back({
                        Source::Ocr, AttemptState::Failed, e.what()});
                }
            }
        }
        const bool native_usable = native && native->usable;
        const bool ocr_usable = recognized && !recognized->regions.empty();
        Utf8Counts ocr_counts;
        if (recognized) {
            for (const auto& region : recognized->regions) {
                const auto counts = count_utf8(region.text);
                ocr_counts.scalars += counts.scalars;
                ocr_counts.visible += counts.visible;
                ocr_counts.replacements += counts.replacements;
            }
        }
        if (options.mode != AcquisitionMode::OcrOnly && native_usable) {
            bool prefer_ocr = false;
            if (ocr_usable &&
                native->assessment.coverage == Coverage::SuspectedIncomplete) {
                prefer_ocr = ocr_counts.visible >
                    native->assessment.visible_scalars * 2;
            }
            result.selected = prefer_ocr ? std::move(*recognized) :
                                           std::move(native->content);
            if (prefer_ocr) {
                result.assessment.unicode_scalars = ocr_counts.scalars;
                result.assessment.visible_scalars = ocr_counts.visible;
                result.assessment.replacement_count = ocr_counts.replacements;
                result.assessment.readability = ocr_counts.replacements == 0 ?
                    Readability::Acceptable : Readability::Suspect;
                result.assessment.coverage = Coverage::Unknown;
                result.assessment.reasons.push_back(
                    "OCR selected after native omission evidence");
            }
            result.outcome =
                native->assessment.readability == Readability::Suspect ||
                native->assessment.coverage == Coverage::SuspectedIncomplete ||
                (need_ocr && !ocr_usable) ?
                Outcome::Degraded : Outcome::Ok;
            if (prefer_ocr) result.reasons.push_back(
                "OCR covers substantially more text than sparse native layer");
            if (result.outcome == Outcome::Degraded)
                result.reasons.push_back("Unresolved native coverage or OCR verification");
        } else if (ocr_usable) {
            result.selected = std::move(*recognized);
            result.assessment.unicode_scalars = ocr_counts.scalars;
            result.assessment.visible_scalars = ocr_counts.visible;
            result.assessment.replacement_count = ocr_counts.replacements;
            result.assessment.readability = ocr_counts.replacements == 0 ?
                Readability::Acceptable : Readability::Suspect;
            result.assessment.coverage = Coverage::Unknown;
            result.outcome = Outcome::Ok;  // Unknown UserUnit is informational.
        } else if ((native && native->assessment.visible_scalars == 0) ||
                   (recognized && recognized->regions.empty())) {
            result.outcome = need_ocr && !recognized ? Outcome::Failed :
                                                     Outcome::NoTextFound;
        } else {
            result.outcome = Outcome::Failed;
        }
        batch.pages.push_back(std::move(result));
    }
    if (ocr_attempts != 0 && !impl_->model_identity.empty())
        batch.model_identity = impl_->model_identity;
    return batch;
}

Result<PdfFactsResult> TextDocument::read_facts(const PdfFactsRequest& request) {
    if (const auto validation = validate_pages(request.pages, page_count()); !validation)
        return validation.error();
    PdfFactsResult result;
    result.input = identity();
    result.pages.reserve(request.pages.size());
    std::lock_guard<std::mutex> document_lock(impl_->acquire_mutex);
    std::lock_guard<std::mutex> pdf_lock(detail::pdfium_mutex());
    for (const PageIndex index : request.pages) {
        PageGuard page(FPDF_LoadPage(impl_->session.handle(), index));
        if (!page.value)
            return Error{ErrorCode::PdfBackend, "PDFium cannot load requested facts page"};
        PdfPageFacts facts;
        facts.page_index = index;
        try {
            facts.geometry = page_geometry(page.value,
                                           impl_->session.user_unit(index));
        } catch (const std::exception& e) {
            return Error{ErrorCode::PdfBackend, e.what()};
        }
        if (request.viewer_labels) {
            const auto bytes_needed = FPDF_GetPageLabel(impl_->session.handle(),
                                                        index, nullptr, 0);
            if (bytes_needed == 0) {
                facts.viewer_label.availability = FactAvailability::Absent;
            } else if (bytes_needed % 2 != 0 || bytes_needed > 65536) {
                facts.viewer_label.availability = FactAvailability::Failed;
                facts.viewer_label.reason = "Invalid UTF-16LE page label length";
            } else {
                std::vector<std::uint8_t> bytes(bytes_needed);
                if (FPDF_GetPageLabel(impl_->session.handle(), index,
                                      bytes.data(), bytes_needed) != bytes_needed) {
                    facts.viewer_label.availability = FactAvailability::Failed;
                    facts.viewer_label.reason = "Page label changed during read";
                } else {
                    std::size_t replacements = 0;
                    facts.viewer_label.value = decode_utf16le(bytes, replacements);
                    facts.viewer_label.availability = FactAvailability::Present;
                    if (replacements) facts.viewer_label.reason =
                        "Invalid UTF-16 replaced in page label";
                }
            }
        }
        if (request.local_links) {
            std::vector<PageIndex> destinations;
            int cursor = 0;
            FPDF_LINK link = nullptr;
            bool ignored_nonlocal = false;
            while (FPDFLink_Enumerate(page.value, &cursor, &link)) {
                FPDF_DEST destination = FPDFLink_GetDest(impl_->session.handle(), link);
                if (!destination) {
                    const FPDF_ACTION action = FPDFLink_GetAction(link);
                    if (action && FPDFAction_GetType(action) == PDFACTION_GOTO)
                        destination = FPDFAction_GetDest(impl_->session.handle(), action);
                    else
                        ignored_nonlocal = true;
                }
                if (!destination) continue;
                const int target =
                    FPDFDest_GetDestPageIndex(impl_->session.handle(), destination);
                if (target >= 0 && target < page_count())
                    destinations.push_back(target);
            }
            facts.local_link_destinations.availability = destinations.empty() ?
                FactAvailability::Absent : FactAvailability::Present;
            if (!destinations.empty())
                facts.local_link_destinations.value = std::move(destinations);
            if (ignored_nonlocal)
                facts.local_link_destinations.reason =
                    "Non-local link actions omitted";
        }
        result.pages.push_back(std::move(facts));
    }
    return result;
}

}  // namespace pdfbookmark::text
