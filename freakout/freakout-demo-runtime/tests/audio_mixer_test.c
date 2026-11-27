#define _POSIX_C_SOURCE 200809L
#include "../native/source/audio_mixer.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    char path[] = "/tmp/freakout-audio-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(setenv("FREAKOUT_AUDIO_WAV", path, 1) == 0);

    AudioMixer *mixer = NULL;
    assert(audio_mixer_open(&mixer, stderr) && mixer);
    FILE *payload = tmpfile();
    assert(payload);
    for (unsigned i = 0; i < 128; ++i)
        assert(fwrite("\x10\x27", 2, 1, payload) == 1); /* PCM: +10000 */
    uint8_t ima[72] = {0x40, 0x1f, 0, 0, 0xc0, 0xe0, 0, 0}; /* +8000 L, -8000 R */
    ima[8] = 0x07;
    ima[12] = 0x0f;
    assert(fwrite(ima, sizeof ima, 1, payload) == 1);
    AudioFile *file = audio_file_create(payload);
    assert(file);

    AudioControl control[AUDIO_VOICES] = {0};
    control[0] = (AudioControl){.clip = {.file = file, .size = 256, .frames = 128, .channels = 1},
                                .serial = 1, .frequency = 48000, .gain = {1, 1}};
    control[1] = (AudioControl){.clip = {.file = file, .offset = 256, .size = sizeof ima,
                                        .frames = 64, .loop_end = 64, .channels = 2, .ima = true},
                                .serial = 2, .frequency = 48000, .gain = {1, 1}, .looping = true};
    audio_mixer_submit(mixer, control);
    audio_file_release(file);
    audio_mixer_frame(mixer);
    assert(audio_mixer_finished(mixer, 0, 1));
    assert(!audio_mixer_finished(mixer, 1, 2));
    assert(audio_mixer_position(mixer, 0) == 128);
    assert(audio_mixer_position(mixer, 1) == 32);

    control[1].paused = true;
    audio_mixer_submit(mixer, control);
    audio_mixer_frame(mixer);
    assert(audio_mixer_position(mixer, 1) == 32);

    control[0].serial = 3;
    control[0].looping = true;
    control[0].clip.loop_end = 128;
    control[0].gain[0] = control[0].gain[1] = 8.f;
    audio_mixer_submit(mixer, control);
    audio_mixer_frame(mixer);
    audio_mixer_close(mixer);

    FILE *wav = fopen(path, "rb");
    assert(wav);
    int16_t frames[2400][2];
    assert(fseek(wav, 44, SEEK_SET) == 0 && fread(frames, sizeof frames[0], 2400, wav) == 2400);
    assert(frames[0][0] == 281 && frames[0][1] == 31);
    assert(frames[64][0] > 11000 && frames[64][1] > 1000);
    assert(frames[200][0] > 7500 && frames[200][1] < -7500);
    for (unsigned i = 800; i < 1600; ++i)
        assert(frames[i][0] == 0 && frames[i][1] == 0);
    for (unsigned i = 1600; i < 2400; ++i)
        assert(frames[i][0] == frames[i][1] && frames[i][0] >= 0 && frames[i][0] <= 30000);
    assert(frames[2399][0] >= 29999);
    fclose(wav);
    unlink(path);
}
