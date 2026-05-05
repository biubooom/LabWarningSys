#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t lvgl_port_init(lv_display_t **display);
esp_err_t lvgl_port_fill_color(uint16_t color);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);
