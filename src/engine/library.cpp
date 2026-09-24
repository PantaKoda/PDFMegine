// Library-level services of the public API: version and OCR model lookup.

#include <pdfbookmark/pdfbookmark.hpp>

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pdfbookmark {

const char* version() noexcept { return PDFBOOKMARK_VERSION_STRING; }

std::optional<ModelResources> models_in(const std::filesystem::path& dir) {
    ModelResources models{dir / "det" / "inference.onnx",
                          dir / "rec" / "inference.onnx",
                          dir / "rec" / "charset.txt"};
    std::error_code ec;
    if (std::filesystem::is_regular_file(models.detector, ec) &&
        std::filesystem::is_regular_file(models.recognizer, ec) &&
        std::filesystem::is_regular_file(models.charset, ec))
        return models;
    return std::nullopt;
}

namespace {

// Directory of the module (DLL or executable) containing this code.
std::optional<std::filesystem::path> module_directory() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&module_directory), &module))
        return std::nullopt;
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(module, buffer.data(),
                                       static_cast<DWORD>(buffer.size()));
    if (n == 0 || n >= buffer.size()) return std::nullopt;
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::nullopt;
#endif
}

}  // namespace

std::optional<ModelResources> find_models() {
#ifdef _WIN32
    wchar_t* env = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&env, &length, L"PDFBOOKMARK_MODELS") == 0 && env) {
        const std::filesystem::path dir(env);
        std::free(env);
        if (auto models = models_in(dir)) return models;
    }
#else
    if (const char* env = std::getenv("PDFBOOKMARK_MODELS"))
        if (auto models = models_in(env)) return models;
#endif
    if (const auto dir = module_directory()) {
        if (auto models = models_in(*dir / "models")) return models;
        if (auto models = models_in(*dir / ".." / "share" / "pdfbookmark" / "models"))
            return models;
    }
    return std::nullopt;
}

}  // namespace pdfbookmark
