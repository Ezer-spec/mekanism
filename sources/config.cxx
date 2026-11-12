#include "headers/config.hxx"

#include <deque>
#include <sstream>

#include "headers/builtins.hxx"
#include "headers/expand.hxx"
#include "headers/log.hxx"
#include "headers/util.hxx"

// ........................ expansion ........................
static bool is_numbered_token(const std::string &key) {   // input0, output12, ...
    for (const char *prefix : {"input", "output"})
        if (starts_with(key, prefix) && key.size() > std::string(prefix).size() &&
            key.find_first_not_of("0123456789", std::string(prefix).size()) == std::string::npos)
            return true;
    return false;
}

std::string expand_text(Config &cfg, const std::string &text, const Tokens &tokens) {
    return expand(text, [&](const std::string &key) -> std::optional<std::string> {
        if (starts_with(key, "var:")) {
            std::string name = key.substr(4);
            auto it = cfg.vars.find(name);
            if (it != cfg.vars.end()) return it->second;
            if (auto value = builtin_var(name, cfg.root)) return *value;
            log_warn("undefined variable {var:%s}", name.c_str());
            return std::string();
        }
        auto it = tokens.find(key);
        if (it != tokens.end()) return it->second;
        if (is_numbered_token(key)) {              // {input3} with only two inputs
            log_warn("{%s} is out of range, using empty string", key.c_str());
            return std::string();
        }
        return std::nullopt;
    });
}

// ........................ parser state ........................
struct Line {
    std::string text;
    int         no;
};

struct Parser {
    Config          &cfg;
    std::deque<Line> in;        // lines still to read; `force` loops push their expansion to the front
    int              cur = -1;  // index of the current target in cfg.targets, -1 before the first one
    int              errors = 0;
};

static void fail(Parser &p, int line, const std::string &msg) {
    log_error("BUILD.mek:%d: %s", line, msg.c_str());
    ++p.errors;
}

static std::string unquote(const std::string &v) {
    if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) return trim(v.substr(1, v.size() - 2));
    return v;
}

// Value of "key: value". A value in quotes may continue over several lines, lines starting with # are skipped.
static std::string read_value(Parser &p, const std::string &raw, int line) {
    std::string v = trim(raw);
    if (v.empty() || (v[0] != '"' && v[0] != '\'')) return v;
    char quote = v[0];
    if (v.size() >= 2 && v.back() == quote) return trim(v.substr(1, v.size() - 2));

    std::vector<std::string> parts;
    auto add = [&](const std::string &s) { if (!trim(s).empty()) parts.push_back(trim(s)); };
    add(v.substr(1));
    while (!p.in.empty()) {
        std::string t = trim(p.in.front().text);
        p.in.pop_front();
        if (!t.empty() && t[0] == '#') continue;
        if (!t.empty() && t.back() == quote) { add(t.substr(0, t.size() - 1)); return join(parts, "\n"); }
        add(t);
    }
    fail(p, line, "quoted value is not closed");
    return join(parts, "\n");
}

// ........................ target fields ........................
// To add a field: add a member to Target (config.hxx), handle it here, use it in plan.cxx.
static void set_field(Parser &p, Target &t, const std::string &key, const std::string &value, int line) {
    auto append = [&](std::vector<std::string> &list) {
        for (const auto &w : split_words(value)) list.push_back(w);
    };
    if      (key == "sources") append(t.sources);
    else if (key == "outputs") append(t.outputs);
    else if (key == "deps")    append(t.deps);
    else if (key == "compile") t.compile = value;
    else if (key == "use")     t.use = value;
    else if (key == "cmd")     t.cmd = value;
    else if (key == "build_env") {
        for (const auto &entry : split_lines(value)) {
            size_t eq = entry.find('=');
            if (eq == 0 || eq == std::string::npos) fail(p, line, "build_env entries look like NAME=value: " + entry);
            else t.env.push_back(entry);
        }
    }
    else if (key == "repo") {
        auto w = split_words(value);
        if (w.size() < 2 || w.size() > 3) fail(p, line, "repo needs: <url> <dest> [branch]");
        else t.repos.push_back({w[0], w[1], w.size() == 3 ? w[2] : ""});
    }
    else log_warn("BUILD.mek:%d: unknown field '%s' in target '%s'", line, key.c_str(), t.name.c_str());
}

// Switch result goes to the current target, or to a variable before the first target.
static void assign(Parser &p, const std::string &key, const std::string &raw, int line) {
    std::string value = expand_text(p.cfg, raw);
    if (p.cur >= 0) set_field(p, p.cfg.targets[p.cur], key, value, line);
    else            p.cfg.vars[key] = value;
}

