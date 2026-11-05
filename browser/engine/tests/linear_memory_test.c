#include "linear_memory.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct {
    size_t bytes;
} Token;
typedef struct {
    unsigned reservations, commits, decommits, releases, live;
    int reserve_fail, commit_fail, partial_fail, release_fail;
} Fake;
static uint32_t reserve(void *ctx, size_t size, uintptr_t *base, void **token) {
    Fake *f = ctx;
    (void)size;
    f->reservations++;
    if (f->reserve_fail)
        return 101;
    Token *t = calloc(1, sizeof(*t));
    CHECK(t);
    *base = (uintptr_t)f->reservations * (UINT64_C(1) << 33);
    *token = t;
    f->live++;
    return 0;
}
static uint32_t commit(void *ctx, void *token, size_t offset, size_t bytes) {
    Fake *f = ctx;
    Token *t = token;
    f->commits++;
    CHECK(offset == t->bytes);
    CHECK(bytes && !(bytes % ENGINE_MEMORY_GRANULE));
    if (f->commit_fail)
        return 102;
    t->bytes += bytes;
    return 0;
}
static uint32_t decommit(void *ctx, void *token, size_t *released) {
    Fake *f = ctx;
    Token *t = token;
    f->decommits++;
    if (f->partial_fail && t->bytes) {
        *released = ENGINE_MEMORY_GRANULE;
        t->bytes -= *released;
        return 103;
    }
    *released = t->bytes;
    t->bytes = 0;
    return 0;
}
static uint32_t release(void *ctx, void *token) {
    Fake *f = ctx;
    Token *t = token;
    f->releases++;
    CHECK(!t->bytes);
    if (f->release_fail)
        return 104;
    free(t);
    f->live--;
    return 0;
}
static EngineMemoryBackend backend(Fake *f) {
    return (EngineMemoryBackend){f, reserve, commit, decommit, release};
}
int main(void) {
    Fake f = {0};
    EngineMemory m;
    EngineMemoryStats s;
    engine_memory_init(&m, backend(&f), ENGINE_MEMORY_COMMIT_LIMIT, 0);
    void *p = engine_memory_reserve(&m, ENGINE_MEMORY_MAX_RESERVE);
    CHECK(p && !f.commits && m.committed == 0 && m.reserved == ENGINE_MEMORY_MAX_RESERVE);
    CHECK(engine_memory_commit(&m, p, 266u * ENGINE_MEMORY_GRANULE));
    CHECK(engine_memory_commit(&m, (char *)p + 266u * ENGINE_MEMORY_GRANULE,
                               16u * ENGINE_MEMORY_GRANULE));
    CHECK(m.committed == 282u * ENGINE_MEMORY_GRANULE && f.commits == 2);
    CHECK(engine_memory_commit(&m, p, ENGINE_MEMORY_GRANULE));
    CHECK(f.commits == 2);
    CHECK(!engine_memory_commit(&m, p, ENGINE_MEMORY_COMMIT_LIMIT + ENGINE_MEMORY_GRANULE));
    CHECK(m.last_error == ENGINE_MEMORY_LIMIT && f.commits == 2);
    CHECK(!engine_memory_commit(&m, (char *)p + m.committed + ENGINE_MEMORY_GRANULE, 1));
    CHECK(!engine_memory_commit(&m, (char *)p - 1, 1));
    CHECK(!engine_memory_commit(&m, (char *)p + ENGINE_MEMORY_MAX_RESERVE, 1));
    CHECK(!engine_memory_commit(&m, p, SIZE_MAX));
    size_t previous = m.committed;
    f.commit_fail = 1;
    CHECK(!engine_memory_commit(&m, p, previous + 1));
    CHECK(m.native_error == 102 && m.committed == previous);
    f.commit_fail = 0;
    CHECK(engine_memory_commit(&m, p, previous + 1));
    CHECK(m.committed == previous + ENGINE_MEMORY_GRANULE);
    CHECK(!engine_memory_release(&m, (char *)p + ENGINE_MEMORY_GRANULE));
    CHECK(engine_memory_release(&m, p));
    CHECK(!m.committed && !m.reserved && !f.live);
    CHECK(!engine_memory_release(&m, p));
    // Partial unmap: retain reservation and all still-mapped backing; retry safely.
    p = engine_memory_reserve(&m, 3u * ENGINE_MEMORY_GRANULE);
    CHECK(engine_memory_commit(&m, p, 3u * ENGINE_MEMORY_GRANULE));
    f.partial_fail = 1;
    CHECK(!engine_memory_release(&m, p));
    engine_memory_stats(&m, &s);
    CHECK(s.quarantined == 1 && s.committed == 2u * ENGINE_MEMORY_GRANULE && s.ranges == 1);
    CHECK(s.native_error == 103 && f.live == 1);
    CHECK(!engine_memory_commit(&m, p, ENGINE_MEMORY_GRANULE));
    CHECK(!engine_memory_contains(&m, p, 1));
    f.partial_fail = 0;
    CHECK(engine_memory_release(&m, p));
    CHECK(!m.reserved && !f.live);
    // Virtual release failure after all backing has already been freed.
    p = engine_memory_reserve(&m, ENGINE_MEMORY_GRANULE);
    CHECK(engine_memory_commit(&m, p, 1));
    f.release_fail = 1;
    CHECK(!engine_memory_release(&m, p));
    CHECK(!m.committed && m.reserved && f.live == 1);
    f.release_fail = 0;
    CHECK(engine_memory_release(&m, p));
    CHECK(!m.reserved && !f.live);
    // Failed reservation has no accounting side effects; invalid sizes never reach backend.
    unsigned calls = f.reservations;
    CHECK(!engine_memory_reserve(&m, 0));
    CHECK(!engine_memory_reserve(&m, SIZE_MAX));
    CHECK(!engine_memory_reserve(&m, ENGINE_MEMORY_MAX_RESERVE + 1));
    CHECK(calls == f.reservations);
    f.reserve_fail = 1;
    CHECK(!engine_memory_reserve(&m, 1));
    CHECK(m.native_error == 101 && !m.reserved);
    f.reserve_fail = 0;
    // Slot exhaustion and rounding; every reservation is eventually released.
    void *slots[ENGINE_MEMORY_SLOTS];
    for (unsigned i = 0; i < ENGINE_MEMORY_SLOTS; i++) {
        slots[i] = engine_memory_reserve(&m, 1);
        CHECK(slots[i]);
    }
    CHECK(!engine_memory_reserve(&m, 1));
    CHECK(f.live == ENGINE_MEMORY_SLOTS);
    for (unsigned i = 0; i < ENGINE_MEMORY_SLOTS; i++)
        CHECK(engine_memory_release(&m, slots[i]));
    CHECK(!m.reserved && !m.committed && !f.live);
    // Quota applies across independent ranges, not only per-memory.
    engine_memory_init(&m, backend(&f), 2u * ENGINE_MEMORY_GRANULE, 4u * ENGINE_MEMORY_GRANULE);
    p = engine_memory_reserve(&m, 1);
    void *q = engine_memory_reserve(&m, 1);
    CHECK(m.reserved == 8u * ENGINE_MEMORY_GRANULE);
    CHECK(engine_memory_commit(&m, p, 2u * ENGINE_MEMORY_GRANULE));
    CHECK(!engine_memory_commit(&m, q, 1));
    CHECK(engine_memory_release(&m, p));
    CHECK(engine_memory_commit(&m, q, 1));
    CHECK(engine_memory_release(&m, q));
    CHECK(!f.live && !m.committed && !m.reserved);
    puts("PASS linear-memory accounting, quota, backend errors, quarantine, retry and cleanup");
    return 0;
}
