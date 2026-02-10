#pragma once
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <stdexcept>
#include <sqlite3.h>

namespace clitheme {
namespace db_interface {

// Substitution rule item (matches Python db_interface.Item)
struct Item {
    std::string match_pattern;
    bool match_is_multiline;
    std::string substitute_pattern;
    bool is_regex;

    std::optional<std::string> effective_locale;
    std::optional<std::string> effective_command;
    int command_match_strictness; // 0: contains all, 1: starts with, 2: equal to, -1: smartcmdmatch
    bool command_is_regex;

    bool foreground_only;
    bool end_match_here;
    int stdout_stderr_only; // 0=both, 1=stdout, 2=stderr

    std::string unique_id;
    std::string file_id;
};

// Exceptions
class need_db_regenerate : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class bad_pattern : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class db_not_found : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Database management
void init_db(const std::string& file_path);
void connect_db(const std::string& path = "");
void close_db();

// Add a substitution entry
void add_subst_entry(
    const std::string& match_pattern,
    const std::string& substitute_pattern,
    const std::optional<std::vector<std::string>>& effective_commands,
    int command_match_strictness,
    bool command_is_regex,
    const std::optional<std::string>& effective_locale,
    bool is_regex,
    bool match_is_multiline,
    bool end_match_here,
    int stdout_stderr_matchoption,
    bool foreground_only,
    const std::string& unique_id,
    const std::string& file_id,
    const std::string& line_number_debug,
    std::function<void(const std::string&)> warning_handler
);

// Fetch substitution rules for a command
std::vector<Item> fetch_substrules(const std::optional<std::string>& command);

// Check if a command matches a filter pattern
bool check_command(const std::string& match_cmd, int strictness, const std::string& target_command, bool is_regex);

// Get/set the database path
void set_db_path(const std::string& path);
std::string get_db_path();

// Check if connection is active
bool is_connected();

} // namespace db_interface
} // namespace clitheme
