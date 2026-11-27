#include "audio_mixer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

enum { BLOCK_FRAMES = 64, BUFFER_FRAMES = 768, READ_BYTES = 8192 };

struct AudioFile {
    FILE *source;
    atomic_uint refs;
};

typedef struct {
    AudioControl control;
    uint64_t phase;
    float gain[2];
    uint32_t first_byte, buffered_bytes, decoded[2];
    int16_t pcm[2][BLOCK_FRAMES][2];
    uint8_t bytes[READ_BYTES];
} MixVoice;

struct AudioMixer {
    FILE *log;
    AudioControl pending[AUDIO_VOICES];
    MixVoice voices[AUDIO_VOICES];
    atomic_uint finished[AUDIO_VOICES], position[AUDIO_VOICES];
    bool changed;
    uint64_t frames, bytes_read;
    unsigned plays, read_errors;
    float limiter;
#ifdef __SWITCH__
    Mutex mutex;
    Thread worker;
    atomic_bool quit;
    AudioOutBuffer buffers[3];
    unsigned drained;
    Result output_error;
    uint64_t peak_mix_ns;
#else
    FILE *capture;
#endif
};

AudioFile *audio_file_create(FILE *source)
{
    AudioFile *file = malloc(sizeof *file);
    if (file) {
        file->source = source;
        atomic_init(&file->refs, 1);
    }
    return file;
}

static void retain(AudioFile *file)
{
    if (file)
        atomic_fetch_add_explicit(&file->refs, 1, memory_order_relaxed);
}

void audio_file_release(AudioFile *file)
{
    if (file && atomic_fetch_sub_explicit(&file->refs, 1, memory_order_acq_rel) == 1) {
        fclose(file->source);
        free(file);
    }
}

static int16_t signed_le(const uint8_t *p)
{
    return (int16_t)(p[0] | (unsigned)p[1] << 8);
}

static void decode_ima(const uint8_t *bytes, unsigned channels, int16_t pcm[BLOCK_FRAMES][2])
{
    static const int index_delta[] = {-1, -1, -1, -1, 2, 4, 6, 8};
    static const int steps[] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
        2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
        8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
        24623, 27086, 29794, 32767,
    };
    for (unsigned channel = 0; channel < channels; ++channel) {
        int predictor = signed_le(bytes + channel * 4);
        int index = bytes[channel * 4 + 2];
        if (index > 88)
            index = 88;
        pcm[0][channel] = predictor;
        for (unsigned i = 0; i < BLOCK_FRAMES - 1; ++i) {
            unsigned at = (channels + (i / 8) * channels + channel) * 4 + i / 2 % 4;
            unsigned code = (bytes[at] >> (i % 2 * 4)) & 15;
            int step = steps[index];
            int difference = step >> 3;
            if (code & 4) difference += step;
            if (code & 2) difference += step >> 1;
            if (code & 1) difference += step >> 2;
            predictor += (code & 8) ? -difference : difference;
            if (predictor < -32768) predictor = -32768;
            if (predictor > 32767) predictor = 32767;
            index += index_delta[code & 7];
            if (index < 0) index = 0;
            if (index > 88) index = 88;
            pcm[i + 1][channel] = predictor;
        }
    }
}

static const int16_t *frame(AudioMixer *mixer, MixVoice *voice, uint32_t index)
{
    const AudioClip *clip = &voice->control.clip;
    uint32_t block = index / BLOCK_FRAMES, slot = block % 2;
    if (voice->decoded[slot] != block) {
        unsigned stride = (clip->ima ? 36 : BLOCK_FRAMES * 2) * clip->channels;
        uint32_t at = block * stride;
        if (at >= clip->size || (clip->ima && clip->size - at < stride))
            return NULL;
        unsigned needed = clip->size - at;
        if (needed > stride)
            needed = stride;
        if (at < voice->first_byte || at + needed > voice->first_byte + voice->buffered_bytes) {
            unsigned bytes = clip->size - at;
            if (bytes > READ_BYTES)
                bytes = READ_BYTES;
            FILE *source = clip->file->source;
            if (fseek(source, clip->offset + at, SEEK_SET) || fread(voice->bytes, 1, bytes, source) != bytes)
                return NULL;
            voice->first_byte = at;
            voice->buffered_bytes = bytes;
            mixer->bytes_read += bytes;
        }
        const uint8_t *bytes = voice->bytes + at - voice->first_byte;
        if (clip->ima) {
            decode_ima(bytes, clip->channels, voice->pcm[slot]);
        } else {
            unsigned frames = needed / (2 * clip->channels);
            for (unsigned i = 0; i < frames; ++i)
                for (unsigned ch = 0; ch < clip->channels; ++ch)
                    voice->pcm[slot][i][ch] = signed_le(bytes + (i * clip->channels + ch) * 2);
        }
        voice->decoded[slot] = block;
    }
    return voice->pcm[slot][index % BLOCK_FRAMES];
}

