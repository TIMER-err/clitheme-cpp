#include "section_entries.hpp"
#include "generator_object.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include "sanity_check.hpp"
#include <regex>

namespace clitheme {

void handle_entries_section(GeneratorObject& self, const std::string& end_phrase) {
    self.handle_begin_section("entries");
    self.in_domainapp = "";
    self.in_subsection = "";

    while (self.goto_next_line()) {
        auto phrases = string_utils::split_whitespace(self.get_current_line());
        if (phrases.empty()) continue;

        if (phrases[0] == "<in_domainapp>" || phrases[0] == "in_domainapp") {
            self.check_enough_args(phrases, 3);
            self.check_extra_args(phrases, 3);
            auto this_phrases = string_utils::split_whitespace(
                self.parse_content(string_utils::extract_content(self.get_current_line()), 1));
            // Should have exactly 2 parts (domain + app)
            if (this_phrases.size() == 2) {
                self.in_domainapp = this_phrases[0] + " " + this_phrases[1];
            } else {
                self.in_domainapp = string_utils::join(this_phrases, " ");
            }
            if (!sanity_check::check(self.in_domainapp)) {
                self.handle_error("Line " + std::to_string(self.linenum()) +
                                 ": Domain and app names " + sanity_check::error_message);
                self.in_domainapp = sanity_check::sanitize_str(self.in_domainapp);
            }
            self.in_subsection = "";
        }
        else if (phrases[0] == "<in_subsection>" || phrases[0] == "in_subsection") {
            self.check_enough_args(phrases, 2);
            self.in_subsection = self.parse_content(string_utils::extract_content(self.get_current_line()), 1);
            // Remove extra spaces
            self.in_subsection = string_utils::join(string_utils::split_whitespace(self.in_subsection), " ");
            if (!sanity_check::check(self.in_subsection)) {
                self.handle_error("Line " + std::to_string(self.linenum()) +
                                 ": Subsection names " + sanity_check::error_message);
                self.in_subsection = sanity_check::sanitize_str(self.in_subsection);
            }
        }
        else if (phrases[0] == "<unset_domainapp>" || phrases[0] == "unset_domainapp") {
            self.check_extra_args(phrases, 1);
            self.in_domainapp = "";
            self.in_subsection = "";
        }
        else if (phrases[0] == "<unset_subsection>" || phrases[0] == "unset_subsection") {
            self.check_extra_args(phrases, 1);
            self.in_subsection = "";
        }
        else if (phrases[0] == "[entry]" || phrases[0] == "entry") {
            std::string ep = (phrases[0] == "[entry]") ? "[/entry]" : "end_entry";
            self.handle_entry(phrases[0], ep);
        }
        else if (self.handle_setters()) { /* handled */ }
        else if (phrases[0] == end_phrase) {
            self.check_extra_args(phrases, 1);
            self.handle_end_section("entries");
            if (phrases[0] == "end_main") {
                self.handle_warning("Line " + std::to_string(self.linenum()) +
                    ": Phrase \"end_main\" is deprecated in this version; please use \"{/entries}\" instead");
            }
            return;
        }
        else {
            self.handle_invalid_phrase(phrases[0]);
        }
    }
    self.handle_unterminated_section("entries");
}

} // namespace clitheme
