#pragma once
#include <optional>
#include <string>

const char *platform_name();   // macos | linux | freebsd | netbsd | unknown
int         cpu_cores();

// Value of a predefined variable ({var:platform}, {var:cxx.detect}, {var:git.branch}, ...)
// Computed on first use and remembered. nullopt if `name` is not a predefined variable
std::optional<std::string> builtin_var(const std::string &name, const std::string &root);
