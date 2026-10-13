#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Append one UTF-8 jamo/key, composing the final syllable (2-beolsik style). */
bool m3_hangul_append(char *text, size_t capacity, const char *key);
