#include <pdfbookmark/writer/writer.hpp>

#include "writer_testing.hpp"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFCryptoProvider.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cstring>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace pdfbookmark::writer {
// Fault injection exists only in test builds (PDFBOOKMARK_TEST_HOOKS).
#ifndef PDFBOOKMARK_TEST_HOOKS
namespace testing {
namespace {
bool fault(FaultPoint, const fs::path&) { return true; }
}  // namespace
}  // namespace testing
#else
namespace testing {
namespace {
std::mutex hook_mutex;
FaultHook& hook_storage() {
    static FaultHook hook;
    return hook;
}
}  // namespace
void set_fault_hook(FaultHook hook) {
    std::lock_guard<std::mutex> lock(hook_mutex);
    hook_storage() = std::move(hook);
}
bool fault(FaultPoint point, const fs::path& temp) {
    FaultHook hook;
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        hook = hook_storage();
    }
    return !hook || hook(point, temp);
}
}  // namespace testing
#endif

namespace {

using Digest = std::array<std::uint8_t, 32>;

Digest sha256(const std::string& bytes) {
    auto impl = QPDFCryptoProvider::getImpl();
    impl->SHA2_init(256);
    impl->SHA2_update(reinterpret_cast<const unsigned char*>(bytes.data()),
                      bytes.size());
    impl->SHA2_finalize();
    const std::string raw = impl->SHA2_digest();
    Digest result{};
    std::memcpy(result.data(), raw.data(), std::min(raw.size(), result.size()));
    return result;
}

Result<std::string> read_bytes(const fs::path& path, std::uint64_t cap,
                               ErrorCode open_error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return Error{open_error, "Not a readable regular file: " +
                                     path.u8string()};
    const auto size = fs::file_size(path, ec);
    if (ec) return Error{open_error, "Cannot size file: " + path.u8string()};
    if (size > cap)
        return Error{ErrorCode::ResourceLimit,
                     "File exceeds the configured byte limit"};
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{open_error, "Cannot open: " + path.u8string()};
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size && !in.read(bytes.data(), static_cast<std::streamsize>(size)))
        return Error{open_error, "Cannot read: " + path.u8string()};
    // Detect growth between sizing and reading.
    if (in.peek() != std::char_traits<char>::eof())
        return Error{ErrorCode::InputChanged,
                     "File changed while being read: " + path.u8string()};
    return std::move(bytes);
}

// QPDF parses directly from `bytes`, which must outlive it.
struct Loaded {
    std::string bytes;
    std::unique_ptr<QPDF> pdf;
};

Result<std::unique_ptr<Loaded>> load(std::string bytes,
                                     const std::string& description) {
    auto loaded = std::make_unique<Loaded>();
    loaded->bytes = std::move(bytes);
    loaded->pdf = std::make_unique<QPDF>();
    loaded->pdf->setSuppressWarnings(true);
    try {
        loaded->pdf->processMemoryFile(description.c_str(),
                                       loaded->bytes.data(),
                                       loaded->bytes.size());
    } catch (const QPDFExc& e) {
        if (e.getErrorCode() == qpdf_e_password)
            return Error{ErrorCode::Unsupported,
                         "Password-protected PDFs are not supported for writing"};
        return Error{ErrorCode::PdfBackend,
                     std::string("qpdf cannot read PDF: ") + e.what()};
    } catch (const std::exception& e) {
        return Error{ErrorCode::PdfBackend,
                     std::string("qpdf cannot read PDF: ") + e.what()};
    }
    return std::move(loaded);
}

// Conservative: DocMDP/UR permissions, the AcroForm "signatures exist" flag,
// or any (possibly widget-less) /Sig field with a value marks a signed PDF.
bool is_signed(QPDF& pdf) {
    auto root = pdf.getRoot();
    if (root.hasKey("/Perms")) return true;
    auto form = root.getKey("/AcroForm");
    if (!form.isDictionary()) return false;
    const auto flags = form.getKey("/SigFlags");
    if (flags.isInteger() && (flags.getIntValue() & 1)) return true;
    std::set<QPDFObjGen> seen;
    std::function<bool(QPDFObjectHandle, std::string, int)> walk =
        [&](QPDFObjectHandle fields, std::string inherited_type,
            int depth) -> bool {
        if (!fields.isArray() || depth > 64) return false;
        for (int i = 0; i < fields.getArrayNItems(); ++i) {
            auto field = fields.getArrayItem(i);
            if (!field.isDictionary()) continue;
            if (field.isIndirect() && !seen.insert(field.getObjGen()).second)
                continue;
            std::string type = inherited_type;
            if (field.getKey("/FT").isName()) type = field.getKey("/FT").getName();
            if (type == "/Sig" && field.getKey("/V").isDictionary()) return true;
            if (walk(field.getKey("/Kids"), type, depth + 1)) return true;
        }
        return false;
    };
    return walk(form.getKey("/Fields"), {}, 0);
}

