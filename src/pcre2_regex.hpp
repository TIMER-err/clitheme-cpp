#pragma once
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace clitheme {
namespace pcre2_regex {

// Exception for bad patterns
class regex_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Try compiling a pattern; throws regex_error on failure
void validate_pattern(const std::string& pattern);

// Test if a substitution is valid (compile pattern and try sub on empty string)
void validate_substitution(const std::string& pattern, const std::string& replacement);

// A single match result
struct Match {
    size_t start;  // byte offset in subject
    size_t end;    // byte offset past match
    std::string str;  // matched text

    // Named/numbered group access
    std::vector<std::string> groups;       // groups[0] = full match, groups[1] = group 1, ...
    std::vector<std::pair<size_t, size_t>> group_offsets; // (start, end) for each group
    std::map<std::string, int> named_groups; // name -> group index
};

// Find all non-overlapping matches of pattern in subject within [start_offset, end_offset)
// Supports PCRE2_MULTILINE flag.
std::vector<Match> finditer(const std::string& pattern, const std::string& subject,
                            size_t start_offset = 0, size_t end_offset = std::string::npos);

// Expand a Python-style replacement string (\g<name>, \g<1>, \1, etc.) using match data
std::string expand_replacement(const std::string& replacement, const Match& match);

// Perform regex substitution: replace first match of pattern in subject
// replacement uses Python syntax: \g<name>, \g<1>, \1, etc.
std::string sub(const std::string& pattern, const std::string& replacement,
                const std::string& subject);

} // namespace pcre2_regex
} // namespace clitheme
