#include "probe.h"
#include "fixtures.h"
#include "memory_port.h"
#include "platform_api_extension.h"
#include "platform_api_vmcore.h"
#include "wasm_export.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <unistd.h>
#endif
#ifndef ENGINE_WAMR_REVISION
#define ENGINE_WAMR_REVISION "unknown"
#endif
#define POOL_SIZE (64u * 1024u * 1024u)
static void *pool;
static int quarantined;
static void persist(FILE *report) {
    fflush(stdout);
    if (report && report != stdout) {
        fflush(report);
#ifdef __SWITCH__
        fsync(fileno(report));
#endif
    }
#ifdef __SWITCH__
    consoleUpdate(NULL);
#endif
}
void engine_probe_checkpoint(FILE *report, const char *phase, const char *detail) {
    printf("BEGIN %s %s\n", phase, detail ? detail : "");
    if (report && report != stdout)
        fprintf(report, "BEGIN %s %s\n", phase, detail ? detail : "");
    persist(report);
}
static void record(FILE *report, const char *name, int success, const char *detail,
                   uint64 elapsed) {
    printf("%s %s (%" PRIu64 " us) %s\n", success ? "PASS" : "FAIL", name, elapsed, detail);
    if (report && report != stdout) {
        fprintf(report, "%s %s (%" PRIu64 " us) %s\n", success ? "PASS" : "FAIL", name, elapsed,
                detail);
        fflush(report);
    }
    persist(report);
}
static int initialize(FILE *report) {
    if (quarantined) {
        record(report, "runtime", 0, "thread join failed earlier; exit the probe", 0);
        return 0;
    }
    engine_probe_checkpoint(report, "runtime.allocate_pool", "64 MiB");
    pool = malloc(POOL_SIZE);
    RuntimeInitArgs args = {0};
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = pool;
    args.mem_alloc_option.pool.heap_size = POOL_SIZE;
    if (pool)
        engine_probe_checkpoint(report, "runtime.full_init", "pool allocated");
    if (!pool || !wasm_runtime_full_init(&args)) {
        free(pool);
        pool = NULL;
        record(report, "runtime", 0, "initialization failed", 0);
        return 0;
    }
    printf("WAMR %s classic interpreter; no JIT/AOT; runtime pool 64 MiB\n", ENGINE_WAMR_REVISION);
    if (report && report != stdout)
        fprintf(report, "WAMR %s; interpreter only; runtime pool 64 MiB\n", ENGINE_WAMR_REVISION);
    engine_probe_checkpoint(report, "runtime.ready", "");
    return 1;
}
static void shutdown_runtime(void) {
    wasm_runtime_destroy();
    free(pool);
    pool = NULL;
}
typedef struct {
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t env;
    unsigned char *bytes;
} Loaded;
static void unload(Loaded *x) {
    if (x->env)
        wasm_runtime_destroy_exec_env(x->env);
    if (x->instance)
        wasm_runtime_deinstantiate(x->instance);
    if (x->module)
        wasm_runtime_unload(x->module);
    free(x->bytes);
    memset(x, 0, sizeof(*x));
}
static int load(const EngineFixture *fixture, Loaded *x, char error[256], FILE *report) {
    memset(x, 0, sizeof(*x));
    x->bytes = malloc(fixture->size);
    if (!x->bytes) {
        snprintf(error, 256, "input allocation failed");
        return 0;
    }
    memcpy(x->bytes, fixture->bytes, fixture->size);
    engine_probe_checkpoint(report, "fixture.load", fixture->name);
    x->module = wasm_runtime_load(x->bytes, (uint32_t)fixture->size, error, 256);
    if (!x->module)
        return 0;
    engine_probe_checkpoint(report, "fixture.instantiate", fixture->name);
    x->instance = wasm_runtime_instantiate(x->module, 64 * 1024, 0, error, 256);
    if (!x->instance)
        return 0;
    error[0] = 0;
    engine_probe_checkpoint(report, "fixture.exec_env", fixture->name);
    x->env = wasm_runtime_create_exec_env(x->instance, 64 * 1024);
    if (!x->env) {
        snprintf(error, 256, "execution environment allocation failed");
        return 0;
    }
    return 1;
}
static int run_fixture(const EngineFixture *fixture, FILE *report) {
    uint64 started = os_time_get_boot_us();
    Loaded x = {0};
    char error[256] = {0};
    EngineMemoryStats before, during, after;
    engine_platform_memory_stats(&before);
    int ok = load(fixture, &x, error, report);
    void *original_base = ok ? wasm_runtime_addr_app_to_native(x.instance, 0) : NULL;
    if (ok) {
        wasm_function_inst_t function = wasm_runtime_lookup_function(x.instance, "run");
        uint32_t results[4] = {0};
        engine_probe_checkpoint(report, "fixture.execute", fixture->name);
        bool called = function && wasm_runtime_call_wasm(x.env, function, 0, results);
        const char *exception = wasm_runtime_get_exception(x.instance);
        if (fixture->trap) {
            ok = !called && exception && strstr(exception, "out of bounds");
            snprintf(error, sizeof(error), "%s", exception ? exception : "expected trap missing");
        } else if (!called) {
            ok = 0;
            snprintf(error, sizeof(error), "%s", exception ? exception : "missing run export");
        } else
            for (unsigned i = 0; i < fixture->results; i++)
                if (results[i] != fixture->expected[i]) {
                    ok = 0;
                    snprintf(error, sizeof(error), "result[%u]=%u expected %u", i, results[i],
                             fixture->expected[i]);
                }
    }
    if (ok && (!strcmp(fixture->name, "client_memory_growth") ||
               !strcmp(fixture->name, "unshared_growth"))) {
        engine_platform_memory_stats(&during);
        void *grown_base = wasm_runtime_addr_app_to_native(x.instance, 0);
        ok = original_base && original_base == grown_base &&
             during.reserved - before.reserved == (!strcmp(fixture->name, "client_memory_growth")
                                                       ? ENGINE_MEMORY_MAX_RESERVE
                                                       : 64u * 1024u * 1024u) &&
             during.committed - before.committed == 282u * ENGINE_MEMORY_GRANULE;
        snprintf(error, sizeof(error), "stable_base=%d reserved=%zu backed=%zu quota=%u",
                 original_base == grown_base, during.reserved - before.reserved,
                 during.committed - before.committed, ENGINE_MEMORY_COMMIT_LIMIT);
        engine_probe_checkpoint(report, "memory.grown", error);
    }
    if (!ok) {
        EngineMemoryStats failed;
        char detail[192];
        engine_platform_memory_stats(&failed);
        snprintf(detail, sizeof(detail), "error=%s native=0x%08x reserved=%zu backed=%zu",
                 engine_memory_error_name(failed.error), failed.native_error, failed.reserved,
                 failed.committed);
        engine_probe_checkpoint(report, "memory.failure", detail);
    }
    engine_probe_checkpoint(report, "fixture.cleanup", fixture->name);
    unload(&x);
    engine_platform_memory_stats(&after);
    if (after.reserved != before.reserved || after.committed != before.committed ||
        after.quarantined != before.quarantined) {
        ok = 0;
        snprintf(error, sizeof(error),
                 "cleanup mismatch reserved=%zu backed=%zu quarantined=%zu native=0x%08x",
                 after.reserved, after.committed, after.quarantined, after.native_error);
    }
    record(report, fixture->name, ok, error, os_time_get_boot_us() - started);
    return ok;
}
typedef struct {
    wasm_module_inst_t instance;
    int success;
} Worker;
static void *increment_worker(void *arg) {
    Worker *worker = arg;
    if (!wasm_runtime_init_thread_env())
        return NULL;
    wasm_exec_env_t env = wasm_runtime_create_exec_env(worker->instance, 64 * 1024);
    wasm_function_inst_t function = wasm_runtime_lookup_function(worker->instance, "increment");
    uint32_t args[2] = {10000, 0};
    worker->success = env && function && wasm_runtime_call_wasm(env, function, 1, args);
    if (env)
        wasm_runtime_destroy_exec_env(env);
    wasm_runtime_destroy_thread_env();
    return NULL;
}
static int parallel_test(FILE *report) {
    const EngineFixture *fixture = NULL;
    for (unsigned i = 0; i < engine_fixture_count; i++)
        if (!strcmp(engine_fixtures[i].name, "atomics"))
            fixture = &engine_fixtures[i];
    uint64 started = os_time_get_boot_us();
    Loaded x = {0};
    char error[256] = {0};
    int ok = fixture && load(fixture, &x, error, report);
    if (ok) {
        Worker workers[2] = {{x.instance, 0}, {x.instance, 0}};
        korp_tid tids[2];
        unsigned created = 0;
        engine_probe_checkpoint(report, "threads.create", "two joinable workers");
        for (unsigned i = 0; i < 2; i++) {
            if (os_thread_create(&tids[i], increment_worker, &workers[i], 256 * 1024))
                break;
            ++created;
        }
        engine_probe_checkpoint(report, "threads.join", "");
        for (unsigned i = 0; i < created; i++)
            if (os_thread_join(tids[i], NULL)) {
                quarantined = 1;
                record(report, "two_native_threads_shared_wasm_atomics", 0,
                       "join failed; runtime retained until process exit",
                       os_time_get_boot_us() - started);
                return 0;
            }
        uint32_t result[2] = {0};
        wasm_function_inst_t read = wasm_runtime_lookup_function(x.instance, "read");
        ok = ok && created == 2 && workers[0].success && workers[1].success && read &&
             wasm_runtime_call_wasm(x.env, read, 0, result) && result[0] == 20000;
        if (!ok)
            snprintf(error, sizeof(error), "workers=%u counter=%u expected 20000", created,
                     result[0]);
    }
    record(report, "two_native_threads_shared_wasm_atomics", ok, error,
           os_time_get_boot_us() - started);
    unload(&x);
    return ok;
}
static int condition_test(FILE *report) {
    uint64 started = os_time_get_boot_us();
    korp_mutex mutex;
    korp_cond cond;
    int ok = !os_mutex_init(&mutex);
    if (ok) {
        if (os_cond_init(&cond))
            ok = 0;
        else {
            os_mutex_lock(&mutex);
            int rc = os_cond_reltimedwait(&cond, &mutex, 1000);
            os_mutex_unlock(&mutex);
            ok = rc == ETIMEDOUT || rc == BHT_TIMED_OUT;
            os_cond_destroy(&cond);
        }
        os_mutex_destroy(&mutex);
    }
    record(report, "native_condition_timeout", ok, "condition returns timeout with lock reacquired",
           os_time_get_boot_us() - started);
    return ok;
}
static const char *ordered_cases[] = {"scalar",
                                      "multivalue",
                                      "bounds_trap",
                                      "bulk_memory",
                                      "extended_const",
                                      "references",
                                      "legacy_exception",
                                      "shared_growth",
                                      "client_memory_growth",
                                      "unshared_growth",
                                      "atomics",
                                      "atomic_wait",
                                      "native_condition_timeout",
                                      "two_native_threads_shared_wasm_atomics"};
