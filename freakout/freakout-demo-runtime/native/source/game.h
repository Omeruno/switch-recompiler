#ifndef FREAKOUT_NATIVE_GAME_H
#define FREAKOUT_NATIVE_GAME_H
#include <stdbool.h>
#include <stdio.h>
#include "cpu.h"
#include "freakout_messages.h"

enum {
    LOW_SIZE = 0x08000000, RUNTIME_BASE = 0x7c340000, RUNTIME_SIZE = 0xe0000,
    API_BASE = 0x100000, CTOR_GATE = 0xff000, WINDOW_RETURN = 0xff010,
    GAME_ENTRY = 0x67de70, PE_BASE = 0x400000, PE_ENTRY = 0x5af59e,
    STACK = 0xbe0000, BSS = 0xa00000, COMMAND = BSS + 0x100, ARGV = BSS + 0x180,
    HEAP_START = 0x01000000, MAX_IMPORTS = 4096, MAX_SPANS = 24000,
    MAX_FILES = 64, MAX_SEARCHES = 8, MAX_LOCKS = 96, MAX_SEMAPHORES = 16, MAX_CTORS = 12, MAX_TEXTURES = 16, MAX_MIPS = 13,
};

typedef struct { const char *dll, *name; } Import;
typedef struct { const char *dll; const char *const *names; size_t count; } Methods;
typedef struct { uint32_t slot, target; } ImportBinding;
typedef struct { const char *name; uint32_t base, size; uint8_t sha256[32]; } Module;
typedef struct { uint32_t start, size; bool used; } Span;
typedef struct { uint32_t next, end, stack, ret; } Constructors;
typedef struct {
    uint32_t format, usage, pool, width, height, data, refs;
    bool locked;
} Surface;
typedef struct {
    uint32_t handle, refs, levels;
    uint32_t surfaces[MAX_MIPS];
    Surface mip[MAX_MIPS];
} Texture;

typedef struct {
    uint8_t *low, *runtime;
    CPU cpu;
    const char *assets;
    FILE *log;
    const char *stop;
    unsigned calls, presents, clears;
    uint64_t file_bytes;
    bool winmain;
    Import imports[MAX_IMPORTS];
    size_t import_count;
    size_t span_count;
    Span spans[MAX_SPANS];
    Constructors ctors[MAX_CTORS];
    unsigned ctor_count;
    FILE *files[MAX_FILES];
    bool archive_file[MAX_FILES];
    unsigned archive_reads, uploads;
    struct Search *searches[MAX_SEARCHES];
    unsigned sound_driver, sound_output;
    uint32_t distance_factor, audio_rate, audio_channels;
    bool audio_open;
    uint32_t video_objects[5], video_refs[5], notify_window, notify_message, notify_arg, notify_flags;
    bool video_unavailable, video_complete, video_visible, video_foreground;
    double video_position_value;
    FreakOutMessageQueue messages;
    struct { uint32_t stack, ret; } window_callbacks[8];
    unsigned window_depth, idle_polls;
    struct { uint32_t address, depth; } locks[MAX_LOCKS];
    struct { uint32_t count, max; } semaphores[MAX_SEMAPHORES];
    uint32_t last_error, random, cursor_count, window_count, window_proc;
    uint32_t d3d, d3d_refs, device, device_refs, backbuffer, depth_buffer;
    Surface back, depth;
    Texture textures[MAX_TEXTURES];
    uint32_t clear, raster[256], samplers[16][16], stages[8][64], transforms[512], viewport[6];
    uint32_t input, input_refs, keyboard, mouse, key_size, mouse_size;
    bool key_acquired, mouse_acquired;
    uint8_t keys[256];
} Game;

extern const Methods d3d_root, d3d_device, d3d_surface, d3d_texture, di_root, di_device, video_graph, video_events, video_control, video_position, video_window;
uint32_t advance(uint8_t *guest, uint8_t *runtime, CPU *cpu);
int has_block(uint32_t address);
void hash_bytes(const void *data, size_t size, uint8_t digest[32]);
void log_hash(FILE *log, const void *data, size_t size);
uint8_t *game_bytes(Game *game, uint32_t address, size_t length);
uint32_t allocate(Game *game, uint32_t size);
bool release(Game *game, uint32_t address);
uint32_t arg(Game *game, unsigned index);
bool result(Game *game, uint32_t eax, unsigned pop);
uint32_t add_import(Game *game, const char *dll, const char *name);
uint32_t make_interface(Game *game, const Methods *methods);
bool asset_path(const Game *game, const char *name, char path[1024]);
FILE *open_asset(const Game *game, const char *name);
bool game_init(Game *game, const char *assets, FILE *log);
bool game_step(Game *game);
void game_close(Game *game);
bool win32_dispatch(Game *game, const Import *call);
bool d3d_dispatch(Game *game, const Import *call);
bool input_dispatch(Game *game, const Import *call);
bool crt_dispatch(Game *game, const Import *call);
bool file_dispatch(Game *game, const Import *call);
void file_close_searches(Game *game);
bool media_dispatch(Game *game, const Import *call);
void media_close(Game *game);
#endif
