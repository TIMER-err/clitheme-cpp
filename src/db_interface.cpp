#include "db_interface.hpp"
#include "globalvar.hpp"
#include "locale_detect.hpp"
#include "string_utils.hpp"
#include <filesystem>
#include <regex>
#include <cassert>
#include <stdexcept>

namespace fs = std::filesystem;

namespace clitheme {
namespace db_interface {

static sqlite3* connection = nullptr;
static std::string db_path;
static bool db_path_initialized = false;

static std::string get_default_db_path() {
    return globalvar::get_root_data_path() + "/" + globalvar::db_filename;
}

void set_db_path(const std::string& path) {
    db_path = path;
    db_path_initialized = true;
}

std::string get_db_path() {
    if (!db_path_initialized) {
        db_path = get_default_db_path();
        db_path_initialized = true;
    }
    return db_path;
}

bool is_connected() {
    return connection != nullptr;
}

// Helper: execute a SQL statement
static void exec_sql(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string error = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        throw std::runtime_error("SQL error: " + error);
    }
}

void init_db(const std::string& file_path) {
    assert(!fs::exists(file_path) && "Database file already exists");
    close_db();
    db_path = file_path;
    db_path_initialized = true;

    int rc = sqlite3_open(file_path.c_str(), &connection);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(connection)));
    }

    // Create main table
    std::string create_sql = "CREATE TABLE " + globalvar::db_data_tablename + " ("
        "match_pattern TEXT NOT NULL,"
        "match_is_multiline INTEGER NOT NULL,"
        "substitute_pattern TEXT NOT NULL,"
        "is_regex INTEGER NOT NULL,"
        "effective_locale TEXT,"
        "effective_command TEXT,"
        "command_match_strictness INTEGER NOT NULL,"
        "command_is_regex INTEGER NOT NULL,"
        "foreground_only INTEGER NOT NULL,"
        "end_match_here INTEGER NOT NULL,"
        "stdout_stderr_only INTEGER NOT NULL,"
        "unique_id TEXT NOT NULL,"
        "file_id TEXT NOT NULL"
        ");";
    exec_sql(create_sql);

    // Create version table
    exec_sql("CREATE TABLE " + globalvar::db_data_tablename + "_version (value INTEGER NOT NULL);");

    // Insert version
    std::string insert_sql = "INSERT INTO " + globalvar::db_data_tablename + "_version (value) VALUES (" + std::to_string(globalvar::db_version) + ");";
    exec_sql(insert_sql);

    sqlite3_exec(connection, "COMMIT;", nullptr, nullptr, nullptr);
}

void connect_db(const std::string& path) {
    std::string target_path = path.empty() ? get_db_path() : path;
    if (!path.empty()) {
        db_path = path;
        db_path_initialized = true;
    }

    if (!fs::exists(target_path)) {
        throw db_not_found("No theme set or theme does not contain substrules");
    }

    close_db();
    int rc = sqlite3_open(target_path.c_str(), &connection);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(connection)));
    }

    // Check db version
    try {
        sqlite3_stmt* stmt;
        std::string sql = "SELECT value FROM " + globalvar::db_data_tablename + "_version";
        rc = sqlite3_prepare_v2(connection, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("version check failed");
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("no version row");
        }
        int version = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        if (version != globalvar::db_version) {
            throw std::runtime_error("version mismatch");
        }
    } catch (...) {
        close_db();
        throw need_db_regenerate("Database version mismatch");
    }
}

