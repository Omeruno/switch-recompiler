#pragma once
#include "hle_util.h"
namespace win32 {
// Shared bounded guest text reader. Null is an empty string; wildcard semantics
// belong to the caller. No native or guest pointer escapes this call.
bool ReadWindowLookupText(cpu::IGuestMemory&,uint32_t,bool,std::u16string&);
}
