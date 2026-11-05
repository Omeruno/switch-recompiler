#include "ca_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    size_t roots, objects;
    int reject;
    const char *directory;
} Native;
static uint32_t import(void *context, const char *pem, size_t bytes) {
    Native *native = context;
    // Mimic the constraints missed by the original importer.
    if (!bytes || pem[bytes - 1] || strlen(pem) + 1 != bytes || native->objects >= 71)
        return 0x1b47b;
    size_t count = 0;
    const char *p = pem;
    while ((p = strstr(p, "-----BEGIN CERTIFICATE-----"))) {
        count++;
        p++;
    }
    assert(count > 0 && count <= 16);
    if (native->reject)
        return 0x1b47b;
    if (native->directory) {
        char name[2048];
        snprintf(name, sizeof(name), "%s/batch-%zu.pem", native->directory, native->objects);
        FILE *file = fopen(name, "wb");
        assert(file);
        assert(fwrite(pem, 1, bytes, file) == bytes);
        assert(!fclose(file));
    }
    native->roots += count;
    native->objects++;
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *bundle = calloc(1, (size_t)size + 1);
    assert(bundle);
    assert(fread(bundle, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    size_t roots = 0;
    const char *p = bundle;
    while ((p = strstr(p, "-----BEGIN CERTIFICATE-----"))) {
        roots++;
        p++;
    }
    assert(roots > 71);
    Native native = {.directory = argc == 3 ? argv[2] : NULL};
    BrowserCAReport report;
    assert(browser_ca_import_batches(bundle, (size_t)size, import, &native, &report));
    assert(report.certificates == roots && native.roots == roots);
    assert(native.objects == (roots + 15) / 16 && native.objects <= 71);
    native = (Native){.reject = 1};
    assert(!browser_ca_import_batches(bundle, (size_t)size, import, &native, &report));
    assert(report.native_result == 0x1b47b && report.failed_first == 1 && report.failed_last == 16);
    const char *bad = "-----BEGIN CERTIFICATE-----broken";
    assert(!browser_ca_import_batches(bad, strlen(bad), import, &native, &report));
    assert(!browser_ca_import_batches("none", 4, import, &native, &report));
    const char nul[] = "-----BEGIN CERTIFICATE-----\0bad-----END CERTIFICATE-----";
    assert(!browser_ca_import_batches(nul, sizeof(nul) - 1, import, &native, &report));
    free(bundle);
    puts("Horizon CA batches and NUL-terminated IPC payload: PASS");
    return 0;
}
