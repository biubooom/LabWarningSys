#include "lcd_bitbang.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_CMD_SWRESET 0x01
#define LCD_CMD_SLPOUT  0x11
#define LCD_CMD_DISPON  0x29
#define LCD_CMD_CASET   0x2A
#define LCD_CMD_RASET   0x2B
#define LCD_CMD_RAMWR   0x2C
#define LCD_CMD_MADCTL  0x36
#define LCD_CMD_COLMOD  0x3A
#define LCD_CMD_INVON   0x21
#define LCD_CMD_CSCON   0xF0
#define LCD_CMD_INVCTR  0xB4
#define LCD_CMD_ENTRY   0xB7
#define LCD_CMD_DFUNCTR 0xB6
#define LCD_CMD_DOCA    0xE8
#define LCD_CMD_PWCTR2  0xC1
#define LCD_CMD_PWCTR3  0xC2
#define LCD_CMD_VMCTR   0xC5
#define LCD_CMD_GMCTRP1 0xE0
#define LCD_CMD_GMCTRN1 0xE1

#define LCD_IO_DELAY_US 1U

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
} st7796_init_cmd_t;

static const char *TAG = "lcd_bitbang";

static const int s_data_pins[8] = {
    APP_LCD_D0_PIN,
    APP_LCD_D1_PIN,
    APP_LCD_D2_PIN,
    APP_LCD_D3_PIN,
    APP_LCD_D4_PIN,
    APP_LCD_D5_PIN,
    APP_LCD_D6_PIN,
    APP_LCD_D7_PIN,
};

static const st7796_init_cmd_t s_init_cmds[] = {
    {LCD_CMD_SLPOUT, {0}, 0, 120},
    {LCD_CMD_CSCON, {0xC3}, 1, 0},
    {LCD_CMD_CSCON, {0x96}, 1, 0},
    {LCD_CMD_MADCTL, {0x28}, 1, 0},
    {LCD_CMD_COLMOD, {0x55}, 1, 0},
    {LCD_CMD_INVCTR, {0x01}, 1, 0},
    {LCD_CMD_ENTRY, {0xC6}, 1, 0},
    {LCD_CMD_DFUNCTR, {0x80, 0x22, 0x3B}, 3, 0},
    {LCD_CMD_DOCA, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {LCD_CMD_PWCTR2, {0x06}, 1, 0},
    {LCD_CMD_PWCTR3, {0xA7}, 1, 0},
    {LCD_CMD_VMCTR, {0x18}, 1, 0},
    {LCD_CMD_GMCTRP1, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 14, 0},
    {LCD_CMD_GMCTRN1, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2D, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 14, 0},
    {LCD_CMD_CSCON, {0x3C}, 1, 0},
    {LCD_CMD_CSCON, {0x69}, 1, 0},
    {LCD_CMD_INVON, {0}, 0, 120},
    {LCD_CMD_DISPON, {0}, 0, 20},
};

static inline void lcd_delay(void)
{
    esp_rom_delay_us(LCD_IO_DELAY_US);
}

static inline void lcd_write_bus(uint8_t value)
{
    size_t i;

    for (i = 0; i < 8; ++i) {
        gpio_set_level(s_data_pins[i], (value >> i) & 0x01U);
    }
}

static inline void lcd_write_strobe(void)
{
    gpio_set_level(APP_LCD_WR_PIN, 0);
    lcd_delay();
    gpio_set_level(APP_LCD_WR_PIN, 1);
    lcd_delay();
}

static void lcd_write_byte(bool is_data, uint8_t value)
{
    gpio_set_level(APP_LCD_DC_PIN, is_data ? 1 : 0);
    gpio_set_level(APP_LCD_CS_PIN, 0);
    lcd_delay();
    lcd_write_bus(value);
    lcd_delay();
    lcd_write_strobe();
    gpio_set_level(APP_LCD_CS_PIN, 1);
    lcd_delay();
}

static inline void lcd_write_cmd(uint8_t cmd)
{
    lcd_write_byte(false, cmd);
}

static inline void lcd_write_data8(uint8_t data)
{
    lcd_write_byte(true, data);
}

static void lcd_write_data16(uint16_t data)
{
    lcd_write_data8((uint8_t)(data >> 8));
    lcd_write_data8((uint8_t)(data & 0xFF));
}

static esp_err_t lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(LCD_CMD_CASET);
    lcd_write_data16(x0);
    lcd_write_data16(x1);
    lcd_write_cmd(LCD_CMD_RASET);
    lcd_write_data16(y0);
    lcd_write_data16(y1);
    lcd_write_cmd(LCD_CMD_RAMWR);
    return ESP_OK;
}

