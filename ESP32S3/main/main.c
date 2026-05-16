#include "nvs_flash.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "cloud_report.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "sensor_data.h"
#include "ui_dashboard.h"
#include "wifi_sta.h"

static const char *TAG = "main";
#define UART_RX_TASK_STACK_SIZE       6144U
#define UART_RX_TASK_PRIORITY         5U
#define CLOUD_REPORT_TASK_STACK_SIZE  12288U
#define CLOUD_REPORT_TASK_PRIORITY    4U
#define UI_STATUS_TASK_STACK_SIZE     4096U
#define UI_STATUS_TASK_PRIORITY       2U
#define CLOUD_REPORT_QUEUE_LENGTH     1U
#define UART_RX_FRAME_MAX_LEN         2048U
#define UART_LINK_TIMEOUT_MS          15000U
#define CLOUD_REPORT_INTERVAL_MS      2000U
#define TEMP_ALARM_THRESHOLD_C        45.0f
#define LOW_TEMP_THRESHOLD_C          15.0f
#define HUMI_ALARM_THRESHOLD_RH       80.0f
#define SMOKE_ALARM_THRESHOLD_PERCENT 20.0f

static sensor_snapshot_t s_runtime_snapshot = {
    .groups = {
        {.online = false, .alarm = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
        {.online = false, .alarm = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
        {.online = false, .alarm = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
        {.online = false, .alarm = false, .temperature = 0.0f, .humidity = 0.0f, .smoke = 0.0f, .light = 0.0f},
    },
    .last_rx_tick = 0,
    .link_online = false,
    .system_alarm = false,
    .threshold_state_valid = false,
    .sequence_ready = false,
    .sequence_prediction_valid = false,
    .sequence_length = 0U,
    .sequence_confidence = 0.0f,
    .threshold_state_label = "",
    .threshold_display_name = "",
    .sequence_state_label = "",
    .sequence_display_name = "",
};
static TickType_t s_last_uart_activity_tick;
static TickType_t s_last_cloud_report_tick;
static TickType_t s_last_uart_frame_tick;
static uint32_t s_uart_frame_total;
static uint32_t s_uart_frame_ok_count;
static uint32_t s_uart_frame_invalid_count;
static uint32_t s_uart_frame_overflow_count;
static bool s_cloud_link_online;
static QueueHandle_t s_cloud_report_queue;

static void dashboard_push_snapshot(void);
static void dashboard_push_cloud_status(bool connected);

static bool group_alarm_active(const sensor_group_data_t *group)
{
    if ((group == NULL) || !group->online) {
        return false;
    }

    return (group->temperature >= TEMP_ALARM_THRESHOLD_C) ||
           (group->temperature <= LOW_TEMP_THRESHOLD_C) ||
           (group->humidity >= HUMI_ALARM_THRESHOLD_RH) ||
           (group->smoke >= SMOKE_ALARM_THRESHOLD_PERCENT);
}

static void build_cloud_snapshot(cloud_report_snapshot_t *cloud_snapshot)
{
    bool any_alarm = false;

    if (cloud_snapshot == NULL) {
        return;
    }

    memset(cloud_snapshot, 0, sizeof(*cloud_snapshot));
    cloud_snapshot->link_online = s_runtime_snapshot.link_online;

    for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
        const sensor_group_data_t *source = &s_runtime_snapshot.groups[i];
        sensor_group_data_t *target = &cloud_snapshot->groups[i];

        target->online = source->online;
        target->temperature = source->temperature;
        target->humidity = source->humidity;
        target->light = source->light;
        target->smoke = source->smoke;
        any_alarm = any_alarm || group_alarm_active(source);
    }

    cloud_snapshot->system_alarm = any_alarm;
}

static void publish_snapshot_to_cloud(bool force_publish)
{
    cloud_report_snapshot_t cloud_snapshot;
    TickType_t now = xTaskGetTickCount();

    if (!force_publish &&
        ((now - s_last_cloud_report_tick) < pdMS_TO_TICKS(CLOUD_REPORT_INTERVAL_MS))) {
        return;
    }

    build_cloud_snapshot(&cloud_snapshot);
    if ((s_cloud_report_queue != NULL) &&
        (xQueueOverwrite(s_cloud_report_queue, &cloud_snapshot) == pdPASS)) {
        s_last_cloud_report_tick = now;
    }
}

static void cloud_report_task(void *arg)
{
    cloud_alarm_state_t alarm_state;
    cloud_report_snapshot_t snapshot;

    (void)arg;

    while (1) {
        if ((s_cloud_report_queue != NULL) &&
            (xQueueReceive(s_cloud_report_queue, &snapshot, portMAX_DELAY) == pdPASS)) {
            if (!wifi_sta_is_connected()) {
                ESP_LOGW(TAG, "skip cloud report because wifi is disconnected");
                dashboard_push_cloud_status(false);
                continue;
            }
            if (cloud_report_publish_snapshot(&snapshot, &alarm_state) != ESP_OK) {
                ESP_LOGW(TAG, "cloud report publish failed");
                dashboard_push_cloud_status(false);
                continue;
            }
            dashboard_push_cloud_status(true);

            if (alarm_state.alarm_valid) {
                s_runtime_snapshot.link_online = alarm_state.link_online;
                s_runtime_snapshot.system_alarm = alarm_state.system_alarm;
                for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
                    s_runtime_snapshot.groups[i].alarm = alarm_state.group_alarm[i];
                }
            }

            if (alarm_state.threshold_valid) {
                s_runtime_snapshot.threshold_state_valid = (alarm_state.threshold_state_label[0] != '\0');
                snprintf(s_runtime_snapshot.threshold_state_label,
                         sizeof(s_runtime_snapshot.threshold_state_label),
                         "%s",
                         alarm_state.threshold_state_label);
                snprintf(s_runtime_snapshot.threshold_display_name,
                         sizeof(s_runtime_snapshot.threshold_display_name),
                         "%s",
                         alarm_state.threshold_display_name);
            } else {
                s_runtime_snapshot.threshold_state_valid = false;
                s_runtime_snapshot.threshold_state_label[0] = '\0';
                s_runtime_snapshot.threshold_display_name[0] = '\0';
            }

            if (alarm_state.sequence_valid) {
                s_runtime_snapshot.sequence_ready = alarm_state.sequence_ready;
                s_runtime_snapshot.sequence_prediction_valid = (alarm_state.sequence_state_label[0] != '\0');
                s_runtime_snapshot.sequence_length = alarm_state.sequence_length;
                s_runtime_snapshot.sequence_confidence = alarm_state.sequence_confidence;
                snprintf(s_runtime_snapshot.sequence_state_label,
                         sizeof(s_runtime_snapshot.sequence_state_label),
                         "%s",
                         alarm_state.sequence_state_label);
                snprintf(s_runtime_snapshot.sequence_display_name,
                         sizeof(s_runtime_snapshot.sequence_display_name),
                         "%s",
                         alarm_state.sequence_display_name);
            } else if (!alarm_state.sequence_ready) {
                s_runtime_snapshot.sequence_ready = false;
            }

            if (alarm_state.valid) {
                dashboard_push_snapshot();
            }
        } 
    }
}

static void ui_status_task(void *arg)
{
    char ssid[33];
    char ip[16];

    (void)arg;

    while (1) {
        bool wifi_connected = wifi_sta_is_connected();

        wifi_sta_get_ssid(ssid, sizeof(ssid));
        wifi_sta_get_ip(ip, sizeof(ip));
        lvgl_port_lock();
        ui_dashboard_update_wifi(wifi_connected, ssid, ip);
        if (!wifi_connected) {
            s_cloud_link_online = false;
            ui_dashboard_update_cloud(false);
        } else {
            ui_dashboard_update_cloud(s_cloud_link_online);
        }
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void dashboard_push_snapshot(void)
{
    lvgl_port_lock();
    ui_dashboard_update(&s_runtime_snapshot);
    lvgl_port_unlock();
}

static void dashboard_push_cloud_status(bool connected)
{
    s_cloud_link_online = connected;
    lvgl_port_lock();
    ui_dashboard_update_cloud(connected);
    lvgl_port_unlock();
}

static void show_default_dashboard(lv_display_t *display)
{
    lvgl_port_lock();
    ui_dashboard_create(display);
    ui_dashboard_update(&s_runtime_snapshot);
    ui_dashboard_update_cloud(false);
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
    group->alarm = false;
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
    sensor_snapshot_t parsed = *snapshot;

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
    char frame_preview[97];
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
                    TickType_t now = xTaskGetTickCount();
                    uint32_t frame_interval_ms = (s_last_uart_frame_tick == 0U)
                        ? 0U
                        : (uint32_t)pdTICKS_TO_MS(now - s_last_uart_frame_tick);

                    frame[frame_len] = '\0';
                    s_last_uart_frame_tick = now;
                    s_uart_frame_total++;

                    (void)snprintf(frame_preview,
                                   sizeof(frame_preview),
                                   "%.*s",
                                   (int)((frame_len < (sizeof(frame_preview) - 1U)) ? frame_len : (sizeof(frame_preview) - 1U)),
                                   frame);

                    if (parse_snapshot_frame(frame, &s_runtime_snapshot)) {
                        s_uart_frame_ok_count++;
                        ESP_LOGI(TAG,
                                 "uart frame ok total=%" PRIu32 " ok=%" PRIu32 " invalid=%" PRIu32
                                 " overflow=%" PRIu32 " len=%u interval=%" PRIu32 "ms",
                                 s_uart_frame_total,
                                 s_uart_frame_ok_count,
                                 s_uart_frame_invalid_count,
                                 s_uart_frame_overflow_count,
                                 (unsigned int)frame_len,
                                 frame_interval_ms);
                        dashboard_push_snapshot();
                        publish_snapshot_to_cloud(false);
                    } else {
                        s_uart_frame_invalid_count++;
                        ESP_LOGW(TAG,
                                 "uart frame invalid total=%" PRIu32 " ok=%" PRIu32 " invalid=%" PRIu32
                                 " overflow=%" PRIu32 " len=%u interval=%" PRIu32 "ms preview=%s",
                                 s_uart_frame_total,
                                 s_uart_frame_ok_count,
                                 s_uart_frame_invalid_count,
                                 s_uart_frame_overflow_count,
                                 (unsigned int)frame_len,
                                 frame_interval_ms,
                                 frame_preview);
                    }
                    frame_len = 0U;
                }
            } else if (frame_len < (sizeof(frame) - 1U)) {
                frame[frame_len++] = (char)rx_byte;
            } else {
                frame[sizeof(frame) - 1U] = '\0';
                frame_len = 0U;
                s_uart_frame_overflow_count++;
                ESP_LOGW(TAG,
                         "uart frame overflow total=%" PRIu32 " ok=%" PRIu32 " invalid=%" PRIu32
                         " overflow=%" PRIu32 " max=%u",
                         s_uart_frame_total,
                         s_uart_frame_ok_count,
                         s_uart_frame_invalid_count,
                         s_uart_frame_overflow_count,
                         (unsigned int)sizeof(frame));
            }
        }

        if (s_runtime_snapshot.link_online &&
            ((xTaskGetTickCount() - s_last_uart_activity_tick) > pdMS_TO_TICKS(UART_LINK_TIMEOUT_MS))) {
            s_runtime_snapshot.link_online = false;
            s_runtime_snapshot.system_alarm = false;
            s_runtime_snapshot.threshold_state_valid = false;
            s_runtime_snapshot.sequence_ready = false;
            s_runtime_snapshot.sequence_prediction_valid = false;
            s_runtime_snapshot.sequence_length = 0U;
            s_runtime_snapshot.sequence_confidence = 0.0f;
            s_runtime_snapshot.threshold_state_label[0] = '\0';
            s_runtime_snapshot.threshold_display_name[0] = '\0';
            s_runtime_snapshot.sequence_state_label[0] = '\0';
            s_runtime_snapshot.sequence_display_name[0] = '\0';
            for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
                s_runtime_snapshot.groups[i].online = false;
                s_runtime_snapshot.groups[i].alarm = false;
            }
            dashboard_push_snapshot();
            publish_snapshot_to_cloud(true);
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
    ESP_ERROR_CHECK(cloud_report_start());
    ESP_ERROR_CHECK(lvgl_port_init(&display));
    show_default_dashboard(display);
    ESP_ERROR_CHECK(uart_link_init());
    s_cloud_report_queue = xQueueCreate(CLOUD_REPORT_QUEUE_LENGTH, sizeof(cloud_report_snapshot_t));
    ESP_ERROR_CHECK(s_cloud_report_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    s_last_uart_activity_tick = xTaskGetTickCount();
    s_last_cloud_report_tick = 0;
    s_last_uart_frame_tick = 0;
    xTaskCreate(cloud_report_task,
                "cloud_report_task",
                CLOUD_REPORT_TASK_STACK_SIZE,
                NULL,
                CLOUD_REPORT_TASK_PRIORITY,
                NULL);
    xTaskCreate(ui_status_task,
                "ui_status_task",
                UI_STATUS_TASK_STACK_SIZE,
                NULL,
                UI_STATUS_TASK_PRIORITY,
                NULL);
    xTaskCreate(uart_rx_task, "uart_rx_task", UART_RX_TASK_STACK_SIZE, NULL, UART_RX_TASK_PRIORITY, NULL);
}
