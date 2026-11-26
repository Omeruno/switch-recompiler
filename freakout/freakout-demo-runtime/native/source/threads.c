#include "game.h"
#include <string.h>

#define IS(s) (!strcmp(name, s))
#define LOCK_FLAG 0x80000000u
#define THREAD_HANDLE 0x20001u
#define EVENT_HANDLE 0x10000u
#define SEM_HANDLE 0x100u

static int test_wait(Game *g, unsigned thread, uint32_t handle)
{
    if (handle & LOCK_FLAG) {
        unsigned i = handle & ~LOCK_FLAG;
        if (i >= MAX_LOCKS || !g->locks[i].address)
            return -1;
        if (g->locks[i].depth && g->locks[i].owner != thread)
            return 0;
        g->locks[i].owner = thread;
        ++g->locks[i].depth;
        return 1;
    }
    unsigned i = handle - SEM_HANDLE;
    if (i < MAX_SEMAPHORES && g->semaphores[i].max) {
        if (!g->semaphores[i].count)
            return 0;
        --g->semaphores[i].count;
        return 1;
    }
    i = handle - EVENT_HANDLE;
    if (i < MAX_EVENTS && g->events[i].open) {
        if (!g->events[i].signaled)
            return 0;
        if (!g->events[i].manual)
            g->events[i].signaled = false;
        return 1;
    }
    i = handle - THREAD_HANDLE;
    if (i < g->thread_count && g->threads[i].open)
        return g->threads[i].exited ? 1 : 0;
    return -1;
}

bool switch_thread(Game *g)
{
    g->import_slice = 0;
    unsigned previous = g->current_thread;
    GuestThread *active = &g->threads[previous];
    active->cpu = g->cpu;
    active->seh = rd32(0);
    for (;;) {
        uint64_t next = UINT64_MAX;
        for (unsigned step = 1; step <= g->thread_count; ++step) {
            unsigned i = (previous + step) % g->thread_count;
            GuestThread *t = &g->threads[i];
            if (t->exited || t->suspended)
                continue;
            if (t->wait) {
                int status = test_wait(g, i, t->wait);
                if (!status && t->deadline > g->clock) {
                    if (t->deadline < next)
                        next = t->deadline;
                    continue;
                }
                t->wait = 0;
                t->cpu.eax = status == 1 ? 0 : status == 0 ? 258 : UINT32_MAX;
                if (status < 0)
                    t->last_error = 6;
            } else if (t->sleeping && t->deadline > g->clock) {
                if (t->deadline < next)
                    next = t->deadline;
                continue;
            }
            t->sleeping = false;
            g->current_thread = i;
            g->cpu = t->cpu;
            wr32(0, t->seh);
            return true;
        }
        if (next == UINT64_MAX) {
            g->stop = "guest threads have no runnable work";
            return false;
        }
        g->clock = next;
    }
}

uint32_t thread_crt(Game *g)
{
    GuestThread *current = &g->threads[g->current_thread];
    if (!current->crt) {
        uint32_t address = allocate(g, 0x8c);
        if (!address)
            return 0;
        memset(guest_pointer(address), 0, 0x8c);
        wr32(address, g->current_thread + 1);
        wr32(address + 4, UINT32_MAX);
        current->crt = address;
    }
    return current->crt;
}

static bool wait_one(Game *g, uint32_t handle, uint32_t timeout, unsigned pop)
{
    int ready = test_wait(g, g->current_thread, handle);
    GuestThread *active = &g->threads[g->current_thread];
    if (ready == 1)
        return result(g, 0, pop);
    if (ready < 0) {
        active->last_error = 6;
        return result(g, UINT32_MAX, pop);
    }
    if (!timeout)
        return result(g, 258, pop);
    result(g, 0, pop);
    active->wait = handle;
    active->deadline = timeout == UINT32_MAX ? UINT64_MAX : g->clock + timeout;
    return switch_thread(g);
}

static bool critical(Game *g, const char *name, uint32_t address)
{
    if (!address)
        return false;
    unsigned i = 0, empty = MAX_LOCKS;
    for (; i < MAX_LOCKS; ++i) {
        if (g->locks[i].address == address)
            break;
        if (!g->locks[i].address && empty == MAX_LOCKS)
            empty = i;
    }
    if (i == MAX_LOCKS) {
        if (empty == MAX_LOCKS || (!IS("InitializeCriticalSection") && !IS("EnterCriticalSection")))
            return false;
        i = empty;
        g->locks[i].address = address;
    }
    if (IS("EnterCriticalSection"))
        return wait_one(g, LOCK_FLAG | i, UINT32_MAX, 4);
    if (IS("DeleteCriticalSection")) {
        if (g->locks[i].depth)
            return false;
        g->locks[i].address = 0;
    } else if (IS("LeaveCriticalSection")) {
        if (!g->locks[i].depth || g->locks[i].owner != g->current_thread)
            return false;
        --g->locks[i].depth;
    }
    return result(g, 0, 4);
}

