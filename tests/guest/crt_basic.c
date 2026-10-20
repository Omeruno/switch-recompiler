/* crt_basic.c — обычная программа на C с CRT mingw (msvcrt.dll):
 * printf/sprintf с числами с плавающей точкой, malloc/realloc/free,
 * строки, qsort, setjmp, математика, аргументы командной строки. */
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (cond) printf("ok   %s\n", #cond); else { printf("FAIL %s (line %d)\n", #cond, __LINE__); failures++; } } while (0)

static int cmp_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }
static jmp_buf jb;
static void jump_back(void) { longjmp(jb, 7); }
static __thread int tls_value = 123;

int main(int argc, char** argv) {
    char buf[128];
    CHECK(argc >= 1 && argv[0] != NULL);
    sprintf(buf, "%d %u %x %s %c %5.2f %e", -42, 42u, 255, "str", 'Z', 3.14159, 12345.678);
    CHECK(strcmp(buf, "-42 42 ff str Z  3.14 1.234568e+004") == 0 || strcmp(buf, "-42 42 ff str Z  3.14 1.234568e+04") == 0);
    printf("formatted: %s\n", buf);

    int* v = malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) v[i] = (i * 7) % 10;
    v = realloc(v, 1000 * sizeof(int));
    qsort(v, 10, sizeof(int), cmp_int);
    int sorted = 1;
    for (int i = 1; i < 10; i++) if (v[i - 1] > v[i]) sorted = 0;
    CHECK(sorted);
    free(v);

    char* d = strdup("hello world");
    CHECK(strlen(d) == 11 && strchr(d, 'w') == d + 6 && strstr(d, "wor") == d + 6);
    CHECK(strtol("-1234", NULL, 10) == -1234 && atoi("77") == 77);
    CHECK(fabs(atof("2.5") - 2.5) < 1e-12 && fabs(strtod("1e3", NULL) - 1000.0) < 1e-9);
    free(d);

    CHECK(fabs(sqrt(2.0) - 1.4142135623730951) < 1e-15);
    CHECK(fabs(sin(1.0) - 0.8414709848078965) < 1e-12 && fabs(pow(2.0, 10.0) - 1024.0) < 1e-9);
    CHECK(floor(-2.5) == -3.0 && ceil(2.1) == 3.0 && (int)2.9 == 2 && (long long)1e15 == 1000000000000000LL);

    int r = setjmp(jb);
    if (r == 0) jump_back();
    CHECK(r == 7);
    CHECK(tls_value == 123);

    printf("crt_basic: %s\n", failures ? "FAILED" : "PASSED");
    return failures;
}
