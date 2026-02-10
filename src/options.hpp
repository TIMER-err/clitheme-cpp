#pragma once
#include <string>
#include <map>
#include <vector>
#include <variant>
#include <optional>

namespace clitheme {

// Option value: either bool or int
using OptionValue = std::variant<bool, int>;
using OptionsDict = std::map<std::string, OptionValue>;

namespace options {

// Option group definitions
inline const std::vector<std::string> lead_indent_options = {"leadtabindents", "leadspaces"};
inline const std::vector<std::string> content_subst_options = {"substvar", "linebounds"};
inline const std::vector<std::string> char_subst_options = {"substesc", "substchar"};

inline std::vector<std::string> subst_options() {
    auto result = content_subst_options;
    result.insert(result.end(), char_subst_options.begin(), char_subst_options.end());
    return result;
}

inline const std::vector<std::string> command_filter_options = {
    "strictcmdmatch", "exactcmdmatch", "smartcmdmatch", "normalcmdmatch", "foregroundonly"
};
inline const std::vector<std::string> substrules_options = {
    "subststdoutonly", "subststderronly", "substallstreams",
    "endmatchhere", "foregroundonly", "nlmatchcurpos"
};

// block_input_options = lead_indent_options + subst_options
inline std::vector<std::string> block_input_options() {
    auto result = lead_indent_options;
    auto so = subst_options();
    result.insert(result.end(), so.begin(), so.end());
    return result;
}

// Value options: options requiring an integer value
inline const std::vector<std::string>& value_options = lead_indent_options;

// Bool options (use no<...> to disable)
inline std::vector<std::string> bool_options() {
    auto so = subst_options();
    // Add substrules_options[3:] (endmatchhere, foregroundonly, nlmatchcurpos)
    so.push_back("endmatchhere");
    so.push_back("foregroundonly");
    so.push_back("nlmatchcurpos");
    return so;
}

// Switch options: only one can be true at a time in each group
inline const std::vector<std::vector<std::string>> switch_options = {
    {"strictcmdmatch", "exactcmdmatch", "smartcmdmatch", "normalcmdmatch"}
};

// substvar ban phrases
inline const std::vector<char> substvar_banphrases = {'{', '}', '[', ']', '(', ')'};

// Helper to check if option is in list
inline bool option_in(const std::string& opt, const std::vector<std::string>& list) {
    for (const auto& item : list) {
        if (item == opt) return true;
    }
    return false;
}

// Helper to get bool value from options dict
inline bool opt_is_true(const OptionsDict& opts, const std::string& name) {
    auto it = opts.find(name);
    if (it == opts.end()) return false;
    if (auto* b = std::get_if<bool>(&it->second)) return *b;
    return false;
}

// Helper to get optional int value from options dict
inline std::optional<int> opt_get_int(const OptionsDict& opts, const std::string& name) {
    auto it = opts.find(name);
    if (it == opts.end()) return std::nullopt;
    if (auto* v = std::get_if<int>(&it->second)) return *v;
    return std::nullopt;
}

} // namespace options
} // namespace clitheme
