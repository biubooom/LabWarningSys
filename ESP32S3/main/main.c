#include "nvs_flash.h"

#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "onenet_mqtt.h"
#include "sensor_data.h"
#include "ui_dashboard.h"
#include "wifi_sta.h"

static const char *TAG = "main";
#define UART_RX_TASK_STACK_SIZE       6144U
#define UART_RX_TASK_PRIORITY         5U
#define UART_RX_FRAME_MAX_LEN         2048U
#define UART_LINK_TIMEOUT_MS          5000U
#define CLOUD_REPORT_INTERVAL_MS      2000U
#define TEMP_ALARM_THRESHOLD_C        50.0f
#define HUMI_ALARM_THRESHOLD_RH       85.0f
#define SMOKE_ALARM_THRESHOLD_PERCENT 20.0f
#define LIGHT_ALARM_THRESHOLD_PERCENT 15.0f

static sensor_snapshot_t s_runtime_snapshot = {
    .groups = {
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
    },
    .last_rx_tick = 0,
    .link_online = false,
};
static TickType_t s_last_uart_activity_tick;
static TickType_t s_last_cloud_report_tick;

static bool group_alarm_active(const sensor_group_data_t *group)
{
    if ((group == NULL) || !group->online) {
        return false;
    }

    return (group->temperature >= TEMP_ALARM_THRESHOLD_C) ||
           (group->humidity >= HUMI_ALARM_THRESHOLD_RH) ||
           (group->smoke >= SMOKE_ALARM_THRESHOLD_PERCENT) ||
           (group->light <= LIGHT_ALARM_THRESHOLD_PERCENT);
}

static void build_onenet_snapshot(onenet_snapshot_t *cloud_snapshot)
{
    bool any_alarm = false;

    if (cloud_snapshot == NULL) {
        return;
    }

    memset(cloud_snapshot, 0, sizeof(*cloud_snapshot));
    cloud_snapshot->link_online = s_runtime_snapshot.link_online;

    for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
        const sensor_group_data_t *source = &s_runtime_snapshot.groups[i];
        onenet_group_telemetry_t *target = &cloud_snapshot->groups[i];

        target->online = source->online;
        target->temperature = source->temperature;
        target->humidity = source->humidity;
        target->light = source->light;
        target->smoke = source->smoke;
        target->alarm = group_alarm_active(source);
        any_alarm = any_alarm || target->alarm;
    }

    cloud_snapshot->system_alarm = any_alarm;
}

static void publish_snapshot_to_onenet(bool force_publish)
{
    onenet_snapshot_t cloud_snapshot;
    TickType_t now = xTaskGetTickCount();

    if (!force_publish &&
        ((now - s_last_cloud_report_tick) < pdMS_TO_TICKS(CLOUD_REPORT_INTERVAL_MS))) {
        return;
    }

    build_onenet_snapshot(&cloud_snapshot);
    if (onenet_mqtt_publish_snapshot(&cloud_snapshot) == ESP_OK) {
        s_last_cloud_report_tick = now;
    }
}

static void dashboard_push_snapshot(void)
{
    lvgl_port_lock();
    ui_dashboard_update(&s_runtime_snapshot);
    lvgl_port_unlock();
}

static void show_default_dashboard(lv_display_t *display)
{
    lvgl_port_lock();
    ui_dashboard_create(display);
    ui_dashboard_update(&s_runtime_snapshot);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "default dashboard loaded");
}

static bool parse_group_object(cJSON *group_json, sensor_group_data_t *group)
{
    cJSON *online;
    cJSON *temperature;
    cJSON *humidity;
    cJSON *smoke;
    cJSON *light;

    if (group_json == NULL || group == NULL) {
        return false;
    }

    online = cJSON_GetObjectItem(group_json, "online");
    temperature = cJSON_GetObjectItem(group_json, "temperature");
    humidity = cJSON_GetObjectItem(group_json, "humidity");
    smoke = cJSON_GetObjectItem(group_json, "smoke");
    light = cJSON_GetObjectItem(group_json, "light");

    if ((!cJSON_IsBool(online) && !cJSON_IsNumber(online)) ||
        !cJSON_IsNumber(temperature) ||
        !cJSON_IsNumber(humidity) ||
        !cJSON_IsNumber(smoke) ||
        !cJSON_IsNumber(light)) {
        return false;
    }

    group->online = cJSON_IsBool(online) ? cJSON_IsTrue(online) : (online->valuedouble != 0.0);
    group->temperature = (float)temperature->valuedouble;
    group->humidity = (float)humidity->valuedouble;
    group->smoke = (float)smoke->valuedouble;
    group->light = (float)light->valuedouble;
    return true;
}

