#include "generator_object.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include "sanity_check.hpp"
#include "db_interface.hpp"
#include <regex>
#include <cmath>
#include <algorithm>
#include <random>
#include <sstream>
#include <cassert>
#include <functional>

namespace clitheme {

int GeneratorObject::hash_index_ = 0;

GeneratorObject::SubstrulesOptions GeneratorObject::make_default_substrules_opts() {
    return SubstrulesOptions{std::nullopt, false, false, 0};
}

std::string GeneratorObject::gen_uuid() {
    // Generate a random UUID v4
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    hash_index_++;

    char buf[40];
    uint32_t a = dis(gen), b = dis(gen), c = dis(gen), d = dis(gen);
    // Set version 4 and variant bits
    c = (c & 0x0FFF0FFF) | 0x40008000;
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
                  a, (b >> 16) & 0xFFFF, (c >> 16) & 0xFFFF,
                  c & 0xFFFF, b & 0xFFFF, d);
    return std::string(buf);
}

GeneratorObject::GeneratorObject(const std::string& fc, const std::string& cin,
                                 const std::string& fn, const std::string& p, bool cdb)
    : DataHandlers(p), section_parsing(false), lineindex(-1),
      custom_infofile_name(cin), filename(fn), file_content(fc), close_db_flag(cdb) {
    file_id = gen_uuid();
    // Split file content into lines
    std::istringstream iss(fc);
    std::string line;
    while (std::getline(iss, line)) {
        // Remove trailing \r if present (for \r\n line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_data.push_back(line);
    }
}

bool GeneratorObject::is_ignore_line() const {
    std::string stripped = string_utils::strip(get_current_line());
    return stripped.empty() || stripped[0] == '#';
}

bool GeneratorObject::goto_next_line() {
    while (lineindex < static_cast<int>(lines_data.size()) - 1) {
        lineindex++;
        if (!is_ignore_line()) return true;
    }
    return false;
}

int GeneratorObject::linenum() const {
    return lineindex + 1;
}

std::string GeneratorObject::get_current_line() const {
    return lines_data[lineindex];
}

void GeneratorObject::handle_invalid_phrase(const std::string& name) {
    handle_syntax_error("Line " + std::to_string(linenum()) + ": Unexpected \"" + string_utils::make_printable(name) + "\"");
}

void GeneratorObject::handle_unterminated_section(const std::string& name) {
    handle_error("Unterminated " + name + " section at end of file");
}

void GeneratorObject::check_enough_args(const std::vector<std::string>& phrases, int count,
                                         const std::string& disp, bool check_processed) {
    bool ok;
    if (check_processed && phrases.size() > 1) {
        std::string rest;
        for (size_t i = 1; i < phrases.size(); i++) {
            if (i > 1) rest += " ";
            rest += phrases[i];
        }
        std::string processed = parse_content(rest, 1, -1, true);
        ok = static_cast<int>(string_utils::split_whitespace(processed).size()) + 1 >= count;
    } else {
        ok = static_cast<int>(phrases.size()) >= count;
    }
    if (!ok) {
        std::string name = disp.empty() ? phrases[0] : disp;
        handle_syntax_error("Line " + std::to_string(linenum()) + ": Not enough arguments for \"" + string_utils::make_printable(name) + "\"");
    }
}

void GeneratorObject::check_extra_args(const std::vector<std::string>& phrases, int count,
                                        const std::string& disp, bool check_processed) {
    bool ok;
    if (check_processed && phrases.size() > 1) {
        std::string rest;
        for (size_t i = 1; i < phrases.size(); i++) {
            if (i > 1) rest += " ";
            rest += phrases[i];
        }
        std::string processed = parse_content(rest, 1, -1, true);
        ok = static_cast<int>(string_utils::split_whitespace(processed).size()) + 1 <= count;
    } else {
        ok = static_cast<int>(phrases.size()) <= count;
    }
    if (!ok) {
        std::string name = disp.empty() ? phrases[0] : disp;
        handle_syntax_error("Line " + std::to_string(linenum()) + ": Extra arguments after \"" + string_utils::make_printable(name) + "\"");
    }
}

