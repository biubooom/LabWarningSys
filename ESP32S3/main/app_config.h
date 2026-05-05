#pragma once

/* Wi-Fi热点名称 */
#define WIFI_SSID               CONFIG_APP_WIFI_SSID
/* Wi-Fi热点密码 */
#define WIFI_PASSWORD           CONFIG_APP_WIFI_PASSWORD
/* Wi-Fi最大重连次数 */
#define WIFI_MAXIMUM_RETRY      CONFIG_APP_WIFI_MAXIMUM_RETRY

/* OneNET MQTT服务器地址 */
#define ONENET_HOST             CONFIG_APP_ONENET_HOST
/* OneNET Token版本号 */
#define ONENET_VERSION          CONFIG_APP_ONENET_VERSION
/* OneNET Token签名算法 */
#define ONENET_METHOD           CONFIG_APP_ONENET_METHOD
/* OneNET Token过期时间戳 */
#define ONENET_TOKEN_EXPIRY     CONFIG_APP_ONENET_TOKEN_EXPIRY

/* OneNET产品ID */
#define ONENET_PRODUCT_ID       CONFIG_APP_ONENET_PRODUCT_ID
/* OneNET设备名称 */
#define ONENET_DEVICE_NAME      CONFIG_APP_ONENET_DEVICE_NAME
/* OneNET设备密钥 */
#define ONENET_DEVICE_KEY       CONFIG_APP_ONENET_DEVICE_KEY

/* UART端口号 */
#define APP_UART_PORT_NUM       CONFIG_APP_UART_PORT
/* UART波特率 */
#define APP_UART_BAUD_RATE      CONFIG_APP_UART_BAUD_RATE
/* UART发送引脚 */
#define APP_UART_TX_PIN         CONFIG_APP_UART_TX_PIN
/* UART接收引脚 */
#define APP_UART_RX_PIN         CONFIG_APP_UART_RX_PIN
/* UART缓冲区大小 */
#define APP_UART_BUFFER_SIZE    CONFIG_APP_UART_BUFFER_SIZE

/* LCD水平分辨率 */
#define APP_LCD_H_RES           CONFIG_APP_LCD_H_RES
/* LCD垂直分辨率 */
#define APP_LCD_V_RES           CONFIG_APP_LCD_V_RES
/* LCD i80像素时钟 */
#define APP_LCD_PIXEL_CLOCK_HZ  CONFIG_APP_LCD_PIXEL_CLOCK_HZ
/* LVGL绘图缓冲行数 */
#define APP_LCD_DRAW_BUF_LINES  CONFIG_APP_LCD_DRAW_BUF_LINES
/* LCD 8bit数据线 D0 */
#define APP_LCD_D0_PIN          CONFIG_APP_LCD_D0_PIN
/* LCD 8bit数据线 D1 */
#define APP_LCD_D1_PIN          CONFIG_APP_LCD_D1_PIN
/* LCD 8bit数据线 D2 */
#define APP_LCD_D2_PIN          CONFIG_APP_LCD_D2_PIN
/* LCD 8bit数据线 D3 */
#define APP_LCD_D3_PIN          CONFIG_APP_LCD_D3_PIN
/* LCD 8bit数据线 D4 */
#define APP_LCD_D4_PIN          CONFIG_APP_LCD_D4_PIN
/* LCD 8bit数据线 D5 */
#define APP_LCD_D5_PIN          CONFIG_APP_LCD_D5_PIN
/* LCD 8bit数据线 D6 */
#define APP_LCD_D6_PIN          CONFIG_APP_LCD_D6_PIN
/* LCD 8bit数据线 D7 */
#define APP_LCD_D7_PIN          CONFIG_APP_LCD_D7_PIN
/* LCD WR引脚 */
#define APP_LCD_WR_PIN          CONFIG_APP_LCD_WR_PIN
/* LCD RD引脚 */
#define APP_LCD_RD_PIN          CONFIG_APP_LCD_RD_PIN
/* LCD DC引脚 */
#define APP_LCD_DC_PIN          CONFIG_APP_LCD_DC_PIN
/* LCD复位引脚 */
#define APP_LCD_RST_PIN         CONFIG_APP_LCD_RST_PIN
/* LCD片选引脚 */
#define APP_LCD_CS_PIN          CONFIG_APP_LCD_CS_PIN
/* LCD背光引脚 */
#define APP_LCD_BL_PIN          CONFIG_APP_LCD_BL_PIN
/* LCD背光有效电平 */
#define APP_LCD_BL_ON_LEVEL     CONFIG_APP_LCD_BL_ON_LEVEL
/* LCD是否使用BGR */
#define APP_LCD_RGB_ORDER_BGR   CONFIG_APP_LCD_RGB_ORDER_BGR
/* LCD X镜像 */
#ifdef CONFIG_APP_LCD_MIRROR_X
#define APP_LCD_MIRROR_X        CONFIG_APP_LCD_MIRROR_X
#else
#define APP_LCD_MIRROR_X        0
#endif
/* LCD Y镜像 */
#ifdef CONFIG_APP_LCD_MIRROR_Y
#define APP_LCD_MIRROR_Y        CONFIG_APP_LCD_MIRROR_Y
#else
#define APP_LCD_MIRROR_Y        0
#endif
/* LCD X/Y交换 */
#ifdef CONFIG_APP_LCD_SWAP_XY
#define APP_LCD_SWAP_XY         CONFIG_APP_LCD_SWAP_XY
#else
#define APP_LCD_SWAP_XY         0
#endif

/* Touch I2C控制器 */
#define APP_TOUCH_I2C_PORT      CONFIG_APP_TOUCH_I2C_PORT
/* Touch I2C频率 */
#define APP_TOUCH_I2C_FREQ_HZ   CONFIG_APP_TOUCH_I2C_FREQ_HZ
/* Touch I2C地址 */
#define APP_TOUCH_I2C_ADDR      CONFIG_APP_TOUCH_I2C_ADDR
/* Touch SDA引脚 */
#define APP_TOUCH_I2C_SDA_PIN   CONFIG_APP_TOUCH_I2C_SDA_PIN
/* Touch SCL引脚 */
#define APP_TOUCH_I2C_SCL_PIN   CONFIG_APP_TOUCH_I2C_SCL_PIN
/* Touch INT引脚 */
#define APP_TOUCH_INT_PIN       CONFIG_APP_TOUCH_INT_PIN
/* Touch RESET引脚 */
#define APP_TOUCH_RST_PIN       CONFIG_APP_TOUCH_RST_PIN
/* Touch X/Y交换 */
#ifdef CONFIG_APP_TOUCH_SWAP_XY
#define APP_TOUCH_SWAP_XY       CONFIG_APP_TOUCH_SWAP_XY
#else
#define APP_TOUCH_SWAP_XY       0
#endif
/* Touch X镜像 */
#ifdef CONFIG_APP_TOUCH_MIRROR_X
#define APP_TOUCH_MIRROR_X      CONFIG_APP_TOUCH_MIRROR_X
#else
#define APP_TOUCH_MIRROR_X      0
#endif
/* Touch Y镜像 */
#ifdef CONFIG_APP_TOUCH_MIRROR_Y
#define APP_TOUCH_MIRROR_Y      CONFIG_APP_TOUCH_MIRROR_Y
#else
#define APP_TOUCH_MIRROR_Y      0
#endif
