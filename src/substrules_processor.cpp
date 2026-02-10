#include "substrules_processor.hpp"
#include "db_interface.hpp"
#include "globalvar.hpp"
#include "string_utils.hpp"
#include "pcre2_regex.hpp"
#include <regex>
#include <vector>
#include <set>
#include <cassert>

namespace clitheme {
namespace substrules_processor {

std::pair<std::string, std::set<int>> match_content(
    const std::string& content,
    const std::optional<std::string>& command,
    bool is_stderr
) {
    assert(!content.empty() && "Empty content string");

    std::string content_str = content;
    auto substrules = db_interface::fetch_substrules(command);

    std::set<std::string> encountered_ids;
    std::string line_match_pattern = globalvar::build_line_match_pattern();

    std::set<std::string> encountered_files;
    std::string last_file_id;
    // condition_map: 0x00=not matched, 0x01=matched, 0x02=end match here
    std::vector<uint8_t> condition_map;

    auto init_condition_map = [&]() {
        condition_map.assign(content_str.size(), 0x00);
    };
    init_condition_map();

    for (const auto& rule : substrules) {
        // Condition checking
        if (encountered_ids.count(rule.unique_id)) continue;
        if (rule.stdout_stderr_only != 0 && static_cast<int>(is_stderr) + 1 != rule.stdout_stderr_only) continue;
        if (command.has_value() && rule.effective_command.has_value()) {
            if (!db_interface::check_command(*rule.effective_command, rule.command_match_strictness,
                                             *command, rule.command_is_regex)) continue;
        }
        // Skip foreground_only check (no PID info in filter mode)

        // Reset condition map for new files
        if (rule.file_id != last_file_id) {
            encountered_files.insert(rule.file_id);
            last_file_id = rule.file_id;
            init_condition_map();
        }

        // Determine line lengths
        std::vector<size_t> line_lengths;
        if (rule.match_is_multiline) {
            line_lengths.push_back(content_str.size());
        } else {
            // Split by line match pattern
            std::regex lm_re(line_match_pattern);
            auto it = std::sregex_iterator(content_str.begin(), content_str.end(), lm_re);
            auto end = std::sregex_iterator();
            for (; it != end; ++it) {
                size_t len = it->length();
                if (len > 0) line_lengths.push_back(len);
            }
            if (line_lengths.empty()) line_lengths.push_back(content_str.size());
        }

        auto new_condition_map = condition_map;
        int new_condition_map_offset = 0;
        std::string new_content = content_str;
        bool matched = false;
        int offset = 0;
        size_t cur_start = 0;

        for (size_t length : line_lengths) {
            // Build match string (replace previous newline with \n for MULTILINE)
            std::string match_str = content_str;
            if (cur_start > 0) {
                // Replace previous newline char with '\n'
                match_str = content_str.substr(0, cur_start - 1) + "\n" + content_str.substr(cur_start);
            }

            try {
                // Use PCRE2 for matching within the line range
                auto pcre_matches = pcre2_regex::finditer(
                    rule.match_pattern, match_str, cur_start, cur_start + length);

                for (const auto& pm : pcre_matches) {
                    size_t abs_start = pm.start;
                    size_t abs_end = pm.end;
                    size_t match_len = abs_end - abs_start;

                    // Check endmatchhere: find line boundaries
                    size_t line_start = abs_start;
                    for (size_t pos = abs_start; pos > 0; pos--) {
                        bool is_newline = false;
                        for (const auto& nl : globalvar::newlines) {
                            if (pos >= nl.size() && content_str.substr(pos - nl.size(), nl.size()) == nl) {
                                is_newline = true;
                                break;
                            }
                        }
                        if (is_newline) break;
                        line_start = pos - 1;
                    }

                    size_t line_end = abs_end;
                    for (size_t pos = abs_end; pos < content_str.size(); pos++) {
                        line_end = pos + 1;
                        bool is_newline = false;
                        for (const auto& nl : globalvar::newlines) {
                            if (pos + nl.size() <= content_str.size() &&
                                content_str.substr(pos, nl.size()) == nl) {
                                is_newline = true;
                                line_end = pos + nl.size();
                                break;
                            }
                        }
                        if (is_newline) break;
                    }

                    // Check if any 0x02 in the line range of condition_map
                    bool skip = false;
                    for (size_t i = line_start; i < std::min(line_end, condition_map.size()); i++) {
                        if (condition_map[i] == 0x02) { skip = true; break; }
                    }
                    if (skip) continue;
                    matched = true;

                    // Perform substitution
                    std::string new_str;
                    if (rule.is_regex) {
                        new_str = pcre2_regex::expand_replacement(rule.substitute_pattern, pm);
                    } else {
                        new_str = rule.substitute_pattern;
                    }

                    size_t pos_in_new = abs_start + offset;
                    new_content = new_content.substr(0, pos_in_new) + new_str +
                                  new_content.substr(pos_in_new + match_len);
                    offset += static_cast<int>(new_str.size()) - static_cast<int>(match_len);

                    // Update condition map
                    uint8_t mark = rule.end_match_here ? 0x02 : 0x01;
                    std::vector<uint8_t> sub_map(new_str.size(), mark);
                    size_t cm_pos = abs_start + new_condition_map_offset;
                    new_condition_map.erase(
                        new_condition_map.begin() + cm_pos,
                        new_condition_map.begin() + cm_pos + match_len);
                    new_condition_map.insert(
                        new_condition_map.begin() + cm_pos,
                        sub_map.begin(), sub_map.end());
                    new_condition_map_offset += static_cast<int>(new_str.size()) - static_cast<int>(match_len);
                }
            } catch (const pcre2_regex::regex_error&) {
                // Skip invalid patterns
            }

            cur_start += length;
        }

        content_str = new_content;
        condition_map = new_condition_map;
        if (matched) encountered_ids.insert(rule.unique_id);
    }

    // Determine changed line indices
    std::set<int> changed_line_indices;
    std::regex lm_re(line_match_pattern);
    std::vector<size_t> final_line_lengths;
    {
        auto it = std::sregex_iterator(content_str.begin(), content_str.end(), lm_re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            if (it->length() > 0) final_line_lengths.push_back(it->length());
        }
    }
    size_t cur_start = 0;
    for (size_t x = 0; x < final_line_lengths.size(); x++) {
        size_t length = final_line_lengths[x];
        for (size_t i = cur_start; i < std::min(cur_start + length, condition_map.size()); i++) {
            if (condition_map[i] == 0x01 || condition_map[i] == 0x02) {
                changed_line_indices.insert(static_cast<int>(x));
                break;
            }
        }
        cur_start += length;
    }

    return {content_str, changed_line_indices};
}

} // namespace substrules_processor
} // namespace clitheme