void GeneratorObject::check_version(const std::string& version_str) {
    std::regex ver_re(R"(^(\d+)\.(\d+)(-beta(\d+))?$)");
    std::smatch m;
    if (!std::regex_match(version_str, m, ver_re) || std::stoi(m[1].str()) < 2) {
        handle_syntax_error("Line " + std::to_string(linenum()) + ": Invalid version information \"" +
                            string_utils::make_printable(version_str) + "\"");
        return;
    }

    int major = std::stoi(m[1].str());
    int minor = std::stoi(m[2].str());

    bool version_ok = globalvar::version_major > major
        || (globalvar::version_major == major && globalvar::version_minor > minor);

    bool eq_cond = (globalvar::version_major == major && globalvar::version_minor == minor);

    if (m[4].matched) {
        // Beta version specified in require
        int req_beta = std::stoi(m[4].str());
        if (globalvar::beta_release >= 0) {
            version_ok = version_ok || (eq_cond && req_beta <= globalvar::beta_release);
        } else {
            version_ok = version_ok || eq_cond;
        }
    } else {
        version_ok = version_ok || eq_cond;
        // If did not specify beta, current version cannot be beta or dev
        version_ok = version_ok && (globalvar::beta_release < 0) && !(globalvar::version_release < 0);
    }

    if (!version_ok) {
        std::string cur_ver = globalvar::clitheme_version;
        if (globalvar::beta_release >= 0 && globalvar::clitheme_version.find("beta") == std::string::npos) {
            cur_ver += " [beta" + std::to_string(globalvar::beta_release) + "]";
        }
        handle_syntax_error("Current version of CLItheme (" + cur_ver +
                            ") does not support this file (requires " +
                            string_utils::make_printable(version_str) + " or higher)");
    }
}

