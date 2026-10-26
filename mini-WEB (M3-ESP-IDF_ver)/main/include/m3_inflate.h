#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decompress a zlib stream (tools write it with zlib level 9) whose exact output size
 * is known. Returns a PSRAM-preferred buffer the caller frees with free(), or NULL on
 * allocation failure or corrupt data. The firmware uses the ROM inflater with its
 * decompressor state on the heap, so callers need no extra stack. */
uint8_t *m3_inflate(const uint8_t *packed, size_t packed_size, size_t size);
