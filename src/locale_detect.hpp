#pragma once
#include <string>
#include <vector>

namespace clitheme {
namespace locale_detect {

// Get list of locale strings from environment variables
// Mirrors Python _globalvar.get_locale()
std::vector<std::string> get_locale(bool debug_mode = false);

} // namespace locale_detect
} // namespace clitheme
