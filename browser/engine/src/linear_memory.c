#include "linear_memory.h"
#include <string.h>
static int fail(EngineMemory *m, EngineMemoryError error, uint32_t native) {
    m->last_error = error;
    m->native_error = native;
    return 0;
}
static int round_size(size_t n, size_t *out) {
    if (!n || n > SIZE_MAX - (ENGINE_MEMORY_GRANULE - 1))
        return 0;
    *out = (n + ENGINE_MEMORY_GRANULE - 1) & ~(size_t)(ENGINE_MEMORY_GRANULE - 1);
    return 1;
}
void engine_memory_init(EngineMemory *m, EngineMemoryBackend backend, size_t limit,
                        size_t minimum) {
    memset(m, 0, sizeof(*m));
    m->backend = backend;
    m->commit_limit = limit;
    m->minimum_reserve = minimum;
}
static EngineMemoryRange *range_for(EngineMemory *m, uintptr_t address, size_t size) {
    for (size_t i = 0; i < ENGINE_MEMORY_SLOTS; i++) {
        EngineMemoryRange *r = &m->ranges[i];
        if (r->used && address >= r->base && address - r->base <= r->reserved &&
            size <= r->reserved - (address - r->base))
            return r;
    }
    return NULL;
}
void *engine_memory_reserve(EngineMemory *m, size_t requested) {
    size_t reserved;
    if (!m || !m->backend.reserve || !m->backend.commit || !m->backend.decommit_all ||
        !m->backend.release)
        return NULL;
    if (!round_size(requested, &reserved) || reserved > ENGINE_MEMORY_MAX_RESERVE) {
        fail(m, ENGINE_MEMORY_ARGUMENT, 0);
        return NULL;
    }
    if (reserved < m->minimum_reserve && !round_size(m->minimum_reserve, &reserved)) {
        fail(m, ENGINE_MEMORY_ARGUMENT, 0);
        return NULL;
    }
    if (reserved > ENGINE_MEMORY_MAX_RESERVE || reserved > SIZE_MAX - m->reserved) {
        fail(m, ENGINE_MEMORY_ARGUMENT, 0);
        return NULL;
    }
    size_t slot;
    for (slot = 0; slot < ENGINE_MEMORY_SLOTS; slot++)
        if (!m->ranges[slot].used)
            break;
    if (slot == ENGINE_MEMORY_SLOTS) {
        fail(m, ENGINE_MEMORY_LIMIT, 0);
        return NULL;
    }
    uintptr_t base = 0;
    void *token = NULL;
    uint32_t rc = m->backend.reserve(m->backend.context, reserved, &base, &token);
    if (rc) {
        fail(m, ENGINE_MEMORY_RESERVE, rc);
        return NULL;
    }
    // Backend guarantees non-overlapping, nonzero, granule-aligned reservations.
    if (!base || base % ENGINE_MEMORY_GRANULE || reserved > UINTPTR_MAX - base) {
        m->backend.release(m->backend.context, token);
        fail(m, ENGINE_MEMORY_BACKEND_ERROR, 0);
        return NULL;
    }
    m->ranges[slot] =
        (EngineMemoryRange){.base = base, .reserved = reserved, .token = token, .used = 1};
    m->reserved += reserved;
    return (void *)base;
}
int engine_memory_commit(EngineMemory *m, void *address, size_t size) {
    if (!m)
        return 0;
    EngineMemoryRange *r = range_for(m, (uintptr_t)address, size);
    if (!r)
        return fail(m, ENGINE_MEMORY_ARGUMENT, 0);
    if (r->quarantined)
        return fail(m, ENGINE_MEMORY_QUARANTINED, 0);
    size_t offset = (uintptr_t)address - r->base, end = offset + size, target;
    if (!size)
        return 1;
    if (offset > r->committed || !round_size(end, &target) || target > r->reserved)
        return fail(m, ENGINE_MEMORY_ARGUMENT, 0);
    if (target <= r->committed)
        return 1;
    size_t delta = target - r->committed;
    if (m->committed > m->commit_limit || delta > m->commit_limit - m->committed)
        return fail(m, ENGINE_MEMORY_LIMIT, 0);
    uint32_t rc = m->backend.commit(m->backend.context, r->token, r->committed, delta);
    if (rc)
        return fail(m, ENGINE_MEMORY_COMMIT, rc);
    r->committed = target;
    m->committed += delta;
    if (m->committed > m->peak_committed)
        m->peak_committed = m->committed;
    return 1;
}
int engine_memory_release(EngineMemory *m, void *address) {
    if (!m)
        return 0;
    EngineMemoryRange *r = range_for(m, (uintptr_t)address, 0);
    if (!r || r->base != (uintptr_t)address)
        return fail(m, ENGINE_MEMORY_ARGUMENT, 0);
    size_t released = 0;
    uint32_t rc = m->backend.decommit_all(m->backend.context, r->token, &released);
    if (released > r->committed) {
        r->quarantined = 1;
        return fail(m, ENGINE_MEMORY_BACKEND_ERROR, rc);
    }
    r->committed -= released;
    m->committed -= released;
    if (rc || r->committed) {
        r->quarantined = 1;
        return fail(m, ENGINE_MEMORY_RELEASE, rc);
    }
    rc = m->backend.release(m->backend.context, r->token);
    if (rc) {
        r->quarantined = 1;
        return fail(m, ENGINE_MEMORY_RELEASE, rc);
    }
    m->reserved -= r->reserved;
    memset(r, 0, sizeof(*r));
    return 1;
}
int engine_memory_contains(EngineMemory *m, void *address, size_t size) {
    EngineMemoryRange *r = range_for(m, (uintptr_t)address, size);
    return r && !r->quarantined;
}
void engine_memory_stats(const EngineMemory *m, EngineMemoryStats *out) {
    memset(out, 0, sizeof(*out));
    out->reserved = m->reserved;
    out->committed = m->committed;
    out->peak_committed = m->peak_committed;
    out->error = m->last_error;
    out->native_error = m->native_error;
    for (size_t i = 0; i < ENGINE_MEMORY_SLOTS; i++)
        if (m->ranges[i].used) {
            out->ranges++;
            if (m->ranges[i].quarantined)
                out->quarantined++;
        }
}
const char *engine_memory_error_name(EngineMemoryError e) {
    static const char *names[] = {"none",   "argument", "quota",       "reserve",
                                  "commit", "release",  "quarantined", "backend-contract"};
    return (unsigned)e < sizeof(names) / sizeof(names[0]) ? names[e] : "unknown";
}
