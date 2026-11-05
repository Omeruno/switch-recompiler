#ifndef SWITCH_ENGINE_PLATFORM_INTERNAL_H
#define SWITCH_ENGINE_PLATFORM_INTERNAL_H
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <time.h>
#define BH_PLATFORM_SWITCH
#define BH_APPLET_PRESERVED_STACK_SIZE (32 * 1024)
#define BH_THREAD_DEFAULT_PRIORITY 0x3b
#define os_thread_local_attribute __thread
#define BH_HAS_DLFCN 0
#define bh_socket_t int
typedef Thread *korp_tid;
typedef Thread *korp_thread;
typedef struct {
    Mutex plain;
    RMutex nested;
    bool recursive;
} korp_mutex;
typedef CondVar korp_cond;
typedef korp_mutex korp_rwlock;
typedef int korp_sem;
typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned os_nfds_t;
typedef struct timespec os_timespec;
#define OS_THREAD_MUTEX_INITIALIZER {0}
static inline os_file_handle os_get_invalid_handle(void) { return -1; }
static inline int os_getpagesize(void) { return 4096; }
#endif
