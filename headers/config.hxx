#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct Repo {
    std::string url, dest, branch;
};

// One `target` block of BUILD.mek
struct Target {
    std::string              name;
    std::vector<std::string> sources, outputs, deps;
    std::vector<std::string> env;      // KEY=VALUE, exported for every command of the target
    std::vector<Repo>        repos;
    std::string              compile;  // func run once per source, must produce {object}
    std::string              use;      // func run once per target (link step, packaging, ...)
    std::string              cmd;      // literal command, alternative to `use`
};

struct Config {
    std::string                                  root;      // absolute workspace path
    std::vector<Target>                          targets;
    std::unordered_map<std::string, std::string> vars;
    std::unordered_map<std::string, std::string> funcs;
    std::vector<std::string>                     default_targets;
};

using Tokens = std::unordered_map<std::string, std::string>;

// Reads <root>/<file>. Problems are logged as "BUILD.mek:LINE: ..."; returns false if there were errors
bool load_config(Config &cfg, const std::string &file);

// Expands {var:name} and every {token} found in `tokens`.
std::string expand_text(Config &cfg, const std::string &text, const Tokens &tokens = {});
