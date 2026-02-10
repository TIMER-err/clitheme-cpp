#include "generator_object.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include "sanity_check.hpp"
#include "db_interface.hpp"
#include "pcre2_regex.hpp"
#include <regex>
#include <set>
#include <cassert>

namespace clitheme {

void GeneratorObject::handle_entry(const std::string& start_phrase, const std::string& end_phrase,
                                    bool is_substrules, const SubstrulesOptions& substrules_opts) {
    struct EntryName {
        std::string value;
        bool is_multiline;
        std::string id;
        std::string line_number;
    };
    std::vector<EntryName> entry_names;

    struct Entry {
        std::string content;
        std::string content_line_number;
        std::optional<std::string> locale;
    };
    std::vector<Entry> entry_items;

    int substrules_stdout_stderr_option = 0;
    OptionsDict got_options;
    bool got_options_set = false;

    auto check_entry_name = [&](const std::string& name) -> bool {
        if (is_substrules) {
            try { pcre2_regex::validate_pattern(name); }
            catch (const pcre2_regex::regex_error& e) {
                handle_error("Line " + std::to_string(linenum()) + ": Bad match pattern (" +
                            string_utils::make_printable(e.what()) + ")");
                return false;
            }
        } else {
            if (!sanity_check::check(name)) {
                handle_error("Line " + std::to_string(linenum()) + ": Entry subsections/names " + sanity_check::error_message);
                return false;
            }
        }
        return true;
    };

    auto add_entry_item = [&](const std::string& content, const std::vector<std::string>& locales,
                         const std::string& line_num = "") {
        for (const auto& loc : locales) {
            entry_items.push_back(Entry{
                content,
                line_num.empty() ? std::to_string(linenum()) : line_num,
                (loc == "default") ? std::nullopt : std::optional<std::string>(loc)
            });
        }
    };

    auto opt = [&](const std::string& name) -> bool {
        return options::opt_is_true(got_options, name);
    };

    bool names_processed = false;
    int start_index = lineindex - 1;

    // First pass: find options on end_phrase line
    while (goto_next_line()) {
        auto phrases = string_utils::split_whitespace(get_current_line());
        if (!phrases.empty() && phrases[0] == end_phrase) {
            std::vector<std::string> opt_parts(phrases.begin() + 1, phrases.end());
            auto allowed = is_substrules ? options::substrules_options : std::vector<std::string>{};
            got_options = parse_options(opt_parts, 1, allowed.empty() ? nullptr : &allowed);
            got_options_set = true;
            if (options::opt_is_true(got_options, "subststdoutonly")) substrules_stdout_stderr_option = 1;
            if (options::opt_is_true(got_options, "subststderronly")) substrules_stdout_stderr_option = 2;
            break;
        }
    }
    assert(got_options_set);

    // Rewind
    lineindex = start_index;

    // Second pass: process content
    while (goto_next_line()) {
        auto phrases = string_utils::split_whitespace(get_current_line());
        std::string line_content = get_current_line();

        // Stop allowing more names after other content
        // Check against start_phrase and start_phrase with ] replaced by >>
        std::string start_phrase_multiline = std::regex_replace(start_phrase, std::regex(R"(\])"), ">>");
        if (!phrases.empty() && phrases[0] != start_phrase && phrases[0] != start_phrase_multiline) {
            names_processed = true;
        }

        // Entry names / Match patterns
        if (!phrases.empty() && phrases[0] == start_phrase && !names_processed) {
            check_enough_args(phrases, 2, "", !is_substrules);
            std::string pattern = string_utils::extract_content(line_content);
            pattern = parse_content(pattern, is_substrules ? 0 : 1);
            if (is_substrules && !substrules_opts.is_regex) {
                pattern = string_utils::regex_escape(pattern);
            }
            if (check_entry_name(pattern)) {
                entry_names.push_back(EntryName{
                    pattern, false, gen_uuid(), std::to_string(linenum())
                });
            }
        }
        // Multiline match pattern (e.g. [subst_regex>>)
        else if (!phrases.empty() && phrases[0] == start_phrase_multiline && !names_processed && is_substrules) {
            check_extra_args(phrases, 1);
            int begin_line_number = linenum() + 1;
            // e.g. <<subst_regex]
            std::string ml_end = std::regex_replace(start_phrase, std::regex(R"(\[)"), "<<");
            auto pattern_lines = handle_block_input_splitlines(true, true, ml_end);
            if (!substrules_opts.is_regex) {
                for (auto& pl : pattern_lines) pl = string_utils::regex_escape(pl);
            }
            std::string joined_pattern;
            for (size_t i = 0; i < pattern_lines.size(); i++) {
                if (i > 0) joined_pattern += "|";
                joined_pattern += pattern_lines[i];
            }
            if (check_entry_name(string_utils::join(pattern_lines, "\n"))) {
                // Build newline separator regex
                std::string newline_sep;
                for (size_t i = 0; i < globalvar::newlines.size(); i++) {
                    if (i > 0) newline_sep += "|";
                    newline_sep += string_utils::regex_escape(globalvar::newlines[i]);
                }
                if (opt("nlmatchcurpos")) {
                    newline_sep += "|\\x1b\\[\\d+;\\d+H";
                }
                std::string line_separator = "(?:" + newline_sep + ")";
                std::string pattern = string_utils::join(pattern_lines, line_separator);
                entry_names.push_back(EntryName{
                    pattern, true, gen_uuid(),
                    handle_linenumber_range(begin_line_number, linenum() - 1)
                });
            }
        }
        // locale[names]: content
        else if (!phrases.empty() && string_utils::starts_with(phrases[0], "locale[")) {
            std::regex locale_re(R"(^locale\[(.+?)\]:(?!\S+))");
            std::smatch m;
            std::string stripped = string_utils::strip(get_current_line());
            if (std::regex_search(stripped, m, locale_re) && !string_utils::split_whitespace(m[1].str()).empty()) {
                int argc = static_cast<int>(string_utils::split_whitespace(m[0].str()).size());
                check_enough_args(phrases, argc + 1, m[0].str(), false);
                auto locales = string_utils::split_whitespace(parse_content(string_utils::strip(m[1].str()), 2));
                if (locales.empty()) {
                    handle_error("Line " + std::to_string(linenum()) + ": Not enough arguments for \"<name> @ locale[<name>]:\"");
                }
                std::string content = string_utils::extract_content(get_current_line(), argc);
                add_entry_item(parse_content(content), locales);
            } else {
                handle_error("Line " + std::to_string(linenum()) + ": Invalid format for \"locale\"");
            }
        }
        // default: content
        else if (!phrases.empty() && phrases[0] == "default:") {
            check_enough_args(phrases, 2, "", false);
            std::string content = string_utils::extract_content(get_current_line());
            add_entry_item(parse_content(content), {"default"});
        }
        // Old syntax: locale or locale:name
        else if (!phrases.empty() && (phrases[0] == "locale" || std::regex_match(phrases[0], std::regex(R"(locale:.+)")))) {
            if (string_utils::starts_with(phrases[0], "locale:")) {
                check_enough_args(phrases, 2, "", false);
                std::smatch lr;
                std::regex_match(phrases[0], lr, std::regex(R"(locale:(.+))"));
                std::string locale_name = lr[1].str();
                std::string content = string_utils::extract_content(line_content);
                auto locales = string_utils::split_whitespace(parse_content(locale_name, 2));
                if (locales.empty()) {
                    handle_error("Line " + std::to_string(linenum()) + ": Not enough arguments for \"<name> @ locale:<name>\"");
                }
                add_entry_item(parse_content(content), locales);
            } else {
                check_enough_args(phrases, 3, "", false);
                std::string content = string_utils::extract_content(line_content, 2);
                auto locales = string_utils::split_whitespace(parse_content(phrases[1], 2));
                if (locales.empty()) {
                    handle_error("Line " + std::to_string(linenum()) + ": Not enough arguments for \"<name> @ locale:<name>\"");
                }
                add_entry_item(parse_content(content), locales);
            }
        }
        // [locale] block
        else if (!phrases.empty() && (phrases[0] == "[locale]" || phrases[0] == "locale_block")) {
            check_enough_args(phrases, 2);
            auto locales = string_utils::split_whitespace(parse_content(
                string_utils::join(std::vector<std::string>(phrases.begin() + 1, phrases.end()), " "), 1));
            int begin_line_number = linenum() + 1;
            std::string ep = (phrases[0] == "[locale]") ? "[/locale]" : "end_block";
            std::string sep = is_substrules ? "\r\n" : "\n";
            std::string content = handle_block_input(true, true, ep, sep);
            add_entry_item(content, locales, handle_linenumber_range(begin_line_number, linenum() - 1));
        }
        // [default] block
        else if (!phrases.empty() && phrases[0] == "[default]") {
            check_extra_args(phrases, 1);
            int begin_line_number = linenum() + 1;
            std::string sep = is_substrules ? "\r\n" : "\n";
            std::string content = handle_block_input(true, true, "[/default]", sep);
            add_entry_item(content, {"default"}, handle_linenumber_range(begin_line_number, linenum() - 1));
        }
        // End phrase
        else if (!phrases.empty() && phrases[0] == end_phrase) {
            break;
        }
        else {
            if (!phrases.empty()) handle_invalid_phrase(phrases[0]);
        }
    }

    // Process entries
    for (const auto& entry_name : entry_names) {
        std::set<std::string> checked_entries;
        for (const auto& entry : entry_items) {
            std::string line_number_debug = entry_name.line_number + ">" + entry.content_line_number +
                "[" + (entry.locale.has_value() ? string_utils::make_printable(*entry.locale) : "default") + "]";

            if (is_substrules) {
                try {
                    db_interface::add_subst_entry(
                        entry_name.value,
                        entry.content,
                        substrules_opts.effective_commands,
                        substrules_opts.strictness,
                        substrules_opts.command_is_regex,
                        entry.locale,
                        substrules_opts.is_regex,
                        entry_name.is_multiline,
                        opt("endmatchhere"),
                        substrules_stdout_stderr_option,
                        opt("foregroundonly"),
                        entry_name.id,
                        file_id,
                        line_number_debug,
                        [this](const std::string& msg) { handle_warning(msg); }
                    );
                } catch (const db_interface::bad_pattern& e) {
                    if (checked_entries.find(entry.content_line_number) == checked_entries.end()) {
                        handle_error("Line " + entry_name.line_number + ">" + entry.content_line_number +
                                    ": Bad substitute pattern (" + string_utils::make_printable(e.what()) + ")");
                        checked_entries.insert(entry.content_line_number);
                    }
                }
            } else {
                // Regular entry
                auto name_parts = string_utils::split_whitespace(entry_name.value);
                std::string target_entry = string_utils::join(name_parts, " ");
                if (entry.locale.has_value()) target_entry += "__" + *entry.locale;
                if (!in_subsection.empty()) target_entry = in_subsection + " " + target_entry;
                if (!in_domainapp.empty()) target_entry = in_domainapp + " " + target_entry;
                DataHandlers::add_entry(datapath, target_entry, entry.content, line_number_debug);
            }
        }
    }
}

} // namespace clitheme
