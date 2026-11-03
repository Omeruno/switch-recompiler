/* Authored public IA32 address-arithmetic workload; no game code or assets. */
#include <windows.h>
__declspec(dllexport) volatile DWORD NativeAddressProbe[16];
#define P NativeAddressProbe
static void Stage(DWORD n){P[0]=n;while(P[15]!=n)Sleep(0);}
static __attribute__((noinline)) DWORD Kernel(DWORD count,DWORD* second){
    DWORD a=0x12345678,b=0x87654321;
    __asm__ volatile("1: leal 0x10203(%%eax,%%edx,2),%%eax\n\t"
        "leal -17(%%edx,%%eax,4),%%edx\n\txorl %%eax,%%edx\n\tdecl %%ecx\n\tjnz 1b"
        : "+a"(a),"+d"(b),"+c"(count) : : "cc");
    *second=b;return a;
}
void WINAPI start(void){
    DWORD second;
    Stage(1);P[1]=Kernel(4096,&second);P[2]=second;
    Stage(2);P[3]=Kernel(12288,&second);P[4]=second;
    Stage(3);ExitProcess(0);
}