// scalar through atomic_wait are fixture cases; the order is intentionally
// conservative so a shared-memory crash cannot hide basic interpreter results.
unsigned engine_probe_case_count(void) {
    return (unsigned)(sizeof(ordered_cases) / sizeof(ordered_cases[0]))
#ifdef __SWITCH__
           + 1
#endif
        ;
}
const char *engine_probe_case_name(unsigned index) {
#ifdef __SWITCH__
    if (!index)
        return "native_tls_roundtrip";
    --index;
#endif
    return index < sizeof(ordered_cases) / sizeof(ordered_cases[0]) ? ordered_cases[index] : NULL;
}
int engine_probe_run_case(unsigned index, FILE *report) {
    const char *name = engine_probe_case_name(index);
    if (!name)
        return 1;
    engine_probe_checkpoint(report, "case.start", name);
#ifdef __SWITCH__
    if (!strcmp(name, "native_tls_roundtrip")) {
        extern int engine_platform_tls_test(void);
        engine_probe_checkpoint(report, "platform.tls", "");
        int ok = engine_platform_tls_test();
        record(report, name, ok, "compiler TLS read/write", 0);
        return ok ? 0 : 1;
    }
#endif
    if (!strcmp(name, "native_condition_timeout"))
        return condition_test(report) ? 0 : 1;
    if (!initialize(report))
        return 1;
    int ok = 0;
    if (!strcmp(name, "two_native_threads_shared_wasm_atomics"))
        ok = parallel_test(report);
    else {
        for (unsigned i = 0; i < engine_fixture_count; i++)
            if (!strcmp(engine_fixtures[i].name, name)) {
                ok = run_fixture(&engine_fixtures[i], report);
                break;
            }
    }
    if (!quarantined) {
        engine_probe_checkpoint(report, "runtime.destroy", name);
        shutdown_runtime();
    }
    engine_probe_checkpoint(report, "case.finished", name);
    return ok ? 0 : 1;
}
int engine_probe_run(FILE *report) {
    unsigned failed = 0, count = engine_probe_case_count();
    for (unsigned i = 0; i < count; i++) {
        if (engine_probe_run_case(i, report))
            failed++;
        if (quarantined)
            return 1;
    }
    printf("SUMMARY %u/%u passed. GPU/JS bindings and game execution NOT tested.\n", count - failed,
           count);
    if (report && report != stdout)
        fprintf(report, "SUMMARY %u/%u passed; GPU/JS bindings and game execution NOT tested.\n",
                count - failed, count);
    persist(report);
    return (int)failed;
}
int engine_probe_load_only(const char *path, FILE *report) {
    engine_probe_checkpoint(report, "client.open", path);
    FILE *file = fopen(path, "rb");
    if (!file) {
        record(report, "client_load_only", 0, "file not found", 0);
        return 1;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 1;
    }
    long length = ftell(file);
    rewind(file);
    if (length < 8 || length > 128 * 1024 * 1024) {
        fclose(file);
        record(report, "client_load_only", 0, "input size must be 8 B..128 MiB", 0);
        return 1;
    }
    unsigned char *bytes = malloc((size_t)length);
    if (!bytes) {
        fclose(file);
        return 1;
    }
    int read = fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!read || !initialize(report)) {
        free(bytes);
        return 1;
    }
    char error[256] = {0};
    uint64 started = os_time_get_boot_us();
    engine_probe_checkpoint(report, "client.load_only", "no instantiation");
    wasm_module_t module = wasm_runtime_load(bytes, (uint32_t)length, error, sizeof(error));
    int ok = module != NULL;
    record(report, "client_load_only", ok,
           ok ? "parsed; NOT instantiated, imports unbound, no game executed" : error,
           os_time_get_boot_us() - started);
    if (module)
        wasm_runtime_unload(module);
    shutdown_runtime();
    free(bytes);
    return ok ? 0 : 1;
}
