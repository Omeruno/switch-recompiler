// libnx platform for a bounded, interpreter-only compatibility probe.
#include "platform_api_extension.h"
#include "platform_api_vmcore.h"
#include <malloc.h>

int bh_platform_init(void) { return 0; }
void bh_platform_destroy(void) {}
void *os_malloc(unsigned size) { return malloc(size); }
void *os_realloc(void *p, unsigned size) { return realloc(p, size); }
void os_free(void *p) { free(p); }
int os_vprintf(const char *format, va_list ap) { return vprintf(format, ap); }
int os_printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vprintf(format, ap);
    va_end(ap);
    return n;
}
uint64 os_time_get_boot_us(void) { return armTicksToNs(armGetSystemTick()) / 1000; }
uint64 os_time_thread_cputime_us(void) {
    u64 ticks = 0;
    return R_SUCCEEDED(
               svcGetInfo(&ticks, InfoType_ThreadTickCount, threadGetCurHandle(), UINT64_MAX))
               ? armTicksToNs(ticks) / 1000
               : 0;
}
korp_tid os_self_thread(void) { return threadGetSelf(); }
uint8 *os_thread_get_stack_boundary(void) {
    Thread *t = threadGetSelf();
    return t && t->stack_mirror ? (uint8 *)t->stack_mirror : NULL;
}
void os_thread_jit_write_protect_np(bool enabled) { (void)enabled; }
int os_mutex_init(korp_mutex *m) {
    memset(m, 0, sizeof(*m));
    mutexInit(&m->plain);
    return 0;
}
int os_recursive_mutex_init(korp_mutex *m) {
    memset(m, 0, sizeof(*m));
    m->recursive = true;
    rmutexInit(&m->nested);
    return 0;
}
int os_mutex_destroy(korp_mutex *m) {
    (void)m;
    return 0;
}
int os_mutex_lock(korp_mutex *m) {
    if (m->recursive)
        rmutexLock(&m->nested);
    else
        mutexLock(&m->plain);
    return 0;
}
int os_mutex_unlock(korp_mutex *m) {
    if (m->recursive)
        rmutexUnlock(&m->nested);
    else
        mutexUnlock(&m->plain);
    return 0;
}
int os_cond_init(korp_cond *c) {
    condvarInit(c);
    return 0;
}
int os_cond_destroy(korp_cond *c) {
    (void)c;
    return 0;
}
int os_cond_reltimedwait(korp_cond *c, korp_mutex *m, uint64 usec) {
    if (m->recursive && m->nested.counter != 1)
        return BHT_ERROR;
    Mutex *plain = m->recursive ? &m->nested.lock : &m->plain;
    if (m->recursive)
        m->nested.counter = 0;
    uint64 ns = usec == UINT64_MAX         ? UINT64_MAX
                : usec > UINT64_MAX / 1000 ? UINT64_MAX - 1
                                           : usec * 1000;
    Result rc = condvarWaitTimeout(c, plain, ns);
    if (m->recursive)
        m->nested.counter = 1;
    return R_SUCCEEDED(rc) ? BHT_OK : rc == 0xea01 ? ETIMEDOUT : BHT_ERROR;
}
int os_cond_wait(korp_cond *c, korp_mutex *m) { return os_cond_reltimedwait(c, m, UINT64_MAX); }
int os_cond_signal(korp_cond *c) { return R_SUCCEEDED(condvarWakeOne(c)) ? 0 : -1; }
int os_cond_broadcast(korp_cond *c) { return R_SUCCEEDED(condvarWakeAll(c)) ? 0 : -1; }
int os_usleep(uint32 usec) {
    svcSleepThread((int64_t)usec * 1000);
    return 0;
}
int os_thread_env_init(void) { return 0; }
void os_thread_env_destroy(void) {}
bool os_thread_env_inited(void) { return true; }
typedef struct {
    Thread thread;
    thread_start_routine_t entry;
    void *arg, *result;
} SwitchThread;
static __thread SwitchThread *current_worker;
static __thread volatile uintptr_t tls_probe_value;
int engine_platform_tls_test(void) {
    uintptr_t saved = tls_probe_value;
    uintptr_t expected = (uintptr_t)&saved;
    tls_probe_value = expected;
    int ok = tls_probe_value == expected;
    tls_probe_value = saved;
    return ok;
}
static void entry(void *arg) {
    SwitchThread *t = arg;
    current_worker = t;
    t->result = t->entry(t->arg);
}
int os_thread_create_with_prio(korp_tid *out, thread_start_routine_t start, void *arg,
                               unsigned stack, int priority) {
    if (!out || !start)
        return BHT_ERROR;
    SwitchThread *t = calloc(1, sizeof(*t));
    if (!t)
        return BHT_ERROR;
    size_t bytes = stack < 128 * 1024 ? 128 * 1024 : stack;
    bytes = (bytes + 4095) & ~(size_t)4095;
    t->entry = start;
    t->arg = arg;
    if (R_FAILED(threadCreate(&t->thread, entry, t, NULL, bytes, priority, -2))) {
        free(t);
        return BHT_ERROR;
    }
    if (R_FAILED(threadStart(&t->thread))) {
        threadClose(&t->thread);
        free(t);
        return BHT_ERROR;
    }
    *out = &t->thread;
    return 0;
}
int os_thread_create(korp_tid *out, thread_start_routine_t start, void *arg, unsigned stack) {
    return os_thread_create_with_prio(out, start, arg, stack, BH_THREAD_DEFAULT_PRIORITY);
}
int os_thread_join(korp_tid thread, void **result) {
    if (!thread || R_FAILED(threadWaitForExit(thread)))
        return BHT_ERROR;
    SwitchThread *t = (SwitchThread *)thread;
    if (result)
        *result = t->result;
    Result rc = threadClose(thread);
    free(t);
    return R_SUCCEEDED(rc) ? 0 : BHT_ERROR;
}
int os_thread_detach(korp_tid thread) {
    (void)thread;
    return BHT_ERROR;
} // Joinable workers only.
void os_thread_exit(void *result) {
    if (current_worker)
        current_worker->result = result;
    threadExit();
}
int os_rwlock_init(korp_rwlock *m) { return os_mutex_init(m); }
int os_rwlock_rdlock(korp_rwlock *m) { return os_mutex_lock(m); }
int os_rwlock_wrlock(korp_rwlock *m) { return os_mutex_lock(m); }
int os_rwlock_unlock(korp_rwlock *m) { return os_mutex_unlock(m); }
int os_rwlock_destroy(korp_rwlock *m) { return os_mutex_destroy(m); }

