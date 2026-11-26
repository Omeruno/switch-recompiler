#include "game.h"
#include <string.h>
#include <strings.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

#define IS(s) (!strcmp(name, (s)))

static bool sound(Game *g, const char *name)
{
    if (IS("_FSOUND_GetVersion@0")) {
        fpush(&g->cpu, (float)3.74);
        return result(g, 0, 0);
    }
    if (IS("_FSOUND_SetOutput@4")) {
        if (g->audio_open)
            return result(g, 0, 4);
        g->sound_output = arg(g, 0);
        return result(g, 1, 4);
    }
    if (IS("_FSOUND_SetDriver@4")) {
        if (arg(g, 0))
            return result(g, 0, 4);
        g->sound_driver = 0;
        return result(g, 1, 4);
    }
    if (IS("_FSOUND_Init@12")) {
        if (g->audio_open || !arg(g, 0) || !arg(g, 1))
            return result(g, 0, 12);
#ifdef __SWITCH__
        if (R_FAILED(audoutInitialize()))
            return result(g, 0, 12);
        if (R_FAILED(audoutStartAudioOut())) {
            audoutExit();
            return result(g, 0, 12);
        }
#endif
        g->audio_open = true;
        g->audio_rate = arg(g, 0);
        g->audio_channels = arg(g, 1);
        return result(g, 1, 12);
    }
    if (IS("_FSOUND_GetNumDrivers@0"))
        return result(g, 1, 0);
    if (IS("_FSOUND_GetDriver@0"))
        return result(g, g->sound_driver, 0);
    if (IS("_FSOUND_GetDriverName@4")) {
        memcpy(guest_pointer(BSS + 0x700), "FreakOut native audio", sizeof "FreakOut native audio");
        return result(g, arg(g, 0) ? 0 : BSS + 0x700, 4);
    }
    if (IS("_FSOUND_Update@0") && g->audio_open)
        return result(g, 0, 0);
    if (IS("_FSOUND_3D_SetDistanceFactor@4")) {
        g->distance_factor = arg(g, 0);
        return result(g, 0, 4);
    }
    return false;
}

static bool multibyte(Game *g)
{
    uint32_t source = arg(g, 2), length = arg(g, 3), dest = arg(g, 4), capacity = arg(g, 5);
    uint32_t page = arg(g, 0);
    const char *text = (const char *)guest_pointer(source);
    if (length == UINT32_MAX)
        length = strlen(text) + 1;
    if (!length || length > 65536 || (page != 0 && page != 1252 && page != 65001))
        return result(g, 0, 24);
    if (dest && capacity < length)
        return result(g, 0, 24);
    for (unsigned i = 0; i < length; ++i) {
        unsigned char ch = text[i];
        if (ch > 127)
            return false; // Initial movie paths only use ASCII.
        if (dest && capacity)
            wr16(dest + 2 * i, ch);
    }
    return result(g, length, 24);
}

enum { GRAPH, EVENTS, CONTROL, POSITION, WINDOW };
static const Methods *const media_interfaces[] = {
    [GRAPH] = &video_graph, [EVENTS] = &video_events, [CONTROL] = &video_control,
    [POSITION] = &video_position, [WINDOW] = &video_window,
};

static uint32_t acquire(Game *g, unsigned id)
{
    if (!g->video_objects[id])
        g->video_objects[id] = make_interface(g, media_interfaces[id]);
    if (g->video_objects[id])
        ++g->video_refs[id];
    return g->video_objects[id];
}

