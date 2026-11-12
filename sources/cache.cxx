#include "headers/cache.hxx"

#include <cctype>
#include <filesystem>

#include "headers/util.hxx"

namespace fs = std::filesystem;

// Headers listed in a Makefile-style .d file: "object: dep dep \<newline> dep".
static std::vector<std::string> read_depfile(const std::string &path) {
    std::vector<std::string> deps;
    auto text = read_file(path);
    if (!text) return deps;

    size_t colon = text->find(": ");
    if (colon == std::string::npos) colon = text->find(":\n");
    if (colon == std::string::npos) return deps;

    std::string word;
    for (size_t i = colon + 1; i < text->size(); ++i) {
        char c = (*text)[i];
        if (c == '\\' && i + 1 < text->size() && (*text)[i + 1] == '\n') { ++i; continue; }
        if (c == '\\' && i + 1 < text->size() && (*text)[i + 1] == ' ')  { word += ' '; ++i; continue; }
        if (isspace(static_cast<unsigned char>(c))) {
            if (!word.empty()) deps.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) deps.push_back(word);
    return deps;
}

std::string stale_reason(const std::string &root, const Job &job, bool dep_ran) {
    if (job.outputs.empty()) return "no outputs declared, always runs";

    std::optional<fs::file_time_type> oldest;
    for (const auto &out : job.outputs) {
        auto t = file_mtime(full_path(root, out));
        if (!t) return "output missing: " + out;
        if (!oldest || *t < *oldest) oldest = t;
    }

    if (!job.stamp.empty()) {
        auto previous = read_file(full_path(root, job.stamp));
        if (!previous)            return "no record of the previous run";
        if (*previous != job.cmd) return "command changed";
    }
    if (job.kind == "build" && dep_ran) return "a dependency was rebuilt";

    std::vector<std::string> inputs = job.inputs;
    if (!job.depfile.empty()) {
        if (!file_exists(full_path(root, job.depfile))) return "dependency file missing: " + job.depfile;
        for (const auto &h : read_depfile(full_path(root, job.depfile))) inputs.push_back(h);
    }
    for (const auto &in : inputs) {
        auto t = file_mtime(full_path(root, in));
        if (!t)         return "input missing: " + in;
        if (*t > *oldest) return "input is newer: " + in;
    }
    return "";
}

void cache_prepare(const std::string &root, const Job &job) {
    for (const auto &out : job.outputs) make_parent_dirs(full_path(root, out));
    if (!job.depfile.empty()) make_parent_dirs(full_path(root, job.depfile));
    if (!job.stamp.empty()) {
        std::error_code ec;
        fs::remove(full_path(root, job.stamp), ec);   // an interrupted run must not look finished
    }
}

void cache_commit(const std::string &root, const Job &job) {
    if (!job.stamp.empty()) write_file(full_path(root, job.stamp), job.cmd);
}

void cache_discard(const std::string &root, const Job &job) {
    std::error_code ec;
    for (const auto &out : job.outputs) {
        std::string path = full_path(root, out);
        if (fs::is_regular_file(path, ec)) fs::remove(path, ec);   // directories are never deleted here
    }
    if (!job.depfile.empty()) fs::remove(full_path(root, job.depfile), ec);
    if (!job.stamp.empty())   fs::remove(full_path(root, job.stamp), ec);
}
