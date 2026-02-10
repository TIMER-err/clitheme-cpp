#include "globalvar.hpp"
#include "generator_object.hpp"
#include "section_header.hpp"
#include "section_entries.hpp"
#include "section_substrules.hpp"
#include "section_manpages.hpp"
#include "db_interface.hpp"
#include "substrules_processor.hpp"
#include "exec_handler.hpp"
#include "string_utils.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <random>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

static void print_usage() {
    std::cerr << "Usage:\n"
              << "  clitheme-cpp generate <file> [options]\n"
              << "  clitheme-cpp exec [options] <command> [args...]\n"
              << "\nGenerate options:\n"
              << "  --output-path <path>    Output directory (default: auto-generated temp dir)\n"
              << "  --overlay               Overlay mode\n"
              << "  --infofile-name <name>  Theme info subdirectory name (default: \"1\")\n"
              << "\nExec options:\n"
              << "  --db-path <path>        Database path (default: ~/.local/share/clitheme/subst-data.db)\n";
}

static std::string generate_temp_path() {
    std::string path = clitheme::globalvar::get_temp_root() + "/clitheme-temp-";
    static const char alphanum[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);
    for (int i = 0; i < 8; i++) {
        path += alphanum[dis(gen)];
    }
    return path;
}

struct GenerateResult {
    bool success;
    std::string dir_path;
    std::vector<std::string> messages;
};

static GenerateResult generate_data_hierarchy(
    const std::string& file_content,
    const std::string& path,
    const std::string& custom_infofile_name,
    const std::string& filename,
    bool close_db = true
) {
    using namespace clitheme;

    GeneratorObject self(file_content, custom_infofile_name, filename, path, close_db);

    // Record file content for database migration
    self.write_infofile(
        self.path + "/" + globalvar::generator_info_pathname + "/" + self.custom_infofile_name,
        "file_content", self.file_content, self.linenum(), "<file_content>");

    // Record full file path for update-themes feature
    self.write_infofile(
        self.path + "/" + globalvar::generator_info_pathname + "/" + self.custom_infofile_name,
        globalvar::format_info_filename("filepath"),
        fs::absolute(filename).string(), self.linenum(), "<filepath>");

    // Update current theme index
    {
        std::string index_path = self.path + "/" + globalvar::generator_info_pathname + "/" + globalvar::generator_index_filename;
        std::ofstream ofs(index_path);
        ofs << self.custom_infofile_name << "\n";
    }

    try {
        bool before_content_lines = true;
        while (self.goto_next_line()) {
            auto phrases = string_utils::split_whitespace(self.get_current_line());
            if (phrases.empty()) continue;
            std::string first_phrase = phrases[0];
            bool is_content = true;

            auto end_phrase = [&]() -> std::string {
                if (first_phrase[0] == '{') {
                    return std::regex_replace(first_phrase, std::regex(R"(^\{)"), "{/");
                } else if (string_utils::starts_with(first_phrase, "begin_")) {
                    return std::regex_replace(first_phrase, std::regex(R"(^begin_)"), "end_");
                }
                return "";
            };

            if (std::regex_match(first_phrase, std::regex(R"(\{header(_section)?\}|begin_header)"))) {
                self.check_extra_args(phrases, 1);
                handle_header_section(self, end_phrase());
            }
            else if (std::regex_match(first_phrase, std::regex(R"(\{entries(_section)?\}|begin_main)"))) {
                self.check_extra_args(phrases, 1);
                if (first_phrase == "begin_main") {
                    self.handle_warning("Line " + std::to_string(self.linenum()) +
                        ": Phrase \"begin_main\" is deprecated in this version; please use \"{entries}\" instead");
                }
                handle_entries_section(self, end_phrase());
            }
            else if (std::regex_match(first_phrase, std::regex(R"(\{substrules(_section)?\})"))) {
                self.check_extra_args(phrases, 1);
                handle_substrules_section(self, end_phrase());
            }
            else if (std::regex_match(first_phrase, std::regex(R"(\{(manpages|manpage_section)\})"))) {
                self.check_extra_args(phrases, 1);
                handle_manpage_section(self, end_phrase());
            }
            else if (self.handle_setters(true)) { /* handled */ }
            else if (first_phrase == "!require_version") {
                is_content = false;
                self.check_enough_args(phrases, 2);
                self.check_extra_args(phrases, 2);
                if (!before_content_lines) {
                    self.handle_error("Line " + std::to_string(self.linenum()) +
                        ": Header macro \"" + first_phrase + "\" must be specified before other lines");
                } else {
                    self.check_version(phrases[1]);
                }
            }
            else {
                self.handle_invalid_phrase(first_phrase);
            }

            if (is_content) before_content_lines = false;
        }

        // Check completeness
        auto has_content = [&]() -> bool {
            for (const auto& s : self.parsed_sections) {
                if (s == "entries" || s == "substrules" || s == "manpages") return true;
            }
            return false;
        };

        bool has_header = false;
        for (const auto& s : self.parsed_sections) {
            if (s == "header") { has_header = true; break; }
        }

        if (self.section_parsing || !has_header || !has_content()) {
            self.handle_error("Missing or incomplete header or content sections");
        }
    } catch (const syntax_error&) {
        // Parsing aborted
    }

    return {self.success, path, self.messages};
}

static int cmd_generate(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: missing file argument\n";
        print_usage();
        return 1;
    }

    std::string filename = argv[2];
    std::string output_path;
    std::string infofile_name = "1";
    bool overlay = false;

    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--output-path" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--overlay") {
            overlay = true;
        } else if (arg == "--infofile-name" && i + 1 < argc) {
            infofile_name = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    // Read file
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "Error: cannot open file \"" << filename << "\"\n";
        return 1;
    }
    std::string file_content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
    ifs.close();

    if (output_path.empty()) {
        output_path = generate_temp_path();
    }

    auto result = generate_data_hierarchy(file_content, output_path, infofile_name, filename);

    // Print messages
    for (const auto& msg : result.messages) {
        std::cerr << msg << "\n";
    }

    if (result.success) {
        std::cout << result.dir_path << "\n";
        return 0;
    }
    return 1;
}

static int cmd_exec(int argc, char* argv[]) {
    std::string db_path;
    int cmd_start = -1;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--db-path" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else {
            cmd_start = i;
            break;
        }
    }

    if (cmd_start < 0) {
        std::cerr << "Error: missing command argument\n";
        print_usage();
        return 1;
    }

    if (!db_path.empty()) {
        clitheme::db_interface::set_db_path(db_path);
    }

    try {
        clitheme::db_interface::connect_db();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::vector<std::string> command_argv;
    for (int i = cmd_start; i < argc; i++) {
        command_argv.push_back(argv[i]);
    }

    try {
        clitheme::ExecHandler handler(command_argv);
        int exit_code = handler.run();
        clitheme::db_interface::close_db();
        return exit_code;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        clitheme::db_interface::close_db();
        return 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string subcommand = argv[1];
    if (subcommand == "generate") {
        return cmd_generate(argc, argv);
    } else if (subcommand == "exec") {
        return cmd_exec(argc, argv);
    } else if (subcommand == "--help" || subcommand == "-h") {
        print_usage();
        return 0;
    } else {
        std::cerr << "Unknown subcommand: " << subcommand << "\n";
        print_usage();
        return 1;
    }
}