struct Tree {
    std::vector<std::size_t> roots;
    std::map<std::size_t, std::vector<std::size_t>> children;
};
Tree tree_of(const BookmarkPlan& plan) {
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < plan.nodes.size(); ++i)
        index.emplace(plan.nodes[i].id, i);
    Tree tree;
    for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
        const auto& parent = plan.nodes[i].parent_id;
        if (parent) tree.children[index.at(*parent)].push_back(i);
        else tree.roots.push_back(i);
    }
    return tree;
}
const std::vector<std::size_t>& kids_of(const Tree& tree, std::size_t node) {
    static const std::vector<std::size_t> none;
    const auto found = tree.children.find(node);
    return found == tree.children.end() ? none : found->second;
}

// Items are created closed: an item with children carries the negative count
// of its direct children (what becomes visible when opened); the root Count is
// the number of visible (top-level) items.
void install_outline(QPDF& pdf, const BookmarkPlan& plan) {
    const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    const Tree tree = tree_of(plan);
    std::vector<QPDFObjectHandle> items(plan.nodes.size());
    for (std::size_t i = 0; i < plan.nodes.size(); ++i)
        items[i] = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    QPDFObjectHandle root =
        pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));

    const auto link = [&](QPDFObjectHandle parent,
                          const std::vector<std::size_t>& kids) {
        if (kids.empty()) return;
        parent.replaceKey("/First", items[kids.front()]);
        parent.replaceKey("/Last", items[kids.back()]);
        for (std::size_t k = 0; k < kids.size(); ++k) {
            auto item = items[kids[k]];
            item.replaceKey("/Parent", parent);
            if (k > 0) item.replaceKey("/Prev", items[kids[k - 1]]);
            if (k + 1 < kids.size())
                item.replaceKey("/Next", items[kids[k + 1]]);
        }
    };
    link(root, tree.roots);
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(
        static_cast<long long>(tree.roots.size())));
    for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
        const auto& node = plan.nodes[i];
        auto item = items[i];
        item.replaceKey("/Title",
                        QPDFObjectHandle::newUnicodeString(node.title));
        auto dest = QPDFObjectHandle::newArray();
        dest.appendItem(pages.at(static_cast<std::size_t>(
            node.destination.pdf_page_index)).getObjectHandle());
        dest.appendItem(QPDFObjectHandle::newName("/Fit"));
        item.replaceKey("/Dest", dest);
        const auto& kids = kids_of(tree, i);
        link(item, kids);
        if (!kids.empty())
            item.replaceKey("/Count", QPDFObjectHandle::newInteger(
                -static_cast<long long>(kids.size())));
    }
    // replace_in_copy: exactly the plan tree; old items become unreachable.
    pdf.getRoot().replaceKey("/Outlines", root);
}

