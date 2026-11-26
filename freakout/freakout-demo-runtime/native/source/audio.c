#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t handle, frames, size, mode, rate, offset;
    unsigned channels;
    float distance[2];
} Sample;

typedef struct AudioBank {
    struct AudioBank *next;
    FILE *source;
    uint32_t handle, count, mode, selected, bytes;
    uint32_t callback, callback_data;
    bool stream;
    Sample samples[];
} AudioBank;

static uint32_t u32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void dispose(Game *g, AudioBank *bank)
{
    for (unsigned i = 0; i < sizeof g->voices / sizeof *g->voices; ++i) {
        struct AudioVoice *voice = &g->voices[i];
        if (voice->source == bank->handle)
            *voice = (struct AudioVoice){0};
    }
    fclose(bank->source);
    free(bank);
}

void audio_bank_close(Game *g)
{
    while (g->banks) {
        AudioBank *bank = g->banks;
        g->banks = bank->next;
        dispose(g, bank);
    }
    g->audio_payload_bytes = 0;
}

static uint32_t bank_open(Game *g, uint32_t path, uint32_t mode, bool stream)
{
    const char *name = (const char *)guest_pointer(path);
    FILE *source = open_asset(g, name);
    if (!source)
        return 0;
    uint8_t header[24];
    AudioBank *bank = NULL;
    if (fread(header, 1, sizeof header, source) != sizeof header || memcmp(header, "FSB3", 4))
        goto fail;
    uint32_t count = u32(header + 4), headers = u32(header + 8), bytes = u32(header + 12);
    uint32_t version = u32(header + 16), flags = u32(header + 20);
    if (!count || count > 4096 || headers > 1024 * 1024 || count > headers / 8 ||
        bytes > 192 * 1024 * 1024 || version != 0x30001 || flags & ~2u)
        goto fail;
    if (fseek(source, 0, SEEK_END) || ftell(source) != (long)(24 + headers + bytes) ||
        fseek(source, 24, SEEK_SET))
        goto fail;
    bank = calloc(1, sizeof *bank + count * sizeof(Sample));
    if (!bank)
        goto fail;
    uint32_t cursor = 24, offset = 24 + headers, limit = offset + bytes, end_headers = offset;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t sample_header[64];
        bool basic = i && flags & 2;
        unsigned required = basic ? 8 : sizeof sample_header;
        if (required > end_headers - cursor || fread(sample_header, 1, required, source) != required)
            goto fail;
        Sample sample;
        unsigned size_header;
        if (basic) {
            sample = bank->samples[i - 1];
            sample.frames = u32(sample_header);
            sample.size = u32(sample_header + 4);
            size_header = 8;
        } else {
            sample = (Sample){.frames = u32(sample_header + 32), .size = u32(sample_header + 36),
                              .mode = u32(sample_header + 48), .rate = u32(sample_header + 52),
                              .channels = sample_header[62] | sample_header[63] << 8};
            size_header = sample_header[0] | sample_header[1] << 8;
        }
        if (size_header < required || size_header > end_headers - cursor || !sample.frames ||
            !sample.size || sample.size > limit - offset || !sample.rate ||
            sample.channels < 1 || sample.channels > 2)
            goto fail;
        if (size_header > required && fseek(source, size_header - required, SEEK_CUR))
            goto fail;
        if (mode & 7)
            sample.mode = (sample.mode & ~7u) | (mode & 7);
        sample.mode |= mode & 0x2000;
        sample.offset = offset;
        bank->samples[i] = sample;
        cursor += size_header;
        offset += sample.size;
    }
    if (cursor != end_headers || offset != limit)
        goto fail;
    uint32_t handles = allocate(g, (count + 1) * 16);
    if (!handles)
        goto fail;
    bank->source = source;
    bank->handle = handles;
    bank->count = count;
    bank->mode = mode;
    bank->bytes = bytes;
    bank->stream = stream;
    for (unsigned i = 0; i < count; ++i)
        bank->samples[i].handle = handles + (i + 1) * 16;
    bank->next = g->banks;
    g->banks = bank;
    g->audio_payload_bytes += bytes;
    if (g->audio_payload_bytes > g->audio_peak_bytes)
        g->audio_peak_bytes = g->audio_payload_bytes;
    fprintf(g->log, "Opened sound bank %s (%u samples)\n", name, count);
    return handles;
fail:
    free(bank);
    fclose(source);
    return 0;
}

static AudioBank *find_bank(Game *g, uint32_t handle, bool stream)
{
    for (AudioBank *bank = g->banks; bank; bank = bank->next)
        if (bank->handle == handle && bank->stream == stream)
            return bank;
    return NULL;
}

