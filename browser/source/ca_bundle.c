#include "ca_bundle.h"
#include <stdlib.h>
#include <string.h>
#define BATCH_CERTIFICATES 16
#define MAX_OBJECTS 71
#define BUNDLE_LIMIT (512u * 1024u)
static const char *find(const char *data, size_t bytes, const char *pattern) {
    size_t n = strlen(pattern);
    if (n > bytes)
        return NULL;
    for (size_t i = 0; i <= bytes - n; i++)
        if (!memcmp(data + i, pattern, n))
            return data + i;
    return NULL;
}
static int submit(BrowserCAImport importer, void *context, char *pem, size_t used, size_t first,
                  size_t last, BrowserCAReport *report) {
    report->failed_first = first;
    report->failed_last = last;
    if (report->objects >= MAX_OBJECTS) {
        report->error = "CA object limit";
        return 0;
    }
    pem[used] = 0;
    report->native_result = importer(context, pem, used + 1);
    if (report->native_result) {
        report->error = "native CA import rejected";
        return 0;
    }
    ++report->objects;
    report->failed_first = report->failed_last = 0;
    return 1;
}
int browser_ca_import_batches(const char *bundle, size_t bytes, BrowserCAImport importer,
                              void *context, BrowserCAReport *report) {
    if (!report)
        return 0;
    memset(report, 0, sizeof(*report));
    if (!bundle || !importer || !bytes || bytes > BUNDLE_LIMIT || memchr(bundle, 0, bytes)) {
        report->error = "invalid CA bundle";
        return 0;
    }
    char *pem = malloc(bytes + BATCH_CERTIFICATES + 1);
    if (!pem) {
        report->error = "CA allocation failed";
        return 0;
    }
    const char *cursor = bundle, *limit = bundle + bytes;
    size_t used = 0, batch = 0, first = 1;
    int ok = 1;
    while (cursor < limit) {
        const char *begin = find(cursor, (size_t)(limit - cursor), "-----BEGIN CERTIFICATE-----");
        if (!begin)
            break;
        const char *end = find(begin, (size_t)(limit - begin), "-----END CERTIFICATE-----");
        if (!end) {
            report->error = "unterminated PEM certificate";
            ok = 0;
            break;
        }
        end += strlen("-----END CERTIFICATE-----");
        const char *nested = find(begin + strlen("-----BEGIN CERTIFICATE-----"),
                                  (size_t)(end - begin) - strlen("-----BEGIN CERTIFICATE-----"),
                                  "-----BEGIN CERTIFICATE-----");
        if (nested) {
            report->error = "nested PEM certificate";
            ok = 0;
            break;
        }
        size_t n = (size_t)(end - begin);
        memcpy(pem + used, begin, n);
        used += n;
        pem[used++] = '\n';
        ++batch;
        ++report->certificates;
        cursor = end;
        if (batch == BATCH_CERTIFICATES) {
            if (!submit(importer, context, pem, used, first, report->certificates, report)) {
                ok = 0;
                break;
            }
            first = report->certificates + 1;
            batch = 0;
            used = 0;
        }
    }
    if (ok && batch)
        ok = submit(importer, context, pem, used, first, report->certificates, report);
    if (ok && !report->certificates) {
        report->error = "no CA certificates";
        ok = 0;
    }
    free(pem);
    return ok;
}
