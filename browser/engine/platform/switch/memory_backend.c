#include "linear_memory.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

// Heap payload pages are inaccessible at their source address while mapped.
// Keep metadata in separate allocations; never free backing before unmapping.
typedef struct Chunk {
    void *source;
    size_t offset, bytes;
    struct Chunk *next;
} Chunk;
typedef struct {
    uintptr_t base;
    size_t committed;
    VirtmemReservation *reservation;
    Chunk *chunks;
} Region;
static uint32_t no_memory(void) { return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); }
static uint32_t reserve(void *context, size_t bytes, uintptr_t *base, void **token) {
    (void)context;
    if (!envIsSyscallHinted(0x04) || !envIsSyscallHinted(0x05))
        return KERNELRESULT(NotImplemented);
    Region *r = calloc(1, sizeof(*r));
    if (!r)
        return no_memory();
    size_t span = bytes + 3 * ENGINE_MEMORY_GRANULE;
    virtmemLock();
    void *raw = virtmemFindStack(span, 0);
    if (raw)
        r->reservation = virtmemAddReservation(raw, span);
    virtmemUnlock();
    if (!r->reservation) {
        free(r);
        return no_memory();
    }
    r->base =
        ((uintptr_t)raw + 2 * ENGINE_MEMORY_GRANULE - 1) & ~(uintptr_t)(ENGINE_MEMORY_GRANULE - 1);
    *base = r->base;
    *token = r;
    return 0;
}
static uint32_t commit(void *context, void *token, size_t offset, size_t bytes) {
    (void)context;
    Region *r = token;
    if (offset != r->committed)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    Chunk *c = calloc(1, sizeof(*c));
    if (!c)
        return no_memory();
    c->source = memalign(ENGINE_MEMORY_GRANULE, bytes);
    if (!c->source) {
        free(c);
        return no_memory();
    }
    memset(c->source, 0, bytes);
    Result rc = svcMapMemory((void *)(r->base + offset), c->source, bytes);
    if (R_FAILED(rc)) {
        free(c->source);
        free(c);
        return rc;
    }
    c->offset = offset;
    c->bytes = bytes;
    c->next = r->chunks;
    r->chunks = c;
    r->committed += bytes;
    return 0;
}
static uint32_t decommit(void *context, void *token, size_t *released) {
    (void)context;
    Region *r = token;
    *released = 0;
    while (r->chunks) {
        Chunk *c = r->chunks;
        Result rc = svcUnmapMemory((void *)(r->base + c->offset), c->source, c->bytes);
        if (R_FAILED(rc))
            return rc;
        r->chunks = c->next;
        r->committed -= c->bytes;
        *released += c->bytes;
        free(c->source);
        free(c);
    }
    return 0;
}
static uint32_t release(void *context, void *token) {
    (void)context;
    Region *r = token;
    if (r->chunks)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    virtmemLock();
    virtmemRemoveReservation(r->reservation);
    virtmemUnlock();
    free(r);
    return 0;
}
EngineMemoryBackend engine_memory_backend(void) {
    return (EngineMemoryBackend){NULL, reserve, commit, decommit, release};
}
