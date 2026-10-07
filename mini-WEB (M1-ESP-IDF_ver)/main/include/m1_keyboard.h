#pragma once
#include "lvgl.h"
#include <stdbool.h>
/* Single keyboard on the M1 screen. The page owns all LVGL objects.
 * Reset before deleting the page; text areas retain their input on close. */
void m1_keyboard_create(lv_obj_t *parent, lv_obj_t *status, lv_obj_t *list,
                        lv_obj_t *pager, lv_obj_t *const actions[3]);
void m1_keyboard_open(lv_obj_t *textarea, const char *field_name);
void m1_keyboard_close(void);
void m1_keyboard_reset(void);
bool m1_keyboard_is_open(void);
