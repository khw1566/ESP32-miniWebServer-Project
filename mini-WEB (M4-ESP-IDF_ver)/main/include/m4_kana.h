#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { M4_KANA_VOICED, M4_KANA_SEMIVOICED, M4_KANA_SMALL };
uint32_t m4_kana_transform(uint32_t codepoint, unsigned kind);
bool m4_kana_edit(char *text, size_t capacity, unsigned kind);
