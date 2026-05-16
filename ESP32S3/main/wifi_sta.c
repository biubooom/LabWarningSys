#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "app_config.h"
#include "wifi_sta.h"

static const char *TAG = "wifi_sta";

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;
static bool s_wifi_connected;
static char s_wifi_ip[16] = "--";
static bool s_wifi_stack_ready;

/**
  * @brief  Wi-Fi事件回调函数
  * @param  arg: 用户参数，当前未使用
  * @param  event_base: 事件基类型
  * @param  event_id: 事件ID
  * @param  event_data: 事件附带数据
  * @retval 无
  */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;

        s_wifi_connected = false;
        strlcpy(s_wifi_ip, "--", sizeof(s_wifi_ip));
        if (s_wifi_event_group != NULL) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG,
                     "retry wifi connection: %d/%d reason=%d",
                     s_retry_num,
                     WIFI_MAXIMUM_RETRY,
                     event ? (int)event->reason : -1);
        } else {
            s_retry_num = 0;
            if (s_wifi_event_group != NULL) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            ESP_LOGE(TAG,
                     "wifi disconnected after max retries, waiting manual or next auto reconnect reason=%d",
                     event ? (int)event->reason : -1);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_wifi_connected = true;
        (void)snprintf(s_wifi_ip, sizeof(s_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
}

/**
  * @brief  初始化Wi-Fi STA模式并连接到指定热点
  * @param  无
  * @retval ESP_OK: 连接成功
  *         ESP_FAIL: 连接失败
  *         ESP_ERR_NO_MEM: 事件组创建失败
  */
esp_err_t wifi_sta_init(void)
{
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* 初始化TCP/IP栈和默认事件循环，并创建STA网卡。 */
    if (!s_wifi_stack_ready) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        s_wifi_stack_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id_handler;
    esp_event_handler_instance_t got_ip_handler;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, &any_id_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, &got_ip_handler));

    wifi_config_t wifi_config = {
        .sta = {
            /* 放宽认证限制，兼容常见热点与混合加密模式。 */
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi start requested ssid=%s", WIFI_SSID);
    return ESP_OK;
}

void wifi_sta_request_reconnect(void)
{
    s_retry_num = 0;
    s_wifi_connected = false;
    strlcpy(s_wifi_ip, "--", sizeof(s_wifi_ip));
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "manual wifi reconnect requested");
    (void)esp_wifi_disconnect();
    (void)esp_wifi_connect();
}

bool wifi_sta_is_connected(void)
{
    return s_wifi_connected;
}

void wifi_sta_get_ssid(char *buffer, size_t buffer_size)
{
    if ((buffer == NULL) || (buffer_size == 0U)) {
        return;
    }

    strlcpy(buffer, WIFI_SSID, buffer_size);
}

void wifi_sta_get_ip(char *buffer, size_t buffer_size)
{
    if ((buffer == NULL) || (buffer_size == 0U)) {
        return;
    }

    strlcpy(buffer, s_wifi_ip, buffer_size);
}
