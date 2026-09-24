#include "pdf_session.hpp"

#include "sha256.hpp"

#include <fstream>
#include <cmath>
#include <limits>
#include <optional>
#include <regex>
#include <string_view>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <functional>

namespace pdfbookmark::text::detail {
namespace {
std::mutex global_mutex;
unsigned runtime_users = 0;

using ObjectMap = std::unordered_map<std::string, std::string_view>;

template <typename Iterator>
std::string object_key(const std::sub_match<Iterator>& number,
                       const std::sub_match<Iterator>& generation) {
    return number.str() + " " + generation.str();
}

// PDFium's public API has no UserUnit getter in the pinned SDK. This parser is
// deliberately limited to explicit, uncompressed page-tree dictionaries.
// Failure returns unknown, never an assumed scale of one.
std::vector<std::optional<double>> resolve_page_units(
    const std::vector<std::uint8_t>& bytes, PageCount count) {
    const auto unknown = [count] {
        return std::vector<std::optional<double>>(
            static_cast<std::size_t>(count), std::nullopt);
    };
    const char* begin = reinterpret_cast<const char*>(bytes.data());
    const char* end = begin + bytes.size();
    const std::string_view all(begin, bytes.size());
    if (all.find("/ObjStm") != std::string_view::npos) return unknown();
    if (all.find("/UserUnit") == std::string_view::npos)
        return std::vector<std::optional<double>>(
            static_cast<std::size_t>(count), 1.0);
    const std::regex object_pattern(R"((\d+)\s+(\d+)\s+obj\b)");
    ObjectMap objects;
    for (std::cregex_iterator it(begin, end, object_pattern), last;
         it != last; ++it) {
        const auto match = *it;
        const auto body_start = static_cast<std::size_t>(
            match.position() + match.length());
        const auto body_end = all.find("endobj", body_start);
        if (body_end == std::string_view::npos) return unknown();
        objects[object_key(match[1], match[2])] =
            all.substr(body_start, body_end - body_start);
    }
    const std::regex catalog_pattern(R"(/Type\s*/Catalog\b)");
    const std::regex pages_pattern(R"(/Pages\s+(\d+)\s+(\d+)\s+R\b)");
    const std::regex page_pattern(R"(/Type\s*/Page\b)");
    const std::regex kids_pattern(R"(/Kids\s*\[([^\]]*)\])");
    const std::regex ref_pattern(R"((\d+)\s+(\d+)\s+R\b)");
    const std::regex unit_pattern(
        R"(/UserUnit\s+([+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))");
    std::string root;
    for (const auto& entry : objects) {
        const std::string body(entry.second);
        if (!std::regex_search(body, catalog_pattern)) continue;
        std::smatch ref;
        if (std::regex_search(body, ref, pages_pattern))
            root = object_key(ref[1], ref[2]);
    }
    if (root.empty()) return unknown();
    std::vector<std::optional<double>> units;
    std::unordered_map<std::string, bool> visiting;
    std::function<bool(const std::string&)> visit =
        [&](const std::string& key) {
            const auto found = objects.find(key);
            if (found == objects.end() || visiting[key]) return false;
            visiting[key] = true;
            const std::string body(found->second);
            if (std::regex_search(body, page_pattern)) {
                std::smatch unit;
                double value = 1.0;  // PDF default when Page dictionary is known.
                if (std::regex_search(body, unit, unit_pattern)) {
                    try {
                        value = std::stod(unit[1].str());
                    } catch (const std::exception&) {
                        return false;
                    }
                }
                if (!std::isfinite(value) || value <= 0) return false;
                units.emplace_back(value);
                return true;
            }
            std::smatch kids;
            if (!std::regex_search(body, kids, kids_pattern)) return false;
            const std::string refs = kids[1].str();
            bool any = false;
            for (std::sregex_iterator it(refs.begin(), refs.end(), ref_pattern),
                 last; it != last; ++it) {
                any = true;
                if (!visit(object_key((*it)[1], (*it)[2]))) return false;
            }
            return any;
        };
    if (!visit(root) || units.size() != static_cast<std::size_t>(count))
        return unknown();
    return units;
}
}

std::mutex& pdfium_mutex() { return global_mutex; }

PdfSession::PdfSession(const std::filesystem::path& path, std::size_t max_bytes) {
    if (path.empty()) throw std::invalid_argument("PDF path is empty");
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open PDF");
    const auto length = stream.tellg();
    if (length <= 0) throw std::runtime_error("PDF is empty or unreadable");
    if (static_cast<std::uintmax_t>(length) > max_bytes)
        throw std::length_error("PDF exceeds max_pdf_bytes");
    if (static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max())
        throw std::length_error("PDF does not fit in address space");
    bytes_.resize(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes_.data()), length);
    if (!stream) throw std::runtime_error("Cannot read complete PDF snapshot");
    identity_.sha256 = sha256(bytes_.data(), bytes_.size());
    identity_.display_path = path;
    {
        std::lock_guard<std::mutex> lock(global_mutex);
        if (runtime_users == 0) FPDF_InitLibrary();
        ++runtime_users;
        document_ = FPDF_LoadMemDocument64(bytes_.data(), bytes_.size(), nullptr);
        if (!document_) {
            const auto error = FPDF_GetLastError();
            --runtime_users;
            if (runtime_users == 0) FPDF_DestroyLibrary();
            throw std::runtime_error("PDFium cannot load PDF (error " +
                                     std::to_string(error) + ")");
        }
        identity_.page_count = FPDF_GetPageCount(document_);
        if (identity_.page_count < 0) {
            FPDF_CloseDocument(document_);
            document_ = nullptr;
            --runtime_users;
            if (runtime_users == 0) FPDF_DestroyLibrary();
            throw std::runtime_error("PDFium returned a negative page count");
        }
    }
    try {
        page_units_ = resolve_page_units(bytes_, identity_.page_count);
    } catch (...) {
        std::lock_guard<std::mutex> lock(global_mutex);
        FPDF_CloseDocument(document_);
        document_ = nullptr;
        --runtime_users;
        if (runtime_users == 0) FPDF_DestroyLibrary();
        throw;
    }
}

PdfSession::~PdfSession() {
    std::lock_guard<std::mutex> lock(global_mutex);
    if (document_) FPDF_CloseDocument(document_);
    if (runtime_users != 0) {
        --runtime_users;
        if (runtime_users == 0) FPDF_DestroyLibrary();
    }
}

}  // namespace pdfbookmark::text::detail
