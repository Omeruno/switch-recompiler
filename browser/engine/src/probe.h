#ifndef SWITCH_ENGINE_PROBE_H
#define SWITCH_ENGINE_PROBE_H
#include <stdio.h>
int engine_probe_run(FILE *report);
unsigned engine_probe_case_count(void);
const char *engine_probe_case_name(unsigned index);
int engine_probe_run_case(unsigned index, FILE *report);
void engine_probe_checkpoint(FILE *report, const char *phase, const char *detail);
// Parse/validate only. Never instantiate the game or bind its web imports.
int engine_probe_load_only(const char *path, FILE *report);
#endif
