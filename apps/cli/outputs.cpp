#include "outputs.hpp"

#include <cwctype>
#include <system_error>

namespace fs = std::filesystem;

namespace pdfbookmark::cli {
namespace {

std::wstring normalised(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    // Resolves the existing part of the path (symlinks, "..", short names).
    fs::path canonical = fs::weakly_canonical(absolute, ec);
    std::wstring text = (ec ? absolute.lexically_normal() : canonical).wstring();
#ifdef _WIN32
    // NTFS names are case-insensitive by default.
    for (auto& c : text) c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
#endif
    return text;
}

}  // namespace

bool same_destination(const fs::path& a, const fs::path& b) {
    std::error_code ea, eb, eq;
    if (fs::exists(a, ea) && fs::exists(b, eb) && fs::equivalent(a, b, eq) && !eq) return true;
    return normalised(a) == normalised(b);
}

std::optional<std::string> output_collision(const std::vector<NamedOutput>& outputs) {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        if (!outputs[i].path) continue;
        for (std::size_t j = i + 1; j < outputs.size(); ++j) {
            if (!outputs[j].path) continue;
            if (same_destination(*outputs[i].path, *outputs[j].path))
                return outputs[i].option + " and " + outputs[j].option +
                       " name the same file: " + outputs[j].path->u8string();
        }
    }
    return std::nullopt;
}

int analysis_exit_code(bool analysis_cancelled, bool metadata_cancelled, bool plan_ready) {
    if (analysis_cancelled || metadata_cancelled) return kCancelled;
    return plan_ready ? kComplete : kPartial;
}

}  // namespace pdfbookmark::cli
