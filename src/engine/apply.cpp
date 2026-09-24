#include <pdfbookmark/engine/apply.hpp>
#include <pdfbookmark/engine/analysis.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace pdfbookmark::engine {

Result<writer::WriteResult> apply(const std::filesystem::path& input,
                                  const std::filesystem::path& output,
                                  const writer::BookmarkPlan& plan,
                                  const ApplyOptions& options,
                                  const RunControl& control) {
    writer::WriteOptions write;
    write.replace_existing_output = options.replace_existing_output;
    return writer::write_copy(input, output, plan, write, control);
}

Result<writer::WriteResult> apply_plan_file(const std::filesystem::path& input,
                                            const std::filesystem::path& output,
                                            const std::filesystem::path& plan_file,
                                            const ApplyOptions& options,
                                            const RunControl& control) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(plan_file, ec))
        return Error{ErrorCode::InputOpen,
                     "Plan file not found: " + plan_file.u8string()};
    const auto size = std::filesystem::file_size(plan_file, ec);
    if (ec || size > options.max_plan_bytes)
        return Error{ErrorCode::ResourceLimit, "Plan file is too large or unreadable"};
    // The plan file must never be the output being written.
    if (std::filesystem::exists(output, ec) &&
        std::filesystem::equivalent(plan_file, output, ec))
        return Error{ErrorCode::InvalidArgument, "Output must not be the plan file"};
    std::ifstream in(plan_file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    if (!in.good() && !in.eof())
        return Error{ErrorCode::InputOpen, "Cannot read plan file"};
    auto plan = parse_plan_json(text);
    if (!plan) return plan.error();
    return apply(input, output, plan.value(), options, control);
}

}  // namespace pdfbookmark::engine
