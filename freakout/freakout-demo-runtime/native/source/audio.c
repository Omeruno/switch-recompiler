#include "game.h"
#include "audio_mixer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AudioClip clip;
    uint32_t handle, mode, rate, volume, pan, priority;
    float distance[2];
} Sample;

typedef struct AudioBank {
    struct AudioBank *next;
    AudioFile *file;
    uint32_t handle, count, mode, selected, bytes;
    uint32_t callback, callback_data, stream_frame;
    bool stream;
    Sample samples[];
} AudioBank;

static uint32_t u32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned u16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static void dispose(Game *g, AudioBank *bank)
{
    for (unsigned i = 0; i < sizeof g->voices / sizeof *g->voices; ++i) {
        struct AudioVoice *voice = &g->voices[i];
        if (voice->source == bank->handle)
            *voice = (struct AudioVoice){0};
    }
    g->audio_dirty = true;
    release(g, bank->handle);
    audio_file_release(bank->file);
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
            sample.clip.frames = u32(sample_header);
            sample.clip.size = u32(sample_header + 4);
            sample.clip.loop_start = sample.clip.loop_end = 0;
            size_header = 8;
        } else {
            sample = (Sample){
                .clip = {.frames = u32(sample_header + 32), .size = u32(sample_header + 36),
                         .loop_start = u32(sample_header + 40), .loop_end = u32(sample_header + 44),
                         .channels = u16(sample_header + 62)},
                .mode = u32(sample_header + 48), .rate = u32(sample_header + 52),
                .volume = u16(sample_header + 56), .pan = u16(sample_header + 58),
                .priority = u16(sample_header + 60), .distance = {1.f, 10000.f},
            };
            sample.clip.ima = (sample.mode & 0x00400000u) != 0;
            size_header = u16(sample_header);
        }
        AudioClip *clip = &sample.clip;
        if (size_header < required || size_header > end_headers - cursor || !clip->frames ||
            !clip->size || clip->size > limit - offset || !sample.rate || sample.rate > 384000 ||
            clip->channels < 1 || clip->channels > 2 || (!clip->ima && (sample.mode & 0x118) != 0x110))
            goto fail;
        if (clip->ima ? ((uint64_t)clip->frames + 63) / 64 * (36 * clip->channels) > clip->size :
                        (uint64_t)clip->frames * 2 * clip->channels > clip->size)
            goto fail;
        if (!basic && size_header >= required + 8) {
            uint8_t distances[8];
            if (fread(distances, 1, sizeof distances, source) != sizeof distances)
                goto fail;
            uint32_t bits[2] = {u32(distances), u32(distances + 4)};
            float values[2];
            memcpy(values, bits, sizeof values);
            if (isfinite(values[0]) && isfinite(values[1]) && values[0] > 0.f && values[1] >= values[0])
                memcpy(sample.distance, values, sizeof values);
            required += 8;
        }
        if (size_header > required && fseek(source, size_header - required, SEEK_CUR))
            goto fail;
        if (mode & 7)
            sample.mode = (sample.mode & ~7u) | (mode & 7);
        sample.mode |= mode & 0x2000;
        clip->offset = offset;
        if (!clip->loop_end || clip->loop_end >= clip->frames)
            clip->loop_end = clip->frames;
        else
            ++clip->loop_end;
        if (clip->loop_start >= clip->loop_end)
            clip->loop_start = 0;
        bank->samples[i] = sample;
        cursor += size_header;
        offset += clip->size;
    }
    if (cursor != end_headers || offset != limit)
        goto fail;
    uint32_t handles = allocate(g, (count + 1) * 16);
    if (!handles)
        goto fail;
    bank->file = audio_file_create(source);
    if (!bank->file) {
        release(g, handles);
        goto fail;
    }
    bank->handle = handles;
    bank->count = count;
    bank->mode = mode;
    bank->bytes = bytes;
    bank->stream = stream;
    for (unsigned i = 0; i < count; ++i) {
        bank->samples[i].handle = handles + (i + 1) * 16;
        bank->samples[i].clip.file = bank->file;
    }
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
    dispose(g, bank);
}

