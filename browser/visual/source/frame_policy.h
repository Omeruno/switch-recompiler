#pragma once
#include "resources.h"
namespace visual {
bool frame_source_allowed(const std::vector<std::string> &, const std::string &source,
                          const std::string &target);
// The embedding response is checked before parsing any foreign HTML or JS.
bool frame_response_allowed(const Resource &, const std::vector<std::string> &ancestors);
} // namespace visual
