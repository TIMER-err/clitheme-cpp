#include "sanity_check.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include <regex>

namespace clitheme {
namespace sanity_check {

std::string error_message;

bool check(const std::string& path) {
    if (string_utils::strip(path).empty()) {
        error_message = "cannot be empty";
        return false;
    }
    auto parts = string_utils::split_whitespace(path);
    for (const auto& p : parts) {
        for (char b : globalvar::startswith_banphrases) {
            if (p[0] == b) {
                error_message = "cannot start with '" + std::string(1, b) + "'";
                return false;
            }
        }
        for (char b : globalvar::entry_banphrases) {
            if (p.find(b) != std::string::npos) {
                error_message = "cannot contain '" + std::string(1, b) + "'";
                return false;
            }
        }
    }
    return true;
}

std::string sanitize_str(const std::string& path) {
    std::string result = path;
    // Replace startswith banphrases at start of words
    for (char b : globalvar::startswith_banphrases) {
        std::string escaped = std::string("\\") + b;
        std::regex re("(^|\\s)" + escaped);
        result = std::regex_replace(result, re, "$1_");
    }
    // Replace banphrase characters
    for (char b : globalvar::entry_banphrases) {
        std::string from(1, b);
        result = string_utils::replace_all(result, from, "_");
    }
    return result;
}

} // namespace sanity_check
} // namespace clitheme
