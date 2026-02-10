#include "section_manpages.hpp"
#include "generator_object.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include "sanity_check.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace clitheme {

void handle_manpage_section(GeneratorObject& self, const std::string& end_phrase) {
    self.handle_begin_section("manpages");

    auto get_file_content = [&](const std::vector<std::string>& filepath) -> std::string {
        std::string parent_dir;
        if (!string_utils::strip(self.filename).empty()) {
            parent_dir = fs::path(self.filename).parent_path().string();
        }
        std::string file_dir = parent_dir;
        if (!file_dir.empty()) file_dir += "/";
        // Build path from filepath parts
        for (size_t i = 0; i < filepath.size(); i++) {
            if (i > 0) file_dir += "/";
            file_dir += filepath[i];
        }

        try {
            std::ifstream ifs(file_dir);
            if (!ifs.is_open()) throw std::runtime_error("Cannot open file");
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            // Write manpage in theme-info for migration
            self.write_manpage_file(filepath, content, -1,
                self.path + "/" + globalvar::generator_info_pathname + "/" +
                self.custom_infofile_name + "/manpage_data");
            return content;
        } catch (const std::exception& e) {
            self.handle_error("Line " + std::to_string(self.linenum()) +
                             ": Unable to read file \"" + string_utils::make_printable(file_dir) +
                             "\":\n" + string_utils::make_printable(e.what()));
            return "";
        }
    };

    while (self.goto_next_line()) {
        auto phrases = string_utils::split_whitespace(self.get_current_line());
        if (phrases.empty()) continue;

        if (phrases[0] == "[file_content]") {
            struct FilePath {
                std::vector<std::string> path;
                int line_number;
            };

            auto handle_fp = [&](const std::vector<std::string>& p) -> FilePath {
                self.check_enough_args(p, 2);
                auto filepath = string_utils::split_whitespace(
                    self.parse_content(string_utils::join(std::vector<std::string>(p.begin() + 1, p.end()), " "), 1));
                if (!sanity_check::check(string_utils::join(filepath, " "))) {
                    self.handle_error("Line " + std::to_string(self.linenum()) +
                                     ": Manpage paths " + sanity_check::error_message +
                                     "; use spaces to denote subdirectories");
                    for (auto& fp : filepath) fp = sanity_check::sanitize_str(fp);
                }
                return {filepath, self.linenum()};
            };

            std::vector<FilePath> file_paths;
            file_paths.push_back(handle_fp(phrases));

            // Handle additional [file_content] phrases
            int prev_line_index = self.lineindex;
            while (self.goto_next_line()) {
                auto p = string_utils::split_whitespace(self.get_current_line());
                if (!p.empty() && p[0] == "[file_content]") {
                    prev_line_index = self.lineindex;
                    file_paths.push_back(handle_fp(p));
                } else {
                    self.lineindex = prev_line_index;
                    break;
                }
            }

            std::string content = self.handle_block_input(true, true, "[/file_content]");
            for (const auto& fp : file_paths) {
                self.write_manpage_file(fp.path, content, fp.line_number);
            }
        }
        else if (phrases[0] == "<include_file>" || phrases[0] == "include_file") {
            self.check_enough_args(phrases, 2);
            auto filepath = string_utils::split_whitespace(
                self.parse_content(string_utils::join(std::vector<std::string>(phrases.begin() + 1, phrases.end()), " "), 1));
            if (!sanity_check::check(string_utils::join(filepath, " "))) {
                self.handle_error("Line " + std::to_string(self.linenum()) +
                                 ": Manpage paths " + sanity_check::error_message +
                                 "; use spaces to denote subdirectories");
                for (auto& fp : filepath) fp = sanity_check::sanitize_str(fp);
            }

            std::string filecontent = get_file_content(filepath);
            if (self.goto_next_line()) {
                auto next_phrases = string_utils::split_whitespace(self.get_current_line());
                if (!next_phrases.empty() && (next_phrases[0] == "as:" || next_phrases[0] == "as")) {
                    auto target_file = string_utils::split_whitespace(
                        self.parse_content(string_utils::join(std::vector<std::string>(next_phrases.begin() + 1, next_phrases.end()), " "), 1));
                    if (!sanity_check::check(string_utils::join(target_file, " "))) {
                        self.handle_error("Line " + std::to_string(self.linenum()) +
                                         ": Manpage paths " + sanity_check::error_message +
                                         "; use spaces to denote subdirectories");
                        for (auto& fp : target_file) fp = sanity_check::sanitize_str(fp);
                    }
                    self.write_manpage_file(target_file, filecontent, self.linenum());
                } else {
                    self.handle_error("Line " + std::to_string(self.linenum() - 1) +
                                     ": Missing \"as <filename>\" phrase on next line");
                    self.lineindex--;
                }
            }
        }
        else if (phrases[0] == "[include_file]") {
            self.check_enough_args(phrases, 2);
            auto filepath = string_utils::split_whitespace(
                self.parse_content(string_utils::join(std::vector<std::string>(phrases.begin() + 1, phrases.end()), " "), 1));
            if (!sanity_check::check(string_utils::join(filepath, " "))) {
                self.handle_error("Line " + std::to_string(self.linenum()) +
                                 ": Manpage paths " + sanity_check::error_message +
                                 "; use spaces to denote subdirectories");
                for (auto& fp : filepath) fp = sanity_check::sanitize_str(fp);
            }
            std::string filecontent = get_file_content(filepath);

            while (self.goto_next_line()) {
                auto p = string_utils::split_whitespace(self.get_current_line());
                if (!p.empty() && (p[0] == "as:" || p[0] == "as")) {
                    self.check_enough_args(p, 2);
                    auto target_file = string_utils::split_whitespace(
                        self.parse_content(string_utils::join(std::vector<std::string>(p.begin() + 1, p.end()), " "), 1));
                    if (!sanity_check::check(string_utils::join(target_file, " "))) {
                        self.handle_error("Line " + std::to_string(self.linenum()) +
                                         ": Manpage paths " + sanity_check::error_message +
                                         "; use spaces to denote subdirectories");
                        for (auto& fp : target_file) fp = sanity_check::sanitize_str(fp);
                    }
                    self.write_manpage_file(target_file, filecontent, self.linenum());
                } else if (!p.empty() && p[0] == "[/include_file]") {
                    self.check_extra_args(p, 1);
                    break;
                } else {
                    if (!p.empty()) self.handle_invalid_phrase(p[0]);
                }
            }
        }
        else if (self.handle_setters()) { /* handled */ }
        else if (phrases[0] == end_phrase) {
            self.check_extra_args(phrases, 1);
            self.handle_end_section("manpages");
            return;
        }
        else {
            self.handle_invalid_phrase(phrases[0]);
        }
    }
    self.handle_unterminated_section("manpages");
}

} // namespace clitheme
