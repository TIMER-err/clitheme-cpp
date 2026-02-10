#include "locale_detect.hpp"
#include "sanity_check.hpp"
#include "string_utils.hpp"
#include <cstdlib>
#include <iostream>
#include <regex>
#include <algorithm>

namespace clitheme {
namespace locale_detect {

static std::string get_env(const char* name) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : "";
}

std::vector<std::string> get_locale(bool debug_mode) {
    std::vector<std::string> lang;

    auto add_language = [&](const std::string& target_lang) {
        if (sanity_check::check(target_lang)) {
            // Strip encoding: e.g. "en_US.UTF-8" -> "en_US"
            std::string no_encoding = std::regex_replace(target_lang, std::regex(R"(^(.+)\..+$)"), "$1");
            if (std::find(lang.begin(), lang.end(), target_lang) == lang.end())
                lang.push_back(target_lang);
            if (std::find(lang.begin(), lang.end(), no_encoding) == lang.end())
                lang.push_back(no_encoding);
        } else {
            if (debug_mode)
                std::cerr << "[Debug] Locale \"" << target_lang << "\": sanity check failed (" << sanity_check::error_message << ")\n";
        }
    };

    std::string lang_value = get_env("LANG");
    if (lang_value.empty()) lang_value = "C";
    std::string lc_all_value = get_env("LC_ALL");
    if (lc_all_value.empty()) lc_all_value = "C";

    bool skip_LANGUAGE = (lang_value == "C" || string_utils::starts_with(lang_value, "C."))
                      && (lc_all_value == "C" || string_utils::starts_with(lc_all_value, "C."));

    std::string env_LANGUAGE = get_env("LANGUAGE");
    std::string env_LC_ALL = get_env("LC_ALL");
    std::string env_LANG = get_env("LANG");

    if (!string_utils::strip(env_LANGUAGE).empty() && !skip_LANGUAGE) {
        if (debug_mode) std::cerr << "[Debug] Using LANGUAGE variable\n";
        auto languages = string_utils::split(env_LANGUAGE, ':');
        for (auto& language : languages) {
            std::string each = string_utils::strip(language);
            if (each.empty()) continue;
            if (each != "en" && each != "en_US") {
                // Treat C locale as en_US
                std::string no_enc = std::regex_replace(each, std::regex(R"(^(.+)\..+$)"), "$1");
                if (no_enc == "C") {
                    for (const auto& item : {"en_US", "en"}) {
                        std::regex enc_re(R"(^.+[\.])");;
                        std::string with_encoding = std::regex_replace(each, enc_re, std::string(item) + ".");
                        if (with_encoding != each) {
                            add_language(with_encoding);
                        } else {
                            add_language(item);
                        }
                    }
                }
                add_language(each);
            }
        }
    } else if (!string_utils::strip(env_LC_ALL).empty()) {
        if (debug_mode) std::cerr << "[Debug] Using LC_ALL variable\n";
        add_language(string_utils::strip(env_LC_ALL));
    } else if (!string_utils::strip(env_LANG).empty()) {
        if (debug_mode) std::cerr << "[Debug] Using LANG variable\n";
        add_language(string_utils::strip(env_LANG));
    }

    return lang;
}

} // namespace locale_detect
} // namespace clitheme