void close_db() {
    if (connection != nullptr) {
        sqlite3_exec(connection, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_close(connection);
        connection = nullptr;
    }
}

// Helper: regex_replace multiple spaces with single space and strip
static std::string normalize_command(const std::string& cmd) {
    std::string result = std::regex_replace(cmd, std::regex(" {2,}"), " ");
    return string_utils::strip(result);
}

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
) {
    assert(connection != nullptr && "No active database connection");

    // Validate match pattern
    try { std::regex(match_pattern); }
    catch (...) { throw std::runtime_error("Uncaught bad match pattern"); }

    // If is_regex, test substitution
    if (is_regex) {
        try { std::regex_replace(std::string(""), std::regex(match_pattern), substitute_pattern); }
        catch (const std::exception& e) { throw bad_pattern(e.what()); }
    }

    std::string locale_condition = effective_locale.has_value()
        ? "effective_locale=?"
        : "typeof(effective_locale)=typeof(?)";

    std::vector<std::optional<std::string>> cmdlist;
    if (effective_commands.has_value() && !effective_commands->empty()) {
        for (const auto& cmd : *effective_commands) {
            cmdlist.push_back(normalize_command(cmd));
        }
    } else {
        cmdlist.push_back(std::nullopt);
    }

    for (const auto& cmd : cmdlist) {
        std::string cmd_condition = cmd.has_value()
            ? "effective_command=?"
            : "typeof(effective_command)=typeof(?)";

        std::string match_condition = "match_pattern=? AND " + cmd_condition +
            " AND command_is_regex=? AND 1 AND " + locale_condition +
            " AND stdout_stderr_only=? AND is_regex=?";

        // Check for existing entries
        std::string select_sql = "SELECT COUNT(*) FROM " + globalvar::db_data_tablename +
            " WHERE " + match_condition + ";";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(connection, select_sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        sqlite3_bind_text(stmt, idx++, match_pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (cmd.has_value())
            sqlite3_bind_text(stmt, idx++, cmd->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, idx++);
        sqlite3_bind_int(stmt, idx++, command_is_regex ? 1 : 0);
        if (effective_locale.has_value())
            sqlite3_bind_text(stmt, idx++, effective_locale->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, idx++);
        sqlite3_bind_int(stmt, idx++, stdout_stderr_matchoption);
        sqlite3_bind_int(stmt, idx++, is_regex ? 1 : 0);

        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);

        if (count > 0) {
            warning_handler("Line " + line_number_debug + ": Repeated substrules entry, overwriting");

            // Delete existing
            std::string delete_sql = "DELETE FROM " + globalvar::db_data_tablename +
                " WHERE " + match_condition + ";";
            sqlite3_prepare_v2(connection, delete_sql.c_str(), -1, &stmt, nullptr);
            idx = 1;
            sqlite3_bind_text(stmt, idx++, match_pattern.c_str(), -1, SQLITE_TRANSIENT);
            if (cmd.has_value())
                sqlite3_bind_text(stmt, idx++, cmd->c_str(), -1, SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(stmt, idx++);
            sqlite3_bind_int(stmt, idx++, command_is_regex ? 1 : 0);
            if (effective_locale.has_value())
                sqlite3_bind_text(stmt, idx++, effective_locale->c_str(), -1, SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(stmt, idx++);
            sqlite3_bind_int(stmt, idx++, stdout_stderr_matchoption);
            sqlite3_bind_int(stmt, idx++, is_regex ? 1 : 0);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // Insert new entry
        std::string insert_sql = "INSERT INTO " + globalvar::db_data_tablename +
            " (match_pattern, match_is_multiline, substitute_pattern, is_regex,"
            " effective_locale, effective_command, command_match_strictness, command_is_regex,"
            " foreground_only, end_match_here, stdout_stderr_only, unique_id, file_id)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?);";

        sqlite3_prepare_v2(connection, insert_sql.c_str(), -1, &stmt, nullptr);
        idx = 1;
        sqlite3_bind_text(stmt, idx++, match_pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, idx++, match_is_multiline ? 1 : 0);
        sqlite3_bind_text(stmt, idx++, substitute_pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, idx++, is_regex ? 1 : 0);
        if (effective_locale.has_value())
            sqlite3_bind_text(stmt, idx++, effective_locale->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, idx++);
        if (cmd.has_value())
            sqlite3_bind_text(stmt, idx++, cmd->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, idx++);
        sqlite3_bind_int(stmt, idx++, command_match_strictness);
        sqlite3_bind_int(stmt, idx++, command_is_regex ? 1 : 0);
        sqlite3_bind_int(stmt, idx++, foreground_only ? 1 : 0);
        sqlite3_bind_int(stmt, idx++, end_match_here ? 1 : 0);
        sqlite3_bind_int(stmt, idx++, stdout_stderr_matchoption);
        sqlite3_bind_text(stmt, idx++, unique_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, idx++, file_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// Parse a row into an Item
static Item row_to_item(sqlite3_stmt* stmt) {
    Item item;
    item.match_pattern = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    item.match_is_multiline = sqlite3_column_int(stmt, 1) != 0;
    item.substitute_pattern = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    item.is_regex = sqlite3_column_int(stmt, 3) != 0;

    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
        item.effective_locale = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
        item.effective_command = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

    item.command_match_strictness = sqlite3_column_int(stmt, 6);
    item.command_is_regex = sqlite3_column_int(stmt, 7) != 0;
    item.foreground_only = sqlite3_column_int(stmt, 8) != 0;
    item.end_match_here = sqlite3_column_int(stmt, 9) != 0;
    item.stdout_stderr_only = sqlite3_column_int(stmt, 10);
    item.unique_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    item.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    return item;
}

bool check_command(const std::string& match_cmd, int strictness, const std::string& target_command, bool is_regex_mode) {
    auto target_parts = string_utils::split_whitespace(target_command);
    if (target_parts.empty()) return false;

    std::string first_phrase = target_parts[0];
    // Valid first phrases: full path, basename, basename without extension
    std::vector<std::string> valid_first_phrases;
    valid_first_phrases.push_back(first_phrase);
    {
        fs::path p(first_phrase);
        std::string basename = p.filename().string();
        valid_first_phrases.push_back(basename);
        // Remove common extensions
        std::string no_ext = std::regex_replace(basename, std::regex(R"((\.(exe|com|ps1|bat|sh))$)"), "");
        valid_first_phrases.push_back(no_ext);
    }

    if (is_regex_mode) {
        for (const auto& fp : valid_first_phrases) {
            std::string test_cmd = fp;
            for (size_t i = 1; i < target_parts.size(); i++) {
                test_cmd += " " + target_parts[i];
            }
            try {
                if (std::regex_search(test_cmd, std::regex("^" + match_cmd))) {
                    return true;
                }
            } catch (...) {}
        }
        return false;
    }

    auto match_parts = string_utils::split_whitespace(match_cmd);
    if (match_parts.empty()) return false;

    // Check first phrase
    bool first_match = false;
    for (const auto& fp : valid_first_phrases) {
        if (fp == match_parts[0]) { first_match = true; break; }
    }
    if (!first_match) return false;

    auto process_smartcmdmatch = [](const std::vector<std::string>& parts) {
        std::vector<std::string> result;
        for (size_t i = 0; i < parts.size(); i++) {
            const auto& ph = parts[i];
            std::smatch m;
            if (i > 0 && std::regex_match(ph, m, std::regex("^-([^-]+)$"))) {
                for (char c : m[1].str()) {
                    result.push_back(std::string("-") + c);
                }
            } else {
                result.push_back(ph);
            }
        }
        return result;
    };

    if (strictness == 1) {
        // Must start with pattern
        if (match_parts.size() > target_parts.size()) return false;
        for (size_t i = 1; i < match_parts.size(); i++) {
            if (target_parts[i] != match_parts[i]) return false;
        }
        return true;
    } else if (strictness == 2) {
        // Must equal
        if (match_parts.size() != target_parts.size()) return false;
        for (size_t i = 1; i < match_parts.size(); i++) {
            if (target_parts[i] != match_parts[i]) return false;
        }
        return true;
    } else if (strictness == -1) {
        // Smart cmd match
        auto match_expanded = process_smartcmdmatch(match_parts);
        auto target_expanded = process_smartcmdmatch(target_parts);
        for (size_t i = 1; i < match_expanded.size(); i++) {
            bool found = false;
            for (size_t j = 1; j < target_expanded.size(); j++) {
                if (target_expanded[j] == match_expanded[i]) { found = true; break; }
            }
            if (!found) return false;
        }
        return true;
    } else {
        // strictness == 0: must contain all phrases
        for (size_t i = 1; i < match_parts.size(); i++) {
            bool found = false;
            for (size_t j = 1; j < target_parts.size(); j++) {
                if (target_parts[j] == match_parts[i]) { found = true; break; }
            }
            if (!found) return false;
        }
        return true;
    }
}

// Internal: get matches from database for a command
static std::vector<Item> get_matches(const std::optional<std::string>& command) {
    assert(connection != nullptr);

    auto locales = locale_detect::get_locale();
    std::vector<Item> match_items;

    // Get all unique entry IDs
    std::string sql = "SELECT DISTINCT unique_id FROM " + globalvar::db_data_tablename;
    sqlite3_stmt* id_stmt;
    sqlite3_prepare_v2(connection, sql.c_str(), -1, &id_stmt, nullptr);

    std::vector<std::string> entry_ids;
    while (sqlite3_step(id_stmt) == SQLITE_ROW) {
        entry_ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(id_stmt, 0)));
    }
    sqlite3_finalize(id_stmt);

    // Column list
    std::string columns = "match_pattern, match_is_multiline, substitute_pattern, is_regex,"
        " effective_locale, effective_command, command_match_strictness, command_is_regex,"
        " foreground_only, end_match_here, stdout_stderr_only, unique_id, file_id";

    for (const auto& eid : entry_ids) {
        bool fetched = false;
        // Try locales in order, then default (null)
        std::vector<std::optional<std::string>> locale_list;
        for (const auto& l : locales) locale_list.push_back(l);
        locale_list.push_back(std::nullopt); // "default" locale

        for (const auto& locale : locale_list) {
            if (fetched) break;

            std::string locale_condition = locale.has_value()
                ? "effective_locale=?"
                : "typeof(effective_locale)=typeof(?)";

            std::string fetch_sql = "SELECT " + columns + " FROM " + globalvar::db_data_tablename +
                " WHERE unique_id=? AND " + locale_condition + ";";

            sqlite3_stmt* stmt;
            sqlite3_prepare_v2(connection, fetch_sql.c_str(), -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, eid.c_str(), -1, SQLITE_TRANSIENT);
            if (locale.has_value())
                sqlite3_bind_text(stmt, 2, locale->c_str(), -1, SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(stmt, 2);

            std::vector<Item> fetches;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                fetches.push_back(row_to_item(stmt));
            }
            sqlite3_finalize(stmt);

            if (!fetches.empty()) {
                for (const auto& item : fetches) {
                    // Filter by command
                    if (command.has_value() && item.effective_command.has_value()) {
                        if (!check_command(*item.effective_command, item.command_match_strictness,
                                          *command, item.command_is_regex)) {
                            continue;
                        }
                    }
                    match_items.push_back(item);
                }
                fetched = true;
            }
        }
    }
    return match_items;
}

std::vector<Item> fetch_substrules(const std::optional<std::string>& command) {
    std::string path = get_db_path();
    if (!fs::exists(path)) return {};

    try {
        connect_db(path);
    } catch (const need_db_regenerate&) {
        return {};
    }

    auto result = get_matches(command);
    close_db();
    return result;
}

} // namespace db_interface
} // namespace clitheme
