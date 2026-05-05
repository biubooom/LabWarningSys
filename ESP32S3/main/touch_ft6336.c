#include "touch_ft6336.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ui_dashboard.h"
#include "lvgl_port.h"

#define FT6336_REG_NUM_FINGERS   0x02
#define FT6336_REG_TOUCH1        0x03
#define FT6336_I2C_TIMEOUT_MS    50
#define FT6336_POLL_PERIOD_MS    30
#define FT6336_TASK_STACK_SIZE   4096
#define FT6336_TASK_PRIORITY     2

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} touch_point_t;

static const char *TAG = "touch_ft6336";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_touch_dev;
static lv_indev_t *s_touch_indev;
static touch_point_t s_last_point;
static TickType_t s_last_log_tick;
static TaskHandle_t s_touch_task_handle;
static SemaphoreHandle_t s_touch_mutex;

static esp_err_t ft6336_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_touch_dev, &reg, 1, data, len, FT6336_I2C_TIMEOUT_MS);
}

static void ft6336_transform_xy(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    int32_t tx = raw_x;
    int32_t ty = raw_y;

    if (APP_TOUCH_SWAP_XY) {
        int32_t tmp = tx;
        tx = ty;
        ty = tmp;
    }

    if (APP_TOUCH_MIRROR_X) {
        tx = (APP_LCD_H_RES - 1) - tx;
    }
    if (APP_TOUCH_MIRROR_Y) {
        ty = (APP_LCD_V_RES - 1) - ty;
    }

    if (tx < 0) {
        tx = 0;
    } else if (tx >= APP_LCD_H_RES) {
        tx = APP_LCD_H_RES - 1;
    }

    if (ty < 0) {
        ty = 0;
    } else if (ty >= APP_LCD_V_RES) {
        ty = APP_LCD_V_RES - 1;
    }

    *x = (uint16_t)tx;
    *y = (uint16_t)ty;
}

