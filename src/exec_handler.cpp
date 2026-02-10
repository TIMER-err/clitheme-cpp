#include "exec_handler.hpp"
#include "substrules_processor.hpp"
#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <cstring>
#include <iostream>
#include <chrono>

namespace clitheme {

// Static members
int ExecHandler::s_pty_master = -1;
pid_t ExecHandler::s_child_pid = -1;
ExecHandler* ExecHandler::s_instance = nullptr;

ExecHandler::ExecHandler(const std::vector<std::string>& argv)
    : child_pid_(-1), pty_master_(-1), is_tty_(false), terminal_saved_(false) {
    // Build command string for match_content
    for (size_t i = 0; i < argv.size(); i++) {
        if (i > 0) command_str_ += " ";
        command_str_ += argv[i];
    }

    is_tty_ = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    int master_fd, slave_fd;
    if (openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) == -1) {
        throw std::runtime_error("openpty failed: " + std::string(strerror(errno)));
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(master_fd);
        close(slave_fd);
        throw std::runtime_error("fork failed: " + std::string(strerror(errno)));
    }

    if (pid == 0) {
        // Child process
        close(master_fd);
        setsid();
        ioctl(slave_fd, TIOCSCTTY, 0);
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        // Build argv for execvp
        std::vector<char*> c_argv;
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        execvp(c_argv[0], c_argv.data());
        // If execvp returns, it failed
        std::cerr << "exec failed: " << strerror(errno) << ": " << argv[0] << std::endl;
        _exit(127);
    }

    // Parent process
    close(slave_fd);
    pty_master_ = master_fd;
    child_pid_ = pid;

    s_pty_master = pty_master_;
    s_child_pid = child_pid_;
    s_instance = this;

    if (is_tty_) {
        setup_raw_terminal();
        update_window_size();
    }

    // Install signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    sa.sa_handler = handle_sigtstp;
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, nullptr);

    sa.sa_handler = handle_sigcont;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &sa, nullptr);
}

ExecHandler::~ExecHandler() {
    if (is_tty_ && terminal_saved_) {
        restore_terminal();
    }
    if (pty_master_ >= 0) {
        close(pty_master_);
    }
    s_instance = nullptr;
    s_pty_master = -1;
    s_child_pid = -1;
}

void ExecHandler::setup_raw_terminal() {
    tcgetattr(STDIN_FILENO, &prev_termios_);
    terminal_saved_ = true;

    struct termios raw = prev_termios_;
    cfmakeraw(&raw);
    // Re-enable signal generation so SIGWINCH etc. still work
    raw.c_lflag |= ISIG;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void ExecHandler::restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &prev_termios_);
}

void ExecHandler::update_window_size() {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        ioctl(pty_master_, TIOCSWINSZ, &ws);
    }
}

void ExecHandler::handle_sigwinch(int) {
    if (s_instance && s_instance->is_tty_) {
        s_instance->update_window_size();
        if (s_child_pid > 0) {
            kill(s_child_pid, SIGWINCH);
        }
    }
}

void ExecHandler::handle_sigint(int) {
    // Forward ^C through the PTY
    if (s_pty_master >= 0) {
        char c = '\x03';
        write(s_pty_master, &c, 1);
    }
}

void ExecHandler::handle_sigtstp(int) {
    if (s_instance && s_instance->is_tty_ && s_instance->terminal_saved_) {
        s_instance->restore_terminal();
    }
    if (s_child_pid > 0) {
        kill(s_child_pid, SIGSTOP);
    }
    // Stop ourselves: reset SIGTSTP to default, then re-raise
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
}

void ExecHandler::handle_sigcont(int) {
    // Re-install our handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigtstp;
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, nullptr);

    if (s_child_pid > 0) {
        kill(s_child_pid, SIGCONT);
    }
    if (s_instance && s_instance->is_tty_) {
        s_instance->setup_raw_terminal();
    }
}

int ExecHandler::run() {
    std::string output_buffer;
    auto last_data_time = std::chrono::steady_clock::now();
    const auto flush_timeout = std::chrono::milliseconds(5);

    while (true) {
        struct pollfd fds[2];
        int nfds = 0;

        // Always poll pty_master
        fds[nfds].fd = pty_master_;
        fds[nfds].events = POLLIN;
        nfds++;

        // Poll stdin if tty
        if (is_tty_) {
            fds[nfds].fd = STDIN_FILENO;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        int poll_timeout = output_buffer.empty() ? -1 : 5;
        int ret = poll(fds, nfds, poll_timeout);

        if (ret == -1) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0 && !output_buffer.empty()) {
            // Timeout: flush incomplete buffer
            auto now = std::chrono::steady_clock::now();
            if (now - last_data_time >= flush_timeout) {
                auto [processed, _] = substrules_processor::match_content(
                    output_buffer, command_str_, false);
                write(STDOUT_FILENO, processed.data(), processed.size());
                output_buffer.clear();
            }
            continue;
        }

        // Check stdin (user input -> pty)
        if (is_tty_) {
            int stdin_idx = nfds - 1; // stdin is the last fd
            if (fds[stdin_idx].revents & POLLIN) {
                char buf[4096];
                ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                if (n > 0) {
                    write(pty_master_, buf, n);
                }
            }
        }

        // Check pty_master (child output -> process + stdout)
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t n = read(pty_master_, buf, sizeof(buf));
            if (n > 0) {
                output_buffer.append(buf, n);
                last_data_time = std::chrono::steady_clock::now();

                // Find the last newline in the buffer
                size_t last_nl = std::string::npos;
                for (size_t i = output_buffer.size(); i > 0; i--) {
                    char c = output_buffer[i - 1];
                    if (c == '\n' || c == '\r') {
                        last_nl = i;
                        break;
                    }
                }

                if (last_nl != std::string::npos) {
                    std::string complete = output_buffer.substr(0, last_nl);
                    output_buffer = output_buffer.substr(last_nl);

                    auto [processed, _] = substrules_processor::match_content(
                        complete, command_str_, false);
                    write(STDOUT_FILENO, processed.data(), processed.size());
                }
            } else {
                // EOF or error from PTY — child likely exited
                break;
            }
        }

        if (fds[0].revents & POLLHUP) {
            // Read any remaining data
            char buf[4096];
            while (true) {
                ssize_t n = read(pty_master_, buf, sizeof(buf));
                if (n <= 0) break;
                output_buffer.append(buf, n);
            }
            break;
        }
    }

    // Flush remaining buffer
    if (!output_buffer.empty()) {
        auto [processed, _] = substrules_processor::match_content(
            output_buffer, command_str_, false);
        write(STDOUT_FILENO, processed.data(), processed.size());
    }

    // Wait for child and get exit status
    int status = 0;
    waitpid(child_pid_, &status, 0);

    if (is_tty_ && terminal_saved_) {
        restore_terminal();
        terminal_saved_ = false;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

} // namespace clitheme
