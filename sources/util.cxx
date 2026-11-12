#include "headers/util.hxx"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

// ........................ strings ........................
std::string trim(const std::string &s) {
    const char *blank = " \t\r\n";
    size_t first = s.find_first_not_of(blank);
    if (first == std::string::npos) return {};
    size_t last = s.find_last_not_of(blank);
    return s.substr(first, last - first + 1);
}

std::vector<std::string> split_words(const std::string &s) {
    std::istringstream in(s);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) words.push_back(word);
    return words;
}

std::vector<std::string> split_lines(const std::string &s) {
    std::istringstream in(s);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::string join(const std::vector<std::string> &v, const std::string &sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

bool starts_with(const std::string &s, const std::string &prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string replace_all(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
        s.replace(pos, from.size(), to);
    return s;
}

std::string shell_quote(const std::string &s) {
    return "'" + replace_all(s, "'", "'\\''") + "'";
}

// ........................ files ........................
std::string full_path(const std::string &root, const std::string &path) {
    if (path.empty() || path[0] == '/' || root.empty()) return path;
    return root + "/" + path;
}

std::optional<std::string> read_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void make_parent_dirs(const std::string &path) {
    std::error_code ec;
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
}

bool write_file(const std::string &path, const std::string &text) {
    make_parent_dirs(path);
    std::ofstream out(path, std::ios::binary);
    out << text;
    return bool(out);
}

bool file_exists(const std::string &path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::optional<std::filesystem::file_time_type> file_mtime(const std::string &path) {
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return std::nullopt;
    return t;
}

// ........................ processes ........................
// The pipe is created and the child forked under one lock, so that a child
// started by another thread can never inherit our pipe and hold it open.
static std::mutex g_spawn_mutex;

CmdResult run_shell(const std::string &cmd, const std::string &dir) {
    std::string script = "cd " + shell_quote(dir) + " && (\n" + cmd + "\n)";

    int fds[2];
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(g_spawn_mutex);
        if (pipe(fds) != 0) return {127, "pipe() failed\n"};
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        pid = fork();
        if (pid == 0) {
            dup2(fds[1], 1);
            dup2(fds[1], 2);
            execl("/bin/sh", "sh", "-c", script.c_str(), (char *)nullptr);
            _exit(127);
        }
    }
    close(fds[1]);
    if (pid < 0) { close(fds[0]); return {127, "fork() failed\n"}; }

    std::string output;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0) output.append(buf, static_cast<size_t>(n));
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return {code, output};
}