static AudioBank *find_sample(Game *g, uint32_t handle, Sample **sample)
{
    for (AudioBank *bank = g->banks; bank; bank = bank->next) {
        uint32_t offset = handle - bank->handle;
        if (offset && offset <= bank->count * 16 && !(offset & 15)) {
            *sample = &bank->samples[offset / 16 - 1];
            return bank;
        }
    }
    return NULL;
}

static void forget(Game *g, AudioBank *bank)
{
    AudioBank **link = &g->banks;
    while (*link != bank)
        link = &(*link)->next;
    *link = bank->next;
    g->audio_payload_bytes -= bank->bytes;
    release(g, bank->handle);
    dispose(g, bank);
}

static bool voice_active(Game *g, const struct AudioVoice *v)
{
    return v->source && (v->paused || (v->mode & 2) || g->presents - v->start < v->duration);
}

static uint32_t play(Game *g, uint32_t requested, AudioBank *bank, const Sample *sample, bool paused)
{
    unsigned limit = g->audio_channels;
    if (limit > sizeof g->voices / sizeof *g->voices)
        limit = sizeof g->voices / sizeof *g->voices;
    if (requested == UINT32_MAX)
        for (requested = 0; requested < limit && voice_active(g, &g->voices[requested]); ++requested) {}
    if (requested >= limit)
        return UINT32_MAX;
    uint32_t duration = ((uint64_t)sample->frames * 60 + sample->rate - 1) / sample->rate;
    g->voices[requested] = (struct AudioVoice){
        .source = bank->handle, .sample = sample->handle, .duration = duration,
        .start = g->presents, .frequency = sample->rate, .mode = bank->stream ? bank->mode : sample->mode,
        .distance = {sample->distance[0], sample->distance[1]},
        .volume = 255, .pan = 128, .paused = paused,
    };
    return requested;
}

#define IS(s) (!strcmp(name, s))

static bool audio_vector(Game *g, uint32_t address, float output[3])
{
    const void *values = address ? game_bytes(g, address, 3 * sizeof(float)) : NULL;
    if (address && !values)
        return false;
    if (values)
        memcpy(output, values, 3 * sizeof(float));
    return true;
}