void audio_mixer_submit(AudioMixer *mixer, const AudioControl controls[AUDIO_VOICES])
{
    if (!mixer)
        return;
    AudioFile *old[AUDIO_VOICES];
#ifdef __SWITCH__
    mutexLock(&mixer->mutex);
#endif
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        old[i] = mixer->pending[i].clip.file;
        if (old[i] != controls[i].clip.file)
            retain(controls[i].clip.file);
        else
            old[i] = NULL;
    }
    memcpy(mixer->pending, controls, sizeof mixer->pending);
    mixer->changed = true;
#ifdef __SWITCH__
    mutexUnlock(&mixer->mutex);
#endif
    for (unsigned i = 0; i < AUDIO_VOICES; ++i)
        audio_file_release(old[i]);
}

static void take_controls(AudioMixer *mixer)
{
    AudioControl next[AUDIO_VOICES];
#ifdef __SWITCH__
    mutexLock(&mixer->mutex);
#endif
    bool changed = mixer->changed;
    if (changed) {
        memcpy(next, mixer->pending, sizeof next);
        for (unsigned i = 0; i < AUDIO_VOICES; ++i)
            if (next[i].clip.file != mixer->voices[i].control.clip.file)
                retain(next[i].clip.file);
        mixer->changed = false;
    }
#ifdef __SWITCH__
    mutexUnlock(&mixer->mutex);
#endif
    if (!changed)
        return;
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        MixVoice *voice = &mixer->voices[i];
        if (next[i].clip.file != voice->control.clip.file)
            audio_file_release(voice->control.clip.file);
        if (next[i].serial != voice->control.serial) {
            voice->buffered_bytes = 0;
            voice->decoded[0] = voice->decoded[1] = UINT32_MAX;
            voice->gain[0] = voice->gain[1] = 0.f;
            voice->phase = (uint64_t)next[i].seek_frame << 32;
            if (next[i].serial)
                ++mixer->plays;
        } else if (next[i].seek_id != voice->control.seek_id) {
            voice->phase = (uint64_t)next[i].seek_frame << 32;
            atomic_store_explicit(&mixer->finished[i], 0, memory_order_relaxed);
        }
        voice->control = next[i];
    }
}

bool audio_mixer_finished(const AudioMixer *mixer, unsigned channel, uint32_t serial)
{
    return mixer && serial && atomic_load_explicit(&mixer->finished[channel], memory_order_relaxed) == serial;
}

uint32_t audio_mixer_position(const AudioMixer *mixer, unsigned channel)
{
    return mixer ? atomic_load_explicit(&mixer->position[channel], memory_order_relaxed) : 0;
}