static esp_err_t ft6336_read_point(bool *pressed, uint16_t *x, uint16_t *y, uint8_t *count_out)
{
    uint8_t touch_count = 0;
    uint8_t buf[4];
    uint16_t point_x = s_last_point.x;
    uint16_t point_y = s_last_point.y;
    bool point_pressed = false;

    ESP_RETURN_ON_FALSE(pressed != NULL && x != NULL && y != NULL, ESP_ERR_INVALID_ARG, TAG, "null output");
    ESP_RETURN_ON_FALSE(s_touch_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "touch mutex not ready");

    if (xSemaphoreTake(s_touch_mutex, pdMS_TO_TICKS(FT6336_I2C_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (ft6336_read_reg(FT6336_REG_NUM_FINGERS, &touch_count, 1) != ESP_OK) {
        (void)xSemaphoreGive(s_touch_mutex);
        return ESP_FAIL;
    }

    if ((touch_count & 0x0F) != 0U) {
        if (ft6336_read_reg(FT6336_REG_TOUCH1, buf, sizeof(buf)) != ESP_OK) {
            (void)xSemaphoreGive(s_touch_mutex);
            return ESP_FAIL;
        }

        {
            uint16_t raw_x = (uint16_t)(((buf[0] & 0x0F) << 8) | buf[1]);
            uint16_t raw_y = (uint16_t)(((buf[2] & 0x0F) << 8) | buf[3]);

            ft6336_transform_xy(raw_x, raw_y, &point_x, &point_y);
        }
        point_pressed = true;
    }

    s_last_point.pressed = point_pressed;
    s_last_point.x = point_x;
    s_last_point.y = point_y;

    *pressed = point_pressed;
    *x = point_x;
    *y = point_y;
    if (count_out != NULL) {
        *count_out = (uint8_t)(touch_count & 0x0F);
    }
    (void)xSemaphoreGive(s_touch_mutex);
    return ESP_OK;
}

static void ft6336_poll_task(void *arg)
{
    (void)arg;

    while (1) {
        bool pressed = false;
        uint16_t x = s_last_point.x;
        uint16_t y = s_last_point.y;
        uint8_t count = 0U;

        if (ft6336_read_point(&pressed, &x, &y, &count) == ESP_OK) {
            lvgl_port_lock();
            ui_dashboard_touch_debug_update(pressed, x, y);
            lvgl_port_unlock();

            if (pressed && ((xTaskGetTickCount() - s_last_log_tick) >= pdMS_TO_TICKS(200U))) {
                ESP_LOGI(TAG, "touch poll x=%u y=%u raw_count=%u", x, y, (unsigned int)count);
                s_last_log_tick = xTaskGetTickCount();
            }
        } else {
            lvgl_port_lock();
            ui_dashboard_touch_debug_update(false, s_last_point.x, s_last_point.y);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(FT6336_POLL_PERIOD_MS));
    }
}

static void ft6336_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    bool pressed = false;
    uint16_t x = s_last_point.x;
    uint16_t y = s_last_point.y;

    (void)indev;

    if (ft6336_read_point(&pressed, &x, &y, NULL) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = s_last_point.x;
        data->point.y = s_last_point.y;
        ui_dashboard_touch_debug_update(false, s_last_point.x, s_last_point.y);
        return;
    }

    if (!pressed) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = x;
        data->point.y = y;
        ui_dashboard_touch_debug_update(false, x, y);
        return;
    }

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
    ui_dashboard_touch_debug_update(true, x, y);
}

esp_err_t touch_ft6336_init(lv_display_t *display)
{
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = ((APP_TOUCH_RST_PIN >= 0) ? (1ULL << APP_TOUCH_RST_PIN) : 0ULL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = APP_TOUCH_I2C_PORT,
        .scl_io_num = APP_TOUCH_I2C_SCL_PIN,
        .sda_io_num = APP_TOUCH_I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = APP_TOUCH_I2C_ADDR,
        .scl_speed_hz = APP_TOUCH_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "display is null");
    if (s_touch_indev != NULL) {
        return ESP_OK;
    }

    if (s_touch_mutex == NULL) {
        s_touch_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_touch_mutex != NULL, ESP_ERR_NO_MEM, TAG, "touch mutex alloc failed");
    }

    if (APP_TOUCH_RST_PIN >= 0) {
        ESP_RETURN_ON_ERROR(gpio_config(&gpio_cfg), TAG, "touch rst gpio config failed");
        gpio_set_level(APP_TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(APP_TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (APP_TOUCH_INT_PIN >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = (1ULL << APP_TOUCH_INT_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "touch int gpio config failed");
    }

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "touch i2c bus create failed");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_touch_dev), TAG, "touch i2c add device failed");

    {
        uint8_t touch_count = 0;
        ESP_RETURN_ON_ERROR(ft6336_read_reg(FT6336_REG_NUM_FINGERS, &touch_count, 1), TAG, "ft6336 probe failed");
    }

    s_touch_indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(s_touch_indev != NULL, ESP_ERR_NO_MEM, TAG, "lv_indev_create failed");
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, ft6336_read_cb);
    lv_indev_set_display(s_touch_indev, display);

    if (s_touch_task_handle == NULL) {
        xTaskCreate(ft6336_poll_task,
                    "ft6336_poll",
                    FT6336_TASK_STACK_SIZE,
                    NULL,
                    FT6336_TASK_PRIORITY,
                    &s_touch_task_handle);
    }

    ESP_LOGI(TAG,
             "FT6336 touch initialized on I2C%d SDA=%d SCL=%d INT=%d RST=%d",
             APP_TOUCH_I2C_PORT,
             APP_TOUCH_I2C_SDA_PIN,
             APP_TOUCH_I2C_SCL_PIN,
             APP_TOUCH_INT_PIN,
             APP_TOUCH_RST_PIN);
    return ESP_OK;
}

bool touch_ft6336_get_last_point(bool *pressed, uint16_t *x, uint16_t *y)
{
    if (pressed != NULL) {
        *pressed = s_last_point.pressed;
    }
    if (x != NULL) {
        *x = s_last_point.x;
    }
    if (y != NULL) {
        *y = s_last_point.y;
    }
    return s_touch_dev != NULL;
}
