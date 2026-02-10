#pragma once
#include <string>
#include <set>
#include <optional>
#include <utility>

namespace clitheme {
namespace substrules_processor {

// Match content against substitution rules from the database
// Returns: (processed_content, set of changed line indices)
std::pair<std::string, std::set<int>> match_content(
    const std::string& content,
    const std::optional<std::string>& command = std::nullopt,
    bool is_stderr = false
);

} // namespace substrules_processor
} // namespace clitheme
