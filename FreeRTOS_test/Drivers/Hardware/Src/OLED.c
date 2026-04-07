/**
 * ********************************************************************
 * @file	OLED.c
 * @brief	OLED驱动函数
 * @note	OLED地址 0x78
 * ********************************************************************
*/

#include "OLED.h"
#include "OLED_Font.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/*引脚配置*/
#define OLED_W_SCL(x) HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, (GPIO_PinState)(x))
#define OLED_W_SDA(x) HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, (GPIO_PinState)(x))

static SemaphoreHandle_t OLED_Mutex;

/**
  * @brief  获取OLED互斥锁，避免多任务同时访问OLED
  * @param  无
  * @retval 无
  */
static void OLED_Lock(void)
{
    if (OLED_Mutex != NULL)
    {
        (void)xSemaphoreTake(OLED_Mutex, portMAX_DELAY);
    }
}

/**
  * @brief  释放OLED互斥锁
  * @param  无
  * @retval 无
  */
static void OLED_Unlock(void)
{
    if (OLED_Mutex != NULL)
    {
        (void)xSemaphoreGive(OLED_Mutex);
    }
}

/**
  * @brief  OLED延时函数，自动适配调度器是否启动
  * @param  Delay: 延时毫秒数
  * @retval 无
  */
static void OLED_DelayMs(uint32_t Delay)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        HAL_Delay(Delay);
    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(Delay));
    }
}

/**
  * @brief  初始化OLED所需GPIO
  * @param  无
  * @retval 无
  */
static void OLED_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, OLED_SCL_Pin | OLED_SDA_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = OLED_SCL_Pin | OLED_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    OLED_W_SCL(1);
    OLED_W_SDA(1);
}

/**
  * @brief  软件I2C起始信号
  * @param  无
  * @retval 无
  */
static void OLED_I2C_Start(void)
{
    OLED_W_SDA(1);
    OLED_W_SCL(1);
    OLED_W_SDA(0);
    OLED_W_SCL(0);
}

/**
  * @brief  软件I2C停止信号
  * @param  无
  * @retval 无
  */
static void OLED_I2C_Stop(void)
{
    OLED_W_SDA(0);
    OLED_W_SCL(1);
    OLED_W_SDA(1);
}

/**
  * @brief  软件I2C发送一个字节
  * @param  Byte: 要发送的字节
  * @retval 无
  */
static void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        OLED_W_SDA(Byte & (0x80U >> i));
        OLED_W_SCL(1);
        OLED_W_SCL(0);
    }

    OLED_W_SCL(1);
    OLED_W_SCL(0);
}

/**
  * @brief  向OLED写入命令
  * @param  Command: 要写入的命令
  * @retval 无
  */
static void OLED_WriteCommand(uint8_t Command)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x00);
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
}

/**
  * @brief  向OLED写入数据
  * @param  Data: 要写入的数据
  * @retval 无
  */
static void OLED_WriteData(uint8_t Data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x40);
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
}

/**
  * @brief  设置OLED显存光标位置
  * @param  Y: 页地址，范围0~7
  * @param  X: 列地址，范围0~127
  * @retval 无
  */
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0U | Y);
    OLED_WriteCommand(0x10U | ((X & 0xF0U) >> 4));
    OLED_WriteCommand(0x00U | (X & 0x0FU));
}

/**
  * @brief  初始化OLED显示屏
  * @param  无
  * @retval OLED_OK: 初始化成功
  *         OLED_ERROR: 初始化失败
  */
OLED_Status_t OLED_Init(void)
{
    if (OLED_Mutex == NULL)
    {
        OLED_Mutex = xSemaphoreCreateMutex();
        if (OLED_Mutex == NULL)
        {
            return OLED_ERROR;
        }
    }

    OLED_DelayMs(100);
    OLED_I2C_Init();

    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);

    OLED_Clear();

    return OLED_OK;
}

/**
  * @brief  清空OLED显示内容
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{
    uint8_t i;
    uint8_t j;

    OLED_Lock();
    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for (i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
    OLED_Unlock();
}

/**
  * @brief  在指定位置显示单个字符
  * @param  Line: 行号，范围1~4
  * @param  Column: 列号，范围1~16
  * @param  Char: 要显示的字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;

    OLED_Lock();
    OLED_SetCursor((Line - 1U) * 2U, (Column - 1U) * 8U);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);
    }

    OLED_SetCursor((Line - 1U) * 2U + 1U, (Column - 1U) * 8U);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8U]);
    }
    OLED_Unlock();
}

/**
  * @brief  在指定位置显示字符串
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  String: 要显示的字符串
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String)
{
    uint8_t i;
    uint8_t j;

    OLED_Lock();
    OLED_SetCursor((Line - 1U) * 2U, (Column - 1U) * 8U);
    for (i = 0; String[i] != '\0'; i++)
    {
        for (j = 0; j < 8; j++)
        {
            OLED_WriteData(OLED_F8x16[String[i] - ' '][j]);
        }
    }

    OLED_SetCursor((Line - 1U) * 2U + 1U, (Column - 1U) * 8U);
    for (i = 0; String[i] != '\0'; i++)
    {
        for (j = 0; j < 8; j++)
        {
            OLED_WriteData(OLED_F8x16[String[i] - ' '][j + 8U]);
        }
    }
    OLED_Unlock();
}

/**
  * @brief  计算整数次幂
  * @param  X: 底数
  * @param  Y: 指数
  * @retval X的Y次幂
  */
static uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;

    while (Y--)
    {
        Result *= X;
    }

    return Result;
}

/**
  * @brief  在指定位置显示无符号十进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, (char)(Number / OLED_Pow(10U, Length - i - 1U) % 10U + '0'));
    }
}

/**
  * @brief  在指定位置显示有符号十进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint8_t i;
    uint32_t Number1;

    if (Number >= 0)
    {
        OLED_ShowChar(Line, Column, '+');
        Number1 = (uint32_t)Number;
    }
    else
    {
        OLED_ShowChar(Line, Column, '-');
        Number1 = (uint32_t)(-Number);
    }

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i + 1U, (char)(Number1 / OLED_Pow(10U, Length - i - 1U) % 10U + '0'));
    }
}

/**
  * @brief  在指定位置显示十六进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    uint8_t SingleNumber;

    for (i = 0; i < Length; i++)
    {
        SingleNumber = (uint8_t)(Number / OLED_Pow(16U, Length - i - 1U) % 16U);
        if (SingleNumber < 10U)
        {
            OLED_ShowChar(Line, Column + i, (char)(SingleNumber + '0'));
        }
        else
        {
            OLED_ShowChar(Line, Column + i, (char)(SingleNumber - 10U + 'A'));
        }
    }
}

/**
  * @brief  在指定位置显示二进制数
  * @param  Line: 起始行号，范围1~4
  * @param  Column: 起始列号，范围1~16
  * @param  Number: 要显示的数值
  * @param  Length: 显示长度
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, (char)(Number / OLED_Pow(2U, Length - i - 1U) % 2U + '0'));
    }
}
