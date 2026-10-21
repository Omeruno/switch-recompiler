// An explicit PE TLS directory avoids compiler-specific emulated TLS helpers.
static DWORD life_tls_index;
static DWORD life_template=LIFE_SEED;
static void NTAPI LifeTlsCallback(PVOID module,DWORD reason,PVOID reserved);
static PIMAGE_TLS_CALLBACK life_callbacks[]={LifeTlsCallback,0};
const IMAGE_TLS_DIRECTORY32 _tls_used __attribute__((used,section(".rdata$T")))={
    (DWORD)&life_template,(DWORD)(&life_template+1),(DWORD)&life_tls_index,
    (DWORD)life_callbacks,4,0
};
static DWORD* life_tls(void) {
    DWORD* vector;
    __asm__ volatile("movl %%fs:0x2c,%0":"=r"(vector));
    return (DWORD*)vector[life_tls_index];
}
static void NTAPI LifeTlsCallback(PVOID module,DWORD reason,PVOID reserved) {
    DWORD* block=life_tls();
    DWORD tid=GetCurrentThreadId();
    (void)module;
    life_record(LIFE_ID,1,reason,block[0],(DWORD)reserved);
    if (reason==DLL_PROCESS_ATTACH || reason==DLL_THREAD_ATTACH) {
        if (block[0]!=LIFE_SEED || block[1]!=0) life_state()->failure|=1;
        block[0]=tid^LIFE_ID;
        block[1]=0xfeed;
    }
}
__declspec(dllexport) DWORD* LifeTlsAddress(void) { return life_tls(); }
