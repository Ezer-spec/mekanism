#include "headers/log.hxx"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

static std::mutex g_mutex;
static bool       g_verbose = false;
static std::FILE *g_file    = nullptr;

void log_init(bool verbose, const std::string &log_file) {
    g_verbose = verbose;
    if (!log_file.empty()) g_file = std::fopen(log_file.c_str(), "a");
    if (!log_file.empty() && !g_file)
        std::fprintf(stderr, "warning: cannot open log file %s\n", log_file.c_str());
}

static void emit(const char *prefix, const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int size = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (size < 0) return;

    std::vector<char> text(static_cast<size_t>(size) + 1);
    std::vsnprintf(text.data(), text.size(), fmt, ap);

    std::lock_guard<std::mutex> lock(g_mutex);
    std::printf("%s%s\n", prefix, text.data());
    std::fflush(stdout);
    if (g_file) {
        std::fprintf(g_file, "%s%s\n", prefix, text.data());
        std::fflush(g_file);
    }
}

void log_info(const char *fmt, ...)  { va_list ap; va_start(ap, fmt); emit("",         fmt, ap); va_end(ap); }
void log_warn(const char *fmt, ...)  { va_list ap; va_start(ap, fmt); emit("warning: ", fmt, ap); va_end(ap); }
void log_error(const char *fmt, ...) { va_list ap; va_start(ap, fmt); emit("error: ",   fmt, ap); va_end(ap); }
void log_debug(const char *fmt, ...) {
    if (!g_verbose) return;
    va_list ap; va_start(ap, fmt); emit("debug: ", fmt, ap); va_end(ap);
}
