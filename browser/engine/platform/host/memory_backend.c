#include "linear_memory.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
typedef struct {
    void *raw;
    size_t raw_size;
    uintptr_t base;
    size_t committed;
} Region;
static uint32_t reserve(void *ctx, size_t size, uintptr_t *base, void **token) {
    (void)ctx;
    Region *r = calloc(1, sizeof(*r));
    if (!r)
        return ENOMEM;
    if (size > SIZE_MAX - 3 * ENGINE_MEMORY_GRANULE) {
        free(r);
        return EINVAL;
    }
    r->raw_size = size + 3 * ENGINE_MEMORY_GRANULE;
    r->raw = mmap(NULL, r->raw_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r->raw == MAP_FAILED) {
        uint32_t code = errno;
        free(r);
        return code;
    }
    r->base = ((uintptr_t)r->raw + 2 * ENGINE_MEMORY_GRANULE - 1) &
              ~(uintptr_t)(ENGINE_MEMORY_GRANULE - 1);
    *base = r->base;
    *token = r;
    return 0;
}
static uint32_t commit(void *ctx, void *token, size_t offset, size_t bytes) {
    (void)ctx;
    Region *r = token;
    if (offset != r->committed)
        return EINVAL;
    if (mprotect((void *)(r->base + offset), bytes, PROT_READ | PROT_WRITE))
        return errno;
    r->committed += bytes;
    return 0;
}
static uint32_t decommit(void *ctx, void *token, size_t *released) {
    (void)ctx;
    Region *r = token;
    *released = 0;
    if (!r->committed)
        return 0;
    if (mprotect((void *)r->base, r->committed, PROT_NONE))
        return errno;
    if (madvise((void *)r->base, r->committed, MADV_DONTNEED))
        return errno;
    *released = r->committed;
    r->committed = 0;
    return 0;
}
static uint32_t release(void *ctx, void *token) {
    (void)ctx;
    Region *r = token;
    if (munmap(r->raw, r->raw_size))
        return errno;
    free(r);
    return 0;
}
EngineMemoryBackend engine_memory_backend(void) {
    return (EngineMemoryBackend){NULL, reserve, commit, decommit, release};
}
