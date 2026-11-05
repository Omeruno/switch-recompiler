#include "memory_port.h"
#include "platform_api_vmcore.h"
static EngineMemory memory;
static korp_mutex memory_lock = OS_THREAD_MUTEX_INITIALIZER;
static int initialized;
static void init(void) {
    if (!initialized) {
        engine_memory_init(&memory, engine_memory_backend(), ENGINE_MEMORY_COMMIT_LIMIT,
                           64u * 1024u * 1024u);
        initialized = 1;
    }
}
static void report(void) {
    os_printf("MEMORY error=%s native=0x%08x reserved=%zu backed=%zu\n",
              engine_memory_error_name(memory.last_error), memory.native_error, memory.reserved,
              memory.committed);
}
void engine_platform_memory_stats(EngineMemoryStats *out) {
    os_mutex_lock(&memory_lock);
    init();
    engine_memory_stats(&memory, out);
    os_mutex_unlock(&memory_lock);
}
void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file) {
    (void)hint;
    if (!size || (prot & MMAP_PROT_EXEC) || (flags & MMAP_MAP_FIXED) ||
        file != os_get_invalid_handle())
        return NULL;
    os_mutex_lock(&memory_lock);
    init();
    void *base = engine_memory_reserve(&memory, size);
    if (base && prot != MMAP_PROT_NONE && !engine_memory_commit(&memory, base, size)) {
        engine_memory_release(&memory, base);
        base = NULL;
    }
    if (!base)
        report();
    os_mutex_unlock(&memory_lock);
    return base;
}
int os_mprotect(void *address, size_t size, int prot) {
    if (prot != (MMAP_PROT_READ | MMAP_PROT_WRITE))
        return -1;
    os_mutex_lock(&memory_lock);
    init();
    int ok = engine_memory_commit(&memory, address, size);
    if (!ok)
        report();
    os_mutex_unlock(&memory_lock);
    return ok ? 0 : -1;
}
void os_munmap(void *address, size_t size) {
    (void)size;
    os_mutex_lock(&memory_lock);
    init();
    if (!engine_memory_release(&memory, address))
        report();
    os_mutex_unlock(&memory_lock);
}
void *os_mremap(void *old, size_t old_size, size_t new_size) {
    (void)old_size;
    os_mutex_lock(&memory_lock);
    init();
    // WAMR calls mprotect after a successful remap and tears down that mapping
    // if protection fails. Commit here so a failed grow keeps the old memory.
    int ok = engine_memory_contains(&memory, old, new_size) &&
             engine_memory_commit(&memory, old, new_size);
    os_mutex_unlock(&memory_lock);
    return ok ? old : NULL;
}
void os_dcache_flush(void) {}
void os_icache_flush(void *start, size_t len) {
    (void)start;
    (void)len;
}
