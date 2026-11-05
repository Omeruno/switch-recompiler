#ifndef SWITCH_ENGINE_LINEAR_MEMORY_H
#define SWITCH_ENGINE_LINEAR_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define ENGINE_MEMORY_SLOTS 32
#define ENGINE_MEMORY_GRANULE 65536u
#define ENGINE_MEMORY_MAX_RESERVE (UINT64_C(1) << 32)
#define ENGINE_MEMORY_COMMIT_LIMIT (256u * 1024u * 1024u)
typedef struct {
    void *context;
    uint32_t (*reserve)(void *, size_t, uintptr_t *, void **);
    uint32_t (*commit)(void *, void *, size_t, size_t);
    uint32_t (*decommit_all)(void *, void *, size_t *);
    uint32_t (*release)(void *, void *);
} EngineMemoryBackend;
typedef enum {
    ENGINE_MEMORY_OK,
    ENGINE_MEMORY_ARGUMENT,
    ENGINE_MEMORY_LIMIT,
    ENGINE_MEMORY_RESERVE,
    ENGINE_MEMORY_COMMIT,
    ENGINE_MEMORY_RELEASE,
    ENGINE_MEMORY_QUARANTINED,
    ENGINE_MEMORY_BACKEND_ERROR
} EngineMemoryError;
typedef struct {
    uintptr_t base;
    size_t reserved, committed;
    void *token;
    int used, quarantined;
} EngineMemoryRange;
typedef struct {
    EngineMemoryBackend backend;
    EngineMemoryRange ranges[ENGINE_MEMORY_SLOTS];
    size_t committed, reserved, peak_committed, commit_limit, minimum_reserve;
    EngineMemoryError last_error;
    uint32_t native_error;
} EngineMemory;
typedef struct {
    size_t reserved, committed, peak_committed, ranges, quarantined;
    EngineMemoryError error;
    uint32_t native_error;
} EngineMemoryStats;
// Single-threaded policy core. The WAMR adapter serializes every entrypoint.
void engine_memory_init(EngineMemory *, EngineMemoryBackend, size_t, size_t);
void *engine_memory_reserve(EngineMemory *, size_t);
int engine_memory_commit(EngineMemory *, void *, size_t);
int engine_memory_release(EngineMemory *, void *);
int engine_memory_contains(EngineMemory *, void *, size_t);
void engine_memory_stats(const EngineMemory *, EngineMemoryStats *);
const char *engine_memory_error_name(EngineMemoryError);
EngineMemoryBackend engine_memory_backend(void);
#endif
