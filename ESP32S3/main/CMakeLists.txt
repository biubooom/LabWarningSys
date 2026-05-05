idf_component_register(SRCS "touch_ft6336.c" "lcd_i80_test.c" "lcd_bitbang.c" "lvgl_port.c" "ui_dashboard.c" "main.c" "wifi_sta.c" "onenet_mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart esp_driver_gpio esp_driver_i2c esp_lcd esp_timer lvgl nvs_flash esp_event esp_netif esp_wifi mqtt mbedtls espressif__cjson)