// ........................ statements ........................
// switch <field> <expr>:
//   case <value>: <result>
//   default: <result>
static void parse_switch(Parser &p, int no, const std::string &s) {
    size_t colon = s.rfind(':');
    auto head = split_words(colon == std::string::npos ? "" : s.substr(6, colon - 6));
    if (head.size() < 2) { fail(p, no, "expected: switch <field> <expression>:"); return; }
    std::string field = head[0];
    std::string expr  = expand_text(p.cfg, join({head.begin() + 1, head.end()}));

    std::optional<std::string> chosen, fallback;
    while (!p.in.empty()) {
        Line ln = p.in.front();
        std::string t = trim(ln.text);
        if (t.empty() || t[0] == '#') { p.in.pop_front(); continue; }
        bool is_case = starts_with(t, "case "), is_default = starts_with(t, "default:");
        if (!is_case && !is_default) break;
        p.in.pop_front();
        size_t cp = t.find(':');
        if (cp == std::string::npos) { fail(p, ln.no, "case needs ':'"); continue; }
        std::string value = unquote(trim(t.substr(cp + 1)));
        if (is_default)                                             fallback = value;
        else if (!chosen && trim(t.substr(5, cp - 5)) == expr)      chosen = value;
    }
    if (!chosen) chosen = fallback;
    if (chosen) assign(p, field, *chosen, no);
    else        log_warn("BUILD.mek:%d: switch '%s' has no case for '%s' and no default", no, field.c_str(), expr.c_str());
}

// force <var> in <items>:
//   ... {var:<var>} ...
// endforce
static void parse_force(Parser &p, int no, const std::string &s) {
    size_t colon = s.rfind(':');
    std::string head = colon == std::string::npos ? "" : trim(s.substr(6, colon - 6));
    size_t in_pos = head.find(" in ");
    if (in_pos == std::string::npos) { fail(p, no, "expected: force <var> in <items>:"); return; }
    std::string var   = trim(head.substr(0, in_pos));
    auto        items = split_words(expand_text(p.cfg, head.substr(in_pos + 4)));

    std::vector<Line> body;
    int depth = 1;
    bool closed = false;
    while (!p.in.empty() && !closed) {
        Line ln = p.in.front();
        p.in.pop_front();
        std::string t = trim(ln.text);
        if (starts_with(t, "force ") || starts_with(t, "forse ")) ++depth;
        if ((t == "endforce" || t == "endforse") && --depth == 0) closed = true;
        else body.push_back(ln);
    }
    if (!closed) { fail(p, no, "force without endforce"); return; }

    std::vector<Line> expanded;
    for (const auto &item : items)
        for (const auto &ln : body) expanded.push_back({replace_all(ln.text, "{var:" + var + "}", item), ln.no});
    p.in.insert(p.in.begin(), expanded.begin(), expanded.end());
}

static void parse_target(Parser &p, int no, const std::string &s) {
    size_t colon = s.find(':');
    std::string name = trim(s.substr(6, colon == std::string::npos ? std::string::npos : colon - 6));
    if (name.empty()) { fail(p, no, "target needs a name"); return; }
    for (const auto &t : p.cfg.targets)
        if (t.name == name) { fail(p, no, "target '" + name + "' is defined twice"); return; }
    Target t;
    t.name = name;
    p.cfg.targets.push_back(t);
    p.cur = static_cast<int>(p.cfg.targets.size()) - 1;
}

static void parse_console(Parser &p, int no, const std::string &s) {
    size_t open = s.find('('), close = s.rfind(')');
    if (close == std::string::npos || close < open) { fail(p, no, "expected: console(\"message\")"); return; }
    log_info("console: %s", expand_text(p.cfg, unquote(trim(s.substr(open + 1, close - open - 1)))).c_str());
}

static void parse_line(Parser &p, int no, const std::string &s) {
    std::string word = s.substr(0, s.find_first_of(" \t(:"));
    size_t colon = s.find(':');

    if (word == "force" || word == "forse") return parse_force(p, no, s);
    if (word == "switch")  return parse_switch(p, no, s);
    if (word == "target")  return parse_target(p, no, s);
    if (word == "console") return parse_console(p, no, s);
    if (word == "case" || word == "default" || word == "endforce" || word == "endforse")
        return fail(p, no, "unexpected '" + word + "'");

    if (colon == std::string::npos) return fail(p, no, "expected 'key: value'");
    std::string key = trim(s.substr(0, colon));
    std::string value = read_value(p, s.substr(colon + 1), no);

    if (word == "var" || word == "func") {
        std::string name = trim(key.substr(word.size()));
        if (name.empty()) return fail(p, no, word + " needs a name");
        if (word == "var") p.cfg.vars[name] = expand_text(p.cfg, value);
        else               p.cfg.funcs[name] = value;   // expanded later, when a target uses it
    } else if (word == "default_target") {
        for (const auto &w : split_words(expand_text(p.cfg, value))) p.cfg.default_targets.push_back(w);
    } else if (p.cur < 0) {
        fail(p, no, "'" + key + "' is outside of a target");
    } else {
        set_field(p, p.cfg.targets[p.cur], key, expand_text(p.cfg, value), no);
    }
}

bool load_config(Config &cfg, const std::string &file) {
    auto text = read_file(full_path(cfg.root, file));
    if (!text) {
        log_error("cannot read %s", full_path(cfg.root, file).c_str());
        return false;
    }
    Parser p{cfg, {}, -1, 0};
    std::istringstream lines(*text);
    std::string raw;
    for (int no = 1; std::getline(lines, raw); ++no) p.in.push_back({raw, no});

    while (!p.in.empty()) {
        Line ln = p.in.front();
        p.in.pop_front();
        std::string s = trim(ln.text);
        if (!s.empty() && s[0] != '#') parse_line(p, ln.no, s);
    }
    for (const auto &t : cfg.targets)
        if (!t.use.empty() && !t.cmd.empty()) {
            log_error("target '%s': 'use' and 'cmd' cannot be combined", t.name.c_str());
            ++p.errors;
        }
    return p.errors == 0;
}
