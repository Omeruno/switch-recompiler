/* Public authored IA32 mixed workloads. No game binaries or external assets. */
#include <windows.h>
__declspec(dllexport) volatile DWORD NativeMixedProbe[16];
__declspec(dllexport) volatile DWORD NativeMixedData[1024];
#define P NativeMixedProbe
static void Stage(DWORD n){P[0]=n;while(P[15]!=n)Sleep(0);}
static __attribute__((noinline)) DWORD Kernel(DWORD kind,DWORD count){
    DWORD a=0x12345678,b=0,other=0,index=0;
    if(kind==0){
        __asm__ volatile("1: leal 7(%%eax,%%eax,2),%%eax\n\tdecl %%ecx\n\tjnz 1b"
            : "+a"(a),"+c"(count) : : "cc");
    }else if(kind==1){
        __asm__ volatile("1: movl (%%esi,%%edi,4),%%edx\n\taddl %%edx,%%eax\n\txorl %%eax,(%%esi,%%edi,4)\n\t"
            "incl %%edi\n\tandl $1023,%%edi\n\tdecl %%ecx\n\tjnz 1b"
            : "+a"(a),"+c"(count),"+D"(index),"=d"(b) : "S"(NativeMixedData) : "cc","memory");
    }else if(kind==2){
        __asm__ volatile("movl %%esp,%[before]\n\t"
            "1: pushl %%eax\n\tpopl %%edx\n\tleal 7(%%eax,%%edx,2),%%eax\n\tdecl %%ecx\n\tjnz 1b\n\t"
            "movl %%esp,%[after]"
            : "+a"(a),"+c"(count),"=d"(b),[before]"=m"(P[6]),[after]"=m"(P[7]) : : "cc","memory");
    }else if(kind==3){
        __asm__ volatile("movl %%esp,%[before]\n\t"
            "1: call 2f\n\tdecl %%ecx\n\tjnz 1b\n\tjmp 3f\n\t"
            "2: leal 7(%%eax,%%eax,2),%%eax\n\tret\n\t3: movl %%esp,%[after]"
            : "+a"(a),"+c"(count),[before]"=m"(P[6]),[after]"=m"(P[7]) : : "cc","memory");
    }else{
        __asm__ volatile("movl %%esp,%[before]\n\t"
            "1: movl (%%esi,%%edi,4),%%edx\n\taddl %%edx,%%eax\n\tpushl %%eax\n\tcall 2f\n\tpopl %%ebx\n\t"
            "xorl %%ebx,%%eax\n\tmovl %%eax,(%%esi,%%edi,4)\n\tincl %%edi\n\tandl $1023,%%edi\n\tdecl %%ecx\n\tjnz 1b\n\tjmp 3f\n\t"
            "2: leal 7(%%eax,%%eax,2),%%eax\n\tret\n\t3: movl %%esp,%[after]"
            : "+a"(a),"+c"(count),"+D"(index),"=d"(b),"=b"(other),[before]"=m"(P[6]),[after]"=m"(P[7])
            : "S"(NativeMixedData) : "cc","memory");
    }
    return a;
}
void WINAPI start(void){
    DWORD i,kind=P[1];if(kind>4)ExitProcess(99);
    for(i=0;i<1024;++i)NativeMixedData[i]=(i*0x10203u)^0x87654321u;
    Stage(1);P[2]=Kernel(kind,4096);
    Stage(2);P[3]=Kernel(kind,12288);
    Stage(3);ExitProcess(0);
}