static bool voice_active(Game *g, const struct AudioVoice *v)
{
    if (!v->source)
        return false;
    if (g->audio_mixer)
        return !audio_mixer_finished(g->audio_mixer, v - g->voices, v->serial);
    return v->paused || (v->mode & 2) || g->presents - v->start < v->duration;
}

static uint32_t play(Game *g, uint32_t requested, AudioBank *bank, const Sample *sample, bool paused)
{
    unsigned limit = g->audio_channels;
    if (limit > sizeof g->voices / sizeof *g->voices)
        limit = sizeof g->voices / sizeof *g->voices;
    bool automatic = requested == UINT32_MAX;
    if (automatic)
        for (requested = 0; requested < limit && voice_active(g, &g->voices[requested]); ++requested) {}
    if (automatic && requested == limit) {
        unsigned priority = sample->priority;
        for (unsigned i = 0; i < limit; ++i) {
            if (g->voices[i].priority < priority) {
                priority = g->voices[i].priority;
                requested = i;
            }
        }
    }
    if (requested >= limit)
        return UINT32_MAX;
    uint32_t duration = ((uint64_t)sample->clip.frames * 60 + sample->rate - 1) / sample->rate;
    if (!++g->audio_serial)
        ++g->audio_serial;
    g->voices[requested] = (struct AudioVoice){
        .source = bank->handle, .sample = sample->handle, .duration = duration,
        .start = g->presents, .frequency = sample->rate, .mode = bank->stream ? bank->mode : sample->mode,
        .distance = {sample->distance[0], sample->distance[1]},
        .volume = sample->volume <= 255 ? sample->volume : 255,
        .pan = sample->pan <= 255 ? sample->pan : 128, .serial = g->audio_serial, .paused = paused,
        .priority = sample->priority, .seek_frame = bank->stream ? bank->stream_frame : 0,
    };
    g->audio_dirty = true;
    if (bank->stream)
        fprintf(g->log, "Audio stream %u/%u started at Present #%u (%u channels, %u Hz)\n",
                bank->selected + 1, bank->count, g->presents, sample->clip.channels, sample->rate);
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

bool audio_output_open(Game *g)
{
    return audio_mixer_open(&g->audio_mixer, g->log);
}

void audio_output_close(Game *g)
{
    audio_mixer_close(g->audio_mixer);
    g->audio_mixer = NULL;
    g->audio_open = false;
}

static void gains(const Game *g, const struct AudioVoice *voice, float out[2])
{
    float volume = voice->volume * (0.75f / 255.f);
    float balance = (voice->pan - 128.f) / 128.f;
    if (voice->spatial && !(voice->mode & 0x2000)) {
        unsigned slot = g->audio_listener;
        const float *up = g->listeners[slot].up, *forward = g->listeners[slot].forward;
        float right[] = {up[1] * forward[2] - up[2] * forward[1],
                         up[2] * forward[0] - up[0] * forward[2],
                         up[0] * forward[1] - up[1] * forward[0]};
        float delta[3], norm = 0.f, stereo = 0.f;
        for (unsigned i = 0; i < 3; ++i) {
            delta[i] = voice->position[i] - g->listeners[slot].position[i];
            norm += delta[i] * delta[i];
            stereo += delta[i] * right[i];
        }
        float distance = sqrtf(norm), minimum = fmaxf(voice->distance[0], 0.001f);
        if (distance > 0.f && isfinite(distance) && isfinite(stereo)) {
            balance = fminf(fmaxf(stereo / distance, -1.f), 1.f);
            volume *= minimum / fmaxf(minimum, fminf(distance, voice->distance[1]));
        }
    }
    out[0] = volume * (1.f - fmaxf(balance, 0.f));
    out[1] = volume * (1.f + fminf(balance, 0.f));
}

static void submit_controls(Game *g)
{
    if (!g->audio_mixer || !g->audio_dirty)
        return;
    AudioControl controls[AUDIO_VOICES] = {0};
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        const struct AudioVoice *voice = &g->voices[i];
        Sample *sample = NULL;
        if (!voice->source || !find_sample(g, voice->sample, &sample))
            continue;
        controls[i] = (AudioControl){.clip = sample->clip, .serial = voice->serial, .frequency = voice->frequency,
                                     .looping = (voice->mode & 2) != 0, .paused = voice->paused,
                                     .seek_id = voice->seek_id, .seek_frame = voice->seek_frame};
        gains(g, voice, controls[i].gain);
    }
    audio_mixer_submit(g->audio_mixer, controls);
    g->audio_dirty = false;
}

