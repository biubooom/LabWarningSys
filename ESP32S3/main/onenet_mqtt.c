#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mqtt_client.h"

#include "app_config.h"
#include "onenet_mqtt.h"

static const char *TAG = "onenet_mqtt";

static esp_mqtt_client_handle_t s_mqtt_client;
static char s_topic_property_post[128];
static char s_topic_property_post_reply[128];
static char s_onenet_token[256];
static onenet_snapshot_t s_last_snapshot = {
    .groups = {
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f, .alarm = false},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f, .alarm = false},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f, .alarm = false},
        {.online = false, .temperature = 0.0f, .humidity = 0.0f, .light = 0.0f, .smoke = 0.0f, .alarm = false},
    },
    .link_online = false,
    .system_alarm = false,
};

/**
  * @brief  按平台步长0.1对浮点数进行量化
  * @param  value: 原始浮点值
  * @retval 量化后的浮点值
  */
static float quantize_step_0_1(float value)
{
    return roundf(value * 10.0f) / 10.0f;
}

/**
  * @brief  向属性对象中添加固定1位小数的数值字段
  * @param  object: 目标JSON对象
  * @param  key: 字段名
  * @param  value: 原始浮点值
  * @retval true: 添加成功
  *         false: 添加失败
  */
static bool add_fixed_1_decimal_number(cJSON *object, const char *key, float value)
{
    char number_buffer[16];

    if ((object == NULL) || (key == NULL))
    {
        return false;
    }

    (void)snprintf(number_buffer, sizeof(number_buffer), "%.1f", quantize_step_0_1(value));
    return (cJSON_AddRawToObject(object, key, number_buffer) != NULL);
}

/**
  * @brief  判断字符是否为URL编码中的免编码字符
  * @param  c: 待判断字符
  * @retval true: 无需编码
  *         false: 需要编码
  */
static bool is_unreserved_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~';
}

/**
  * @brief  对字符串执行URL编码
  * @param  src: 原始字符串
  * @param  dst: 编码结果缓冲区
  * @param  dst_size: 结果缓冲区大小
  * @retval 无
  */
static void url_encode(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;

    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        if (is_unreserved_char(src[i])) {
            dst[out++] = src[i];
            continue;
        }

        if (out + 3 >= dst_size) {
            break;
        }

        (void)snprintf(&dst[out], dst_size - out, "%%%02X", (unsigned char)src[i]);
        out += 3;
    }

    dst[out] = '\0';
}

/**
  * @brief  按OneNET鉴权规则生成MQTT连接Token
  * @param  token: Token输出缓冲区
  * @param  token_size: Token缓冲区大小
  * @retval ESP_OK: 生成成功
  *         ESP_FAIL: 生成失败
  */
static esp_err_t build_onenet_token(char *token, size_t token_size)
{
    unsigned char key_bin[128] = {0};
    unsigned char hmac[32] = {0};
    unsigned char sign_b64[128] = {0};
    size_t key_bin_len = 0;
    size_t sign_b64_len = 0;
    char resource[96];
    char resource_encoded[160];
    char signature_src[256];
    char sign_encoded[192];
    int ret;

    (void)snprintf(resource, sizeof(resource), "products/%s/devices/%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    url_encode(resource, resource_encoded, sizeof(resource_encoded));

    ret = mbedtls_base64_decode(key_bin, sizeof(key_bin), &key_bin_len,
                                (const unsigned char *)ONENET_DEVICE_KEY, strlen(ONENET_DEVICE_KEY));
    if (ret != 0) {
        ESP_LOGE(TAG, "failed to decode device key, ret=%d", ret);
        return ESP_FAIL;
    }

    (void)snprintf(signature_src, sizeof(signature_src), "%s\n%s\n%s\n%s",
                   ONENET_TOKEN_EXPIRY, ONENET_METHOD, resource, ONENET_VERSION);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        ESP_LOGE(TAG, "failed to get sha256 md info");
        return ESP_FAIL;
    }

    ret = mbedtls_md_hmac(md_info,
                          key_bin, key_bin_len,
                          (const unsigned char *)signature_src, strlen(signature_src),
                          hmac);
    if (ret != 0) {
        ESP_LOGE(TAG, "failed to calculate hmac, ret=%d", ret);
        return ESP_FAIL;
    }

    ret = mbedtls_base64_encode(sign_b64, sizeof(sign_b64), &sign_b64_len, hmac, sizeof(hmac));
    if (ret != 0) {
        ESP_LOGE(TAG, "failed to encode sign, ret=%d", ret);
        return ESP_FAIL;
    }
    sign_b64[sign_b64_len] = '\0';

    url_encode((const char *)sign_b64, sign_encoded, sizeof(sign_encoded));

    (void)snprintf(token, token_size,
                   "version=%s&res=%s&et=%s&method=%s&sign=%s",
                   ONENET_VERSION, resource_encoded, ONENET_TOKEN_EXPIRY, ONENET_METHOD, sign_encoded);
    return ESP_OK;
}

/**
  * @brief  构造OneNET物模型通信所需的Topic
  * @param  无
  * @retval 无
  */
