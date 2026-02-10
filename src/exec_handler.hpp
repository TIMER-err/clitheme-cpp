#pragma once
#include <string>
#include <vector>
#include <termios.h>
#include <sys/types.h>

namespace clitheme {

class ExecHandler {
public:
    ExecHandler(const std::vector<std::string>& argv);
    ~ExecHandler();

    // Main loop: forward I/O and process output. Returns child exit code.
    int run();

private:
    void setup_raw_terminal();
    void restore_terminal();
    void update_window_size();

    static void handle_sigwinch(int sig);
    static void handle_sigint(int sig);
    static void handle_sigtstp(int sig);
    static void handle_sigcont(int sig);

    pid_t child_pid_;
    int pty_master_;
    struct termios prev_termios_;
    bool is_tty_;
    bool terminal_saved_;
    std::string command_str_;

    // Static state for signal handlers
    static int s_pty_master;
    static pid_t s_child_pid;
    static ExecHandler* s_instance;
};

} // namespace clitheme
