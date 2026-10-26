#include "m4_layout.h"

lv_obj_t *m4_layout_create_root(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    if (!root) return NULL;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, M4_LAYOUT_WIDTH, M4_LAYOUT_HEIGHT);
    lv_obj_set_pos(root, 0, M4_LAYOUT_TOP);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(root, 0, 0);
    lv_obj_set_style_transform_pivot_y(root, 0, 0);
    lv_obj_set_style_transform_scale(root, M4_LAYOUT_SCALE, 0);
    return root;
}

void m4_layout_get_point(const lv_indev_t *indev, lv_point_t *point) {
    lv_indev_get_point(indev, point);
    /* LVGL handles rotated touch and transformed hit testing. Only manual
     * gesture distances/thresholds need conversion into the app design space. */
    point->x = point->x * 256 / M4_LAYOUT_SCALE;
    point->y = (point->y - M4_LAYOUT_TOP) * 256 / M4_LAYOUT_SCALE;
}
