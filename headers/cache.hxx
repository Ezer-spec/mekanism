#pragma once
#include <string>

#include "headers/plan.hxx"

// The cache is the files the compiler leaves behind, like in make:
//   .mekanism/obj/<target>/<source>.o     object of one source
//   .mekanism/obj/<target>/<source>.d     headers the object depends on (written by the compiler)
//   .mekanism/obj/<target>/<source>.cmd   command that produced the object
//   .mekanism/stamp/<target>.cmd          command that produced the target's outputs

// Empty string: the job is up to date. Otherwise the reason it has to run
// `dep_ran` tells that a dependency with outputs was rebuilt in this run
std::string stale_reason(const std::string &root, const Job &job, bool dep_ran);

void cache_prepare(const std::string &root, const Job &job);   // before the command: create dirs, forget old command
void cache_commit (const std::string &root, const Job &job);   // after success: remember the command
void cache_discard(const std::string &root, const Job &job);   // after failure: remove half-written outputs
