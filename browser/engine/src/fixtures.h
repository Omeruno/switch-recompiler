#ifndef SWITCH_ENGINE_FIXTURES_H
#define SWITCH_ENGINE_FIXTURES_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *name;
    const unsigned char *bytes;
    size_t size;
    uint32_t expected[2];
    unsigned results;
    int trap;
} EngineFixture;
extern const EngineFixture engine_fixtures[];
extern const unsigned engine_fixture_count;
#endif