void audio_frame(Game *g)
{
    submit_controls(g);
    audio_mixer_frame(g->audio_mixer);
}

bool audio_update(Game *g)
{
    submit_controls(g);
    bool returned = result(g, 0, 0);
    if (!g->audio_mixer || g->audio_callback_active)
        return returned;
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        struct AudioVoice *voice = &g->voices[i];
        if (voice->notified || !audio_mixer_finished(g->audio_mixer, i, voice->serial))
            continue;
        voice->notified = true;
        AudioBank *bank = find_bank(g, voice->source, true);
        if (!bank || !bank->callback)
            continue;
        if (!has_block(bank->callback)) {
            fprintf(g->log, "Untranslated audio stream end callback %08x\n", bank->callback);
            continue;
        }
        g->audio_callback_active = true;
        g->audio_callback_cpu = g->cpu;
        push32(&g->cpu, bank->callback_data);
        push32(&g->cpu, 0);
        push32(&g->cpu, 0);
        push32(&g->cpu, bank->handle);
        push32(&g->cpu, AUDIO_RETURN);
        g->cpu.eip = bank->callback;
        break;
    }
    return returned;
}

static void seek_voice(Game *g, struct AudioVoice *voice, const Sample *sample, uint64_t frame)
{
    voice->seek_frame = frame < sample->clip.frames ? frame : sample->clip.frames;
    voice->seek_id = ++g->audio_seek;
    voice->notified = false;
    g->audio_dirty = true;
}

