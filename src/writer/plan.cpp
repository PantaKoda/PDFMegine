#include <pdfbookmark/writer/plan.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pdfbookmark::writer {
namespace {

// Strict UTF-8: no overlongs, surrogates, or scalars above U+10FFFF.
bool valid_utf8(const std::string& text) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t scalar = 0, minimum = 0;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) { length = 2; scalar = c & 0x1F; minimum = 0x80; }
        else if ((c & 0xF0) == 0xE0) { length = 3; scalar = c & 0x0F; minimum = 0x800; }
        else if ((c & 0xF8) == 0xF0) { length = 4; scalar = c & 0x07; minimum = 0x10000; }
        else return false;
        if (n - i < length) return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF ||
            (scalar >= 0xD800 && scalar <= 0xDFFF))
            return false;
        i += length;
    }
    return true;
}

bool blank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
               c == '\f' || c == '\v';
    });
}

void add(PlanValidation& result, PlanIssueCode code,
         std::optional<std::size_t> index, std::string id, std::string field,
         std::string message) {
    result.issues.push_back(
        {code, index, std::move(id), std::move(field), std::move(message)});
}

}  // namespace

PlanValidation validate(const BookmarkPlan& plan) {
    PlanValidation result;
    if (plan.schema_version != kPlanSchemaVersion)
        add(result, PlanIssueCode::UnsupportedSchemaVersion, std::nullopt, {},
            "schema_version", "Unsupported plan schema version " +
                                  std::to_string(plan.schema_version));
    if (plan.page_index_base != kPageIndexBase)
        add(result, PlanIssueCode::UnsupportedPageIndexBase, std::nullopt, {},
            "page_index_base", "Plan page indices must be zero based");
    if (std::all_of(plan.input.sha256.begin(), plan.input.sha256.end(),
                    [](std::uint8_t b) { return b == 0; }))
        add(result, PlanIssueCode::MissingInputDigest, std::nullopt, {},
            "input.sha256", "Plan is not bound to an input digest");
    if (plan.input.page_count <= 0)
        add(result, PlanIssueCode::InvalidPageCount, std::nullopt, {},
            "input.page_count", "Plan input page count must be positive");
    if (plan.nodes.empty())
        add(result, PlanIssueCode::EmptyPlan, std::nullopt, {}, "nodes",
            "A writable plan needs at least one node; an empty plan would "
            "delete the output outline");

    std::map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
        const auto& node = plan.nodes[i];
        if (node.id.empty())
            add(result, PlanIssueCode::EmptyNodeId, i, {}, "id",
                "Node ID must be nonempty");
        else if (!ids.emplace(node.id, i).second)
            add(result, PlanIssueCode::DuplicateNodeId, i, node.id, "id",
                "Duplicate node ID");
        if (!valid_utf8(node.title))
            add(result, PlanIssueCode::InvalidUtf8Title, i, node.id, "title",
                "Title is not valid UTF-8");
        else if (blank(node.title))
            add(result, PlanIssueCode::EmptyTitle, i, node.id, "title",
                "Title must contain visible text");
        if (plan.input.page_count > 0 &&
            (node.destination.pdf_page_index < 0 ||
             node.destination.pdf_page_index >= plan.input.page_count))
            add(result, PlanIssueCode::DestinationOutOfRange, i, node.id,
                "destination.pdf_page_index",
                "Destination " +
                    std::to_string(node.destination.pdf_page_index) +
                    " is outside [0, " +
                    std::to_string(plan.input.page_count) + ")");
    }
    for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
        const auto& node = plan.nodes[i];
        if (!node.parent_id) continue;
        if (*node.parent_id == node.id)
            add(result, PlanIssueCode::SelfParent, i, node.id, "parent_id",
                "Node cannot be its own parent");
        else if (!ids.count(*node.parent_id))
            add(result, PlanIssueCode::MissingParent, i, node.id, "parent_id",
                "Parent '" + *node.parent_id + "' is not a node in this plan");
    }
    // Cycle detection over the first occurrence of each ID (self-parents are
    // reported above). Each cycle is reported once, at its lowest node index.
    // Active marks exist only on the current walk, so a revisit is a cycle.
    enum class Mark { None, Active, Done };
    std::vector<Mark> marks(plan.nodes.size(), Mark::None);
    for (std::size_t start = 0; start < plan.nodes.size(); ++start) {
        std::vector<std::size_t> path;
        std::size_t current = start;
        while (true) {
            if (marks[current] == Mark::Done) break;
            if (marks[current] == Mark::Active) {
                const auto first =
                    std::find(path.begin(), path.end(), current);
                const std::size_t lowest =
                    *std::min_element(first, path.end());
                add(result, PlanIssueCode::ParentCycle, lowest,
                    plan.nodes[lowest].id, "parent_id",
                    "Parent chain forms a cycle");
                break;
            }
            marks[current] = Mark::Active;
            path.push_back(current);
            const auto& parent = plan.nodes[current].parent_id;
            if (!parent) break;
            const auto found = ids.find(*parent);
            if (found == ids.end() || found->second == current) break;
            current = found->second;
        }
        for (const auto index : path) marks[index] = Mark::Done;
    }

    for (const auto& omitted : plan.omitted_entries)
        if (omitted.entry_id.empty() || ids.count(omitted.entry_id))
            add(result, PlanIssueCode::InvalidOmission, std::nullopt,
                omitted.entry_id, "omitted_entries",
                "Omitted entry must be nonempty and absent from nodes");
    for (const auto& promotion : plan.promotions) {
        const auto found = ids.find(promotion.node_id);
        if (found == ids.end() ||
            plan.nodes[found->second].parent_id != promotion.new_parent_id ||
            promotion.original_parent_id == promotion.new_parent_id)
            add(result, PlanIssueCode::InvalidPromotion,
                found == ids.end() ? std::nullopt
                                   : std::optional<std::size_t>(found->second),
                promotion.node_id, "promotions",
                "Promotion must name a node whose current parent is "
                "new_parent_id and differs from original_parent_id");
    }
    result.valid = result.issues.empty();
    return result;
}

}  // namespace pdfbookmark::writer