static void mix(AudioMixer *mixer, int16_t *pcm, unsigned frames)
{
    float stereo[BUFFER_FRAMES][2] = {{0}};
    take_controls(mixer);
    for (unsigned channel = 0; channel < AUDIO_VOICES; ++channel) {
        MixVoice *voice = &mixer->voices[channel];
        const AudioControl *control = &voice->control;
        const AudioClip *clip = &control->clip;
        if (!clip->file || control->paused || audio_mixer_finished(mixer, channel, control->serial))
            continue;
        uint32_t end = control->looping ? clip->loop_end : clip->frames;
        uint64_t step = ((uint64_t)control->frequency << 32) / AUDIO_RATE;
        float gain[2] = {voice->gain[0], voice->gain[1]};
        for (unsigned i = 0; i < frames; ++i) {
            uint32_t at = voice->phase >> 32;
            if (at >= end) {
                if (!control->looping) {
                    atomic_store_explicit(&mixer->finished[channel], control->serial, memory_order_relaxed);
                    break;
                }
                at = clip->loop_start + (at - end) % (end - clip->loop_start);
                voice->phase = ((uint64_t)at << 32) | (uint32_t)voice->phase;
            }
            uint32_t next = at + 1 < end ? at + 1 : control->looping ? clip->loop_start : at;
            const int16_t *a = frame(mixer, voice, at);
            int16_t current[2] = {0};
            if (a) {
                current[0] = a[0];
                current[1] = a[clip->channels - 1];
            }
            const int16_t *b = a ? frame(mixer, voice, next) : NULL;
            if (!b) {
                ++mixer->read_errors;
                atomic_store_explicit(&mixer->finished[channel], control->serial, memory_order_relaxed);
                break;
            }
            float fraction = (uint32_t)voice->phase * (1.f / 4294967296.f);
            for (unsigned out = 0; out < 2; ++out) {
                gain[out] += (control->gain[out] - gain[out]) * (1.f / 64.f);
                float sample = current[out] + (b[out && clip->channels == 2] - current[out]) * fraction;
                stereo[i][out] += sample * gain[out];
            }
            voice->phase += step;
        }
        voice->gain[0] = gain[0];
        voice->gain[1] = gain[1];
        atomic_store_explicit(&mixer->position[channel], voice->phase >> 32, memory_order_relaxed);
    }
    float limited = mixer->limiter;
    for (unsigned i = 0; i < frames; ++i) {
        float left = stereo[i][0], right = stereo[i][1];
        float peak_left = left < 0.f ? -left : left, peak_right = right < 0.f ? -right : right;
        float peak = peak_left > peak_right ? peak_left : peak_right;
        float target = peak > 30000.f ? 30000.f / peak : 1.f;
        limited += (1.f - limited) * (1.f / 1024.f);
        if (limited > target)
            limited = target;
        for (unsigned ch = 0; ch < 2; ++ch) {
            pcm[i * 2 + ch] = (int16_t)(stereo[i][ch] * limited);
        }
    }
    mixer->limiter = limited;
    mixer->frames += frames;
}

#ifdef __SWITCH__
static void run_output(void *arg)
{
    AudioMixer *mixer = arg;
    unsigned queued = 0;
    for (unsigned i = 0; i < 3; ++i) {
        AudioOutBuffer *buffer = &mixer->buffers[i];
        armDCacheFlush(buffer->buffer, buffer->buffer_size);
        mixer->output_error = audoutAppendAudioOutBuffer(buffer);
        if (R_FAILED(mixer->output_error))
            return;
        ++queued;
    }
    mixer->output_error = audoutStartAudioOut();
    if (R_FAILED(mixer->output_error))
        return;
    while (!atomic_load_explicit(&mixer->quit, memory_order_relaxed)) {
        AudioOutBuffer *ready[3];
        unsigned count = 0;
        for (;;) {
            AudioOutBuffer *buffer = NULL;
            u32 released = 0;
            Result rc = audoutGetReleasedAudioOutBuffer(&buffer, &released);
            if (R_SUCCEEDED(rc) && !released && !count) {
                rc = audoutWaitPlayFinish(&buffer, &released, 25000000);
                if (rc == KERNELRESULT(TimedOut))
                    break;
            }
            if (R_FAILED(rc)) {
                mixer->output_error = rc;
                return;
            }
            if (!released || !buffer)
                break;
            ready[count++] = buffer;
            --queued;
            if (count == 3)
                break;
        }
        if (count && !queued)
            ++mixer->drained;
        for (unsigned i = 0; i < count; ++i) {
            AudioOutBuffer *buffer = ready[i];
            uint64_t start = armGetSystemTick();
            mix(mixer, buffer->buffer, BUFFER_FRAMES);
            uint64_t ns = armTicksToNs(armGetSystemTick() - start);
            if (ns > mixer->peak_mix_ns)
                mixer->peak_mix_ns = ns;
            armDCacheFlush(buffer->buffer, buffer->buffer_size);
            mixer->output_error = audoutAppendAudioOutBuffer(buffer);
            if (R_FAILED(mixer->output_error))
                return;
            ++queued;
        }
    }
}
#else
static void wav_header(FILE *file, uint32_t bytes)
{
    uint32_t words[] = {0x46464952, bytes + 36, 0x45564157, 0x20746d66, 16, 0x20001,
                        AUDIO_RATE, AUDIO_RATE * 4, 0x100004, 0x61746164, bytes};
    rewind(file);
    fwrite(words, sizeof words, 1, file);
}
#endif

