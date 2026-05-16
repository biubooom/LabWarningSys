#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "app_config.h"
#include "cloud_report.h"

static const char *TAG = "cloud_report";
#define CLOUD_REPORT_RESPONSE_BUFFER_SIZE 4096U

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} http_response_buffer_t;

static cloud_report_snapshot_t s_last_snapshot = {
    .groups = {
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f},
    },
    .link_online = false,
    .system_alarm = false,
};

static void quantize_group(sensor_group_data_t *group)
{
    if (group == NULL) {
        return;
    }

    group->temperature = (float)((int)(group->temperature * 10.0f + (group->temperature >= 0.0f ? 0.5f : -0.5f))) / 10.0f;
    group->humidity = (float)((int)(group->humidity * 10.0f + (group->humidity >= 0.0f ? 0.5f : -0.5f))) / 10.0f;
    group->smoke = (float)((int)(group->smoke * 10.0f + (group->smoke >= 0.0f ? 0.5f : -0.5f))) / 10.0f;
    group->light = (float)((int)(group->light * 10.0f + (group->light >= 0.0f ? 0.5f : -0.5f))) / 10.0f;
}

static bool add_fixed_1_decimal_number(cJSON *object, const char *key, float value)
{
    char number_buffer[16];

    if ((object == NULL) || (key == NULL)) {
        return false;
    }

    (void)snprintf(number_buffer, sizeof(number_buffer), "%.1f", value);
    return (cJSON_AddRawToObject(object, key, number_buffer) != NULL);
}

static bool add_group_payload(cJSON *groups, const sensor_group_data_t *group)
{
    cJSON *group_json;

    if ((groups == NULL) || (group == NULL)) {
        return false;
    }

    group_json = cJSON_CreateObject();
    if (group_json == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(group_json, "online", group->online);
    if (!add_fixed_1_decimal_number(group_json, "temperature", group->temperature) ||
        !add_fixed_1_decimal_number(group_json, "humidity", group->humidity) ||
        !add_fixed_1_decimal_number(group_json, "smoke", group->smoke) ||
        !add_fixed_1_decimal_number(group_json, "light", group->light)) {
        cJSON_Delete(group_json);
        return false;
    }
    cJSON_AddItemToArray(groups, group_json);
    return true;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_buffer_t *response = (http_response_buffer_t *)event->user_data;

    if ((event == NULL) || (response == NULL) || (response->buffer == NULL) || (response->capacity == 0U)) {
        return ESP_OK;
    }

    if ((event->event_id == HTTP_EVENT_ON_DATA) &&
        (event->data != NULL) &&
        (event->data_len > 0)) {
        size_t writable = response->capacity - response->length - 1U;
        size_t copy_len = ((size_t)event->data_len < writable) ? (size_t)event->data_len : writable;

        if (copy_len > 0U) {
            memcpy(response->buffer + response->length, event->data, copy_len);
            response->length += copy_len;
            response->buffer[response->length] = '\0';
        }
    }

    return ESP_OK;
}

static bool parse_alarm_response(const char *body, cloud_alarm_state_t *alarm_state)
{
    cJSON *root = NULL;
    cJSON *alarm = NULL;
    cJSON *groups = NULL;
    cJSON *threshold_state = NULL;
    cJSON *sequence_prediction = NULL;

    if ((body == NULL) || (alarm_state == NULL) || (body[0] == '\0')) {
        return false;
    }

    root = cJSON_Parse(body);
    if (root == NULL) {
        return false;
    }

    memset(alarm_state, 0, sizeof(*alarm_state));
    alarm = cJSON_GetObjectItem(root, "alarm");
    groups = (alarm != NULL) ? cJSON_GetObjectItem(alarm, "groups") : NULL;
    if ((alarm != NULL) && cJSON_IsArray(groups) && (cJSON_GetArraySize(groups) >= SENSOR_GROUP_COUNT)) {
        cJSON *link_online = cJSON_GetObjectItem(alarm, "linkOnline");
        cJSON *system_alarm = cJSON_GetObjectItem(alarm, "systemAlarm");

        alarm_state->alarm_valid = true;
        alarm_state->link_online = cJSON_IsBool(link_online) ? cJSON_IsTrue(link_online) : false;
        alarm_state->system_alarm = cJSON_IsBool(system_alarm) ? cJSON_IsTrue(system_alarm) : false;

        for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
            cJSON *group = cJSON_GetArrayItem(groups, (int)i);
            cJSON *group_alarm = (group != NULL) ? cJSON_GetObjectItem(group, "alarm") : NULL;
            alarm_state->group_alarm[i] = cJSON_IsBool(group_alarm) ? cJSON_IsTrue(group_alarm) : false;
        }
    }

    {
        cJSON *sequence_ready = cJSON_GetObjectItem(root, "sequenceReady");
        cJSON *sequence_length = cJSON_GetObjectItem(root, "sequenceLength");

        alarm_state->sequence_ready = cJSON_IsBool(sequence_ready) ? cJSON_IsTrue(sequence_ready) : false;
        if (cJSON_IsNumber(sequence_length) && (sequence_length->valuedouble >= 0.0)) {
            double length_value = sequence_length->valuedouble;
            if (length_value > 255.0) {
                length_value = 255.0;
            }
            alarm_state->sequence_length = (uint8_t)length_value;
            alarm_state->sequence_valid = true;
        }
    }

    threshold_state = cJSON_GetObjectItem(root, "thresholdState");
    if (cJSON_IsObject(threshold_state)) {
        cJSON *state_label = cJSON_GetObjectItem(threshold_state, "stateLabel");
        cJSON *display_name = cJSON_GetObjectItem(threshold_state, "displayName");

        if (cJSON_IsString(state_label) && (state_label->valuestring != NULL)) {
            snprintf(alarm_state->threshold_state_label,
                     sizeof(alarm_state->threshold_state_label),
                     "%s",
                     state_label->valuestring);
            alarm_state->threshold_valid = true;
        }
        if (cJSON_IsString(display_name) && (display_name->valuestring != NULL)) {
            snprintf(alarm_state->threshold_display_name,
                     sizeof(alarm_state->threshold_display_name),
                     "%s",
                     display_name->valuestring);
            alarm_state->threshold_valid = true;
        }
    }

    sequence_prediction = cJSON_GetObjectItem(root, "sequencePrediction");
    if (cJSON_IsObject(sequence_prediction)) {
        cJSON *state_label = cJSON_GetObjectItem(sequence_prediction, "stateLabel");
        cJSON *display_name = cJSON_GetObjectItem(sequence_prediction, "displayName");
        cJSON *confidence = cJSON_GetObjectItem(sequence_prediction, "confidence");

        if (cJSON_IsString(state_label) && (state_label->valuestring != NULL)) {
            snprintf(alarm_state->sequence_state_label,
                     sizeof(alarm_state->sequence_state_label),
                     "%s",
                     state_label->valuestring);
            alarm_state->sequence_valid = true;
        }
        if (cJSON_IsString(display_name) && (display_name->valuestring != NULL)) {
            snprintf(alarm_state->sequence_display_name,
                     sizeof(alarm_state->sequence_display_name),
                     "%s",
                     display_name->valuestring);
            alarm_state->sequence_valid = true;
        }
        if (cJSON_IsNumber(confidence)) {
            alarm_state->sequence_confidence = (float)confidence->valuedouble;
            alarm_state->sequence_valid = true;
        }
    }

    alarm_state->valid = alarm_state->alarm_valid || alarm_state->threshold_valid || alarm_state->sequence_valid;
    cJSON_Delete(root);
    return alarm_state->valid;
}

