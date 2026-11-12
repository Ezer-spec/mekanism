#include "headers/plan.hxx"

#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "headers/log.hxx"
#include "headers/util.hxx"

namespace fs = std::filesystem;

static const char *OBJECT_DIR = ".mekanism/obj/";
static const char *STAMP_DIR  = ".mekanism/stamp/";

std::vector<std::string> default_targets(const Config &cfg) {
    if (!cfg.default_targets.empty()) return cfg.default_targets;
    std::unordered_set<std::string> used;
    for (const auto &t : cfg.targets)
        for (const auto &d : t.deps) used.insert(d);
    std::vector<std::string> roots;
    for (const auto &t : cfg.targets)
        if (!used.count(t.name)) roots.push_back(t.name);
    return roots;
}

// ........................ dependency walk ........................
struct Walk {
    const Config                         &cfg;
    std::unordered_map<std::string, int>  index;
    std::vector<int>                      state;   // 0 new, 1 on the current path, 2 done
    std::vector<int>                      order;   // dependencies before dependents
    std::vector<int>                      path;
    bool                                  ok = true;
};

static void visit(Walk &w, int i) {
    w.state[i] = 1;
    w.path.push_back(i);
    for (const auto &dep : w.cfg.targets[i].deps) {
        auto it = w.index.find(dep);
        if (it == w.index.end()) {
            log_error("target '%s' depends on unknown target '%s'", w.cfg.targets[i].name.c_str(), dep.c_str());
            w.ok = false;
            continue;
        }
        int j = it->second;
        if (w.state[j] == 1) {
            std::string cycle;
            size_t from = 0;
            while (w.path[from] != j) ++from;
            for (size_t k = from; k < w.path.size(); ++k) cycle += w.cfg.targets[w.path[k]].name + " -> ";
            log_error("dependency cycle: %s%s", cycle.c_str(), w.cfg.targets[j].name.c_str());
            w.ok = false;
        } else if (w.state[j] == 0) {
            visit(w, j);
        }
    }
    w.path.pop_back();
    w.state[i] = 2;
    w.order.push_back(i);
}

// ........................ names of generated files ........................
static std::string safe_name(const std::string &s) {
    std::string out = s;
    for (char &c : out)
        if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') c = '_';
    return out;
}

// build/mekanism + sources/log.cxx  ->  .mekanism/obj/mekanism/sources/log.cxx.o
static std::string object_path(const std::string &target, const std::string &source) {
    std::string path = OBJECT_DIR + safe_name(target);
    for (const auto &part : fs::path(source).relative_path()) {
        if (part == ".") continue;
        path += "/" + (part == ".." ? std::string("__") : part.string());
    }
    return path + ".o";
}

static std::string with_extension(const std::string &object, const std::string &ext) {
    return object.substr(0, object.size() - 2) + ext;   // object always ends with ".o"
}

// ........................ job creation ........................
static std::string env_prefix(const Target &t) {
    std::string out;
    for (const auto &entry : t.env) {
        size_t eq = entry.find('=');
        out += "export " + entry.substr(0, eq) + "=" + shell_quote(entry.substr(eq + 1)) + "; ";
    }
    return out;
}

static Tokens target_tokens(const Target &t, const std::vector<std::string> &objects) {
    Tokens k = {{"name", t.name},
                {"sources", join(t.sources)}, {"inputs", join(t.sources)},
                {"outputs", join(t.outputs)}, {"deps", join(t.deps)},
                {"objects", join(objects)}};
    for (size_t i = 0; i < t.sources.size(); ++i) k["input" + std::to_string(i)]  = t.sources[i];
    for (size_t i = 0; i < t.outputs.size(); ++i) k["output" + std::to_string(i)] = t.outputs[i];
    return k;
}

