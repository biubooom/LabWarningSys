#include "lvgl_port.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/lock.h>
#include <sys/param.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "draw/sw/lv_draw_sw_utils.h"
#include "touch_ft6336.h"

#define APP_LCD_I80_BUS_WIDTH         8U
#define LCD_CMD_SWRESET               0x01
#define LCD_CMD_SLPOUT                0x11
#define LCD_CMD_DISPON                0x29
#define LCD_CMD_CASET                 0x2A
#define LCD_CMD_RASET                 0x2B
#define LCD_CMD_RAMWR                 0x2C
#define LCD_CMD_MADCTL                0x36
#define LCD_CMD_COLMOD                0x3A
#define LCD_CMD_INVON                 0x21
#define LCD_CMD_INVOFF                0x20
#define LCD_CMD_CSCON                 0xF0
#define LCD_CMD_INVCTR                0xB4
#define LCD_CMD_ENTRY_MODE            0xB7
#define LCD_CMD_DFUNCTR               0xB6
#define LCD_CMD_DOCA                  0xE8
#define LCD_CMD_PWCTR2                0xC1
#define LCD_CMD_PWCTR3                0xC2
#define LCD_CMD_VMCTR                 0xC5
#define LCD_CMD_GMCTRP1               0xE0
#define LCD_CMD_GMCTRN1               0xE1

#define LCD_MADCTL_MY                 0x80
#define LCD_MADCTL_MX                 0x40
#define LCD_MADCTL_MV                 0x20
#define LCD_MADCTL_BGR                0x08

#define LVGL_TICK_PERIOD_MS           2U
#define LVGL_TASK_MAX_DELAY_MS        500U
#define LVGL_TASK_MIN_DELAY_MS        5U
#define LVGL_TASK_STACK_SIZE          8192U
#define LVGL_TASK_PRIORITY            4U
#define LVGL_TASK_CORE_ID             1
#define APP_LCD_DMA_BURST_SIZE        16U
#define APP_LCD_SAFE_PCLK_HZ          8000000U
#define APP_LCD_TEST_CHUNK_LINES      1U

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
} st7796_init_cmd_t;

static const st7796_init_cmd_t s_st7796_init_cmds[] = {
    {LCD_CMD_SLPOUT, {0x00}, 0, 120},
    {LCD_CMD_CSCON, {0xC3}, 1, 0},
    {LCD_CMD_CSCON, {0x96}, 1, 0},
    {LCD_CMD_MADCTL, {0x00}, 1, 0},
    {LCD_CMD_COLMOD, {0x55}, 1, 0},
    {LCD_CMD_INVCTR, {0x01}, 1, 0},
    {LCD_CMD_ENTRY_MODE, {0xC6}, 1, 0},
    {LCD_CMD_DFUNCTR, {0x80, 0x22, 0x3B}, 3, 0},
    {LCD_CMD_DOCA, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {LCD_CMD_PWCTR2, {0x06}, 1, 0},
    {LCD_CMD_PWCTR3, {0xA7}, 1, 0},
    {LCD_CMD_VMCTR, {0x18}, 1, 0},
    {LCD_CMD_GMCTRP1, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 14, 0},
    {LCD_CMD_GMCTRN1, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2D, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 14, 0},
    {LCD_CMD_CSCON, {0x3C}, 1, 0},
    {LCD_CMD_CSCON, {0x69}, 1, 0},
    {LCD_CMD_INVON, {0x00}, 0, 120},
    {LCD_CMD_DISPON, {0x00}, 0, 0},
};

static const char *TAG = "lvgl_port";
static _lock_t s_lvgl_lock;
static lv_display_t *s_lv_display;
static esp_lcd_i80_bus_handle_t s_i80_bus;
static esp_lcd_panel_io_handle_t s_panel_io;
static size_t s_draw_buffer_size;
static size_t s_test_chunk_pixels;
static void *s_test_color_buffer;
static SemaphoreHandle_t s_color_done_sem;

static uint8_t lcd_madctl_value(void)
{
    /* Reverse the current landscape orientation so the whole UI appears upside down.
     * 0xA8 = MY | MV | BGR.
     */
    return (uint8_t)(LCD_MADCTL_MY | LCD_MADCTL_MV | LCD_MADCTL_BGR);
}

static esp_err_t lcd_send_cmd(uint8_t cmd, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_panel_io, cmd, data, len);
}

static esp_err_t lcd_set_address_window(int x1, int y1, int x2, int y2)
{
    uint8_t col_data[4] = {
        (uint8_t)((x1 >> 8) & 0xFF),
        (uint8_t)(x1 & 0xFF),
        (uint8_t)((x2 >> 8) & 0xFF),
        (uint8_t)(x2 & 0xFF),
    };
    uint8_t row_data[4] = {
        (uint8_t)((y1 >> 8) & 0xFF),
        (uint8_t)(y1 & 0xFF),
        (uint8_t)((y2 >> 8) & 0xFF),
        (uint8_t)(y2 & 0xFF),
    };

    ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_CASET, col_data, sizeof(col_data)), TAG, "set column failed");
    ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_RASET, row_data, sizeof(row_data)), TAG, "set row failed");
    return ESP_OK;
}

