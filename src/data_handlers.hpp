#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace clitheme {

class DataHandlers {
public:
    std::string path;
    std::string datapath;
    bool success;
    std::vector<std::string> messages;

    explicit DataHandlers(const std::string& path);

    void handle_error(const std::string& message);
    void handle_syntax_error(const std::string& message);
    void handle_warning(const std::string& message);

    bool recursive_mkdir(const std::string& base_path, const std::string& entry_name, const std::string& line_number_debug);
    void add_entry(const std::string& base_path, const std::string& entry_name, const std::string& entry_content, const std::string& line_number_debug);
    void write_infofile(const std::string& dir_path, const std::string& filename, const std::string& content, int line_number_debug, const std::string& header_name_debug);
    void write_infofile_newlines(const std::string& dir_path, const std::string& filename, const std::vector<std::string>& content_phrases, int line_number_debug, const std::string& header_name_debug);
    void write_manpage_file(const std::vector<std::string>& file_path, const std::string& content, int line_number_debug, const std::string& custom_parent_path = "");
};

// Custom exception for syntax errors (used to abort parsing)
class syntax_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace clitheme