static const std::string *find_func(Config &cfg, const Target &t, const std::string &field, const std::string &name) {
    auto it = cfg.funcs.find(name);
    if (it != cfg.funcs.end()) return &it->second;
    log_error("target '%s': %s: unknown func '%s'", t.name.c_str(), field.c_str(), name.c_str());
    return nullptr;
}

static bool add_target_jobs(Config &cfg, const Target &t, std::unordered_map<std::string, int> &main_job, Plan &plan) {
    auto add = [&](Job j) { plan.jobs.push_back(std::move(j)); return static_cast<int>(plan.jobs.size()) - 1; };

    // What every job of this target waits for: dependency targets and cloned repos
    std::vector<int> before;
    for (const auto &d : t.deps) before.push_back(main_job.at(d));
    for (const auto &r : t.repos) {
        Job j;
        j.kind = "clone"; j.target = t.name; j.what = r.dest; j.outputs = {r.dest};
        j.cmd = "git clone --depth 1" + (r.branch.empty() ? "" : " --branch " + r.branch) +
                " -- " + shell_quote(r.url) + " " + shell_quote(r.dest);
        before.push_back(add(j));
    }

    // One compile job per source
    std::vector<std::string> objects;
    std::vector<int>         compiles;
    if (!t.compile.empty()) {
        const std::string *tmpl = find_func(cfg, t, "compile", t.compile);
        if (!tmpl) return false;
        if (tmpl->find("{object}") == std::string::npos) {
            log_error("func '%s' (compile of '%s') must contain {object}", t.compile.c_str(), t.name.c_str());
            return false;
        }
        for (const auto &src : t.sources) objects.push_back(object_path(t.name, src));
        for (size_t i = 0; i < t.sources.size(); ++i) {
            Tokens k = target_tokens(t, objects);
            k["source"]  = t.sources[i];
            k["object"]  = objects[i];
            k["depfile"] = with_extension(objects[i], ".d");
            Job j;
            j.kind = "compile"; j.target = t.name; j.what = t.sources[i];
            j.cmd = env_prefix(t) + expand_text(cfg, *tmpl, k);
            j.inputs = {t.sources[i]};
            j.outputs = {objects[i]};
            if (tmpl->find("{depfile}") != std::string::npos) j.depfile = k["depfile"];
            j.stamp = with_extension(objects[i], ".cmd");
            j.deps = before;
            compiles.push_back(add(j));
        }
    }

    // The target's own command
    Job j;
    j.kind = "build"; j.target = t.name;
    j.what = t.outputs.empty() ? "-" : join(t.outputs, " ");
    j.inputs = t.compile.empty() ? t.sources : objects;
    j.outputs = t.outputs;
    j.stamp = STAMP_DIR + safe_name(t.name) + ".cmd";
    j.deps = before;
    j.deps.insert(j.deps.end(), compiles.begin(), compiles.end());
    std::string tmpl = t.cmd;
    if (!t.use.empty()) {
        const std::string *f = find_func(cfg, t, "use", t.use);
        if (!f) return false;
        tmpl = *f;
    }
    if (!tmpl.empty()) j.cmd = env_prefix(t) + expand_text(cfg, tmpl, target_tokens(t, objects));
    main_job[t.name] = add(j);
    return true;
}

bool make_plan(Config &cfg, const std::vector<std::string> &requested, Plan &plan) {
    Walk w{cfg, {}, std::vector<int>(cfg.targets.size(), 0), {}, {}, true};
    for (size_t i = 0; i < cfg.targets.size(); ++i) w.index[cfg.targets[i].name] = static_cast<int>(i);

    for (const auto &name : requested) {
        auto it = w.index.find(name);
        if (it == w.index.end()) { log_error("unknown target '%s'", name.c_str()); w.ok = false; }
        else if (w.state[it->second] == 0) visit(w, it->second);
    }
    if (!w.ok) return false;

    std::unordered_map<std::string, int> main_job;
    for (int i : w.order)
        if (!add_target_jobs(cfg, cfg.targets[i], main_job, plan)) return false;
    return true;
}
