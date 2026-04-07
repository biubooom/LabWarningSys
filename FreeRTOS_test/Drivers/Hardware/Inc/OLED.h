#ifndef __OLED_H
#define __OLED_H

#include "stm32f1xx_hal.h"
#include "main.h"

typedef enum
{
    OLED_OK = 0,
    OLED_ERROR
} OLED_Status_t;

/**
  * @brief  初始化OLED显示屏
  * @param  无
  * @retval OLED_OK: 初始化成功
  *         OLED_ERROR: 初始化失败
  */
OLED_Status_t OLED_Init(void);
/**
  * @brief  清空OLED显示内容
  * @param  无
  * @retval 无
  */
void OLED_Clear(void);
/**
  * @brief  在指定位置显示单个字符
  * @param  Line: 行号，范围1~4
  * @param  Column: 列号，范围1~16
  * @param  Char: 要显示的字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
/**
  * @brief  在指定位置显示字符串
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  String: 要显示的字符串
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String);
/**
  * @brief  在指定位置显示无符号十进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
/**
  * @brief  在指定位置显示有符号十进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
/**
  * @brief  在指定位置显示十六进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
/**
  * @brief  在指定位置显示二进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);



#endif
