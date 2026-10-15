#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { M3_PHOTO_W = 432, M3_PHOTO_H = 480, M3_PHOTO_BYTES = M3_PHOTO_W * M3_PHOTO_H * 2 };
typedef enum { M3_PHOTO_IDLE, M3_PHOTO_LOADING, M3_PHOTO_READY, M3_PHOTO_EMPTY,
    M3_PHOTO_BUSY, M3_PHOTO_INVALID, M3_PHOTO_NO_MEMORY, M3_PHOTO_NO_SD } m3_photo_status_t;
typedef struct {
    m3_photo_status_t status;
    unsigned revision, count, index;
    bool paused;
    char name[192];
} m3_photo_state_t;
void m3_photos_activate(bool active);
void m3_photos_step(int direction);
void m3_photos_pause(void);
void m3_photos_refresh(void);
void m3_photos_state(m3_photo_state_t *out);
void m3_photos_poll(void); /* Service task only; never calls LVGL. */
/* UI task only: returned canvas stays immutable until the next call. */
const uint8_t *m3_photos_frame(void);
bool m3_photo_bmp_fit(const uint8_t *data, size_t size, uint8_t *canvas);
bool m3_photo_rgb565_fit(const uint8_t *data, unsigned width, unsigned height, unsigned stride, uint8_t *canvas);
/* Bounded JPEG/BMP/PNG decode for service workers, never from an LVGL callback. */
bool m3_photo_decode(const uint8_t *data, size_t size, uint8_t *canvas);
