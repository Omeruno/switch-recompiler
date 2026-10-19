/* oracle_entry.S — вход в тестовый код и возврат из него (32-битный x86).
 *
 * run_case: сохраняет состояние хоста, загружает регистры/флаги/XMM теста
 * и прыгает на код по ORACLE_CODE_BASE. Код теста заканчивается
 * эпилогом (его собирает oracle.c), который сохраняет регистры и флаги и
 * прыгает обратно на oracle_return.
 */
    .intel_syntax noprefix
    .text
    .globl run_case
    .globl oracle_return

run_case:
    push ebp
    push ebx
    push esi
    push edi
    mov [g_hostEsp], esp

    movdqu xmm0, [g_xmmIn + 0]
    movdqu xmm1, [g_xmmIn + 16]
    movdqu xmm2, [g_xmmIn + 32]
    movdqu xmm3, [g_xmmIn + 48]
    movdqu xmm4, [g_xmmIn + 64]
    movdqu xmm5, [g_xmmIn + 80]
    movdqu xmm6, [g_xmmIn + 96]
    movdqu xmm7, [g_xmmIn + 112]
    ldmxcsr [g_mxcsrIn]
    fninit
    fldcw [g_fpuCwIn]

    push dword ptr [g_inFlags]
    popfd
    mov eax, [g_in + 0]
    mov ecx, [g_in + 4]
    mov edx, [g_in + 8]
    mov ebx, [g_in + 12]
    mov ebp, [g_in + 20]
    mov esi, [g_in + 24]
    mov edi, [g_in + 28]
    mov esp, [g_in + 16]
    jmp dword ptr [g_codeAddr]

oracle_return:
    movdqu [g_xmmOut + 0], xmm0
    movdqu [g_xmmOut + 16], xmm1
    movdqu [g_xmmOut + 32], xmm2
    movdqu [g_xmmOut + 48], xmm3
    movdqu [g_xmmOut + 64], xmm4
    movdqu [g_xmmOut + 80], xmm5
    movdqu [g_xmmOut + 96], xmm6
    movdqu [g_xmmOut + 112], xmm7
    stmxcsr [g_mxcsrOut]
    fnstenv [g_fenvOut]
    fninit
    cld
    mov esp, [g_hostEsp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .section .note.GNU-stack,"",@progbits