static esp_err_t lcd_hw_reset(void)
{
    if (APP_LCD_RST_PIN < 0) {
        lcd_write_cmd(LCD_CMD_SWRESET);
        vTaskDelay(pdMS_TO_TICKS(120));
        return ESP_OK;
    }

    gpio_set_level(APP_LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(APP_LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(APP_LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t lcd_bitbang_init(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    size_t i;

    for (i = 0; i < 8; ++i) {
        io_config.pin_bit_mask |= (1ULL << s_data_pins[i]);
    }
    io_config.pin_bit_mask |= (1ULL << APP_LCD_WR_PIN) |
                              (1ULL << APP_LCD_DC_PIN) |
                              (1ULL << APP_LCD_CS_PIN) |
                              ((APP_LCD_RST_PIN >= 0) ? (1ULL << APP_LCD_RST_PIN) : 0ULL) |
                              ((APP_LCD_BL_PIN >= 0) ? (1ULL << APP_LCD_BL_PIN) : 0ULL) |
                              ((APP_LCD_RD_PIN >= 0) ? (1ULL << APP_LCD_RD_PIN) : 0ULL);
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "gpio config failed");

    if (APP_LCD_RD_PIN >= 0) {
        gpio_set_level(APP_LCD_RD_PIN, 1);
    }
    gpio_set_level(APP_LCD_CS_PIN, 1);
    gpio_set_level(APP_LCD_WR_PIN, 1);
    gpio_set_level(APP_LCD_DC_PIN, 1);
    if (APP_LCD_BL_PIN >= 0) {
        gpio_set_level(APP_LCD_BL_PIN, APP_LCD_BL_ON_LEVEL);
    }

    ESP_RETURN_ON_ERROR(lcd_hw_reset(), TAG, "lcd reset failed");

    for (i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); ++i) {
        size_t j;

        lcd_write_cmd(s_init_cmds[i].cmd);
        for (j = 0; j < s_init_cmds[i].len; ++j) {
            lcd_write_data8(s_init_cmds[i].data[j]);
        }
        if (s_init_cmds[i].delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(s_init_cmds[i].delay_ms));
        }
    }

    ESP_LOGI(TAG, "bitbang lcd initialized");
    return ESP_OK;
}

esp_err_t lcd_bitbang_fill_color(uint16_t color)
{
    size_t total_pixels = (size_t)APP_LCD_H_RES * (size_t)APP_LCD_V_RES;
    size_t i;

    ESP_RETURN_ON_ERROR(lcd_set_window(0, 0, APP_LCD_H_RES - 1, APP_LCD_V_RES - 1), TAG, "set window failed");

    gpio_set_level(APP_LCD_DC_PIN, 1);
    gpio_set_level(APP_LCD_CS_PIN, 0);
    lcd_delay();

    for (i = 0; i < total_pixels; ++i) {
        lcd_write_bus((uint8_t)(color >> 8));
        lcd_delay();
        lcd_write_strobe();
        lcd_write_bus((uint8_t)(color & 0xFF));
        lcd_delay();
        lcd_write_strobe();
    }

    gpio_set_level(APP_LCD_CS_PIN, 1);
    lcd_delay();
    return ESP_OK;
}