static esp_err_t lcd_reset(void)
{
    if (APP_LCD_RST_PIN < 0) {
        ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_SWRESET, NULL, 0), TAG, "software reset failed");
        vTaskDelay(pdMS_TO_TICKS(120));
        return ESP_OK;
    }

    gpio_set_level(APP_LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(APP_LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t lcd_init_panel(void)
{
    uint8_t madctl = lcd_madctl_value();
    size_t i;

    ESP_RETURN_ON_ERROR(lcd_reset(), TAG, "lcd reset failed");

    for (i = 0; i < sizeof(s_st7796_init_cmds) / sizeof(s_st7796_init_cmds[0]); ++i) {
        ESP_RETURN_ON_ERROR(lcd_send_cmd(s_st7796_init_cmds[i].cmd,
                                         s_st7796_init_cmds[i].data,
                                         s_st7796_init_cmds[i].len),
                            TAG,
                            "panel init command failed");
        if (s_st7796_init_cmds[i].delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(s_st7796_init_cmds[i].delay_ms));
        }
    }

    ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_MADCTL, &madctl, 1), TAG, "set rotation failed");
    if (!APP_LCD_RGB_ORDER_BGR) {
        ESP_RETURN_ON_ERROR(lcd_send_cmd(LCD_CMD_INVOFF, NULL, 0), TAG, "disable inversion failed");
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    lv_display_t *display = (lv_display_t *)user_ctx;
    BaseType_t high_task_wakeup = pdFALSE;

    (void)panel_io;
    (void)edata;
    if (display != NULL) {
        lv_display_flush_ready(display);
    }
    if (s_color_done_sem != NULL) {
        xSemaphoreGiveFromISR(s_color_done_sem, &high_task_wakeup);
    }
    return high_task_wakeup == pdTRUE;
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    size_t pixel_count;

    if (s_panel_io == NULL) {
        lv_display_flush_ready(display);
        return;
    }

    pixel_count = (size_t)(area->x2 - area->x1 + 1) * (size_t)(area->y2 - area->y1 + 1);
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)pixel_count);
    if (lcd_set_address_window(area->x1, area->y1, area->x2, area->y2) != ESP_OK ||
        esp_lcd_panel_io_tx_color(s_panel_io, LCD_CMD_RAMWR, px_map, pixel_count * sizeof(lv_color16_t)) != ESP_OK) {
        lv_display_flush_ready(display);
    }
}

esp_err_t lvgl_port_fill_color(uint16_t color)
{
    uint16_t *buffer;
    size_t pixel_count;
    size_t i;

    if (s_panel_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    buffer = (uint16_t *)s_test_color_buffer;
    pixel_count = s_test_chunk_pixels;
    if (buffer == NULL || pixel_count == 0U) {
        return ESP_ERR_NO_MEM;
    }

    for (i = 0; i < pixel_count; ++i) {
        buffer[i] = color;
    }

    ESP_RETURN_ON_ERROR(lcd_set_address_window(0, 0, APP_LCD_H_RES - 1, APP_LCD_V_RES - 1), TAG, "set full window failed");

    {
        size_t remaining_pixels = (size_t)APP_LCD_H_RES * (size_t)APP_LCD_V_RES;
        while (remaining_pixels > 0U) {
            size_t chunk_pixels = MIN(remaining_pixels, pixel_count);
            xSemaphoreTake(s_color_done_sem, 0);
            ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_panel_io,
                                                          LCD_CMD_RAMWR,
                                                          buffer,
                                                          chunk_pixels * sizeof(uint16_t)),
                                TAG,
                                "tx color failed");
            ESP_RETURN_ON_FALSE(xSemaphoreTake(s_color_done_sem, pdMS_TO_TICKS(1000)) == pdTRUE,
                                ESP_ERR_TIMEOUT,
                                TAG,
                                "wait color done timeout");
            remaining_pixels -= chunk_pixels;
        }
    }

    return ESP_OK;
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    uint32_t delay_ms;
    TickType_t delay_ticks;

    (void)arg;

    while (1) {
        _lock_acquire(&s_lvgl_lock);
        delay_ms = lv_timer_handler();
        _lock_release(&s_lvgl_lock);

        delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY_MS);
        delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY_MS);
        delay_ticks = pdMS_TO_TICKS(delay_ms);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
        vTaskDelay(delay_ticks);
    }
}

