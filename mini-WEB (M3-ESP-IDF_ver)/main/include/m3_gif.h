#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { M3_GIF_OK, M3_GIF_END, M3_GIF_FORMAT, M3_GIF_STORAGE, M3_GIF_MEMORY, M3_GIF_STOPPED } m3_gif_result_t;
typedef struct m3_gif m3_gif_t;
/* Worker only: streams frames from SD; only the composited screen stays in PSRAM. */
m3_gif_result_t m3_gif_open(const char *relative, unsigned job, bool (*current)(unsigned), m3_gif_t **out);
/* Writes the next composited frame fitted to the photo canvas and loops at the trailer.
 * M3_GIF_END means a still GIF: the canvas holds its only frame and no more follow. */
m3_gif_result_t m3_gif_frame(m3_gif_t *gif, uint8_t *canvas, unsigned *delay_ms);
void m3_gif_close(m3_gif_t *gif);