static void build_topics(void)
{
    (void)snprintf(s_topic_property_post, sizeof(s_topic_property_post),
                   "$sys/%s/%s/thing/property/post", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    (void)snprintf(s_topic_property_post_reply, sizeof(s_topic_property_post_reply),
                   "$sys/%s/%s/thing/property/post/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
}

static bool add_bool_property(cJSON *params, const char *key, bool value)
{
    cJSON *property;

    if ((params == NULL) || (key == NULL))
    {
        return false;
    }

    property = cJSON_AddObjectToObject(params, key);
    if (property == NULL)
    {
        return false;
    }

    cJSON_AddBoolToObject(property, "value", value);
    return true;
}

static bool add_number_property(cJSON *params, const char *key, float value)
{
    cJSON *property;

    if ((params == NULL) || (key == NULL))
    {
        return false;
    }

    property = cJSON_AddObjectToObject(params, key);
    if (property == NULL)
    {
        return false;
    }

    return add_fixed_1_decimal_number(property, "value", value);
}

/**
  * @brief  向params中添加单组传感器扁平属性
  * @param  params: OneNET params对象
  * @param  group_name: 当前组名前缀，例如g1
  * @param  group: 当前组快照
  * @retval true: 添加成功
  *         false: 添加失败
  */
static bool add_group_payload(cJSON *params, const char *group_name, const onenet_group_telemetry_t *group)
{
    char key[32];

    if ((params == NULL) || (group_name == NULL) || (group == NULL))
    {
        return false;
    }

    (void)snprintf(key, sizeof(key), "%s_online", group_name);
    if (!add_bool_property(params, key, group->online))
    {
        return false;
    }

    (void)snprintf(key, sizeof(key), "%s_temperature", group_name);
    if (!add_number_property(params, key, group->temperature))
    {
        return false;
    }

    (void)snprintf(key, sizeof(key), "%s_humidity", group_name);
    if (!add_number_property(params, key, group->humidity))
    {
        return false;
    }

    (void)snprintf(key, sizeof(key), "%s_light", group_name);
    if (!add_number_property(params, key, group->light))
    {
        return false;
    }

    (void)snprintf(key, sizeof(key), "%s_smoke", group_name);
    if (!add_number_property(params, key, group->smoke))
    {
        return false;
    }

    (void)snprintf(key, sizeof(key), "%s_alarm", group_name);
    if (!add_bool_property(params, key, group->alarm))
    {
        return false;
    }

    return true;
}

/**
  * @brief  按OneJSON格式发布四组属性上报消息
  * @param  snapshot: 待上报的四组快照
  * @retval ESP_OK: 发布成功
  *         ESP_FAIL: 发布失败
  */
static esp_err_t publish_property_report(const onenet_snapshot_t *snapshot)
{
    static const char *const group_names[SENSOR_GROUP_COUNT] = {"g1", "g2", "g3", "g4"};
    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_AddObjectToObject(root, "params");
    cJSON *link_online;
    cJSON *system_alarm;
    char *payload = NULL;
    esp_err_t ret = ESP_FAIL;

    if ((root == NULL) || (params == NULL) || (snapshot == NULL)) {
        goto cleanup;
    }

    cJSON_AddStringToObject(root, "id", "1");
    cJSON_AddStringToObject(root, "version", "1.0");

    link_online = cJSON_AddObjectToObject(params, "link_online");
    system_alarm = cJSON_AddObjectToObject(params, "system_alarm");
    if ((link_online == NULL) || (system_alarm == NULL)) {
        goto cleanup;
    }

    cJSON_AddBoolToObject(link_online, "value", snapshot->link_online);
    cJSON_AddBoolToObject(system_alarm, "value", snapshot->system_alarm);

    for (uint32_t i = 0; i < SENSOR_GROUP_COUNT; ++i) {
        if (!add_group_payload(params, group_names[i], &snapshot->groups[i])) {
            goto cleanup;
        }
    }

    payload = cJSON_PrintUnformatted(root);
    if (payload == NULL) {
        goto cleanup;
    }

    if (s_mqtt_client != NULL) {
        (void)esp_mqtt_client_publish(s_mqtt_client, s_topic_property_post, payload, 0, 0, 0);
        ESP_LOGI(TAG, "property report: %s", payload);
        ret = ESP_OK;
    }

cleanup:
    cJSON_free(payload);
    cJSON_Delete(root);
    return ret;
}

/**
  * @brief  MQTT事件回调函数
  * @param  handler_args: 用户参数，当前未使用
  * @param  base: 事件基类型
  * @param  event_id: 事件ID
  * @param  event_data: 事件数据
  * @retval 无
  */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "mqtt connected");
        (void)esp_mqtt_client_subscribe(event->client, s_topic_property_post_reply, 0);
        (void)publish_property_report(&s_last_snapshot);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "mqtt topic=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "mqtt data=%.*s", event->data_len, event->data);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "mqtt disconnected");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "mqtt error");
        break;
    default:
        break;
    }
}

/**
  * @brief  启动OneNET MQTT连接
  * @param  无
  * @retval ESP_OK: 启动成功
  *         其他: 启动失败
  */
esp_err_t onenet_mqtt_start(void)
{
    build_topics();
    ESP_ERROR_CHECK(build_onenet_token(s_onenet_token, sizeof(s_onenet_token)));

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = ONENET_HOST,
        .credentials.client_id = ONENET_DEVICE_NAME,
        .credentials.username = ONENET_PRODUCT_ID,
        .credentials.authentication.password = s_onenet_token,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        return ESP_FAIL;
    }

    (void)esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_mqtt_client);
}

/**
  * @brief  上报四组遥测数据到OneNET
  * @param  snapshot: 待上报的四组快照
  * @retval ESP_OK: 上报成功
  *         ESP_ERR_INVALID_ARG: 参数无效
  *         ESP_FAIL: 上报失败
  */
esp_err_t onenet_mqtt_publish_snapshot(const onenet_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_last_snapshot = *snapshot;
    return publish_property_report(&s_last_snapshot);
}
