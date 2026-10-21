// Standalone libnx ARM64 JIT feasibility probe; never linked into the
// Win32 runtime. Execute only on your own Switch with known-good backup.
// Expected: jitCreate -> writable -> executable -> function returns 42.
#include <switch.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    consoleInit(nullptr);
    std::printf("Switch Recompiler / JIT permission probe\n");
    Jit jit{};
    Result rc=jitCreate(&jit,0x1000);
    if(R_FAILED(rc)) {
        std::printf("jitCreate failed: 0x%08x\n",rc);
    } else {
        rc=jitTransitionToWritable(&jit);
        if(R_FAILED(rc)) {
            std::printf("jitTransitionToWritable failed: 0x%08x\n",rc);
        } else {
            // movz w0,#42; ret
            const uint32_t instructions[2]={0x52800540u,0xd65f03c0u};
            void* writeAddr=jitGetRwAddr(&jit);
            if(!writeAddr) {
                std::puts("jitGetRwAddr returned null");
            } else {
                std::memcpy(writeAddr,instructions,sizeof(instructions));
                // Synchronize generated code to the instruction stream
                // using compiler-supported cache clearing.
                __builtin___clear_cache(
                    static_cast<char*>(writeAddr),
                    static_cast<char*>(writeAddr)+sizeof(instructions));
                rc=jitTransitionToExecutable(&jit);
                if(R_FAILED(rc)) {
                    std::printf("jitTransitionToExecutable failed: 0x%08x\n",rc);
                } else {
                    void* execAddr=jitGetRxAddr(&jit);
                    if(!execAddr) {
                        std::puts("jitGetRxAddr returned null");
                    } else {
                        __builtin___clear_cache(
                            static_cast<char*>(execAddr),
                            static_cast<char*>(execAddr)+sizeof(instructions));
                        using Fn=int(*)();
                        Fn fn=reinterpret_cast<Fn>(execAddr);
                        const int value=fn();
                        std::printf("ARM64 JIT returned %d: %s\n",
                                    value,value==42?"PASSED":"FAILED");
                    }
                }
            }
        }
        const Result closeResult=jitClose(&jit);
        if(R_FAILED(closeResult))
            std::printf("jitClose failed: 0x%08x\n",closeResult);
    }
    std::puts("Press + to exit");
    while(appletMainLoop()) {
        hidScanInput();
        if(hidKeysDown(CONTROLLER_P1_AUTO)&KEY_PLUS) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 0;
}
