#include "probe.h"
int main(int argc, char **argv) {
    int failed = engine_probe_run(stdout);
    if (!failed && argc == 2)
        failed = engine_probe_load_only(argv[1], stdout);
    return failed ? 1 : 0;
}
