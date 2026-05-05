#include "lcd_i80_test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/param.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

#define LCD_I80_BUS_WIDTH        8U
#define LCD_DMA_BURST_SIZE       16U
#define LCD_I80_PCLK_HZ          8000000U
#define LCD_CHUNK_LINES          8U
#define LCD_TRANS_QUEUE_DEPTH    1U

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
} st7796_init_cmd_t;

static const char *TAG = "lcd_i80";

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

static esp_lcd_i80_bus_handle_t s_i80_bus;
static esp_lcd_panel_io_handle_t s_panel_io;
static SemaphoreHandle_t s_color_done_sem;
static uint16_t *s_chunk_buffer;
static size_t s_chunk_pixels;

static bool lcd_color_done_cb(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *edata,
                              void *user_ctx)
{
    BaseType_t task_woken = pdFALSE;

    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    if (s_color_done_sem != NULL) {
        xSemaphoreGiveFromISR(s_color_done_sem, &task_woken);
    }
    return task_woken == pdTRUE;
}

static esp_err_t lcd_send_cmd(uint8_t cmd, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_panel_io, cmd, data, len);
}

static esp_err_t lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t col_data[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
    };
    uint8_t row_data[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
    };

    ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_CASET, col_data, sizeof(col_data)), TAG, "set col failed");
    ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_RASET, row_data, sizeof(row_data)), TAG, "set row failed");
    return ESP_OK;
}

static esp_err_t lcd_hw_reset(void)
{
    if (APP_LCD_RST_PIN < 0) {
        ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_SWRESET, NULL, 0), TAG, "sw reset failed");
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

esp_err_t lcd_i80_test_init(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask = ((APP_LCD_RST_PIN >= 0) ? (1ULL << APP_LCD_RST_PIN) : 0ULL) |
                        ((APP_LCD_BL_PIN >= 0) ? (1ULL << APP_LCD_BL_PIN) : 0ULL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = APP_LCD_DC_PIN,
        .wr_gpio_num = APP_LCD_WR_PIN,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            APP_LCD_D0_PIN,
            APP_LCD_D1_PIN,
            APP_LCD_D2_PIN,
            APP_LCD_D3_PIN,
            APP_LCD_D4_PIN,
            APP_LCD_D5_PIN,
            APP_LCD_D6_PIN,
            APP_LCD_D7_PIN,
        },
        .bus_width = LCD_I80_BUS_WIDTH,
        .max_transfer_bytes = APP_LCD_H_RES * LCD_CHUNK_LINES * sizeof(uint16_t),
        .dma_burst_size = LCD_DMA_BURST_SIZE,
    };
    esp_lcd_panel_io_i80_config_t panel_io_config = {
        .cs_gpio_num = APP_LCD_CS_PIN,
        .pclk_hz = MIN(APP_LCD_PIXEL_CLOCK_HZ, LCD_I80_PCLK_HZ),
        .trans_queue_depth = LCD_TRANS_QUEUE_DEPTH,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
    };
    size_t i;

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "gpio config failed");
    if (APP_LCD_BL_PIN >= 0) {
        gpio_set_level(APP_LCD_BL_PIN, APP_LCD_BL_ON_LEVEL);
    }

    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &s_i80_bus), TAG, "new bus failed");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(s_i80_bus, &panel_io_config, &s_panel_io), TAG, "new panel io failed");
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_panel_io,
                                                              &(esp_lcd_panel_io_callbacks_t) {
                                                                  .on_color_trans_done = lcd_color_done_cb,
                                                              },
                                                              NULL));

    s_color_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_color_done_sem != NULL, ESP_ERR_NO_MEM, TAG, "sem alloc failed");
    s_chunk_pixels = APP_LCD_H_RES * LCD_CHUNK_LINES;
    s_chunk_buffer = heap_caps_malloc(s_chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_chunk_buffer != NULL, ESP_ERR_NO_MEM, TAG, "chunk alloc failed");

    ESP_RETURN_ON_ERROR(lcd_hw_reset(), TAG, "lcd reset failed");
    for (i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); ++i) {
        ESP_RETURN_ON_ERROR(lcd_send_cmd(s_init_cmds[i].cmd, s_init_cmds[i].data, s_init_cmds[i].len), TAG, "init cmd failed");
        if (s_init_cmds[i].delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(s_init_cmds[i].delay_ms));
        }
    }

    ESP_LOGI(TAG, "i80 lcd initialized at %u Hz", (unsigned)MIN(APP_LCD_PIXEL_CLOCK_HZ, LCD_I80_PCLK_HZ));
    return ESP_OK;
}

esp_err_t lcd_i80_test_fill_color(uint16_t color)
{
    uint16_t y = 0;
    size_t i;

    ESP_RETURN_ON_FALSE(s_panel_io != NULL && s_chunk_buffer != NULL, ESP_ERR_INVALID_STATE, TAG, "panel not ready");
    for (i = 0; i < s_chunk_pixels; ++i) {
        s_chunk_buffer[i] = color;
    }

    while (y < APP_LCD_V_RES) {
        uint16_t chunk_lines = (uint16_t)MIN((uint16_t)LCD_CHUNK_LINES, (uint16_t)(APP_LCD_V_RES - y));
        size_t chunk_pixels = (size_t)APP_LCD_H_RES * chunk_lines;

        ESP_RETURN_ON_ERROR(lcd_set_window(0, y, APP_LCD_H_RES - 1, y + chunk_lines - 1), TAG, "set window failed");
        xSemaphoreTake(s_color_done_sem, 0);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_panel_io,
                                                      LCD_CMD_RAMWR,
                                                      s_chunk_buffer,
                                                      chunk_pixels * sizeof(uint16_t)),
                            TAG,
                            "tx color failed");
        ESP_RETURN_ON_FALSE(xSemaphoreTake(s_color_done_sem, pdMS_TO_TICKS(1000)) == pdTRUE,
                            ESP_ERR_TIMEOUT,
                            TAG,
                            "wait color done timeout");
        y = (uint16_t)(y + chunk_lines);
    }

    return ESP_OK;
}
