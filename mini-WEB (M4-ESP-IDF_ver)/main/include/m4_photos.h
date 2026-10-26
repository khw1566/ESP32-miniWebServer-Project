#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { M4_PHOTO_W = 432, M4_PHOTO_H = 480, M4_PHOTO_BYTES = M4_PHOTO_W * M4_PHOTO_H * 2 };
typedef enum { M4_PHOTO_IDLE, M4_PHOTO_LOADING, M4_PHOTO_READY, M4_PHOTO_EMPTY,
    M4_PHOTO_BUSY, M4_PHOTO_INVALID, M4_PHOTO_NO_MEMORY, M4_PHOTO_NO_SD } m4_photo_status_t;
typedef struct {
    m4_photo_status_t status;
    unsigned revision, count, index;
    bool paused;
    char name[192];
} m4_photo_state_t;
void m4_photos_activate(bool active);
void m4_photos_step(int direction);
void m4_photos_pause(void);
void m4_photos_refresh(void);
void m4_photos_state(m4_photo_state_t *out);
void m4_photos_poll(void); /* Service task only; never calls LVGL. */
/* UI task only: returned canvas stays immutable until the next call. */
const uint8_t *m4_photos_frame(void);
bool m4_photo_bmp_fit(const uint8_t *data, size_t size, uint8_t *canvas);
bool m4_photo_rgb565_fit(const uint8_t *data, unsigned width, unsigned height, unsigned stride, uint8_t *canvas);
/* Bounded JPEG/BMP/PNG decode for service workers, never from an LVGL callback. */
bool m4_photo_decode(const uint8_t *data, size_t size, uint8_t *canvas);
