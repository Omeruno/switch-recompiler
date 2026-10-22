/* Deliberate faults use real IA-32 and FS:[0], never host-only injections. */
struct FaultRegistration{struct FaultRegistration* next;void* handler;};
static int __cdecl fault_handler(EXCEPTION_RECORD* record,void* frame,CONTEXT* ctx,void* dispatch){
    InitFaultState* s=InitFaultControl();(void)frame;(void)dispatch;
    ++s->handlerCalls;
    if(s->mode==15){ExitThread(38);++s->handlerAfter;return 1;}
    if(s->mode==16){ExitProcess(39);++s->handlerAfter;return 1;}
    if(s->mode==8){RaiseException(INITFAULT_INNER,0,0,NULL);++s->handlerAfter;return 0;}
    if(s->mode==13){__asm__ volatile("movl 2, %%eax":::"eax");++s->handlerAfter;return 0;}
    if(s->mode==14){Sleep(5);return 1;}
    if(s->mode==2 && record->ExceptionCode==EXCEPTION_BREAKPOINT)ctx->Eip++;
    return 0;
}
static void init_fault(void){
    InitFaultState* s=InitFaultControl();struct FaultRegistration reg;void* old;
    __asm__ volatile("movl %%fs:0,%0":"=r"(old));
    if(s->mode==1 || s->mode==2 || s->mode==8 || s->mode==9 || s->mode==13 || s->mode==14 || s->mode==15 || s->mode==16){
        reg.next=(struct FaultRegistration*)old;reg.handler=(void*)fault_handler;
        __asm__ volatile("movl %0,%%fs:0"::"r"(&reg):"memory");
    }
    if(s->mode==7)__asm__ volatile("movl $0x1234,%%fs:0":::"memory");
    if(s->mode==14)SetEvent(s->go);
    if(s->mode==2)__asm__ volatile("int3");
    else if(s->mode==4)__asm__ volatile("movl 2, %%eax":::"eax");
    else if(s->mode==6){void (*fn)(void)=(void(*)(void))0;fn();}
    else if(s->mode==12)__asm__ volatile("ud2");
    else RaiseException(INITFAULT_CODE,s->mode==9?EXCEPTION_NONCONTINUABLE:0,0,NULL);
    ++s->afterFault;
    __asm__ volatile("movl %0,%%fs:0"::"r"(old):"memory");
}
