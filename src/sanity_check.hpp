#pragma once
#include <string>

namespace clitheme {
namespace sanity_check {

// Last error message from sanity_check
extern std::string error_message;

// Check whether the path contains invalid phrases
// Returns true if valid, false if invalid
bool check(const std::string& path);

// Sanitize a string by replacing invalid characters with '_'
std::string sanitize_str(const std::string& path);

} // namespace sanity_check
} // namespace clitheme