static bool parse_snapshot_frame(const char *frame, sensor_snapshot_t *snapshot)
{
    cJSON *root;
    cJSON *groups;
    sensor_snapshot_t parsed = {0};

    if (frame == NULL || snapshot == NULL) {
        return false;
    }

    root = cJSON_Parse(frame);
    if (root == NULL) {
        return false;
    }

    groups = cJSON_GetObjectItem(root, "groups");
    if (!cJSON_IsArray(groups) || cJSON_GetArraySize(groups) < SENSOR_GROUP_COUNT) {
        cJSON_Delete(root);
        return false;
    }

    for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
        if (!parse_group_object(cJSON_GetArrayItem(groups, (int)i), &parsed.groups[i])) {
            cJSON_Delete(root);
            return false;
        }
    }

    parsed.last_rx_tick = (uint32_t)xTaskGetTickCount();
    parsed.link_online = true;
    *snapshot = parsed;
    cJSON_Delete(root);
    return true;
}

static esp_err_t uart_link_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = APP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(APP_UART_PORT_NUM,
                                            APP_UART_BUFFER_SIZE,
                                            0,
                                            0,
                                            NULL,
                                            0),
                        TAG,
                        "uart driver install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(APP_UART_PORT_NUM, &uart_config), TAG, "uart param config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(APP_UART_PORT_NUM,
                                     APP_UART_TX_PIN,
                                     APP_UART_RX_PIN,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart set pin failed");
    ESP_LOGI(TAG,
             "uart ready port=%d baud=%d tx=%d rx=%d",
             APP_UART_PORT_NUM,
             APP_UART_BAUD_RATE,
             APP_UART_TX_PIN,
             APP_UART_RX_PIN);
    return ESP_OK;
}

static void uart_rx_task(void *arg)
{
    char frame[UART_RX_FRAME_MAX_LEN];
    size_t frame_len = 0U;
    uint8_t rx_byte = 0U;

    (void)arg;

    while (1) {
        int read_len = uart_read_bytes(APP_UART_PORT_NUM, &rx_byte, 1, pdMS_TO_TICKS(100));

        if (read_len > 0) {
            s_last_uart_activity_tick = xTaskGetTickCount();

            if (rx_byte == '\r') {
                continue;
            }

            if (rx_byte == '\n') {
                if (frame_len > 0U) {
                    frame[frame_len] = '\0';
                    ESP_LOGI(TAG, "stm32 frame: %s", frame);
                    if (parse_snapshot_frame(frame, &s_runtime_snapshot)) {
                        dashboard_push_snapshot();
                        publish_snapshot_to_onenet(false);
                    } else {
                        ESP_LOGW(TAG, "invalid stm32 frame: %s", frame);
                    }
                    frame_len = 0U;
                }
            } else if (frame_len < (sizeof(frame) - 1U)) {
                frame[frame_len++] = (char)rx_byte;
            } else {
                frame[sizeof(frame) - 1U] = '\0';
                frame_len = 0U;
                ESP_LOGW(TAG, "uart frame overflow, frame dropped (max=%u)", (unsigned int)sizeof(frame));
            }
        }

        if (s_runtime_snapshot.link_online &&
            ((xTaskGetTickCount() - s_last_uart_activity_tick) > pdMS_TO_TICKS(UART_LINK_TIMEOUT_MS))) {
            s_runtime_snapshot.link_online = false;
            for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
                s_runtime_snapshot.groups[i].online = false;
            }
            dashboard_push_snapshot();
            publish_snapshot_to_onenet(true);
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    lv_display_t *display = NULL;

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(wifi_sta_init());
    ESP_ERROR_CHECK(onenet_mqtt_start());
    ESP_ERROR_CHECK(lvgl_port_init(&display));
    show_default_dashboard(display);
    ESP_ERROR_CHECK(uart_link_init());
    s_last_uart_activity_tick = xTaskGetTickCount();
    s_last_cloud_report_tick = 0;
    xTaskCreate(uart_rx_task, "uart_rx_task", UART_RX_TASK_STACK_SIZE, NULL, UART_RX_TASK_PRIORITY, NULL);
}
