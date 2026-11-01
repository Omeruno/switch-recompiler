#pragma once
#include <cstdint>
namespace rt {
// Portable,checked full-runtime probe used by the Switch console and host/QEMU
// gate. No assertions/success stubs. A false pump cancels and drains guest frames.
using CpuValidationPump=bool(*)(unsigned sample,uint64_t quanta,void* context);
bool RunCpuValidation(const char* executable,const char* report,const char* build,
                      CpuValidationPump pump=nullptr,void* context=nullptr,const char* environment=nullptr);
} // namespace rt