static bool create_thread(Game *g)
{
    unsigned i = g->thread_count;
    uint32_t size = arg(g, 1), entry = arg(g, 2), value = arg(g, 3), flags = arg(g, 4);
    if (i == MAX_THREADS || arg(g, 0) || !entry || (flags & ~4u) || !has_block(entry))
        return result(g, 0, 0);
    if (size < (1u << 20))
        size = 1u << 20;
    uint32_t stack = allocate(g, size);
    if (!stack)
        return result(g, 0, 0);
    GuestThread *t = &g->threads[i];
    t->seh = UINT32_MAX;
    t->stack_base = stack;
    t->cpu.esp = ((stack + size) & ~15u) - 8;
    t->cpu.eip = entry;
    t->suspended = flags & 4;
    t->open = true;
    wr32(t->cpu.esp, THREAD_RETURN);
    wr32(t->cpu.esp + 4, value);
    if (arg(g, 5))
        wr32(arg(g, 5), i + 1);
    ++g->thread_count;
    return result(g, THREAD_HANDLE + i, 0);
}

bool thread_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    GuestThread *active = &g->threads[g->current_thread];
    uint32_t a = arg(g, 0);
    if (IS("GetCurrentThreadId"))
        return result(g, g->current_thread + 1, 0);
    if (IS("GetLastError"))
        return result(g, active->last_error, 0);
    if (IS("SetLastError")) {
        active->last_error = a;
        return result(g, 0, 4);
    }
    if (IS("_beginthreadex"))
        return create_thread(g);
    if (IS("_endthreadex") || IS("ExitThread")) {
        active->exited = true;
        return switch_thread(g);
    }
    if (IS("Sleep")) {
        if (a) {
            active->sleeping = true;
            active->deadline = g->clock + a;
        }
        result(g, 0, 4);
        return switch_thread(g);
    }
    if (IS("InitializeCriticalSection") || IS("EnterCriticalSection") ||
        IS("LeaveCriticalSection") || IS("DeleteCriticalSection"))
        return critical(g, name, a);
    if (IS("WaitForSingleObject"))
        return wait_one(g, a, arg(g, 1), 8);
    if (IS("CreateSemaphoreA")) {
        uint32_t initial = arg(g, 1), maximum = arg(g, 2);
        if (!maximum || initial > maximum || arg(g, 3))
            return result(g, 0, 16);
        for (unsigned i = 0; i < MAX_SEMAPHORES; ++i) {
            if (!g->semaphores[i].max) {
                g->semaphores[i].count = initial;
                g->semaphores[i].max = maximum;
                return result(g, SEM_HANDLE + i, 16);
            }
        }
        return result(g, 0, 16);
    }
    if (IS("CreateEventA") && !a && !arg(g, 3)) {
        for (unsigned i = 0; i < MAX_EVENTS; ++i) {
            if (!g->events[i].open) {
                g->events[i].open = true;
                g->events[i].manual = arg(g, 1) != 0;
                g->events[i].signaled = arg(g, 2) != 0;
                return result(g, EVENT_HANDLE + i, 16);
            }
        }
        return result(g, 0, 16);
    }
    unsigned i = a - EVENT_HANDLE;
    if ((IS("SetEvent") || IS("ResetEvent")) && i < MAX_EVENTS && g->events[i].open) {
        g->events[i].signaled = IS("SetEvent");
        result(g, 1, 4);
        return !g->events[i].signaled || g->thread_count == 1 || switch_thread(g);
    }
    if (IS("CloseHandle")) {
        if (i < MAX_EVENTS && g->events[i].open) {
            g->events[i].open = false;
            return result(g, 1, 4);
        }
        i = a - THREAD_HANDLE;
        if (i < g->thread_count && g->threads[i].open) {
            g->threads[i].open = false;
            return result(g, 1, 4);
        }
        i = a - SEM_HANDLE;
        if (i < MAX_SEMAPHORES && g->semaphores[i].max) {
            g->semaphores[i].max = 0;
            return result(g, 1, 4);
        }
        active->last_error = 6;
        return result(g, 0, 4);
    }
    i = a - THREAD_HANDLE;
    if (i < g->thread_count && g->threads[i].open) {
        if (IS("SetThreadPriority"))
            return result(g, 1, 8);
        if (IS("ResumeThread") || IS("SuspendThread")) {
            bool before = g->threads[i].suspended;
            g->threads[i].suspended = IS("SuspendThread");
            return result(g, before, 4);
        }
    }
    i = a - SEM_HANDLE;
    if (IS("ReleaseSemaphore") && (i >= MAX_SEMAPHORES || !g->semaphores[i].max)) {
        active->last_error = 6;
        return result(g, 0, 12);
    }
    if (IS("ReleaseSemaphore")) {
        uint32_t count = g->semaphores[i].count, add = arg(g, 1);
        if (!add || add > g->semaphores[i].max - count)
            return result(g, 0, 12);
        if (arg(g, 2))
            wr32(arg(g, 2), count);
        g->semaphores[i].count += add;
        return result(g, 1, 12);
    }
    return false;
}
