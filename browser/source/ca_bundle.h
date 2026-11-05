#ifndef SWITCH_BROWSER_CA_BUNDLE_H
#define SWITCH_BROWSER_CA_BUNDLE_H
#include <stddef.h>
#include <stdint.h>
// ImportServerPki accepts multiple PEM certificates per object; Horizon allows
// only 71 objects per context. Size passed to callback includes the final NUL.
typedef uint32_t (*BrowserCAImport)(void *context, const char *pem, size_t bytes);
typedef struct {
    size_t certificates, objects, failed_first, failed_last;
    uint32_t native_result;
    const char *error;
} BrowserCAReport;
int browser_ca_import_batches(const char *bundle, size_t bytes, BrowserCAImport importer,
                              void *context, BrowserCAReport *report);
#endif
