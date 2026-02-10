#include "section_substrules.hpp"
#include "generator_object.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include "db_interface.hpp"
#include "options.hpp"
#include <regex>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace clitheme {

void handle_substrules_section(GeneratorObject& self, const std::string& end_phrase) {
    self.handle_begin_section("substrules");

    std::optional<std::vector<std::string>> command_filters;
    bool command_filter_is_regex = false;
    int command_filter_strictness = 0;
    std::optional<bool> outline_foregroundonly;

    auto reset_outline_foregroundonly = [&]() {
        if (outline_foregroundonly.has_value()) {
            self.global_options["foregroundonly"] = *outline_foregroundonly;
            outline_foregroundonly.reset();
        }
    };

    auto check_pattern = [&](const std::string& pattern, int linenum = -1) {
        try { std::regex re(pattern); }
        catch (const std::regex_error& e) {
            self.handle_error("Line " + std::to_string(linenum >= 0 ? linenum : self.linenum()) +
                             ": Bad command filter pattern (" +
                             string_utils::make_printable(e.what()) + ")");
        }
    };

    std::string db_path = self.path + "/" + globalvar::db_filename;
    if (fs::exists(db_path)) {
        try {
            if (!db_interface::is_connected()) {
                db_interface::connect_db(db_path);
            }
        } catch (...) {
            self.handle_syntax_error("The current substrules database version is incompatible; please run \"clitheme repair-theme\" and try again");
        }
    } else {
        db_interface::init_db(db_path);
    }

    while (self.goto_next_line()) {
        auto phrases = string_utils::split_whitespace(self.get_current_line());
        if (phrases.empty()) continue;

        // [subst_string], [subst_regex], [substitute_string], [substitute_regex]
        std::regex subst_re(R"(\[(subst(itute)?_(string|regex))(\]|>>))");
        std::smatch subst_match;
        if (std::regex_match(phrases[0], subst_match, subst_re)) {
            std::string name = subst_match[1].str();
            bool is_regex = std::regex_match(phrases[0], std::regex(R"(\[subst(itute)?_regex(\]|>>))"));

            GeneratorObject::SubstrulesOptions opts;
            opts.effective_commands = command_filters;
            opts.command_is_regex = command_filter_is_regex;
            opts.is_regex = is_regex;
            opts.strictness = command_filter_strictness;

            self.handle_entry("[" + name + "]", "[/" + name + "]", true, opts);
        }
        // [filter_commands] / [filter_cmds] / [filter_commands_regex]
        else if (std::regex_match(phrases[0], std::regex(R"(\[filter_(cmds|commands)(_regex)?\])"))) {
            self.check_extra_args(phrases, 1);
            reset_outline_foregroundonly();
            command_filter_is_regex = std::regex_match(phrases[0], std::regex(R"(\[filter_(cmds|commands)_regex\])"));

            int prev_linenum = self.linenum();
            std::string filter_end = std::regex_replace(phrases[0], std::regex(R"(\[)"), "[/");
            auto command_strings = self.handle_block_input_splitlines(false, false, filter_end, false, true);

            if (command_filter_is_regex) {
                for (const auto& cmd : command_strings) {
                    prev_linenum++;
                    check_pattern(cmd, prev_linenum);
                }
            }

            int strictness = 0;
            OptionsDict got_options = self.global_options;
            OptionsDict inline_options;
            auto end_opts = string_utils::split_whitespace(self.get_current_line());
            if (end_opts.size() > 1) {
                std::vector<std::string> opt_parts(end_opts.begin() + 1, end_opts.end());
                auto bio = options::block_input_options();
                auto allowed = bio;
                if (!command_filter_is_regex) {
                    auto cfo = options::command_filter_options;
                    allowed.insert(allowed.end(), cfo.begin(), cfo.end());
                } else {
                    allowed.push_back("foregroundonly");
                }
                got_options = self.parse_options(opt_parts, 1, &allowed);
                inline_options = self.parse_options(opt_parts, 0, &allowed);
            }
            if (options::opt_is_true(got_options, "strictcmdmatch")) strictness = 1;
            if (options::opt_is_true(got_options, "exactcmdmatch")) strictness = 2;
            if (options::opt_is_true(got_options, "smartcmdmatch")) strictness = -1;
            if (inline_options.find("foregroundonly") != inline_options.end()) {
                outline_foregroundonly = options::opt_is_true(self.global_options, "foregroundonly");
                self.global_options["foregroundonly"] = inline_options["foregroundonly"];
            }
            command_filters = command_strings;
            command_filter_strictness = strictness;
        }
        // filter_command / filter_cmd (single line) - with or without angle brackets
        else if (phrases[0] == "filter_cmd" || phrases[0] == "filter_command" ||
                 phrases[0] == "<filter_cmd>" || phrases[0] == "<filter_command>" ||
                 phrases[0] == "filter_cmd_regex" || phrases[0] == "filter_command_regex" ||
                 phrases[0] == "<filter_cmd_regex>" || phrases[0] == "<filter_command_regex>") {
            self.check_enough_args(phrases, 2);
            reset_outline_foregroundonly();
            command_filter_is_regex = (phrases[0] == "filter_cmd_regex" || phrases[0] == "filter_command_regex" ||
                                       phrases[0] == "<filter_cmd_regex>" || phrases[0] == "<filter_command_regex>");

            std::string content_str = string_utils::join(std::vector<std::string>(phrases.begin() + 1, phrases.end()), " ");
            auto extra = command_filter_is_regex ? std::vector<std::string>{"foregroundonly"} : options::command_filter_options;
            auto result = self.parse_content_with_options(content_str, extra, 1);

            if (command_filter_is_regex) check_pattern(result.content);

            int strictness = 0;
            if (options::opt_is_true(result.options, "strictcmdmatch")) strictness = 1;
            if (options::opt_is_true(result.options, "exactcmdmatch")) strictness = 2;
            if (options::opt_is_true(result.options, "smartcmdmatch")) strictness = -1;
            if (result.inline_options.find("foregroundonly") != result.inline_options.end()) {
                outline_foregroundonly = options::opt_is_true(self.global_options, "foregroundonly");
                self.global_options["foregroundonly"] = result.inline_options["foregroundonly"];
            }
            command_filters = std::vector<std::string>{result.content};
            command_filter_strictness = strictness;
        }
        // unset_filter_command
        else if (phrases[0] == "unset_filter_cmd" || phrases[0] == "unset_filter_command" ||
                 phrases[0] == "<unset_filter_cmd>" || phrases[0] == "<unset_filter_command>") {
            self.check_extra_args(phrases, 1);
            reset_outline_foregroundonly();
            command_filters.reset();
        }
        else if (self.handle_setters()) { /* handled */ }
        else if (phrases[0] == end_phrase) {
            self.check_extra_args(phrases, 1);
            self.handle_end_section("substrules");
            if (self.close_db_flag) db_interface::close_db();
            return;
        }
        else {
            self.handle_invalid_phrase(phrases[0]);
        }
    }
    self.handle_unterminated_section("substrules");
}

} // namespace clitheme
