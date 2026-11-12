#pragma once
#include <string>

#include "headers/plan.hxx"

struct RunOptions {
    bool dry_run    = false;   // print what would run, run nothing
    bool force      = false;   // run every job, ignore the cache
    bool keep_going = false;   // after a failure keep building what does not depend on it
    int  workers    = 0;       // 0 = number of CPU cores
};

struct RunStats {
    int built = 0, up_to_date = 0, failed = 0, not_run = 0;
};

// Runs the jobs of `plan` in parallel, dependencies first. Returns true if nothing failed
bool run_plan(const std::string &root, const Plan &plan, const RunOptions &opt, RunStats &stats);
