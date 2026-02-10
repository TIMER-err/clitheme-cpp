#include "globalvar.hpp"
#include <cstdlib>
#include <iostream>
#include <filesystem>

namespace clitheme {
namespace globalvar {

std::string get_root_data_path() {
    // Try XDG_DATA_HOME first
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/clitheme";
    }
    // Fall back to HOME
    const char* home = std::getenv("HOME");
    if (home && home[0] == '/') {
        return std::string(home) + "/.local/share/clitheme";
    }
    std::cerr << "[CLItheme] Error: unable to get your home directory or invalid home directory information.\n"
              << "Please make sure that the $HOME environment variable is set correctly.\n"
              << "Try restarting your terminal session to fix this issue.\n";
    std::exit(1);
}

std::string get_temp_root() {
    return std::filesystem::temp_directory_path().string();
}

} // namespace globalvar
} // namespace clitheme
