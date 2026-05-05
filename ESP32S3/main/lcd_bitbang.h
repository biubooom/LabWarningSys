#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t lcd_bitbang_init(void);
esp_err_t lcd_bitbang_fill_color(uint16_t color);
