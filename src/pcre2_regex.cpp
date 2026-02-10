#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2_regex.hpp"
#include <cstring>

namespace clitheme {
namespace pcre2_regex {

// Helper: get PCRE2 error message
static std::string pcre2_error_message(int errorcode) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
    return reinterpret_cast<const char*>(buffer);
}

// RAII wrapper for pcre2_code
struct CompiledPattern {
    pcre2_code* code;
    CompiledPattern(const std::string& pattern, uint32_t options = 0) {
        int errorcode;
        PCRE2_SIZE erroroffset;
        code = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(pattern.c_str()),
            pattern.size(),
            options | PCRE2_UTF | PCRE2_MULTILINE,
            &errorcode, &erroroffset, nullptr);
        if (code == nullptr) {
            throw regex_error(pcre2_error_message(errorcode));
        }
    }
    ~CompiledPattern() { if (code) pcre2_code_free(code); }
    CompiledPattern(const CompiledPattern&) = delete;
    CompiledPattern& operator=(const CompiledPattern&) = delete;
};

void validate_pattern(const std::string& pattern) {
    CompiledPattern cp(pattern);
}

void validate_substitution(const std::string& pattern, const std::string& replacement) {
    CompiledPattern cp(pattern);
    // Try a substitution on empty string to validate replacement syntax
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(cp.code, nullptr);
    // Just try matching - replacement validation happens at expand time
    pcre2_match(cp.code, reinterpret_cast<PCRE2_SPTR>(""), 0, 0, 0, match_data, nullptr);
    pcre2_match_data_free(match_data);
}

// Extract named groups mapping from compiled pattern
static std::map<std::string, int> extract_named_groups(pcre2_code* code) {
    std::map<std::string, int> named_groups;
    uint32_t namecount = 0;
    pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &namecount);
    if (namecount > 0) {
        PCRE2_SPTR nametable;
        uint32_t nameentrysize;
        pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &nametable);
        pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &nameentrysize);
        for (uint32_t i = 0; i < namecount; i++) {
            PCRE2_SPTR entry = nametable + i * nameentrysize;
            int group_num = (entry[0] << 8) | entry[1];
            std::string name(reinterpret_cast<const char*>(entry + 2));
            named_groups[name] = group_num;
        }
    }
    return named_groups;
}

static Match build_match(pcre2_code* code, pcre2_match_data* match_data,
                          const std::string& subject) {
    Match m;
    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
    uint32_t count = pcre2_get_ovector_count(match_data);

    m.start = ovector[0];
    m.end = ovector[1];
    m.str = subject.substr(m.start, m.end - m.start);

    for (uint32_t i = 0; i < count; i++) {
        if (ovector[2 * i] == PCRE2_UNSET) {
            m.groups.push_back("");
            m.group_offsets.push_back({std::string::npos, std::string::npos});
        } else {
            size_t s = ovector[2 * i];
            size_t e = ovector[2 * i + 1];
            m.groups.push_back(subject.substr(s, e - s));
            m.group_offsets.push_back({s, e});
        }
    }

    m.named_groups = extract_named_groups(code);
    return m;
}

std::vector<Match> finditer(const std::string& pattern, const std::string& subject,
                            size_t start_offset, size_t end_offset) {
    if (end_offset == std::string::npos) end_offset = subject.size();

    CompiledPattern cp(pattern);
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(cp.code, nullptr);

    std::vector<Match> results;
    size_t offset = start_offset;

    while (offset <= end_offset) {
        int rc = pcre2_match(cp.code,
                             reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                             end_offset, offset, 0, match_data, nullptr);
        if (rc < 0) break;

        Match m = build_match(cp.code, match_data, subject);

        results.push_back(m);

        // Advance past match (handle zero-length matches)
        if (m.end == m.start) {
            offset = m.end + 1;
            if (offset > end_offset) break;
        } else {
            offset = m.end;
        }
    }

    pcre2_match_data_free(match_data);
    return results;
}

// Expand Python-style replacement: \g<name>, \g<1>, \1, \\, etc.
std::string expand_replacement(const std::string& replacement, const Match& match) {
    std::string result;
    size_t i = 0;
    while (i < replacement.size()) {
        if (replacement[i] == '\\' && i + 1 < replacement.size()) {
            char next = replacement[i + 1];
            if (next == 'g' && i + 2 < replacement.size() && replacement[i + 2] == '<') {
                // \g<name> or \g<number>
                size_t close = replacement.find('>', i + 3);
                if (close != std::string::npos) {
                    std::string ref = replacement.substr(i + 3, close - i - 3);
                    // Try as number first
                    bool is_number = !ref.empty();
                    for (char c : ref) { if (!std::isdigit(c)) { is_number = false; break; } }

                    if (is_number) {
                        int idx = std::stoi(ref);
                        if (idx >= 0 && idx < static_cast<int>(match.groups.size())) {
                            result += match.groups[idx];
                        }
                    } else {
                        // Named group - look up via named_groups map
                        auto it = match.named_groups.find(ref);
                        if (it != match.named_groups.end()) {
                            int idx = it->second;
                            if (idx >= 0 && idx < static_cast<int>(match.groups.size())) {
                                result += match.groups[idx];
                            }
                        }
                    }
                    i = close + 1;
                    continue;
                }
            } else if (next == '\\') {
                result += '\\';
                i += 2;
                continue;
            } else if (next == 'n') {
                result += '\n';
                i += 2;
                continue;
            } else if (next == 't') {
                result += '\t';
                i += 2;
                continue;
            } else if (std::isdigit(next)) {
                // \1, \2, etc.
                int idx = next - '0';
                if (idx >= 0 && idx < static_cast<int>(match.groups.size())) {
                    result += match.groups[idx];
                }
                i += 2;
                continue;
            }
        }
        result += replacement[i];
        i++;
    }
    return result;
}

// Perform all the finditer + expand in one go, building the result string
std::string sub(const std::string& pattern, const std::string& replacement,
                const std::string& subject) {
    auto matches = finditer(pattern, subject);

    std::string result;
    size_t last = 0;

    if (!matches.empty()) {
        const auto& m = matches[0];
        result += subject.substr(last, m.start - last);
        result += expand_replacement(replacement, m);
        last = m.end;
    }
    result += subject.substr(last);

    return result;
}

} // namespace pcre2_regex
} // namespace clitheme
