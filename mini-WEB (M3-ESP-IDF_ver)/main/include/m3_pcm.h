#pragma once
#include <stdint.h>
/* 100 is unity. 101..150 apply sample gain with signed PCM16 saturation. */
int16_t m3_pcm_gain(int16_t sample, unsigned percent);