static esp_err_t post_snapshot(const cloud_report_snapshot_t *snapshot, cloud_alarm_state_t *alarm_state)
{
    char *payload = NULL;
    char response_buffer[CLOUD_REPORT_RESPONSE_BUFFER_SIZE];
    http_response_buffer_t response = {
        .buffer = response_buffer,
        .capacity = sizeof(response_buffer),
        .length = 0U,
    };
    esp_http_client_handle_t client = NULL;
    esp_err_t ret = ESP_FAIL;
    esp_err_t http_ret = ESP_FAIL;
    int status_code = -1;
    cJSON *root = NULL;
    cJSON *groups = NULL;

    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        goto cleanup;
    }

    groups = cJSON_AddArrayToObject(root, "groups");
    if (groups == NULL) {
        goto cleanup;
    }

    cJSON_AddStringToObject(root, "deviceName", APP_DEVICE_NAME);
    cJSON_AddStringToObject(root, "productId", APP_PRODUCT_ID);
    cJSON_AddBoolToObject(root, "linkOnline", snapshot->link_online);

    for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
        sensor_group_data_t quantized = snapshot->groups[i];
        quantize_group(&quantized);
        if (!add_group_payload(groups, &quantized)) {
            goto cleanup;
        }
    }

    payload = cJSON_PrintUnformatted(root);
    if (payload == NULL) {
        goto cleanup;
    }

    esp_http_client_config_t config = {
        .url = APP_CLOUD_REPORT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = APP_CLOUD_REPORT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &response,
    };

    client = esp_http_client_init(&config);
    if (client == NULL) {
        goto cleanup;
    }

    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "set content-type failed");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Accept", "application/json"), cleanup, TAG, "set accept failed");
    if (strlen(APP_DEVICE_REPORT_TOKEN) > 0U) {
        ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "x-device-token", APP_DEVICE_REPORT_TOKEN), cleanup, TAG, "set token failed");
    }
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, payload, (int)strlen(payload)), cleanup, TAG, "set body failed");
    http_ret = esp_http_client_perform(client);
    if (http_ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "http report failed err=%s(%d) url=%s",
                 esp_err_to_name(http_ret),
                 (int)http_ret,
                 APP_CLOUD_REPORT_URL);
        goto cleanup;
    }

    status_code = esp_http_client_get_status_code(client);
    response_buffer[response.length] = '\0';

    if ((status_code < 200) || (status_code >= 300)) {
        ESP_LOGE(TAG, "report rejected status=%d payload=%s", status_code, payload);
        goto cleanup;
    }

    if ((alarm_state != NULL) && !parse_alarm_response(response_buffer, alarm_state)) {
        ESP_LOGW(TAG, "alarm response missing or invalid len=%u body=%s",
                 (unsigned int)response.length,
                 response_buffer);
    }

    ESP_LOGI(TAG, "device report ok status=%d payload=%s", status_code, payload);
    ret = ESP_OK;

cleanup:
    if ((ret != ESP_OK) && (payload != NULL)) {
        ESP_LOGW(TAG,
                 "device report failed status=%d http_err=%s(%d) payload=%s",
                 status_code,
                 esp_err_to_name(http_ret),
                 (int)http_ret,
                 payload);
    }
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    cJSON_free(payload);
    cJSON_Delete(root);
    return ret;
}

esp_err_t cloud_report_start(void)
{
    ESP_LOGI(TAG, "cloud reporter ready url=%s", APP_CLOUD_REPORT_URL);
    return ESP_OK;
}

esp_err_t cloud_report_publish_snapshot(const cloud_report_snapshot_t *snapshot, cloud_alarm_state_t *alarm_state)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (alarm_state != NULL) {
        memset(alarm_state, 0, sizeof(*alarm_state));
    }
    s_last_snapshot = *snapshot;
    return post_snapshot(&s_last_snapshot, alarm_state);
}
