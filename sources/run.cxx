#include "headers/run.hxx"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "headers/builtins.hxx"
#include "headers/cache.hxx"
#include "headers/log.hxx"
#include "headers/util.hxx"

enum State { WAITING, BUILT, UP_TO_DATE, FAILED, SKIPPED };

// Everything the worker threads share. Fields marked (lock) are guarded by `mutex`
struct Sched {
    const std::string *root = nullptr;
    const Plan        *plan = nullptr;
    const RunOptions  *opt  = nullptr;

    std::mutex                    mutex;
    std::condition_variable       wakeup;
    std::deque<int>               ready;      // (lock) jobs whose dependencies are done
    std::vector<int>              waiting_for; // (lock) number of unfinished dependencies
    std::vector<char>             poisoned;   // (lock) a dependency failed or was skipped
    std::vector<std::vector<int>> users;      // jobs that depend on job i
    std::vector<int>              state;      // (lock) State of every job
    std::vector<char>             ran;        // job executed (or would, with --dry-run); written before the job finishes
    int  running = 0, finished = 0;           // (lock)
    bool stop = false;                        // (lock) a job failed and --keep-going is off

    std::atomic<int> counter{0};              // number shown in "[n/total]"
    int total = 0, width = 1;                 // jobs that have a command, digits in total
};

static std::string seconds(std::chrono::steady_clock::duration d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2fs", std::chrono::duration<double>(d).count());
    return buf;
}

static void say(const Sched &s, int n, const char *action, const Job &job, const std::string &note) {
    log_info("[%*d/%d] %-7s %s  %s  (%s)", s.width, n, s.total, action, job.target.c_str(), job.what.c_str(), note.c_str());
}

// Decides, runs and reports one job. Called without the lock held.
static int execute(Sched &s, int index) {
    const Job &job = s.plan->jobs[index];
    if (job.cmd.empty()) return UP_TO_DATE;   // target that only groups its dependencies

    bool dep_ran = false;
    for (int d : job.deps)
        if (s.ran[d] && !s.plan->jobs[d].outputs.empty()) dep_ran = true;

    std::string reason = s.opt->force ? "forced by --force" : stale_reason(*s.root, job, dep_ran);
    int n = ++s.counter;
    if (reason.empty()) { say(s, n, "skip", job, "up to date"); return UP_TO_DATE; }

    say(s, n, job.kind.c_str(), job, reason);
    s.ran[index] = 1;
    if (s.opt->dry_run) { log_info("          command: %s", job.cmd.c_str()); return BUILT; }

    log_debug("command: %s", job.cmd.c_str());
    cache_prepare(*s.root, job);
    auto start = std::chrono::steady_clock::now();
    CmdResult r = run_shell(job.cmd, *s.root);
    std::string took = seconds(std::chrono::steady_clock::now() - start);

    std::string problem;
    if (r.code != 0) problem = "command exited with code " + std::to_string(r.code);
    else for (const auto &out : job.outputs)
        if (!file_exists(full_path(*s.root, out))) { problem = "command did not create '" + out + "'"; break; }

    std::string output = trim(r.output);
    if (problem.empty()) {
        cache_commit(*s.root, job);
        log_debug("%s %s finished in %s", job.kind.c_str(), job.what.c_str(), took.c_str());
        if (!output.empty()) log_info("%s", output.c_str());   // compiler warnings and the like
        return BUILT;
    }
    cache_discard(*s.root, job);
    log_error("%s %s (target %s) failed after %s: %s\ncommand: %s%s%s", job.kind.c_str(), job.what.c_str(),
              job.target.c_str(), took.c_str(), problem.c_str(), job.cmd.c_str(),
              output.empty() ? "" : "\n", output.c_str());
    return FAILED;
}

// Records the result and releases dependents. Lock must be held
static void finish(Sched &s, int index, int state) {
    s.state[index] = state;
    ++s.finished;
    if (state == FAILED && !s.opt->keep_going) s.stop = true;
    bool bad = state == FAILED || state == SKIPPED;
    for (int u : s.users[index]) {
        if (bad) s.poisoned[u] = 1;
        if (--s.waiting_for[u] > 0) continue;
        if (!s.poisoned[u]) { s.ready.push_back(u); continue; }
        const Job &job = s.plan->jobs[u];
        if (!job.cmd.empty()) say(s, ++s.counter, "skip", job, "a dependency failed");
        finish(s, u, SKIPPED);
    }
}

static bool all_done(const Sched &s) {
    return s.finished == static_cast<int>(s.plan->jobs.size()) || (s.stop && s.running == 0);
}

static void worker(Sched &s) {
    std::unique_lock<std::mutex> lock(s.mutex);
    while (true) {
        s.wakeup.wait(lock, [&] { return all_done(s) || (!s.stop && !s.ready.empty()); });
        if (all_done(s)) return;
        int index = s.ready.front();
        s.ready.pop_front();
        ++s.running;
        lock.unlock();
        int state = execute(s, index);
        lock.lock();
        --s.running;
        finish(s, index, state);
        s.wakeup.notify_all();
    }
}

bool run_plan(const std::string &root, const Plan &plan, const RunOptions &opt, RunStats &stats) {
    size_t n = plan.jobs.size();
    Sched s;
    s.root = &root; s.plan = &plan; s.opt = &opt;
    s.waiting_for.resize(n);
    s.poisoned.assign(n, 0);
    s.users.resize(n);
    s.state.assign(n, WAITING);
    s.ran.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        s.waiting_for[i] = static_cast<int>(plan.jobs[i].deps.size());
        for (int d : plan.jobs[i].deps) s.users[d].push_back(static_cast<int>(i));
        if (!plan.jobs[i].cmd.empty()) ++s.total;
        if (s.waiting_for[i] == 0) s.ready.push_back(static_cast<int>(i));
    }
    s.width = static_cast<int>(std::to_string(s.total).size());

    int workers = opt.workers > 0 ? opt.workers : cpu_cores();
    workers = std::max(1, std::min<int>(workers, static_cast<int>(n)));
    log_info("plan: %d commands, %d worker(s)%s", s.total, workers, opt.dry_run ? ", dry run" : "");

    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i) threads.emplace_back(worker, std::ref(s));
    for (auto &t : threads) t.join();

    for (size_t i = 0; i < n; ++i) {
        if (plan.jobs[i].cmd.empty()) continue;
        switch (s.state[i]) {
            case BUILT:      ++stats.built;      break;
            case UP_TO_DATE: ++stats.up_to_date; break;
            case FAILED:     ++stats.failed;     break;
            default:         ++stats.not_run;    break;   // skipped, or never reached after a failure
        }
    }
    return stats.failed == 0 && stats.not_run == 0;
}
