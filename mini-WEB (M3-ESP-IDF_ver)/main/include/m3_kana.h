#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { M3_KANA_VOICED, M3_KANA_SEMIVOICED, M3_KANA_SMALL };
uint32_t m3_kana_transform(uint32_t codepoint, unsigned kind);
bool m3_kana_edit(char *text, size_t capacity, unsigned kind);
