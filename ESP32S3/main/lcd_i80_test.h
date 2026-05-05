#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t lcd_i80_test_init(void);
esp_err_t lcd_i80_test_fill_color(uint16_t color);