bool audio_mixer_open(AudioMixer **output, FILE *log)
{
#ifndef __SWITCH__
    const char *path = getenv("FREAKOUT_AUDIO_WAV");
    if (!path || !*path)
        return true;
#endif
    AudioMixer *mixer = calloc(1, sizeof *mixer);
    if (!mixer)
        return false;
    mixer->log = log;
    mixer->limiter = 1.f;
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        atomic_init(&mixer->finished[i], 0);
        atomic_init(&mixer->position[i], 0);
    }
#ifdef __SWITCH__
    Result rc = audoutInitialize();
    if (R_FAILED(rc))
        goto fail;
    if (audoutGetSampleRate() != AUDIO_RATE || audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16)
        goto close_output;
    mutexInit(&mixer->mutex);
    atomic_init(&mixer->quit, false);
    for (unsigned i = 0; i < 3; ++i) {
        void *data = aligned_alloc(4096, 4096);
        if (!data)
            goto close_output;
        memset(data, 0, 4096);
        mixer->buffers[i] = (AudioOutBuffer){.buffer = data, .buffer_size = 4096,
                                            .data_size = BUFFER_FRAMES * 4};
    }
    u64 cores = 0;
    svcGetInfo(&cores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    rc = threadCreate(&mixer->worker, run_output, mixer, NULL, 64 * 1024, 0x2b, (cores & 4) ? 2 : -2);
    if (R_FAILED(rc))
        goto close_output;
    rc = threadStart(&mixer->worker);
    if (R_FAILED(rc)) {
        threadClose(&mixer->worker);
        goto close_output;
    }
#else
    mixer->capture = fopen(path, "wb");
    if (!mixer->capture)
        goto fail;
    wav_header(mixer->capture, 0);
#endif
    fputs("Audio mixer initialized: 48000 Hz stereo, PCM16 and FSB IMA ADPCM\n", log);
    *output = mixer;
    return true;
#ifdef __SWITCH__
close_output:
    audoutExit();
    for (unsigned i = 0; i < 3; ++i)
        free(mixer->buffers[i].buffer);
#endif
fail:
    free(mixer);
    fputs("Audio mixer initialization failed\n", log);
    return false;
}

void audio_mixer_frame(AudioMixer *mixer)
{
#ifndef __SWITCH__
    if (!mixer || !mixer->capture || mixer->frames > (UINT32_MAX - 4096) / 4)
        return;
    const unsigned frames = AUDIO_RATE / 60;
    int16_t pcm[frames * 2];
    mix(mixer, pcm, BUFFER_FRAMES);
    mix(mixer, pcm + BUFFER_FRAMES * 2, frames - BUFFER_FRAMES);
    fwrite(pcm, sizeof(int16_t) * 2, frames, mixer->capture);
#else
    (void)mixer;
#endif
}

void audio_mixer_close(AudioMixer *mixer)
{
    if (!mixer)
        return;
#ifdef __SWITCH__
    atomic_store_explicit(&mixer->quit, true, memory_order_relaxed);
    threadWaitForExit(&mixer->worker);
    threadClose(&mixer->worker);
    audoutStopAudioOut();
    audoutExit();
    for (unsigned i = 0; i < 3; ++i)
        free(mixer->buffers[i].buffer);
    fprintf(mixer->log, "Audio output: %u empty queues, %.2f ms peak mix, service result 0x%08x\n",
            mixer->drained, mixer->peak_mix_ns / 1e6, mixer->output_error);
#else
    wav_header(mixer->capture, mixer->frames * 4);
    fclose(mixer->capture);
#endif
    fprintf(mixer->log, "Audio mix: %u voices started, %llu frames, %llu bytes streamed, %u read errors\n",
            mixer->plays, (unsigned long long)mixer->frames, (unsigned long long)mixer->bytes_read, mixer->read_errors);
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        audio_file_release(mixer->pending[i].clip.file);
        audio_file_release(mixer->voices[i].control.clip.file);
    }
    free(mixer);
}
