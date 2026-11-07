#pragma once
#include "wasm_c_api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BROWSER_WASM_CALL_BUDGET 5000000
#define BROWSER_WASM_MAX_PAGES 512
struct BrowserWasmState {
    uint64_t generation;
    void *owner;
    void (*grown)(void *, struct BrowserWasmState *);
};
bool browser_wasm_module_supported(const wasm_module_t *, char *, size_t);
uint32_t browser_wasm_memory_cap(const wasm_module_t *);
bool browser_wasm_start(wasm_instance_t *, struct BrowserWasmState *, char *, size_t);
void browser_wasm_budget(wasm_func_t *);
bool browser_wasm_grow(wasm_memory_t *, uint32_t);
bool browser_wasm_table_grow(wasm_table_t *, uint32_t, wasm_func_t *);
bool browser_wasm_same_function(const wasm_func_t *, const wasm_func_t *);
bool browser_wasm_same_external(const wasm_extern_t *, const wasm_extern_t *);
bool browser_wasm_table_set(wasm_table_t *, uint32_t, wasm_func_t *);
#ifdef __cplusplus
}
#endif
