#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ........................ strings ........................
std::string              trim(const std::string &s);
std::vector<std::string> split_words(const std::string &s);
std::vector<std::string> split_lines(const std::string &s);   // trimmed, non-empty lines
std::string              join(const std::vector<std::string> &v, const std::string &sep = " ");
bool                     starts_with(const std::string &s, const std::string &prefix);
std::string              replace_all(std::string s, const std::string &from, const std::string &to);
std::string              shell_quote(const std::string &s);

// ........................ files ........................
// Relative paths in BUILD.mek are relative to the workspace root
std::string                full_path(const std::string &root, const std::string &path);
std::optional<std::string> read_file(const std::string &path);
bool                       write_file(const std::string &path, const std::string &text); // creates parent dirs
void                       make_parent_dirs(const std::string &path);
bool                       file_exists(const std::string &path);
std::optional<std::filesystem::file_time_type> file_mtime(const std::string &path);

// ........................ processes ........................
struct CmdResult {
    int         code;     // exit code, 128+N if killed by signal N
    std::string output;   // stdout and stderr together
};
// Runs `cmd` with /bin/sh in directory `dir`. Thread-safe. (Maybe)
CmdResult run_shell(const std::string &cmd, const std::string &dir);
