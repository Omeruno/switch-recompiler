#ifndef FREAKOUT_NATIVE_GAME_H
#define FREAKOUT_NATIVE_GAME_H
#include <stdbool.h>
#include <stdio.h>
#include "cpu.h"
#include "freakout_messages.h"

enum {
    LOW_SIZE = FO_GUEST_MEMORY, RUNTIME_BASE = 0x7c340000, RUNTIME_SIZE = 0xe0000,
    API_BASE = 0x100000, CTOR_GATE = 0xff000, WINDOW_RETURN = 0xff010, THREAD_RETURN = 0xff020,
    AUDIO_RETURN = 0xff030,
    GAME_ENTRY = 0x67de70, PE_BASE = 0x400000, PE_ENTRY = 0x5af59e,
    STACK = 0xbe0000, BSS = 0xa00000, COMMAND = BSS + 0x100, ARGV = BSS + 0x180,
    HEAP_START = 0x01000000, RESOURCE_HEAP_START = 0x07000000, MAX_IMPORTS = 4096, MAX_SPANS = 131072,
    MAX_FILES = 64, MAX_SEARCHES = 8, MAX_LOCKS = 96, MAX_SEMAPHORES = 16, MAX_CTORS = 12, MAX_TEXTURES = 512, MAX_MIPS = 13,
    MAX_THREADS = 8, MAX_EVENTS = 32, MAX_BUFFERS = 8192, MAX_WAIT = 64, MAX_PROGRAMS = 256, MAX_SURFACES = 32,
};

typedef struct { const char *dll, *name; } Import;
typedef struct { const char *dll; const char *const *names; size_t count; } Methods;
typedef struct { uint32_t slot, target; } ImportBinding;
typedef struct { const char *name; uint32_t base, size; uint8_t sha256[32]; } Module;
typedef struct { uint32_t start, size; bool used; } Span;
typedef struct {
    size_t span_count, free_span_count;
    Span spans[MAX_SPANS];
    struct { uint32_t start, size; } free_spans[MAX_SPANS];
    uint32_t top, limit;
} Heap;
typedef struct { uint32_t next, end, stack, ret; } Constructors;
typedef struct {
    uint32_t format, usage, pool, width, height, data, refs;
    bool locked;
    uint32_t *decoded;
    float *depth_samples;
    uint32_t gpu_generation;
} Surface;
typedef struct {
    uint32_t handle, refs, levels;
    uint32_t surfaces[MAX_MIPS];
    Surface mip[MAX_MIPS];
} Texture;

typedef struct {
    CPU cpu;
    uint32_t seh, crt, last_error, stack_base;
    uint32_t wait[MAX_WAIT];
    unsigned wait_count;
    bool wait_all;
    uint64_t deadline;
    bool sleeping, suspended, exited, open;
} GuestThread;

typedef struct {
    uint32_t handle, refs, bytes, data, usage, format, pool, kind, lock_start, lock_size;
    bool locked;
} Buffer;
typedef struct { uint32_t handle, refs, bytes, data, kind; } Program;
enum { D3D_COLOR_WRITE_ENABLE = 168 };

typedef struct {
    uint8_t *low, *runtime;
    CPU cpu;
    const char *assets;
    FILE *log;
    const char *stop;
    uint32_t recent[8];
    unsigned recent_count;
    unsigned calls, presents, clears, draws, frame_draws, presented_draws, program_draws;
    uint64_t render_ns;
    struct RenderScratch *render_scratch;
    unsigned frame_width, frame_height, raster_divisor;
    struct NativeGpu *gpu;
    uint32_t texture_generation;
    bool gpu_requested;
    unsigned current_thread, thread_count, import_slice;
    uint64_t clock;
    GuestThread threads[MAX_THREADS];
    struct { bool open, manual, signaled; } events[MAX_EVENTS];
    uint64_t file_bytes;
    bool winmain;
    Import imports[MAX_IMPORTS];
    size_t import_count;
    struct { const Methods *methods; uint32_t address; } vtables[32];
    size_t vtable_count;
    Heap heap, resources;
    Constructors ctors[MAX_CTORS];
    unsigned ctor_count;
    FILE *files[MAX_FILES];
    void *file_memory[MAX_FILES];
    char *pending_save[MAX_FILES], *save_path[MAX_FILES];
    bool archive_file[MAX_FILES];
    unsigned archive_reads, uploads;
    struct Search *searches[MAX_SEARCHES];
    unsigned sound_driver, sound_output;
    uint32_t distance_factor, audio_rate, audio_channels;
    bool audio_open, audio_dirty;
    struct AudioMixer *audio_mixer;
    uint32_t audio_serial, audio_seek;
    bool audio_callback_active;
    CPU audio_callback_cpu;
    unsigned audio_listener;
    struct { float position[3], velocity[3], forward[3], up[3]; } listeners[8];
    struct AudioBank *banks;
    unsigned audio_payload_bytes, audio_peak_bytes;
    struct AudioVoice {
        uint32_t source, sample, duration, start, frequency, mode, volume, pan;
        uint32_t serial, seek_id, seek_frame, priority;
        float distance[2], position[3], velocity[3];
        bool paused, spatial, notified;
    } voices[64];
    uint32_t video_objects[5], video_refs[5], notify_window, notify_message, notify_arg, notify_flags;
    bool video_unavailable, video_complete, video_visible, video_foreground;
    double video_position_value;
    FreakOutMessageQueue messages;
    struct { uint32_t stack, ret; } window_callbacks[8];
    unsigned window_depth, idle_polls;
    struct { uint32_t address, depth; unsigned owner; } locks[MAX_LOCKS];
    struct { uint32_t count, max; } semaphores[MAX_SEMAPHORES];
    uint32_t random, cursor_count, window_count, window_proc;
    uint32_t d3d, d3d_refs, device, device_refs, backbuffer, depth_buffer;
    uint32_t color_target, depth_target;
    Surface back, depth;
    struct { uint32_t handle; Surface image; } surfaces[MAX_SURFACES];
    Texture textures[MAX_TEXTURES];
    Buffer buffers[MAX_BUFFERS];
    Program programs[MAX_PROGRAMS];
    uint32_t declaration, vertex_shader, pixel_shader;
    uint32_t vertex_constants[256][4], pixel_constants[32][4];
    unsigned deformed_frame, deformed_count, software_skin_draw;
    uint32_t deformed_skins[64];
    float software_skin_constants[7][4];
    uint32_t clear, raster[256], samplers[16][16], stages[8][64], transforms[257][16], viewport[6];
    uint32_t stream, stream_offset, stride, indices, fvf, texture_slots[16];
    uint32_t material[17];
    struct { uint32_t index, values[26]; bool defined, enabled; } lights[64];
    bool scene;
    uint32_t *pixels;
    float *depth_pixels;
    uint32_t input, input_refs, keyboard, mouse, key_size, mouse_size;
    bool key_acquired, mouse_acquired;
    uint8_t keys[256], virtual_keys[256];
} Game;