bool audio_bank_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    uint32_t a = arg(g, 0), b = arg(g, 1);
    bool stream = !strncmp(name, "_FSOUND_Stream_", 15);
    if (IS("_FSOUND_3D_Listener_SetCurrent@8")) {
        if (a < sizeof g->listeners / sizeof *g->listeners && (!b || a < b))
            g->audio_listener = a;
        return result(g, 0, 8);
    }
    if (IS("_FSOUND_3D_Listener_SetAttributes@32")) {
        unsigned slot = g->audio_listener;
        if (!audio_vector(g, a, g->listeners[slot].position) || !audio_vector(g, b, g->listeners[slot].velocity))
            return false;
        memcpy(g->listeners[slot].forward, guest_pointer(g->cpu.esp + 12), 3 * sizeof(float));
        memcpy(g->listeners[slot].up, guest_pointer(g->cpu.esp + 24), 3 * sizeof(float));
        return result(g, 0, 32);
    }
    if (IS("_FMUSIC_LoadSongEx@24") && !b && !arg(g, 2) && !arg(g, 4) && !arg(g, 5))
        return result(g, bank_open(g, a, arg(g, 3), false), 24);
    if (IS("_FSOUND_Stream_Open@16") && !arg(g, 2) && !arg(g, 3))
        return result(g, bank_open(g, a, b, true), 16);
    AudioBank *bank = find_bank(g, a, stream);
    if (IS("_FMUSIC_GetNumSamples@4") || IS("_FSOUND_Stream_GetNumSubStreams@4"))
        return result(g, bank ? bank->count : 0, 4);
    if (IS("_FMUSIC_GetSample@8"))
        return result(g, bank && b < bank->count ? bank->samples[b].handle : 0, 8);
    if (IS("_FMUSIC_FreeSong@4") || IS("_FSOUND_Stream_Close@4")) {
        if (bank) {
            forget(g, bank);
            return result(g, 1, 4);
        }
        return result(g, 0, 4);
    }
    Sample *sample = NULL;
    if (IS("_FSOUND_Sample_SetMode@8")) {
        if (find_sample(g, a, &sample)) {
            sample->mode = b;
            return result(g, 1, 8);
        }
        return result(g, 0, 8);
    }
    if (IS("_FSOUND_Stream_SetSubStream@8") || IS("_FSOUND_Stream_SetMode@8")) {
        if (!bank || (IS("_FSOUND_Stream_SetSubStream@8") && b >= bank->count))
            return result(g, 0, 8);
        if (IS("_FSOUND_Stream_SetSubStream@8"))
            bank->selected = b;
        else
            bank->mode = b;
        return result(g, 1, 8);
    }
    if (IS("_FSOUND_Stream_SetEndCallback@12")) {
        if (!bank)
            return result(g, 0, 12);
        bank->callback = b;
        bank->callback_data = arg(g, 2);
        return result(g, 1, 12);
    }
    if (IS("_FSOUND_Stream_Play@8") || IS("_FSOUND_Stream_PlayEx@16") ||
        IS("_FSOUND_PlaySound@8") || IS("_FSOUND_PlaySoundEx@16")) {
        bool extended = IS("_FSOUND_Stream_PlayEx@16") || IS("_FSOUND_PlaySoundEx@16");
        if (stream) {
            bank = find_bank(g, b, true);
            if (bank)
                sample = &bank->samples[bank->selected];
        } else {
            bank = find_sample(g, b, &sample);
        }
        uint32_t voice = bank && (!extended || !arg(g, 2)) ? play(g, a, bank, sample, extended && arg(g, 3)) : UINT32_MAX;
        return result(g, voice, extended ? 16 : 8);
    }
    if (IS("_FSOUND_Stream_Stop@4")) {
        if (bank)
            for (unsigned i = 0; i < sizeof g->voices / sizeof *g->voices; ++i)
                if (g->voices[i].source == a)
                    g->voices[i] = (struct AudioVoice){0};
        return result(g, bank != NULL, 4);
    }
    struct AudioVoice *voice = a < sizeof g->voices / sizeof *g->voices ? &g->voices[a] : NULL;
    bool active = voice && voice_active(g, voice);
    if (IS("_FSOUND_3D_SetAttributes@12"))
        return result(g, active && audio_vector(g, b, voice->position) &&
                         audio_vector(g, arg(g, 2), voice->velocity), 12);
    if (IS("_FSOUND_Sample_SetMinMaxDistance@12") || IS("_FSOUND_3D_SetMinMaxDistance@12")) {
        float minimum = rdf32(g->cpu.esp + 8), maximum = rdf32(g->cpu.esp + 12);
        if (!isfinite(minimum) || !isfinite(maximum) || minimum < 0.f || maximum < minimum)
            return result(g, 0, 12);
        float *distance = NULL;
        if (IS("_FSOUND_Sample_SetMinMaxDistance@12")) {
            if (find_sample(g, a, &sample))
                distance = sample->distance;
        } else if (active) {
            distance = voice->distance;
        }
        if (distance) {
            distance[0] = minimum;
            distance[1] = maximum;
        }
        return result(g, distance != NULL, 12);
    }
    if (IS("_FSOUND_IsPlaying@4"))
        return result(g, active, 4);
    if (IS("_FSOUND_GetCurrentSample@4"))
        return result(g, active ? voice->sample : 0, 4);
    if (IS("_FSOUND_StopSound@4")) {
        if (active)
            *voice = (struct AudioVoice){0};
        return result(g, active, 4);
    }
    if (IS("_FSOUND_GetFrequency@4"))
        return result(g, active ? voice->frequency : 0, 4);
    if (IS("_FSOUND_GetVolume@4"))
        return result(g, active ? voice->volume : 0, 4);
    if (IS("_FSOUND_GetLoopMode@4"))
        return result(g, active ? voice->mode & 7 : 0, 4);
    if (IS("_FSOUND_SetVolume@8") || IS("_FSOUND_SetPan@8") || IS("_FSOUND_SetFrequency@8") ||
        IS("_FSOUND_SetLoopMode@8") || IS("_FSOUND_SetPaused@8")) {
        if (!active)
            return result(g, 0, 8);
        if (IS("_FSOUND_SetVolume@8"))
            voice->volume = b;
        else if (IS("_FSOUND_SetPan@8"))
            voice->pan = b;
        else if (IS("_FSOUND_SetFrequency@8"))
            voice->frequency = b;
        else if (IS("_FSOUND_SetLoopMode@8"))
            voice->mode = (voice->mode & ~7u) | (b & 7);
        else
            voice->paused = b != 0;
        return result(g, 1, 8);
    }
    if (IS("_FSOUND_GetMemoryStats@8")) {
        if (a)
            wr32(a, g->audio_payload_bytes);
        if (b)
            wr32(b, g->audio_peak_bytes);
        return result(g, 0, 8);
    }
    return false;
}
