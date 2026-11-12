#include "headers/builtins.hxx"

#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "headers/log.hxx"
#include "headers/util.hxx"

const char *platform_name() {
#if defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__NetBSD__)
    return "netbsd";
#else
    return "unknown";
#endif
}

int cpu_cores() {
    int n = static_cast<int>(std::thread::hardware_concurrency());
    return n > 0 ? n : 1;
}

// ........................ tool detection ........................
// To support another language, add a row: variable name and candidates in priority order.
static const std::vector<std::pair<std::string, std::vector<std::string>>> TOOLS = {
    {"cxx.detect",  {"clang++", "g++", "c++"}},
    {"cc.detect",   {"clang", "gcc", "cc"}},
    {"go.detect",   {"go"}},
    {"java.detect", {"javac"}},
    {"lua.detect",  {"luajit", "lua5.4", "lua5.3", "lua"}},
};

static std::string find_tool(const std::string &var, const std::vector<std::string> &candidates) {
    for (const auto &name : candidates)
        if (run_shell("command -v " + name + " >/dev/null 2>&1", ".").code == 0) return name;
    log_warn("{var:%s}: none of [%s] found in PATH", var.c_str(), join(candidates, ", ").c_str());
    return {};
}

// ........................ git ........................
static std::string git(const std::string &root, const std::string &args) {
    return trim(run_shell("git " + args, root).output);
}

static std::optional<std::string> git_var(const std::string &name, const std::string &root) {
    bool inside = git(root, "rev-parse --is-inside-work-tree 2>/dev/null") == "true";
    if (name == "git.branch") return inside ? git(root, "rev-parse --abbrev-ref HEAD") : "";
    if (name == "git.commit") return inside ? git(root, "rev-parse --short HEAD")       : "";
    if (name == "git.dirty")  return inside && !git(root, "status --porcelain").empty() ? "1" : "0";
    return std::nullopt;
}

// ........................ platform ........................
static std::optional<std::string> platform_var(const std::string &name) {
    std::string self = platform_name();
    if (name == "platform") return self;
    if (name == "platform.family") {
        if (self == "linux")   return "unix";
        if (self == "unknown") return "unknown";
        return "bsd";
    }
    if (name == "platform.bsd") return self == "macos" || self == "freebsd" || self == "netbsd" ? "1" : "0";
    for (const char *os : {"macos", "linux", "freebsd", "netbsd"})
        if (name == std::string("platform.") + os) return self == os ? "1" : "0";
    return std::nullopt;
}

static std::optional<std::string> compute(const std::string &name, const std::string &root) {
    if (auto v = platform_var(name)) return v;
    if (name == "cpu.cores") return std::to_string(cpu_cores());
    for (const auto &tool : TOOLS)
        if (name == tool.first) return find_tool(tool.first, tool.second);
    if (starts_with(name, "git.")) return git_var(name, root);
    return std::nullopt;
}

std::optional<std::string> builtin_var(const std::string &name, const std::string &root) {
    static std::unordered_map<std::string, std::string> memo;   // only used while BUILD.mek is parsed (one thread)
    auto it = memo.find(name);
    if (it != memo.end()) return it->second;
    auto value = compute(name, root);
    if (value) memo[name] = *value;
    return value;
}
