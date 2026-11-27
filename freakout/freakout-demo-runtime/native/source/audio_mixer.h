#ifndef FREAKOUT_AUDIO_MIXER_H
#define FREAKOUT_AUDIO_MIXER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { AUDIO_VOICES = 64, AUDIO_RATE = 48000 };

typedef struct AudioFile AudioFile;
typedef struct AudioMixer AudioMixer;

typedef struct {
    AudioFile *file;
    uint32_t offset, size, frames, loop_start, loop_end;
    unsigned channels;
    bool ima;
} AudioClip;

typedef struct {
    AudioClip clip;
    uint32_t serial, frequency, seek_id, seek_frame;
    float gain[2];
    bool looping, paused;
} AudioControl;

AudioFile *audio_file_create(FILE *source);
void audio_file_release(AudioFile *file);
bool audio_mixer_open(AudioMixer **output, FILE *log);
void audio_mixer_close(AudioMixer *mixer);
void audio_mixer_submit(AudioMixer *mixer, const AudioControl controls[AUDIO_VOICES]);
bool audio_mixer_finished(const AudioMixer *mixer, unsigned channel, uint32_t serial);
uint32_t audio_mixer_position(const AudioMixer *mixer, unsigned channel);
void audio_mixer_frame(AudioMixer *mixer);

#endif
