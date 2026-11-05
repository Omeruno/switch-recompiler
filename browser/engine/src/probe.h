#ifndef SWITCH_ENGINE_PROBE_H
#define SWITCH_ENGINE_PROBE_H
#include <stdio.h>
int engine_probe_run(FILE *report);
// Parse/validate only. Never instantiate the game or bind its web imports.
int engine_probe_load_only(const char *path, FILE *report);
#endif