esp_err_t lvgl_port_init(lv_display_t **display)
{
    static esp_timer_handle_t lvgl_tick_timer;
    static bool initialized;
    static lv_display_t *lv_display;
    static void *buf1;
    static void *buf2;

    gpio_config_t io_config;
    esp_lcd_i80_bus_config_t bus_config;
    esp_lcd_panel_io_i80_config_t panel_io_config;
    esp_timer_create_args_t timer_args;
    size_t draw_buffer_pixels;
    size_t draw_buffer_size;

    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "display output is null");

    if (initialized) {
        *display = s_lv_display;
        return ESP_OK;
    }

    io_config = (gpio_config_t) {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((APP_LCD_RST_PIN >= 0) ? (1ULL << APP_LCD_RST_PIN) : 0ULL) |
                        ((APP_LCD_BL_PIN >= 0) ? (1ULL << APP_LCD_BL_PIN) : 0ULL),
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "configure lcd gpio failed");

    if (APP_LCD_BL_PIN >= 0) {
        gpio_set_level(APP_LCD_BL_PIN, !APP_LCD_BL_ON_LEVEL);
    }

    bus_config = (esp_lcd_i80_bus_config_t) {
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
        .bus_width = APP_LCD_I80_BUS_WIDTH,
        .max_transfer_bytes = APP_LCD_H_RES * APP_LCD_DRAW_BUF_LINES * sizeof(lv_color16_t),
        .dma_burst_size = APP_LCD_DMA_BURST_SIZE,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &s_i80_bus), TAG, "create i80 bus failed");

    panel_io_config = (esp_lcd_panel_io_i80_config_t) {
        .cs_gpio_num = APP_LCD_CS_PIN,
        .pclk_hz = MIN(APP_LCD_PIXEL_CLOCK_HZ, APP_LCD_SAFE_PCLK_HZ),
        .trans_queue_depth = 1,
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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(s_i80_bus, &panel_io_config, &s_panel_io), TAG, "create panel io failed");
    ESP_RETURN_ON_ERROR(lcd_init_panel(), TAG, "lcd init failed");

    lv_init();

    draw_buffer_pixels = APP_LCD_H_RES * APP_LCD_DRAW_BUF_LINES;
    draw_buffer_size = draw_buffer_pixels * sizeof(lv_color16_t);
    buf1 = heap_caps_malloc(draw_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = heap_caps_malloc(draw_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(buf1 != NULL && buf2 != NULL, ESP_ERR_NO_MEM, TAG, "lvgl draw buffer alloc failed");
    s_test_chunk_pixels = APP_LCD_H_RES * APP_LCD_TEST_CHUNK_LINES;
    s_test_color_buffer = heap_caps_malloc(s_test_chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_test_color_buffer != NULL, ESP_ERR_NO_MEM, TAG, "test color buffer alloc failed");
    s_draw_buffer_size = draw_buffer_size;
    s_color_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_color_done_sem != NULL, ESP_ERR_NO_MEM, TAG, "color done sem alloc failed");

    lv_display = lv_display_create(APP_LCD_H_RES, APP_LCD_V_RES);
    ESP_RETURN_ON_FALSE(lv_display != NULL, ESP_ERR_NO_MEM, TAG, "lv_display_create failed");
    lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(lv_display, buf1, buf2, draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lv_display, lvgl_flush_cb);

    /* Register the display pointer after creation so async flush completion can signal LVGL. */
    panel_io_config.user_ctx = lv_display;
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_panel_io,
                                                              &(esp_lcd_panel_io_callbacks_t) {
                                                                  .on_color_trans_done = notify_lvgl_flush_ready,
                                                              },
                                                              lv_display));

    s_lv_display = lv_display;

    if (APP_LCD_BL_PIN >= 0) {
        gpio_set_level(APP_LCD_BL_PIN, APP_LCD_BL_ON_LEVEL);
    }

    timer_args = (esp_timer_create_args_t) {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &lvgl_tick_timer), TAG, "tick timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000U), TAG, "tick timer start failed");

    *display = lv_display;
    xTaskCreatePinnedToCore(lvgl_task,
                            "lvgl_task",
                            LVGL_TASK_STACK_SIZE,
                            NULL,
                            LVGL_TASK_PRIORITY,
                            NULL,
                            LVGL_TASK_CORE_ID);

    ESP_RETURN_ON_ERROR(touch_ft6336_init(lv_display), TAG, "touch init failed");

    initialized = true;
    ESP_LOGI(TAG, "LVGL display initialized: %dx%d", APP_LCD_H_RES, APP_LCD_V_RES);
    return ESP_OK;
}

void lvgl_port_lock(void)
{
    _lock_acquire(&s_lvgl_lock);
}

void lvgl_port_unlock(void)
{
    _lock_release(&s_lvgl_lock);
}
