#include "data_handlers.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include <fstream>
#include <zlib.h>

namespace fs = std::filesystem;

namespace clitheme {

DataHandlers::DataHandlers(const std::string& p) : path(p), success(true) {
    if (!fs::exists(path)) fs::create_directory(path);
    datapath = path + "/" + globalvar::generator_data_pathname;
    if (!fs::exists(datapath)) fs::create_directory(datapath);
}

void DataHandlers::handle_error(const std::string& message) {
    std::string output = "Error: " + message;
    success = false;
    messages.push_back(output);
}

void DataHandlers::handle_syntax_error(const std::string& message) {
    std::string output = "Syntax error: " + message;
    success = false;
    messages.push_back(output);
    throw syntax_error(output);
}

void DataHandlers::handle_warning(const std::string& message) {
    std::string output = "Warning: " + message;
    messages.push_back(output);
}

bool DataHandlers::recursive_mkdir(const std::string& base_path, const std::string& entry_name, const std::string& line_number_debug) {
    auto parts = string_utils::split_whitespace(entry_name);
    std::string current_path = base_path;
    std::string current_entry;
    // Exclude the last part (file name)
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        current_entry += parts[i] + " ";
        current_path += "/" + parts[i];
        if (fs::is_regular_file(current_path)) {
            handle_error("Line " + line_number_debug + ": Cannot create subsection \"" +
                         string_utils::make_printable(string_utils::strip(current_entry)) +
                         "\" because an entry with the same name already exists");
            return false;
        }
        if (!fs::is_directory(current_path)) {
            fs::create_directory(current_path);
        }
    }
    return true;
}

void DataHandlers::add_entry(const std::string& base_path, const std::string& entry_name, const std::string& entry_content, const std::string& line_number_debug) {
    if (!recursive_mkdir(base_path, entry_name, line_number_debug)) return;
    std::string target_path = base_path;
    auto parts = string_utils::split_whitespace(entry_name);
    for (const auto& part : parts) {
        target_path += "/" + part;
    }
    if (fs::is_directory(target_path)) {
        handle_error("Line " + line_number_debug + ": Cannot create entry \"" +
                     string_utils::make_printable(entry_name) +
                     "\" because a subsection with the same name already exists");
    } else {
        if (fs::is_regular_file(target_path)) {
            handle_warning("Line " + line_number_debug + ": Repeated entry \"" +
                          string_utils::make_printable(entry_name) + "\", overwriting");
        }
        std::ofstream ofs(target_path);
        ofs << entry_content << "\n";
    }
}

void DataHandlers::write_infofile(const std::string& dir_path, const std::string& filename, const std::string& content, int line_number_debug, const std::string& header_name_debug) {
    if (!fs::is_directory(dir_path)) {
        fs::create_directories(dir_path);
    }
    std::string target_path = dir_path + "/" + filename;
    if (fs::is_regular_file(target_path)) {
        handle_warning("Line " + std::to_string(line_number_debug) + ": Repeated header info \"" +
                      string_utils::make_printable(header_name_debug) + "\", overwriting");
    }
    std::ofstream ofs(target_path);
    ofs << content << "\n";
}

void DataHandlers::write_infofile_newlines(const std::string& dir_path, const std::string& filename, const std::vector<std::string>& content_phrases, int line_number_debug, const std::string& header_name_debug) {
    if (!fs::is_directory(dir_path)) {
        fs::create_directories(dir_path);
    }
    std::string target_path = dir_path + "/" + filename;
    if (fs::is_regular_file(target_path)) {
        handle_warning("Line " + std::to_string(line_number_debug) + ": Repeated header info \"" +
                      string_utils::make_printable(header_name_debug) + "\", overwriting");
    }
    std::ofstream ofs(target_path);
    for (const auto& line : content_phrases) {
        ofs << line << "\n";
    }
}

void DataHandlers::write_manpage_file(const std::vector<std::string>& file_path, const std::string& content, int line_number_debug, const std::string& custom_parent_path) {
    std::string parent_path = custom_parent_path.empty()
        ? (path + "/" + globalvar::generator_manpage_pathname)
        : custom_parent_path;

    // Build subdirectory path (all parts except last)
    for (size_t i = 0; i + 1 < file_path.size(); i++) {
        parent_path += "/" + file_path[i];
    }

    try {
        fs::create_directories(parent_path);
    } catch (const std::exception&) {
        if (line_number_debug >= 0) {
            handle_error("Line " + std::to_string(line_number_debug) +
                        ": Conflicting files and subdirectories; please check previous definitions");
        }
        return;
    }

    std::string full_path = parent_path + "/" + file_path.back();
    if (fs::is_regular_file(full_path) && line_number_debug >= 0) {
        handle_warning("Line " + std::to_string(line_number_debug) + ": Repeated manpage file, overwriting");
    }

    try {
        // Write original file
        {
            std::ofstream ofs(full_path);
            ofs << content;
        }
        // Write gzip compressed version
        {
            std::string gz_path = full_path + ".gz";
            std::vector<unsigned char> input(content.begin(), content.end());
            uLongf compressed_size = compressBound(input.size());
            std::vector<unsigned char> compressed(compressed_size);

            // Use gzip format (deflateInit2 with windowBits=15+16)
            z_stream strm{};
            deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
            strm.next_in = input.data();
            strm.avail_in = input.size();
            strm.next_out = compressed.data();
            strm.avail_out = compressed_size;
            deflate(&strm, Z_FINISH);
            compressed_size = strm.total_out;
            deflateEnd(&strm);

            std::ofstream ofs(gz_path, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(compressed.data()), compressed_size);
        }
    } catch (const std::exception&) {
        if (line_number_debug >= 0) {
            handle_error("Line " + std::to_string(line_number_debug) +
                        ": Conflicting files and subdirectories; please check previous definitions");
        }
    }
}

} // namespace clitheme
