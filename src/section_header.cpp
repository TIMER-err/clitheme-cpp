#include "section_header.hpp"
#include "generator_object.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include <regex>

namespace clitheme {

void handle_header_section(GeneratorObject& self, const std::string& end_phrase) {
    self.handle_begin_section("header");
    bool name_specified = false;

    while (self.goto_next_line()) {
        auto phrases = string_utils::split_whitespace(self.get_current_line());
        if (phrases.empty()) continue;

        std::smatch last_match;
        auto match_first_phrase = [&](const std::string& pattern) -> bool {
            return std::regex_match(phrases[0], last_match, std::regex(pattern));
        };

        if (match_first_phrase(R"((name|version|description)(:)?)")) {
            self.check_enough_args(phrases, 2);
            std::string entry = last_match[1].str();
            std::string content = self.parse_content(
                string_utils::extract_content(self.get_current_line()), 1,
                (entry == "name" || entry == "description") ? 1 : 0);
            self.write_infofile(
                self.path + "/" + globalvar::generator_info_pathname + "/" + self.custom_infofile_name,
                globalvar::format_info_filename(entry),
                content, self.linenum(), entry);
            if (entry == "name") name_specified = true;
        }
        else if (match_first_phrase(R"((locales|supported_apps)(:)?)")) {
            self.check_enough_args(phrases, 2);
            std::string entry = last_match[1].str();
            auto content_parts = string_utils::split_whitespace(
                self.parse_content(
                    string_utils::join(std::vector<std::string>(phrases.begin() + 1, phrases.end()), " "), 1));
            self.write_infofile_newlines(
                self.path + "/" + globalvar::generator_info_pathname + "/" + self.custom_infofile_name,
                globalvar::format_info_v2filename(entry),
                content_parts, self.linenum(), entry);
        }
        else if (phrases[0] == "[locales]" || phrases[0] == "[supported_apps]" || phrases[0] == "[description]" ||
                 phrases[0] == "locales_block" || phrases[0] == "supported_apps_block" || phrases[0] == "description_block") {
            self.check_extra_args(phrases, 1);
            std::string endphrase;
            if (!string_utils::ends_with(phrases[0], "_block")) {
                // [x] -> [/x]
                endphrase = std::regex_replace(phrases[0], std::regex(R"(\[)"), "[/");
            } else {
                endphrase = "end_block";
            }

            bool is_description = (phrases[0] == "description_block" || phrases[0] == "[description]");
            std::string content = self.handle_block_input(is_description, is_description, endphrase, "\n", true, true);

            // Determine filename
            std::string base_name = std::regex_replace(phrases[0], std::regex(R"(_block$)"), "");
            base_name = string_utils::replace_all(base_name, "[", "");
            base_name = string_utils::replace_all(base_name, "]", "");
            std::string file_name = is_description
                ? globalvar::format_info_filename(base_name)
                : globalvar::format_info_v2filename(base_name);

            std::string debug_name = std::regex_replace(phrases[0], std::regex(R"(_block$)"), "");
            self.write_infofile(
                self.path + "/" + globalvar::generator_info_pathname + "/" + self.custom_infofile_name,
                file_name, content, self.linenum(), debug_name);
        }
        else if (self.handle_setters()) { /* handled */ }
        else if (phrases[0] == end_phrase) {
            self.check_extra_args(phrases, 1);
            if (!name_specified) {
                self.handle_error("header section missing required entries: name");
            }
            self.handle_end_section("header");
            return;
        }
        else {
            self.handle_invalid_phrase(phrases[0]);
        }
    }
    self.handle_unterminated_section("header");
}

} // namespace clitheme