OptionsDict GeneratorObject::parse_options(const std::vector<std::string>& options_data, int merge_global_options,
                                            const std::vector<std::string>* allowed_options,
                                            const std::vector<std::string>* ban_options) {
    // Hash for dedup
    size_t h = std::hash<int>()(linenum());
    for (const auto& o : options_data) {
        h ^= std::hash<std::string>()(o) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    bool show_warnings = parsed_option_lines.find(h) == parsed_option_lines.end();
    if (show_warnings) parsed_option_lines.insert(h);

    auto do_handle_error = [&](const std::string& msg) {
        if (show_warnings) handle_error(msg);
    };

    OptionsDict final_options;
    if (merge_global_options != 0) {
        final_options = (merge_global_options == 1) ? global_options : really_really_global_options;
    }
    if (options_data.empty()) return final_options;

    // Parse content of options (substvar only)
    std::string joined;
    for (const auto& o : options_data) {
        if (!joined.empty()) joined += " ";
        joined += o;
    }
    auto parsed = string_utils::split_whitespace(parse_content(joined, 2));

    auto value_opts = options::value_options;
    auto bool_opts = options::bool_options();
    auto switch_opts = options::switch_options;

    for (size_t x = 0; x < parsed.size(); x++) {
        const std::string& each_option = parsed[x];
        // Extract option name (remove "no" prefix and ":value" suffix)
        std::string option_name = std::regex_replace(each_option, std::regex(R"(^(no)?(.+?)(:.+)?$)"), "$2");
        std::string option_name_preserve_no = std::regex_replace(each_option, std::regex(R"(^(.+?)(:.+)?$)"), "$1");

        if (options::option_in(option_name_preserve_no, value_opts)) {
            // Value option
            std::smatch results;
            std::regex val_re(R"(^(.+?):(.+)$)");
            if (std::regex_match(each_option, results, val_re)) {
                try {
                    int value = std::stoi(results[2].str());
                    final_options[option_name] = value;
                } catch (...) {
                    do_handle_error("Line " + std::to_string(linenum()) +
                                   ": The value specified for option \"" +
                                   string_utils::make_printable(option_name) + "\" is not an integer");
                }
            } else {
                do_handle_error("Line " + std::to_string(linenum()) +
                               ": No value specified for option \"" +
                               string_utils::make_printable(option_name) + "\"");
            }
        } else if (options::option_in(option_name, bool_opts)) {
            // Bool option
            final_options[option_name] = !string_utils::starts_with(option_name_preserve_no, "no");
        } else {
            // Check switch options
            bool found = false;
            for (const auto& group : switch_opts) {
                if (options::option_in(option_name_preserve_no, group)) {
                    // Check conflicts with previous options
                    for (size_t prev = 0; prev < x; prev++) {
                        if (parsed[prev] != option_name_preserve_no &&
                            options::option_in(parsed[prev], group)) {
                            do_handle_error("Line " + std::to_string(linenum()) +
                                           ": The option \"" + string_utils::make_printable(option_name_preserve_no) +
                                           "\" can't be set at the same time with \"" +
                                           string_utils::make_printable(parsed[prev]) + "\"");
                        }
                    }
                    // Set all others to false
                    for (const auto& opt : group) final_options[opt] = false;
                    final_options[option_name_preserve_no] = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                do_handle_error("Line " + std::to_string(linenum()) + ": Unknown option \"" +
                               string_utils::make_printable(option_name_preserve_no) + "\"");
                continue;
            }
        }

        // Check allowed/banned
        if ((allowed_options && !options::option_in(option_name, *allowed_options)) ||
            (ban_options && options::option_in(option_name, *ban_options))) {
            do_handle_error("Line " + std::to_string(linenum()) + ": Option \"" +
                           string_utils::make_printable(option_name) + "\" not allowed here");
        }
    }
    return final_options;
}

void GeneratorObject::handle_set_global_options(const std::vector<std::string>& opts_data, bool really_really_global) {
    if (really_really_global) {
        really_really_global_options = parse_options(opts_data, 2);
    }
    global_options = parse_options(opts_data, 1);
    auto specified = parse_options(opts_data, 0);
    auto all_subst = options::subst_options();
    for (const auto& option : all_subst) {
        if (!options::opt_is_true(global_options, option) &&
            specified.find(option) != specified.end()) {
            warnings[option] = true;
        }
    }
}

void GeneratorObject::handle_setup_global_options() {
    auto prev_options = global_options;
    global_options = really_really_global_options;
    auto all_subst = options::subst_options();
    for (const auto& option : all_subst) {
        if (!options::opt_is_true(global_options, option) && options::opt_is_true(prev_options, option)) {
            warnings[option] = true;
        }
    }
    global_variables = really_really_global_variables;
}

std::string GeneratorObject::handle_subst(const std::string& content,
                                           const std::string& line_number_debug,
                                           bool silence_warnings,
                                           int subst_var, int subst_esc, int subst_chars) {
    // Determine effective options (-1 = use global)
    bool do_var = (subst_var == -1) ? options::opt_is_true(global_options, "substvar") : (subst_var == 1);
    bool do_chars = (subst_chars == -1) ? options::opt_is_true(global_options, "substchar") : (subst_chars == 1);
    bool do_esc = (subst_esc == -1) ? options::opt_is_true(global_options, "substesc") : (subst_esc == 1);

    std::string ln_debug = line_number_debug.empty() ? std::to_string(linenum()) : line_number_debug;

    std::regex substvar_re(R"(\{\{([^\s]+?)??\}\})");
    std::regex substchar_re(R"(\{\{\[([^\s]+?)??\]\}\})");

    // substvar warning
    if (!silence_warnings && !do_var && warnings.find("substvar") == warnings.end()) {
        std::sregex_iterator it(content.begin(), content.end(), substvar_re);
        std::sregex_iterator end;
        for (; it != end; ++it) {
            std::string var_name = (*it)[1].str();
            if (global_variables.find(var_name) != global_variables.end()) {
                handle_warning("Line " + ln_debug + ": Attempted to reference a defined variable, but \"substvar\" option is not enabled");
                break;
            }
        }
    }

    std::string new_content = content;

    // substvar processing
    if (do_var) {
        std::string result;
        size_t last_pos = 0;
        std::sregex_iterator it(content.begin(), content.end(), substvar_re);
        std::sregex_iterator end;
        std::set<std::string> encountered_variables;
        for (; it != end; ++it) {
            std::string var_name = (*it)[1].str();
            result += content.substr(last_pos, it->position() - last_pos);
            last_pos = it->position() + it->length();

            if (var_name.empty() || string_utils::strip(var_name).empty()) {
                result += it->str();
                continue;
            }
            if (var_name == "ESC") { result += it->str(); continue; } // Leave for substesc
            if (var_name.size() >= 2 && var_name[0] == '[' && var_name.back() == ']') {
                result += it->str(); continue; // Skip substchar format
            }

            auto var_it = global_variables.find(var_name);
            if (var_it != global_variables.end()) {
                result += var_it->second;
            } else {
                if (!silence_warnings && encountered_variables.find(var_name) == encountered_variables.end()) {
                    handle_warning("Line " + ln_debug + ": Unknown variable \"" +
                                  string_utils::make_printable(var_name) + "\", not performing substitution");
                }
                result += it->str(); // Keep original
            }
            encountered_variables.insert(var_name);
        }
        result += content.substr(last_pos);
        new_content = result;
    }

    // substesc warning
    if (!silence_warnings && !do_esc && warnings.find("substesc") == warnings.end()) {
        if (new_content.find("{{ESC}}") != std::string::npos) {
            handle_warning("Line " + ln_debug + ": Attempted to use \"{{ESC}}\", but \"substesc\" option is not enabled");
        }
    }

    // substesc processing
    if (do_esc) {
        new_content = string_utils::replace_all(new_content, "{{ESC}}", "\x1b");
    }

    // substchar warning
    if (!silence_warnings && !do_chars && warnings.find("substchar") == warnings.end()) {
        if (std::regex_search(new_content, substchar_re)) {
            handle_warning("Line " + ln_debug + ": Attempted to use character substitution, but \"substchar\" option is not enabled");
        }
    }

    // substchar processing
    if (do_chars) {
        std::string result;
        size_t last_pos = 0;
        std::sregex_iterator it(new_content.begin(), new_content.end(), substchar_re);
        std::sregex_iterator end_it;
        for (; it != end_it; ++it) {
            std::string pattern = (*it)[1].str();
            result += new_content.substr(last_pos, it->position() - last_pos);
            last_pos = it->position() + it->length();

            if (pattern.empty() || string_utils::strip(pattern).empty()) {
                result += it->str();
                continue;
            }

            // Match x.., u...., U........
            std::regex char_re(R"(^(x.{2}|u.{4}|U.{8})$)");
            std::smatch char_match;
            if (std::regex_match(pattern, char_match, char_re)) {
                try {
                    uint32_t cp = std::stoul(pattern.substr(1), nullptr, 16);
                    result += string_utils::codepoint_to_utf8(cp);
                } catch (...) {
                    if (!silence_warnings) {
                        handle_warning("Line " + ln_debug + ": Invalid character code \"" +
                                      string_utils::make_printable(pattern.substr(1)) + "\", not performing substitution");
                    }
                    result += it->str();
                }
            } else {
                if (!silence_warnings) {
                    handle_warning("Line " + ln_debug + ": Invalid substchar format \"" +
                                  string_utils::make_printable(pattern) + "\", not performing substitution");
                }
                result += it->str();
            }
        }
        result += new_content.substr(last_pos);
        new_content = result;
    }

    return new_content;
}

std::pair<std::string, std::string> GeneratorObject::handle_linebounds(const std::string& content,
                                                                        int condition,
                                                                        bool preserve_indents,
                                                                        bool allow_options,
                                                                        int debug_linenumber,
                                                                        bool silence_warn) {
    bool cond = (condition == -1) ? options::opt_is_true(global_options, "linebounds") : (condition == 1);
    std::string stripped = string_utils::strip(content);

    // Build regex pattern for |...|
    std::string pattern_str = R"(^\|(.+?)\|)";
    if (allow_options) {
        pattern_str += R"((\s+([^\|]+))?)";
    }
    pattern_str += "$";

    std::regex lb_re(pattern_str);
    std::smatch match;
    bool has_match = std::regex_match(stripped, match, lb_re);

    if (!cond || !string_utils::starts_with(stripped, "|")) {
        if (has_match && !silence_warn && warnings.find("linebounds") == warnings.end()) {
            int ln = (debug_linenumber >= 0) ? debug_linenumber : linenum();
            handle_warning("Line " + std::to_string(ln) + ": Attempted to use line boundaries, but \"linebounds\" option is not enabled");
        }
        return {content, ""};
    }

    if (has_match) {
        std::string text = match[1].str();
        if (!preserve_indents) text = string_utils::strip(text);
        std::string options_str = (allow_options && match[3].matched) ? match[3].str() : "";
        return {text, options_str};
    } else {
        if (!silence_warn) {
            int ln = (debug_linenumber >= 0) ? debug_linenumber : linenum();
            handle_error("Line " + std::to_string(ln) + ": Invalid line boundary format");
        }
        return {content, ""};
    }
}

void GeneratorObject::handle_set_variable(const std::vector<std::string>& var_names, const std::string& var_content, bool really_really_global) {
    std::string parsed_content = parse_content(var_content, 1, 1); // pure_name=True, preserve_indents=True
    for (const auto& name : var_names) {
        // Sanity check
        bool bad = (name == "ESC");
        if (!bad) {
            for (char c : name) {
                for (char bp : options::substvar_banphrases) {
                    if (c == bp) { bad = true; break; }
                }
                if (bad) break;
            }
        }
        if (bad) {
            handle_error("Line " + std::to_string(linenum()) + ": \"" +
                        string_utils::make_printable(name) + "\" is not a valid variable name");
        } else {
            if (really_really_global) really_really_global_variables[name] = parsed_content;
            global_variables[name] = parsed_content;
        }
    }
}

void GeneratorObject::handle_begin_section(const std::string& section_name) {
    for (const auto& s : parsed_sections) {
        if (s == section_name) {
            handle_error("Line " + std::to_string(linenum()) + ": Repeated " + section_name + " section");
            break;
        }
    }
    section_parsing = true;
    handle_setup_global_options();
}

void GeneratorObject::handle_end_section(const std::string& section_name) {
    parsed_sections.push_back(section_name);
    section_parsing = false;
    handle_setup_global_options();
}

std::string GeneratorObject::handle_linenumber_range(int begin, int end) {
    if (begin == end) return std::to_string(end);
    return std::to_string(begin) + "-" + std::to_string(end);
}

std::string GeneratorObject::parse_content(const std::string& content, int pure_name,
                                            int preserve_indents, bool ignore_options) {
    return parse_content_with_options(content, {}, pure_name, preserve_indents, ignore_options).content;
}

GeneratorObject::ParseContentResult GeneratorObject::parse_content_with_options(
    const std::string& content, const std::vector<std::string>& extra_options,
    int pure_name, int preserve_indents, bool ignore_options) {

    if (preserve_indents == -1) preserve_indents = (pure_name == 0) ? 1 : 0;
    // Choose subst options based on pure_name
    auto subst_opts = (pure_name != 0) ? options::content_subst_options : options::subst_options();

    // Dedup warnings
    size_t h = std::hash<int>()(linenum());
    h ^= std::hash<std::string>()(content) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>()(pure_name) + 0x9e3779b9 + (h << 6) + (h >> 2);
    bool no_warn = parsed_lines.find(h) != parsed_lines.end();
    if (!no_warn) parsed_lines.insert(h);

    std::string target_content;
    std::string options_str;
    OptionsDict opts;
    OptionsDict inline_opts;

    if (pure_name != 2) {
        auto [tc, os] = handle_linebounds(content, -1, preserve_indents != 0, true, -1, no_warn);
        target_content = tc;
        options_str = os;
    } else {
        target_content = content;
    }

    if (!options_str.empty()) {
        auto opt_parts = string_utils::split_whitespace(options_str);
        auto allowed = subst_opts;
        allowed.insert(allowed.end(), extra_options.begin(), extra_options.end());
        std::vector<std::string> ban = {"linebounds"};
        if (!ignore_options) {
            opts = parse_options(opt_parts, 1, &allowed, &ban);
            inline_opts = parse_options(opt_parts, 0, &allowed, &ban);
        } else {
            opts = parse_options(opt_parts, 1, nullptr, &ban);
            inline_opts = parse_options(opt_parts, 0, nullptr, &ban);
        }
    } else {
        opts = global_options;
    }

    target_content = handle_subst(target_content,
        "", // use default linenum
        no_warn,
        options::opt_is_true(opts, "substvar") ? 1 : 0,
        (pure_name == 0 && options::opt_is_true(opts, "substesc")) ? 1 : 0,
        (pure_name == 0 && options::opt_is_true(opts, "substchar")) ? 1 : 0);

    if (preserve_indents == 0) target_content = string_utils::strip(target_content);

    return {target_content, opts, inline_opts};
}

bool GeneratorObject::handle_setters(bool really_really_global) {
    auto phrases = string_utils::split_whitespace(get_current_line());
    if (phrases.empty()) return false;

    // setvar[...]: format
    if (string_utils::starts_with(phrases[0], "setvar[")) {
        std::regex setvar_re(R"(^setvar\[(.+?)\]:(?!\S+))");
        std::smatch m;
        std::string stripped = string_utils::strip(get_current_line());
        if (std::regex_search(stripped, m, setvar_re) && !string_utils::split_whitespace(m[1].str()).empty()) {
            int argc = static_cast<int>(string_utils::split_whitespace(m[0].str()).size());
            check_enough_args(phrases, argc + 1, m[0].str(), false);
            std::string var_content = string_utils::extract_content(get_current_line(), argc);
            handle_set_variable(string_utils::split_whitespace(m[1].str()), var_content, really_really_global);
        } else {
            handle_error("Line " + std::to_string(linenum()) + ": Invalid format for \"setvar\"");
        }
        return true;
    }

    // Old setvar:name format
    std::regex setvar_old_re(R"(^setvar:(.+))");
    std::smatch old_m;
    if (std::regex_match(phrases[0], old_m, setvar_old_re)) {
        check_enough_args(phrases, 2, "", false);
        std::string var_name = old_m[1].str();
        std::string var_content = string_utils::extract_content(get_current_line(), 1);
        handle_set_variable({var_name}, var_content, really_really_global);
        return true;
    }

    if (phrases[0] == "(set_options)" || phrases[0] == "set_options") {
        check_enough_args(phrases, 2);
        std::vector<std::string> opts(phrases.begin() + 1, phrases.end());
        handle_set_global_options(opts, really_really_global);
        return true;
    }

    if (phrases[0] == "(enable_subst)") {
        check_extra_args(phrases, 1);
        handle_set_global_options(options::subst_options(), really_really_global);
        return true;
    }

    if (phrases[0] == "(disable_subst)") {
        check_extra_args(phrases, 1);
        auto so = options::subst_options();
        std::vector<std::string> disabled;
        for (const auto& o : so) disabled.push_back("no" + o);
        handle_set_global_options(disabled, really_really_global);
        return true;
    }

    return false;
}

std::vector<std::string> GeneratorObject::handle_block_input_splitlines(
    bool preserve_indents, bool preserve_empty_lines, const std::string& end_phrase,
    bool disallow_other_options, bool disable_char_subst) {

    int minspaces = std::numeric_limits<int>::max();
    std::vector<std::string> blockinput_lines;
    int begin_line_number = linenum() + 1;

    while (lineindex < static_cast<int>(lines_data.size()) - 1) {
        lineindex++;
        std::string line = get_current_line();
        if (string_utils::strip(line).empty()) {
            if (preserve_empty_lines) blockinput_lines.push_back("");
            continue;
        }
        auto line_parts = string_utils::split_whitespace(line);
        if (!line_parts.empty() && line_parts[0] == end_phrase) break;

        if (preserve_indents) {
            // Find leading whitespace
            auto ws_pos = line.find_first_not_of(" \t");
            std::string leading_ws;
            if (ws_pos != std::string::npos) {
                leading_ws = line.substr(0, ws_pos);
            }
            // Replace tabs with 8 spaces for counting
            std::string ws_for_count = std::regex_replace(leading_ws, std::regex("\t"), "        ");
            // Replace \end_phrase with end_phrase (unescape)
            std::string rest = string_utils::lstrip(line);
            std::regex esc_re("^\\\\(\\\\*)" + string_utils::regex_escape(end_phrase));
            rest = std::regex_replace(rest, esc_re, "$1" + end_phrase);
            line = ws_for_count + rest;
            minspaces = std::min(minspaces, static_cast<int>(ws_for_count.length()));
        } else {
            std::string stripped = string_utils::lstrip(line);
            std::regex esc_re("^\\\\(\\\\*)" + string_utils::regex_escape(end_phrase));
            line = std::regex_replace(stripped, esc_re, "$1" + end_phrase);
        }
        blockinput_lines.push_back(string_utils::rstrip(line));
    }

    // Check if we reached the end without finding end_phrase
    if (lineindex >= static_cast<int>(lines_data.size()) - 1) {
        auto line_parts = string_utils::split_whitespace(get_current_line());
        if (line_parts.empty() || line_parts[0] != end_phrase) {
            handle_syntax_error("Line " + std::to_string(begin_line_number - 1) + ": Unterminated content block");
        }
    }

    if (blockinput_lines.empty()) return {};

    // Remove common leading whitespace
    if (preserve_indents && minspaces > 0 && minspaces != std::numeric_limits<int>::max()) {
        for (auto& line : blockinput_lines) {
            if (static_cast<int>(line.size()) >= minspaces) {
                line = line.substr(minspaces);
            }
        }
    }

    // Parse options on end_phrase line
    OptionsDict got_options = global_options;
    auto end_line_parts = string_utils::split_whitespace(get_current_line());
    if (end_line_parts.size() > 1) {
        std::vector<std::string> opt_parts(end_line_parts.begin() + 1, end_line_parts.end());
        std::vector<std::string> ban_opts, allowed_opts;
        if (!disallow_other_options) {
            if (!preserve_indents) ban_opts.insert(ban_opts.end(), options::lead_indent_options.begin(), options::lead_indent_options.end());
            if (disable_char_subst) ban_opts.insert(ban_opts.end(), options::char_subst_options.begin(), options::char_subst_options.end());
            got_options = parse_options(opt_parts, 1, nullptr, ban_opts.empty() ? nullptr : &ban_opts);
        } else {
            if (preserve_indents) allowed_opts.insert(allowed_opts.end(), options::lead_indent_options.begin(), options::lead_indent_options.end());
            if (!disable_char_subst) allowed_opts.insert(allowed_opts.end(), options::char_subst_options.begin(), options::char_subst_options.end());
            allowed_opts.insert(allowed_opts.end(), options::content_subst_options.begin(), options::content_subst_options.end());
            got_options = parse_options(opt_parts, 1, &allowed_opts, nullptr);
        }
    }

    int line_offset = 0;
    for (size_t x = 0; x < blockinput_lines.size(); x++) {
        std::string& line = blockinput_lines[x];

        // leadtabindents
        if (preserve_indents) {
            auto tab_val = options::opt_get_int(got_options, "leadtabindents");
            if (tab_val.has_value()) {
                std::string prefix;
                for (int i = 0; i < *tab_val; i++) prefix += '\t';
                line = prefix + line;
            }
            auto space_val = options::opt_get_int(got_options, "leadspaces");
            if (space_val.has_value()) {
                line = std::string(*space_val, ' ') + line;
            }
        }

        // linebounds (without inline options)
        auto ws_pos = line.find_first_not_of(" \t");
        std::string leading_ws = (ws_pos != std::string::npos) ? line.substr(0, ws_pos) : "";
        std::string stripped_line = string_utils::strip(line);
        auto [lb_content, _] = handle_linebounds(stripped_line,
            options::opt_is_true(got_options, "linebounds") ? 1 : 0,
            preserve_indents, false, begin_line_number + line_offset, false);
        line = leading_ws + lb_content;

        // subst
        line = handle_subst(line, std::to_string(begin_line_number + line_offset), false,
            options::opt_is_true(got_options, "substvar") ? 1 : 0,
            (options::opt_is_true(got_options, "substesc") && !disable_char_subst) ? 1 : 0,
            (options::opt_is_true(got_options, "substchar") && !disable_char_subst) ? 1 : 0);

        line_offset++;
    }
    return blockinput_lines;
}

std::string GeneratorObject::handle_block_input(bool preserve_indents, bool preserve_empty_lines,
                                                  const std::string& end_phrase,
                                                  const std::string& line_separator,
                                                  bool disallow_other_options,
                                                  bool disable_char_subst) {
    auto lines = handle_block_input_splitlines(preserve_indents, preserve_empty_lines, end_phrase,
                                                disallow_other_options, disable_char_subst);
    return string_utils::join(lines, line_separator);
}

} // namespace clitheme
