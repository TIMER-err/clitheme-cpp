#pragma once
#include <string>
#include <vector>
#include <regex>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <cctype>

namespace clitheme {
namespace string_utils {

// Split string by delimiter
inline std::vector<std::string> split(const std::string& s, char delim = ' ') {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        result.push_back(token);
    }
    return result;
}

// Split string by whitespace (like Python str.split())
inline std::vector<std::string> split_whitespace(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token) {
        result.push_back(token);
    }
    return result;
}

// Strip leading and trailing whitespace
inline std::string strip(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(start, end - start + 1);
}

// Left strip
inline std::string lstrip(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return "";
    return s.substr(start);
}

// Right strip
inline std::string rstrip(const std::string& s) {
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    if (end == std::string::npos) return "";
    return s.substr(0, end + 1);
}

// Check if string starts with prefix
inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Check if string ends with suffix
inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Join strings with separator
inline std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += sep;
        result += parts[i];
    }
    return result;
}

// Extract content after N space-separated phrases at the beginning
// Equivalent to Python extract_content(line_content, begin_phrase_count)
inline std::string extract_content(const std::string& line_content, int begin_phrase_count = 1) {
    std::string stripped = strip(line_content);
    // Match begin_phrase_count phrases followed by content
    // Each phrase is: \s*.+?\s+
    std::string pattern = "^(?:\\s*.+?\\s+){" + std::to_string(begin_phrase_count) + "}(.+)";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(stripped, match, re)) {
        return match[1].str();
    }
    throw std::runtime_error("Match content failed (no matches)");
}

// Make non-printable characters visible
inline std::string make_printable(const std::string& content) {
    std::string result;
    for (unsigned char ch : content) {
        if (std::isprint(ch) || std::isspace(ch)) {
            result += static_cast<char>(ch);
        } else {
            // Format as <0xHH>
            char buf[8];
            std::snprintf(buf, sizeof(buf), "<0x%02x>", ch);
            result += buf;
        }
    }
    return result;
}

// Convert a Unicode codepoint to UTF-8 string
inline std::string codepoint_to_utf8(uint32_t cp) {
    std::string result;
    if (cp <= 0x7F) {
        result += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return result;
}

// Replace all occurrences of 'from' with 'to' in 'str'
inline std::string replace_all(const std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

// Regex escape (like Python re.escape)
inline std::string regex_escape(const std::string& s) {
    static const std::regex special_chars(R"([-[\]{}()*+?.,\^$|#\s])");
    return std::regex_replace(s, special_chars, R"(\$&)");
}

} // namespace string_utils
} // namespace clitheme
