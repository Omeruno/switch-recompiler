#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace visual {
// Check the strongest recognized SHA-2 algorithm; a weaker match cannot override it.
bool script_integrity_matches(const std::string &, const uint8_t *, size_t);
} // namespace visual