// No executable mappings or demand paging are implemented here. Shared-memory
// probes reserve a small, stable, eagerly backed range. Larger mappings fail.
#define MAP_BUDGET (64u * 1024u * 1024u)
typedef struct {
    void *address;
    size_t size;
} Mapping;
static Mapping maps[32];
static Mutex maps_lock;
static size_t mapped;
void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file) {
    (void)hint;
    if (!size || size > MAP_BUDGET || (prot & MMAP_PROT_EXEC) || (flags & MMAP_MAP_FIXED) ||
        file != -1)
        return NULL;
    size = (size + 4095) & ~(size_t)4095;
    mutexLock(&maps_lock);
    size_t i;
    for (i = 0; i < 32; i++)
        if (!maps[i].address)
            break;
    if (i == 32 || size > MAP_BUDGET - mapped) {
        mutexUnlock(&maps_lock);
        return NULL;
    }
    void *p = memalign(4096, size);
    if (p) {
        memset(p, 0, size);
        maps[i] = (Mapping){p, size};
        mapped += size;
    }
    mutexUnlock(&maps_lock);
    return p;
}
void os_munmap(void *address, size_t size) {
    (void)size;
    mutexLock(&maps_lock);
    for (size_t i = 0; i < 32; i++)
        if (maps[i].address == address) {
            mapped -= maps[i].size;
            free(address);
            maps[i] = (Mapping){0};
            break;
        }
    mutexUnlock(&maps_lock);
}
int os_mprotect(void *address, size_t size, int prot) {
    if (prot != (MMAP_PROT_READ | MMAP_PROT_WRITE))
        return -1;
    uintptr_t start = (uintptr_t)address;
    int ok = -1;
    mutexLock(&maps_lock);
    for (size_t i = 0; i < 32; i++) {
        uintptr_t base = (uintptr_t)maps[i].address;
        if (base && start >= base && start - base <= maps[i].size &&
            size <= maps[i].size - (start - base)) {
            ok = 0;
            break;
        }
    }
    mutexUnlock(&maps_lock);
    return ok;
}
void *os_mremap(void *old, size_t old_size, size_t new_size) {
    return new_size <= old_size ? old : NULL;
}
void os_dcache_flush(void) {}
void os_icache_flush(void *start, size_t size) {
    (void)start;
    (void)size;
}
