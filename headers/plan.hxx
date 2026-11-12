#pragma once
#include <string>
#include <vector>

#include "headers/config.hxx"

// One shell command and the files it reads and writes
// A target with a `compile` func becomes one "compile" job per source plus one "build" job (the link step)
struct Job {
    std::string              kind;      // clone | compile | build
    std::string              target;
    std::string              what;      // shown in the output: source file, outputs, repo dir
    std::string              cmd;       // empty = nothing to run (target that only groups its deps)
    std::vector<std::string> inputs;    // files whose mtime is compared with the outputs
    std::vector<std::string> outputs;   // no outputs = the job always runs
    std::string              depfile;   // header list written by the compiler (-MMD -MF), may be empty
    std::string              stamp;     // file that remembers the command of the last successful run
    std::vector<int>         deps;      // indexes of jobs that must finish first
};

struct Plan {
    std::vector<Job> jobs;
};

// Targets to build when none were named: `default_target`, else every target nobody depends on
std::vector<std::string> default_targets(const Config &cfg);

// Builds the job list for `requested` targets and everything they depend on
// Logs and returns false on unknown targets, missing dependencies, cycles and bad funcs
bool make_plan(Config &cfg, const std::vector<std::string> &requested, Plan &plan);
