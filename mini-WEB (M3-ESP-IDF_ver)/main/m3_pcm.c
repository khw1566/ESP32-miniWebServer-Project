#include "m3_pcm.h"
int16_t m3_pcm_gain(int16_t sample, unsigned percent) {
    if (percent > 150) percent = 150;
    if (percent <= 100) return sample;
    int32_t value = (int32_t)sample * (int32_t)percent / 100;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    return (int16_t)value;
}
