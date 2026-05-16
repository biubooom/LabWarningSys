idf_component_register(SRCS "ui_dashboard.c" "touch_ft6336.c" "lcd_i80_test.c" "lcd_bitbang.c" "lvgl_port.c" "main.c" "wifi_sta.c" "cloud_report.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart esp_driver_gpio esp_driver_i2c esp_lcd esp_timer lvgl nvs_flash esp_event esp_netif esp_wifi esp_http_client mbedtls espressif__cjson)