bool media_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    if (!strcasecmp(call->dll, "fmod.dll"))
        return sound(g, name) || audio_bank_dispatch(g, call);
    if (IS("MultiByteToWideChar"))
        return multibyte(g);
    if (IS("CoCreateInstance")) {
        static const uint8_t filter_graph[] = {0xb3,0xeb,0x36,0xe4,0x4f,0x52,0xce,0x11,
                                               0x9f,0x53,0,0x20,0xaf,0x0b,0xa7,0x70};
        if (memcmp(guest_pointer(arg(g, 0)), filter_graph, sizeof filter_graph))
            return result(g, 0x80040154u, 20);
        uint32_t graph = acquire(g, GRAPH);
        if (!graph)
            return false;
        wr32(arg(g, 4), graph);
        return result(g, 0, 20);
    }
    uint32_t self = arg(g, 0);
    for (unsigned id = 0; self && id < sizeof media_interfaces / sizeof *media_interfaces; ++id) {
        if (self == g->video_objects[id] && !strcmp(call->dll, media_interfaces[id]->dll)) {
            if (IS("AddRef"))
                return result(g, ++g->video_refs[id], 4);
            if (IS("Release") && g->video_refs[id])
                return result(g, --g->video_refs[id], 4);
            break;
        }
    }
    if (self && self == g->video_objects[GRAPH] && !strcmp(call->dll, video_graph.dll)) {
        if (IS("QueryInterface")) {
            uint32_t iid = rd32(arg(g, 1));
            unsigned id;
            switch (iid) {
            case 0x56a868a9: id = GRAPH; break;
            case 0x56a868b6: id = EVENTS; break;
            case 0x56a868b1: id = CONTROL; break;
            case 0x56a868b2: id = POSITION; break;
            case 0x56a868b4: id = WINDOW; break;
            default:
                wr32(arg(g, 2), 0);
                return result(g, 0x80004002u, 12);
            }
            uint32_t object = acquire(g, id);
            if (!object)
                return false;
            wr32(arg(g, 2), object);
            return result(g, 0, 12);
        }
        if (IS("Abort")) {
            g->video_unavailable = g->video_complete = false;
            return result(g, 0, 4);
        }
        if (IS("RenderFile")) {
            g->video_unavailable = true;
            uint32_t source = arg(g, 1);
            char path[512];
            for (unsigned i = 0; i < sizeof path; ++i) {
                uint16_t ch = rd16(source + i * 2);
                if (ch > 127)
                    return result(g, 0x80040218u, 12);
                path[i] = ch;
                if (!ch) {
                    FILE *file = open_asset(g, path);
                    if (!file)
                        return result(g, 0x80070002u, 12);
                    fclose(file);
                    return result(g, 0x80040218u, 12);
                }
            }
            return result(g, 0x80040218u, 12);
        }
    }
    if (self && self == g->video_objects[EVENTS] && !strcmp(call->dll, video_events.dll)) {
        if (IS("SetNotifyWindow")) {
            g->notify_window = arg(g, 1);
            g->notify_message = arg(g, 2);
            g->notify_arg = arg(g, 3);
            return result(g, 0, 16);
        }
        if (IS("SetNotifyFlags")) {
            g->notify_flags = arg(g, 1);
            return result(g, 0, 8);
        }
        if (IS("GetEvent")) {
            if (!g->video_complete)
                return result(g, 0x80004004u, 20);
            g->video_complete = false;
            wr32(arg(g, 1), 1);
            wr32(arg(g, 2), 0);
            wr32(arg(g, 3), 0);
            return result(g, 0, 20);
        }
        if (IS("FreeEventParams"))
            return result(g, 0, 16);
    }
    if (self && self == g->video_objects[WINDOW] && !strcmp(call->dll, video_window.dll)) {
        if (IS("put_Visible")) {
            g->video_visible = arg(g, 1) != 0;
            return result(g, 0, 8);
        }
        if (IS("SetWindowForeground")) {
            g->video_foreground = arg(g, 1) != 0;
            return result(g, 0, 8);
        }
    }
    if (self && self == g->video_objects[CONTROL] && !strcmp(call->dll, video_control.dll)) {
        if (IS("Run") && g->video_unavailable) {
            // The host explicitly skips unplayable intros and wakes the caller.
            FreakOutMessage done = {.hwnd = g->notify_window, .message = g->notify_message,
                                   .lparam = g->notify_arg, .time = g->idle_polls};
            if (done.hwnd && done.message && !freakout_post_message(&g->messages, done))
                return false;
            g->video_unavailable = false;
            g->video_complete = true;
            return result(g, 0, 4);
        }
        if (IS("Stop"))
            return result(g, 0, 4);
    }
    if (self && self == g->video_objects[POSITION] && !strcmp(call->dll, video_position.dll)) {
        if (IS("get_Duration")) {
            wr64(arg(g, 1), 0);
            return result(g, 0, 8);
        }
        if (IS("put_CurrentPosition")) {
            g->video_position_value = rdf64(g->cpu.esp + 8);
            return result(g, 0, 12);
        }
    }
    return false;
}

void media_close(Game *g)
{
#ifdef __SWITCH__
    if (g->audio_open) {
        audoutStopAudioOut();
        audoutExit();
    }
#endif
    g->audio_open = false;
}
