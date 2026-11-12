#pragma once
#include <functional>
#include <optional>
#include <string>

// Replaces every {key} for which `lookup` returns a value
// Unknown {keys} are left untouched, so shell text like ${HOME} or awk '{print}' survives
using Lookup = std::function<std::optional<std::string>(const std::string &key)>;
std::string expand(const std::string &text, const Lookup &lookup);
