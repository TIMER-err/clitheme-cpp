#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace clitheme {
namespace globalvar {

// Version info
constexpr int version_major = 2;
constexpr int version_minor = 1;
constexpr int version_release = 0; // -1 = "dev"
// Set to -1 if not beta
constexpr int beta_release = -1; // -1 means not beta (None in Python)
inline const std::string clitheme_version = "2.1";

// Generator file and folder names
inline const std::string generator_info_pathname = "theme-info";
inline const std::string generator_data_pathname = "theme-data";
inline const std::string generator_manpage_pathname = "manpages";
inline const std::string generator_index_filename = "current_theme_index";
// Use format_info_filename() to get actual filename
inline const std::string generator_info_filename_prefix = "clithemeinfo_";
// e.g. clithemeinfo_name, clithemeinfo_description
inline std::string format_info_filename(const std::string& info) {
    return "clithemeinfo_" + info;
}
// e.g. clithemeinfo_name_v2
inline std::string format_info_v2filename(const std::string& info) {
    return "clithemeinfo_" + info + "_v2";
}

// Database file and table names
inline const std::string db_data_tablename = "clitheme_subst_data";
inline const std::string db_filename = "subst-data.db";
constexpr int db_version = 8;

// Timeout for output substitution
constexpr double output_subst_timeout = 1.0;

// Newline byte sequences (order matters: \r\n must come before \r and \n)
inline const std::vector<std::string> newlines = {
    "\r\n", "\r", "\n", "\x0b", "\x0c", "\x1c", "\x1d", "\x1e"
};

// Build the line_match regex pattern
inline std::string build_line_match_pattern() {
    std::string alt;
    for (size_t i = 0; i < newlines.size(); i++) {
        if (i > 0) alt += "|";
        // Escape special regex chars in the newline strings
        for (char c : newlines[i]) {
            if (c == '\r') alt += "\\r";
            else if (c == '\n') alt += "\\n";
            else if (c == '\x0b') alt += "\\x0b";
            else if (c == '\x0c') alt += "\\x0c";
            else if (c == '\x1c') alt += "\\x1c";
            else if (c == '\x1d') alt += "\\x1d";
            else if (c == '\x1e') alt += "\\x1e";
            else alt += c;
        }
    }
    return ".*?(" + alt + "|$)";
}

// Sanity check ban phrases
inline const std::vector<char> entry_banphrases = {'<', '>', ':', '"', '/', '\\', '|', '?', '*'};
inline const std::vector<char> startswith_banphrases = {'.'};

// Get the root data path (e.g. ~/.local/share/clitheme)
std::string get_root_data_path();
// Get the temp root path
std::string get_temp_root();

} // namespace globalvar
} // namespace clitheme
