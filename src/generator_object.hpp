#pragma once
#include "data_handlers.hpp"
#include "options.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <tuple>

namespace clitheme {

class GeneratorObject : public DataHandlers {
public:
    // Data tracking
    std::map<std::string, bool> warnings;
    std::set<size_t> parsed_lines;    // For parse_content dedup
    std::set<size_t> parsed_option_lines; // For parse_options dedup
    bool section_parsing;
    std::vector<std::string> parsed_sections;
    std::vector<std::string> lines_data;
    int lineindex;
    OptionsDict global_options;
    OptionsDict really_really_global_options;
    std::map<std::string, std::string> global_variables;
    std::map<std::string, std::string> really_really_global_variables;

    // For {entries} section
    std::string in_domainapp;
    std::string in_subsection;

    std::string custom_infofile_name;
    std::string filename;
    std::string file_content;
    std::string file_id;
    bool close_db_flag;

    GeneratorObject(const std::string& file_content, const std::string& custom_infofile_name,
                    const std::string& filename, const std::string& path, bool close_db);

    bool is_ignore_line() const;
    bool goto_next_line();
    int linenum() const;
    std::string get_current_line() const;

    void handle_invalid_phrase(const std::string& name);
    void handle_unterminated_section(const std::string& name);
    void check_enough_args(const std::vector<std::string>& phrases, int count,
                           const std::string& disp = "", bool check_processed = true);
    void check_extra_args(const std::vector<std::string>& phrases, int count,
                          const std::string& disp = "", bool check_processed = true);
    void check_version(const std::string& version_str);

    // Options parsing
    OptionsDict parse_options(const std::vector<std::string>& options_data, int merge_global_options,
                              const std::vector<std::string>* allowed_options = nullptr,
                              const std::vector<std::string>* ban_options = nullptr);
    void handle_set_global_options(const std::vector<std::string>& options_data, bool really_really_global = false);
    void handle_setup_global_options();

    // Content processing
    std::string handle_subst(const std::string& content,
                             const std::string& line_number_debug = "",
                             bool silence_warnings = false,
                             int subst_var = -1, int subst_esc = -1, int subst_chars = -1);

    // handle_linebounds returns (content, options_str_or_empty)
    std::pair<std::string, std::string> handle_linebounds(const std::string& content,
                                                           int condition = -1,
                                                           bool preserve_indents = true,
                                                           bool allow_options = true,
                                                           int debug_linenumber = -1,
                                                           bool silence_warn = false);

    void handle_set_variable(const std::vector<std::string>& var_names, const std::string& var_content, bool really_really_global = false);

    void handle_begin_section(const std::string& section_name);
    void handle_end_section(const std::string& section_name);

    std::string handle_linenumber_range(int begin, int end);

    // parse_content: pure_name: 0=false, 1=true, 2=disable linebounds
    std::string parse_content(const std::string& content, int pure_name = 0,
                              int preserve_indents = -1, bool ignore_options = false);

    struct ParseContentResult {
        std::string content;
        OptionsDict options;
        OptionsDict inline_options;
    };
    ParseContentResult parse_content_with_options(const std::string& content,
                                                   const std::vector<std::string>& extra_options = {},
                                                   int pure_name = 0,
                                                   int preserve_indents = -1,
                                                   bool ignore_options = false);

    bool handle_setters(bool really_really_global = false);

    // Block input processing
    std::vector<std::string> handle_block_input_splitlines(bool preserve_indents, bool preserve_empty_lines,
                                                            const std::string& end_phrase,
                                                            bool disallow_other_options = true,
                                                            bool disable_char_subst = false);
    std::string handle_block_input(bool preserve_indents, bool preserve_empty_lines,
                                    const std::string& end_phrase,
                                    const std::string& line_separator = "\n",
                                    bool disallow_other_options = true,
                                    bool disable_char_subst = false);

    // Entry/subst block handler
    struct SubstrulesOptions {
        std::optional<std::vector<std::string>> effective_commands;
        bool command_is_regex;
        bool is_regex;
        int strictness;
    };
    static SubstrulesOptions make_default_substrules_opts();
    void handle_entry(const std::string& start_phrase, const std::string& end_phrase,
                      bool is_substrules = false, const SubstrulesOptions& substrules_opts = make_default_substrules_opts());

private:
    // UUID generation
    static int hash_index_;
    static std::string gen_uuid();
};

} // namespace clitheme