// Walks the raw reopened outline objects and compares them with the plan.
bool verify_outline(QPDF& pdf, const BookmarkPlan& plan,
                    std::size_t& item_count, std::string& why) {
    item_count = 0;
    const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    std::map<QPDFObjGen, PageIndex> page_index;
    for (std::size_t i = 0; i < pages.size(); ++i)
        page_index[pages[i].getObjectHandle().getObjGen()] =
            static_cast<PageIndex>(i);
    const Tree tree = tree_of(plan);
    auto root = pdf.getRoot().getKey("/Outlines");
    if (!root.isDictionary()) { why = "Output has no /Outlines"; return false; }
    std::set<QPDFObjGen> seen;
    std::function<bool(QPDFObjectHandle, const std::vector<std::size_t>&)>
        check = [&](QPDFObjectHandle parent,
                    const std::vector<std::size_t>& kids) -> bool {
        auto item = parent.getKey("/First");
        QPDFObjectHandle previous;
        for (std::size_t k = 0; k < kids.size(); ++k) {
            if (!item.isDictionary() || !seen.insert(item.getObjGen()).second) {
                why = "Outline sibling chain is broken or cyclic";
                return false;
            }
            ++item_count;
            const auto& node = plan.nodes[kids[k]];
            const auto title = item.getKey("/Title");
            if (!title.isString() || title.getUTF8Value() != node.title) {
                why = "Title mismatch for node " + node.id;
                return false;
            }
            if (item.getKey("/Parent").getObjGen() != parent.getObjGen() ||
                (k > 0 && item.getKey("/Prev").getObjGen() !=
                              previous.getObjGen()) ||
                (k == 0 && item.hasKey("/Prev"))) {
                why = "Parent/Prev link mismatch for node " + node.id;
                return false;
            }
            const auto dest = item.getKey("/Dest");
            if (!dest.isArray() || dest.getArrayNItems() < 1) {
                why = "Missing destination for node " + node.id;
                return false;
            }
            const auto page = page_index.find(dest.getArrayItem(0).getObjGen());
            if (page == page_index.end() ||
                page->second != node.destination.pdf_page_index) {
                why = "Destination mismatch for node " + node.id;
                return false;
            }
            const auto& grandkids = kids_of(tree, kids[k]);
            const auto count = item.getKey("/Count");
            if (grandkids.empty() ? item.hasKey("/First")
                                  : (!count.isInteger() ||
                                     count.getIntValue() !=
                                         -static_cast<long long>(
                                             grandkids.size()))) {
                why = "Child count mismatch for node " + node.id;
                return false;
            }
            if (!check(item, grandkids)) return false;
            if (k + 1 == kids.size() &&
                (item.hasKey("/Next") ||
                 parent.getKey("/Last").getObjGen() != item.getObjGen())) {
                why = "Last/Next link mismatch for node " + node.id;
                return false;
            }
            previous = item;
            item = item.getKey("/Next");
        }
        if (kids.empty() && parent.hasKey("/First")) {
            why = "Unexpected outline children";
            return false;
        }
        return true;
    };
    if (!check(root, tree.roots)) return false;
    const auto root_count = root.getKey("/Count");
    if (!root_count.isInteger() ||
        root_count.getIntValue() !=
            static_cast<long long>(tree.roots.size())) {
        why = "Outline root count mismatch";
        return false;
    }
    if (item_count != plan.nodes.size()) {
        why = "Outline item count mismatch";
        return false;
    }
    return true;
}

// ---- Output transaction (platform file operations) ----

bool create_exclusive_and_write(const fs::path& path, const std::string& data) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::size_t written_total = 0;
    bool ok = true;
    while (ok && written_total < data.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            data.size() - written_total, 1u << 30));
        DWORD written = 0;
        ok = WriteFile(file, data.data() + written_total, chunk, &written,
                       nullptr) && written == chunk;
        written_total += written;
    }
    ok = ok && FlushFileBuffers(file);
    ok = CloseHandle(file) && ok;
    return ok;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return false;
    std::size_t written_total = 0;
    bool ok = true;
    while (ok && written_total < data.size()) {
        const auto written = ::write(fd, data.data() + written_total,
                                     data.size() - written_total);
        ok = written > 0;
        if (ok) written_total += static_cast<std::size_t>(written);
    }
    ok = ok && ::fsync(fd) == 0;
    ok = ::close(fd) == 0 && ok;
    return ok;
#endif
}

// No-clobber unless `replace`. Never follows a replacement onto the input:
// callers recheck aliasing immediately before.
bool commit_file(const fs::path& temp, const fs::path& output, bool replace) {
#ifdef _WIN32
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (replace) flags |= MOVEFILE_REPLACE_EXISTING;
    return MoveFileExW(temp.c_str(), output.c_str(), flags) != 0;
#else
    if (replace) return ::rename(temp.c_str(), output.c_str()) == 0;
    if (::link(temp.c_str(), output.c_str()) != 0) return false;
    ::unlink(temp.c_str());
    return true;
#endif
}

struct TempGuard {
    fs::path path;
    ~TempGuard() {
        if (path.empty()) return;
        std::error_code ec;
        fs::remove(path, ec);
    }
};

