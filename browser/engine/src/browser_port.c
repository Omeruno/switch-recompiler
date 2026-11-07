// Adapter for the exact WAMR revision in wamr.lock.json. Private ABI stays here.
#include "browser_port.h"
#include "wasm_c_api_internal.h"
#include "wasm_export.h"
#include "wasm_runtime.h"
#include <stdio.h>
bool browser_wasm_module_supported(const wasm_module_t *module, char *error, size_t size) {
    WASMModule *m = (WASMModule *)*module;
    if (m->module_type != Wasm_Module_Bytecode) {
        snprintf(error, size, "Only Wasm bytecode is supported");
        return false;
    }
    if (m->memory_count + m->import_memory_count > 1) {
        snprintf(error, size, "Multiple memories are not supported by this bridge");
        return false;
    }
    for (uint32 i = 0; i < m->memory_count; i++)
        if (m->memories[i].flags & 2) {
            snprintf(error, size, "Shared WebAssembly memory requires the worker bridge");
            return false;
        }
    for (uint32 i = 0; i < m->import_memory_count; i++)
        if (m->import_memories[i].u.memory.mem_type.flags & 2) {
            snprintf(error, size, "Shared WebAssembly memory requires the worker bridge");
            return false;
        }
    // Exported tables can grow from JavaScript even without a table.grow opcode.
    for (uint32 i = 0; i < m->table_count; i++) {
        if (m->tables[i].table_type.init_size > 65536) {
            snprintf(error, size, "Wasm table initial size exceeds quota");
            return false;
        }
        if (m->tables[i].table_type.max_size > 65536)
            m->tables[i].table_type.max_size = 65536;
        m->tables[i].table_type.possible_grow = true;
    }
    return true;
}
uint32_t browser_wasm_memory_cap(const wasm_module_t *module) {
    WASMModule *m = (WASMModule *)*module;
    return m->memory_count && m->memories[0].max_page_count < BROWSER_WASM_MAX_PAGES
               ? m->memories[0].max_page_count
               : BROWSER_WASM_MAX_PAGES;
}
void visual_wasm_memory_grown(void *module) {
    struct BrowserWasmState *s = wasm_runtime_get_custom_data(module);
    if (s) {
        s->generation++;
        if (s->grown)
            s->grown(s->owner, s);
    }
}
bool browser_wasm_start(wasm_instance_t *instance, struct BrowserWasmState *state, char *error,
                        size_t size) {
    WASMModuleInstance *inst = (WASMModuleInstance *)instance->inst_comm_rt;
    wasm_runtime_set_custom_data(instance->inst_comm_rt, state);
    uint32 index = inst->module->start_function;
    if (index == (uint32)-1)
        return true;
    WASMExecEnv *env = wasm_runtime_get_exec_env_singleton(instance->inst_comm_rt);
    if (!env) {
        snprintf(error, size, "Wasm execution environment unavailable");
        return false;
    }
    wasm_runtime_set_instruction_count_limit(env, BROWSER_WASM_CALL_BUDGET);
    uint32 args[2] = {0};
    if (wasm_runtime_call_wasm(env, &inst->e->functions[index], 0, args))
        return true;
    const char *message = wasm_runtime_get_exception(instance->inst_comm_rt);
    snprintf(error, size, "%s", message ? message : "Wasm start trapped");
    return false;
}
void browser_wasm_budget(wasm_func_t *func) {
    WASMExecEnv *env = wasm_runtime_get_exec_env_singleton(func->inst_comm_rt);
    if (env)
        wasm_runtime_set_instruction_count_limit(env, BROWSER_WASM_CALL_BUDGET);
}
bool browser_wasm_grow(wasm_memory_t *memory, uint32_t delta) {
    if (!memory || !memory->inst_comm_rt || memory->memory_idx_rt)
        return false;
    uint32_t pages = wasm_memory_size(memory);
    if (pages > BROWSER_WASM_MAX_PAGES || delta > BROWSER_WASM_MAX_PAGES - pages)
        return false;
    return wasm_runtime_enlarge_memory(memory->inst_comm_rt, delta);
}
bool browser_wasm_same_function(const wasm_func_t *a, const wasm_func_t *b) {
    return a && b && a->inst_comm_rt == b->inst_comm_rt && a->func_idx_rt == b->func_idx_rt;
}
bool browser_wasm_same_external(const wasm_extern_t *a, const wasm_extern_t *b) {
    if (!a || !b || a->kind != b->kind)
        return false;
    switch (a->kind) {
    case WASM_EXTERN_FUNC:
        return browser_wasm_same_function((const wasm_func_t *)a, (const wasm_func_t *)b);
    case WASM_EXTERN_MEMORY: {
        const wasm_memory_t *x = (const wasm_memory_t *)a, *y = (const wasm_memory_t *)b;
        return x->inst_comm_rt == y->inst_comm_rt && x->memory_idx_rt == y->memory_idx_rt;
    }
    case WASM_EXTERN_GLOBAL: {
        const wasm_global_t *x = (const wasm_global_t *)a, *y = (const wasm_global_t *)b;
        return x->inst_comm_rt == y->inst_comm_rt && x->global_idx_rt == y->global_idx_rt;
    }
    case WASM_EXTERN_TABLE: {
        const wasm_table_t *x = (const wasm_table_t *)a, *y = (const wasm_table_t *)b;
        return x->inst_comm_rt == y->inst_comm_rt && x->table_idx_rt == y->table_idx_rt;
    }
    default:
        return false;
    }
}
bool browser_wasm_table_set(wasm_table_t *table, uint32_t index, wasm_func_t *function) {
    if (function && function->inst_comm_rt != table->inst_comm_rt)
        return false;
    wasm_ref_t *ref = function ? wasm_func_as_ref(function) : NULL;
    bool result = wasm_table_set(table, index, ref);
    if (ref)
        wasm_ref_delete(ref);
    return result;
}
bool browser_wasm_table_grow(wasm_table_t *table, uint32_t delta, wasm_func_t *function) {
    if (function && function->inst_comm_rt != table->inst_comm_rt)
        return false;
    uint32_t length = wasm_table_size(table);
    if (length > 65536 || delta > 65536 - length)
        return false;
    return wasm_enlarge_table((WASMModuleInstance *)table->inst_comm_rt, table->table_idx_rt, delta,
                              function ? function->func_idx_rt : NULL_REF);
}
