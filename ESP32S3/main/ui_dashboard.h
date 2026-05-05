#pragma once

#include "lvgl.h"
#include "sensor_data.h"

void ui_dashboard_create(lv_display_t *display);
void ui_dashboard_update(const sensor_snapshot_t *snapshot);
void ui_dashboard_touch_debug_update(bool pressed, uint16_t x, uint16_t y);