bool audio_bank_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    uint32_t a = arg(g, 0), b = arg(g, 1);
    bool stream = !strncmp(name, "_FSOUND_Stream_", 15);
    if (IS("_FSOUND_3D_Listener_SetCurrent@8")) {
        if (a < sizeof g->listeners / sizeof *g->listeners && (!b || a < b)) {
            g->audio_listener = a;
            g->audio_dirty = true;
        }
        return result(g, 0, 8);
    }
    if (IS("_FSOUND_3D_Listener_SetAttributes@32")) {
        unsigned slot = g->audio_listener;
        if (!audio_vector(g, a, g->listeners[slot].position) || !audio_vector(g, b, g->listeners[slot].velocity))
            return false;
        memcpy(g->listeners[slot].forward, guest_pointer(g->cpu.esp + 12), 3 * sizeof(float));
        memcpy(g->listeners[slot].up, guest_pointer(g->cpu.esp + 24), 3 * sizeof(float));
        g->audio_dirty = true;
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
    if (IS("_FSOUND_Stream_GetSample@4"))
        return result(g, bank ? bank->samples[bank->selected].handle : 0, 4);
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
    if (IS("_FSOUND_Sample_SetDefaults@20")) {
        if (!find_sample(g, a, &sample))
            return result(g, 0, 20);
        if (b > 0 && b <= 384000) sample->rate = b;
        if (arg(g, 2) <= 255) sample->volume = arg(g, 2);
        if (arg(g, 3) <= 255 || arg(g, 3) == UINT32_MAX) sample->pan = arg(g, 3);
        if (arg(g, 4) <= 255) sample->priority = arg(g, 4);
        return result(g, 1, 20);
    }
    if (IS("_FSOUND_Stream_SetSubStream@8") || IS("_FSOUND_Stream_SetMode@8")) {
        if (!bank || (IS("_FSOUND_Stream_SetSubStream@8") && b >= bank->count))
            return result(g, 0, 8);
        if (IS("_FSOUND_Stream_SetSubStream@8")) {
            bank->selected = b;
            bank->stream_frame = 0;
        } else {
            bank->mode = b;
            for (unsigned i = 0; i < AUDIO_VOICES; ++i)
                if (g->voices[i].source == a)
                    g->voices[i].mode = b;
            g->audio_dirty = true;
        }
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
        if (bank) {
            bank->stream_frame = 0;
            g->audio_dirty = true;
        }
        return result(g, bank != NULL, 4);
    }
    if (IS("_FSOUND_Stream_GetTime@4") || IS("_FSOUND_Stream_SetTime@8")) {
        bool set = IS("_FSOUND_Stream_SetTime@8");
        if (!bank)
            return result(g, set ? 0 : UINT32_MAX, set ? 8 : 4);
        sample = &bank->samples[bank->selected];
        uint64_t frame = (uint64_t)b * sample->rate / 1000;
        if (set)
            bank->stream_frame = frame < sample->clip.frames ? frame : sample->clip.frames;
        for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
            struct AudioVoice *voice = &g->voices[i];
            if (voice->source != a)
                continue;
            if (set)
                seek_voice(g, voice, sample, frame);
            else if (g->audio_mixer)
                return result(g, (uint64_t)audio_mixer_position(g->audio_mixer, i) * 1000 / sample->rate, 4);
            else
                return result(g, ((g->presents - voice->start) * 1000u) / 60, 4);
            break;
        }
        return result(g, set ? 1 : (uint64_t)bank->stream_frame * 1000 / sample->rate, set ? 8 : 4);
    }
    struct AudioVoice *voice = a < sizeof g->voices / sizeof *g->voices ? &g->voices[a] : NULL;
    bool active = voice && voice_active(g, voice);
    if (IS("_FSOUND_3D_SetAttributes@12")) {
        bool updated = active && audio_vector(g, b, voice->position) && audio_vector(g, arg(g, 2), voice->velocity);
        if (updated) {
            voice->spatial = true;
            g->audio_dirty = true;
        }
        return result(g, updated, 12);
    }
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
            g->audio_dirty = true;
        }
        return result(g, distance != NULL, 12);
    }
    if (IS("_FSOUND_IsPlaying@4"))
        return result(g, active, 4);
    if (IS("_FSOUND_GetCurrentSample@4"))
        return result(g, active ? voice->sample : 0, 4);
    if (IS("_FSOUND_StopSound@4")) {
        if (active) {
            *voice = (struct AudioVoice){0};
            g->audio_dirty = true;
        }
        return result(g, active, 4);
    }
    if (IS("_FSOUND_SetCurrentPosition@8")) {
        if (!active || !find_sample(g, voice->sample, &sample))
            return result(g, 0, 8);
        seek_voice(g, voice, sample, b);
        return result(g, 1, 8);
    }
    if (IS("_FSOUND_GetFrequency@4"))
        return result(g, active ? voice->frequency : 0, 4);
    if (IS("_FSOUND_GetVolume@4"))
        return result(g, active ? voice->volume : 0, 4);
    if (IS("_FSOUND_GetLoopMode@4"))
        return result(g, active ? voice->mode & 7 : 0, 4);
    if (IS("_FSOUND_SetVolume@8") || IS("_FSOUND_SetPan@8") || IS("_FSOUND_SetFrequency@8") ||
        IS("_FSOUND_SetLoopMode@8") || IS("_FSOUND_SetPaused@8") || IS("_FSOUND_SetPriority@8")) {
        if (!active)
            return result(g, 0, 8);
        if (IS("_FSOUND_SetVolume@8"))
            voice->volume = b <= 255 ? b : 255;
        else if (IS("_FSOUND_SetPan@8"))
            voice->pan = b <= 255 ? b : 128;
        else if (IS("_FSOUND_SetFrequency@8")) {
            if (!b || b > 384000)
                return result(g, 0, 8);
            voice->frequency = b;
        } else if (IS("_FSOUND_SetLoopMode@8"))
            voice->mode = (voice->mode & ~7u) | (b & 7);
        else if (IS("_FSOUND_SetPriority@8"))
            voice->priority = b <= 255 ? b : 255;
        else
            voice->paused = b != 0;
        g->audio_dirty = true;
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
