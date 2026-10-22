#pragma once
#include <stdint.h>
#include <math.h>

/* Low-level, zero-centred PCM16 effects at 48 kHz. Selection 0 is silence. */
static inline unsigned m3_sound_frames(unsigned sound) {
    return sound == 1 ? 1920 : sound == 2 ? 6720 : sound == 3 ? 8640 : 0;
}
static inline int16_t m3_sound_sample(unsigned sound, unsigned index) {
    unsigned frames = m3_sound_frames(sound);
    if (!frames || index >= frames) return 0;
    float frequency = sound == 1 ? 800.0f : sound == 2 ? 880.0f : 660.0f;
    unsigned local = index, length = frames;
    if (sound == 3) {
        if (index >= 3840 && index < 4800) return 0;
        if (index >= 4800) { local = index - 4800; frequency = 990.0f; }
        length = 3840;
    }
    float gain = 1.0f;
    if (local < 384) gain = local / 384.0f;
    unsigned remaining = length - 1 - local;
    if (remaining < 768 && remaining / 768.0f < gain) gain = remaining / 768.0f;
    float wave = sinf(6.28318530718f * frequency * local / 48000.0f);
    if (sound == 2) wave = 0.8f * wave + 0.2f * sinf(6.28318530718f * 1760.0f * local / 48000.0f);
    return (int16_t)(wave * gain * 1800.0f);
}
