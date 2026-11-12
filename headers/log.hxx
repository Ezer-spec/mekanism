#pragma once
#include <string>

// Plain text logging, one line per call, safe to call from any thread
// Everything goes to stdout (and to the log file if one was given) -__=
//   info  -> "text"
//   warn  -> "warning: text"
//   error -> "error: text"
//   debug -> "debug: text"   (only with --verbose)

void log_init(bool verbose, const std::string &log_file);

#define LOG_FORMAT(a, b) __attribute__((format(printf, a, b)))
void log_info (const char *fmt, ...) LOG_FORMAT(1, 2);
void log_warn (const char *fmt, ...) LOG_FORMAT(1, 2);
void log_error(const char *fmt, ...) LOG_FORMAT(1, 2);
void log_debug(const char *fmt, ...) LOG_FORMAT(1, 2);
