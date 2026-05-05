#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t touch_ft6336_init(lv_display_t *display);
bool touch_ft6336_get_last_point(bool *pressed, uint16_t *x, uint16_t *y);