std::optional<fs::path> write_temp_sibling(const fs::path& output,
                                           const std::string& data) {
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
        static const char* hex = "0123456789abcdef";
        std::string suffix = ".pdfbookmark-";
        for (int i = 0; i < 12; ++i) suffix += hex[random() & 0xF];
        fs::path temp = output;
        temp += suffix + ".tmp";
        std::error_code ec;
        if (fs::exists(temp, ec)) continue;
        if (create_exclusive_and_write(temp, data)) return temp;
        if (fs::exists(temp, ec)) {
            // Partial write of our own new file: remove it and give up.
            fs::remove(temp, ec);
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool aliases(const fs::path& input, const fs::path& output) {
    std::error_code ec;
    if (!fs::exists(output, ec)) return false;
    const bool same = fs::equivalent(input, output, ec);
    return same || ec;  // An unverifiable relationship is treated as alias.
}

Error cancelled() {
    return Error{ErrorCode::Cancelled,
                 "Cancelled before commit; no output written"};
}

}  // namespace

Result<InputIdentity> read_input_identity(const fs::path& input,
                                          const WriteOptions& options) {
    auto bytes = read_bytes(input, options.max_input_bytes,
                            ErrorCode::InputOpen);
    if (!bytes) return bytes.error();
    const auto digest = sha256(bytes.value());
    auto loaded = load(bytes.take(), input.u8string());
    if (!loaded) return loaded.error();
    InputIdentity identity;
    identity.sha256 = digest;
    const auto count = QPDFPageDocumentHelper(*loaded.value()->pdf)
                           .getAllPages().size();
    if (count > static_cast<std::size_t>(std::numeric_limits<PageCount>::max()))
        return Error{ErrorCode::ResourceLimit, "Too many pages"};
    identity.page_count = static_cast<PageCount>(count);
    identity.display_path = input;
    return identity;
}

Result<WriteResult> write_copy(const fs::path& input, const fs::path& output,
                               const BookmarkPlan& plan,
                               const WriteOptions& options,
                               const RunControl& control) {
    // 1. Structural plan validation: never skipped, even if done earlier.
    const auto validation = validate(plan);
    if (!validation.valid) {
        std::string message = "Invalid bookmark plan:";
        for (std::size_t i = 0; i < validation.issues.size() && i < 5; ++i)
            message += " [" + validation.issues[i].field + "] " +
                       validation.issues[i].message + ";";
        return Error{ErrorCode::InvalidArgument, message};
    }
    if (output.empty() || input.empty())
        return Error{ErrorCode::InvalidArgument, "Input and output are required"};

    // 2. Protected input and output policy.
    if (aliases(input, output))
        return Error{ErrorCode::InvalidArgument,
                     "Output refers to the input file; the input is never "
                     "modified"};
    std::error_code ec;
    const bool output_existed = fs::exists(output, ec);
    if (output_existed && !fs::is_regular_file(output, ec))
        return Error{ErrorCode::InvalidArgument,
                     "Output exists and is not a regular file"};
    if (output_existed && !options.replace_existing_output)
        return Error{ErrorCode::OutputExists,
                     "Output already exists; explicit replacement required"};
    const auto parent = output.has_parent_path() ? output.parent_path()
                                                 : fs::path(".");
    if (!fs::is_directory(parent, ec))
        return Error{ErrorCode::OutputWrite, "Output directory does not exist"};
    if (control.is_cancelled()) return cancelled();

    // 3. Bind to the exact analyzed bytes.
    auto bytes = read_bytes(input, options.max_input_bytes,
                            ErrorCode::InputOpen);
    if (!bytes) return bytes.error();
    if (sha256(bytes.value()) != plan.input.sha256)
        return Error{ErrorCode::InputChanged,
                     "Input SHA-256 does not match the plan (stale plan)"};
    auto loaded = load(bytes.take(), input.u8string());
    if (!loaded) return loaded.error();
    QPDF& pdf = *loaded.value()->pdf;

    WriteResult result;
    result.output = output;
    std::string data;
    try {
        if (pdf.isEncrypted())
            return Error{ErrorCode::Unsupported,
                         "Encrypted PDFs are not supported for writing"};
        if (is_signed(pdf))
            return Error{ErrorCode::Unsupported,
                         "Signed PDFs are not supported for writing"};
        const auto pages = QPDFPageDocumentHelper(pdf).getAllPages().size();
        if (pages != static_cast<std::size_t>(plan.input.page_count))
            return Error{ErrorCode::InputChanged,
                         "Input page count does not match the plan"};
        result.input_had_outline =
            pdf.getRoot().getKey("/Outlines").isDictionary();
        if (control.is_cancelled()) return cancelled();

        // 4. Serialize the copy with exactly the plan outline.
        install_outline(pdf, plan);
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setDeterministicID(true);
        writer.write();
        const auto buffer = writer.getBufferSharedPointer();
        data.assign(reinterpret_cast<const char*>(buffer->getBuffer()),
                    buffer->getSize());
        if (pdf.anyWarnings())
            result.diagnostics.push_back(
                "qpdf reported warnings while reading or writing the input");
    } catch (const std::exception& e) {
        return Error{ErrorCode::PdfBackend,
                     std::string("qpdf failed to write outline: ") + e.what()};
    }
    if (control.is_cancelled()) return cancelled();

    // 5. Temporary sibling, closed, then reopened and verified.
    const auto temp_path = write_temp_sibling(output, data);
    if (!temp_path)
        return Error{ErrorCode::OutputWrite,
                     "Cannot create temporary output next to the destination"};
    TempGuard temp{*temp_path};
    if (!testing::fault(testing::FaultPoint::AfterTempWritten, temp.path))
        return Error{ErrorCode::OutputWrite, "Temporary output write failed"};
    auto reread = read_bytes(temp.path, std::numeric_limits<std::uint64_t>::max(),
                             ErrorCode::OutputWrite);
    if (!reread) return Error{ErrorCode::OutputWrite, reread.error().message};
    result.output_sha256 = sha256(reread.value());
    if (reread.value() != data)
        return Error{ErrorCode::OutputWrite,
                     "Temporary output differs from the serialized copy"};
    auto reopened = load(reread.take(), temp.path.u8string());
    if (!reopened)
        return Error{ErrorCode::OutputWrite,
                     "Temporary output cannot be reopened: " +
                         reopened.error().message};
    try {
        QPDF& check = *reopened.value()->pdf;
        const auto count = QPDFPageDocumentHelper(check).getAllPages().size();
        result.verification.page_count = static_cast<PageCount>(count);
        std::string why;
        result.verification.structure_matches =
            count == static_cast<std::size_t>(plan.input.page_count) &&
            verify_outline(check, plan, result.verification.outline_items,
                           why);
        if (!result.verification.structure_matches)
            return Error{ErrorCode::OutputWrite,
                         "Output verification failed: " +
                             (why.empty() ? std::string("page count mismatch")
                                          : why)};
    } catch (const std::exception& e) {
        return Error{ErrorCode::OutputWrite,
                     std::string("Output verification failed: ") + e.what()};
    }

    // 6. The input must still be the analyzed input immediately before commit.
    auto again = read_bytes(input, options.max_input_bytes,
                            ErrorCode::InputChanged);
    if (!again || sha256(again.value()) != plan.input.sha256)
        return Error{ErrorCode::InputChanged,
                     "Input changed during writing; output not committed"};
    result.verification.input_unchanged = true;
    if (control.is_cancelled()) return cancelled();
    if (!testing::fault(testing::FaultPoint::BeforeCommit, temp.path))
        return Error{ErrorCode::OutputWrite, "Commit failed"};

    // 7. Commit. Recheck aliasing and the output policy at the last moment.
    if (aliases(input, output))
        return Error{ErrorCode::InvalidArgument,
                     "Output refers to the input file; the input is never "
                     "modified"};
    const bool exists_now = fs::exists(output, ec);
    if (exists_now && !options.replace_existing_output)
        return Error{ErrorCode::OutputExists,
                     "Output appeared during writing; not replaced"};
    if (!commit_file(temp.path, output, options.replace_existing_output))
        return Error{exists_now || fs::exists(output, ec)
                         ? ErrorCode::OutputExists
                         : ErrorCode::OutputWrite,
                     "Cannot commit output file"};
    temp.path.clear();  // Now the committed output; do not delete.
    result.committed = true;
    result.replaced_existing_output = exists_now;
    testing::fault(testing::FaultPoint::AfterCommit, output);
    if (control.is_cancelled())
        result.diagnostics.push_back(
            "Cancellation requested after commit; output is complete");
    return result;
}

}  // namespace pdfbookmark::writer
