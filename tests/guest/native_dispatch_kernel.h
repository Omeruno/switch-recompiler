/* Authored register-only IA32 workload,shared by EXE and loader/callback probes. */
static __attribute__((noinline)) DWORD Kernel(DWORD count,DWORD* second){
    DWORD a=0x12345678,b=0x87654321,c=0;
    __asm__ volatile("1: addl $0x10203,%%eax\n\tadcl %%eax,%%edx\n\txorl %%edx,%%eax\n\tincl %%ebx\n\tdecl %%ecx\n\tjnz 1b"
        : "+a"(a),"+d"(b),"+b"(c),"+c"(count) : : "cc");
    *second=b;return a^c;
}
