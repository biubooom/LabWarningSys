idf_component_register(SRCS "main.c" "wifi_sta.c" "onenet_mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart esp_event esp_netif esp_wifi mqtt nvs_flash mbedtls)
