// mekanism: reads BUILD.mek, compiles every source to its own .o file and runs the targets in parallel.
// The code lives in sources/*.cxx, this file only handles the command line.
#include <getopt.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "headers/builtins.hxx"
#include "headers/config.hxx"
#include "headers/log.hxx"
#include "headers/plan.hxx"
#include "headers/run.hxx"
#include "headers/util.hxx"
#include "headers/version.hxx"

namespace fs = std::filesystem;

static const char *USAGE =
    "usage: mekanism [options] [target...]\n"
    "\n"
    "options:\n"
    "  --workspace <path>  directory with BUILD.mek (default: current directory)\n"
    "  --jobs <N>          number of parallel commands (default: number of CPU cores)\n"
    "  --keep-going        after a failure, keep building what does not depend on it\n"
    "  --force             run every command, ignore up-to-date objects\n"
    "  --dry-run           show what would run, run nothing\n"
    "  --clean             delete .mekanism/ and the declared outputs of all targets\n"
    "  --list-targets      list targets with their dependencies and outputs\n"
    "  --check-deps        check that dependencies exist and have no cycles\n"
    "  --git-info          show git branch, commit and dirty state of the workspace\n"
    "  --log-file <path>   also append all messages to this file\n"
    "  --verbose           show commands and timings\n"
    "  --version           show version\n"
    "  -h, --help          show this text\n";

struct Args {
    std::string workspace = ".", log_file;
    RunOptions  run;
    bool clean = false, list = false, check_deps = false, git_info = false, verbose = false, version = false;
    std::vector<std::string> targets;
};

enum { OPT_WORKSPACE = 256, OPT_JOBS, OPT_KEEP_GOING, OPT_FORCE, OPT_DRY_RUN, OPT_CLEAN, OPT_LIST,
       OPT_CHECK_DEPS, OPT_GIT_INFO, OPT_LOG_FILE, OPT_VERBOSE, OPT_VERSION };

// Returns false (after printing the reason) if the command line is wrong.
static bool parse_args(int argc, char **argv, Args &a) {
    static const option options[] = {
        {"workspace",    required_argument, nullptr, OPT_WORKSPACE},
        {"jobs",         required_argument, nullptr, OPT_JOBS},
        {"keep-going",   no_argument,       nullptr, OPT_KEEP_GOING},
        {"force",        no_argument,       nullptr, OPT_FORCE},
        {"dry-run",      no_argument,       nullptr, OPT_DRY_RUN},
        {"clean",        no_argument,       nullptr, OPT_CLEAN},
        {"list-targets", no_argument,       nullptr, OPT_LIST},
        {"check-deps",   no_argument,       nullptr, OPT_CHECK_DEPS},
        {"git-info",     no_argument,       nullptr, OPT_GIT_INFO},
        {"log-file",     required_argument, nullptr, OPT_LOG_FILE},
        {"verbose",      no_argument,       nullptr, OPT_VERBOSE},
        {"version",      no_argument,       nullptr, OPT_VERSION},
        {"help",         no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}};

    for (int c; (c = getopt_long(argc, argv, "h", options, nullptr)) != -1;) {
        switch (c) {
            case OPT_WORKSPACE:  a.workspace = optarg;       break;
            case OPT_JOBS: {
                char *end = nullptr;
                long n = std::strtol(optarg, &end, 10);
                if (*end != '\0' || n < 1) { std::cerr << "error: --jobs needs a positive number\n"; return false; }
                a.run.workers = static_cast<int>(n);
                break;
            }
            case OPT_KEEP_GOING: a.run.keep_going = true;    break;
            case OPT_FORCE:      a.run.force = true;         break;
            case OPT_DRY_RUN:    a.run.dry_run = true;       break;
            case OPT_CLEAN:      a.clean = true;             break;
            case OPT_LIST:       a.list = true;              break;
            case OPT_CHECK_DEPS: a.check_deps = true;        break;
            case OPT_GIT_INFO:   a.git_info = true;          break;
            case OPT_LOG_FILE:   a.log_file = optarg;        break;
            case OPT_VERBOSE:    a.verbose = true;           break;
            case OPT_VERSION:    a.version = true;           break;
            case 'h':            std::cout << USAGE; std::exit(0);
            default:             std::cerr << USAGE; return false;
        }
    }
    for (int i = optind; i < argc; ++i) a.targets.push_back(argv[i]);
    return true;
}

static bool is_inside_workspace(const std::string &path) {
    if (path.empty() || path[0] == '/') return false;
    for (const auto &part : fs::path(path))
        if (part == "..") return false;
    return true;
}

static void clean(const Config &cfg) {
    std::vector<std::string> paths = {".mekanism"};
    for (const auto &t : cfg.targets)
        for (const auto &o : t.outputs) paths.push_back(o);

    int removed = 0;
    for (const auto &p : paths) {
        if (!is_inside_workspace(p)) { log_warn("not deleting '%s': path leaves the workspace", p.c_str()); continue; }
        std::error_code ec;
        if (fs::remove_all(full_path(cfg.root, p), ec) > 0) { log_info("removed %s", p.c_str()); ++removed; }
    }
    log_info("clean: %d path(s) removed", removed);
}

static void list_targets(const Config &cfg) {
    for (const auto &t : cfg.targets) {
        std::string line = t.name;
        if (!t.deps.empty())    line += "  deps: "    + join(t.deps);
        if (!t.sources.empty()) line += "  sources: " + std::to_string(t.sources.size());
        if (!t.outputs.empty()) line += "  outputs: " + join(t.outputs);
        log_info("%s", line.c_str());
    }
}

int main(int argc, char **argv) {
    Args args;
    if (!parse_args(argc, argv, args)) return 2;
    if (args.version) {
        std::cout << "mekanism " << mekanism::VERSION << " by " << mekanism::AUTHOR << "\n";
        return 0;
    }

    std::error_code ec;
    std::string root = fs::absolute(args.workspace, ec).lexically_normal().string();
    while (root.size() > 1 && root.back() == '/') root.pop_back();

    log_init(args.verbose, args.log_file);
    if (args.git_info) {
        log_info("branch: %s", builtin_var("git.branch", root)->c_str());
        log_info("commit: %s", builtin_var("git.commit", root)->c_str());
        log_info("dirty:  %s", builtin_var("git.dirty", root)->c_str());
        return 0;
    }

    auto start = std::chrono::steady_clock::now();
    log_info("mekanism %s, platform %s, workspace %s", std::string(mekanism::VERSION).c_str(), platform_name(), root.c_str());

    Config cfg;
    cfg.root = root;
    if (!load_config(cfg, "BUILD.mek")) return 1;
    if (args.list)  { list_targets(cfg); return 0; }
    if (args.clean) { clean(cfg);        return 0; }

    std::vector<std::string> requested = args.targets;
    if (args.check_deps) for (const auto &t : cfg.targets) requested.push_back(t.name);
    if (requested.empty()) requested = default_targets(cfg);
    if (requested.empty()) { log_error("BUILD.mek has no targets"); return 1; }

    Plan plan;
    if (!make_plan(cfg, requested, plan)) return 1;
    if (args.check_deps) { log_info("dependencies ok: %zu target(s)", cfg.targets.size()); return 0; }

    log_info("targets: %s", join(requested, ", ").c_str());
    RunStats stats;
    bool ok = run_plan(root, plan, args.run, stats);

    auto took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    log_info("%s: %d built, %d up to date, %d failed, %d not run, %.2fs", ok ? "done" : "FAILED",
             stats.built, stats.up_to_date, stats.failed, stats.not_run, took);
    return ok ? 0 : 1;
}