extern const Methods d3d_root, d3d_device, d3d_surface, d3d_texture, di_root, di_device, video_graph, video_events, video_control, video_position, video_window;
extern const Methods d3d_vertex_buffer, d3d_index_buffer;
uint32_t advance(uint8_t *guest, uint8_t *runtime, CPU *cpu);
int has_block(uint32_t address);
void hash_bytes(const void *data, size_t size, uint8_t digest[32]);
void log_hash(FILE *log, const void *data, size_t size);
uint8_t *game_bytes(Game *game, uint32_t address, size_t length);
uint32_t allocate(Game *game, uint32_t size);
uint32_t reallocate(Game *g, uint32_t address, uint32_t size);
uint32_t resource_allocate(Game *g, uint32_t size);
bool release(Game *game, uint32_t address);
uint32_t arg(Game *game, unsigned index);
bool result(Game *game, uint32_t eax, unsigned pop);
uint32_t add_import(Game *game, const char *dll, const char *name);
uint32_t make_interface(Game *game, const Methods *methods);
bool asset_path(const Game *game, const char *name, char path[1024]);
FILE *open_asset(const Game *game, const char *name);
FILE *open_level_script(Game *g, const char *name, bool text, void **memory);
bool game_init(Game *game, const char *assets, FILE *log);
uint64_t native_now_ns(void);
bool game_step(Game *game);
void game_close(Game *game);
bool switch_thread(Game *g);
uint32_t thread_crt(Game *g);
bool thread_dispatch(Game *g, const Import *call);
bool render_allocate(Game *g);
bool render_clear(Game *g, unsigned flags, uint32_t color, float depth);
Surface *d3d_surface_lookup(Game *g, uint32_t handle, Texture **owner);
uint32_t surface_pitch(const Surface *s);
uint32_t surface_bytes(const Surface *s);
uint32_t *decode_texture(Game *g, const Surface *s);
bool d3dx_load_surface(Game *g);
void game_keys(Game *g, const uint8_t keys[256]);
bool audio_bank_dispatch(Game *g, const Import *call);
void audio_bank_close(Game *g);
bool audio_output_open(Game *g);
void audio_output_close(Game *g);
void audio_frame(Game *g);
bool audio_update(Game *g);
bool render_geometry(Game *g, const Buffer *vb, const Buffer *ib);
void render_close(Game *g);
void skin_observe(Game *g);
bool skin_constants(Game *g, void *constants, uint32_t vertex_shader);
bool win32_dispatch(Game *game, const Import *call);
bool d3d_dispatch(Game *game, const Import *call);
bool program_dispatch(Game *g, const Import *call);
const void *program_data(Game *g, uint32_t handle, size_t *bytes);
int program_source_index(Game *g, uint32_t handle);
void program_clear_declaration(Game *g);
bool d3dx_math_dispatch(Game *g, const char *name);
bool input_dispatch(Game *game, const Import *call);
bool crt_dispatch(Game *game, const Import *call);
bool file_dispatch(Game *game, const Import *call);
void file_close_searches(Game *game);
bool media_dispatch(Game *game, const Import *call);
void media_close(Game *game);
#endif
