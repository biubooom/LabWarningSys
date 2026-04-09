#ifndef __ONEWIRE_H
#define __ONEWIRE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    ONEWIRE_OK = 0,
    ONEWIRE_ERROR
} Onewire_Status_t;

/**
  * @brief  初始化单总线驱动
  * @param  无
  * @retval ONEWIRE_OK: 初始化成功
  *         ONEWIRE_ERROR: 初始化失败
  */
Onewire_Status_t Onewire_Init(void);

/**
  * @brief  获取单总线互斥锁
  * @param  无
  * @retval 无
  */
void Onewire_Lock(void);

/**
  * @brief  释放单总线互斥锁
  * @param  无
  * @retval 无
  */
void Onewire_Unlock(void);

/**
  * @brief  发送复位脉冲并检测存在脉冲
  * @param  无
  * @retval 1: 检测到设备
  *         0: 未检测到设备
  */
uint8_t Onewire_Reset(void);

/**
  * @brief  写入1位数据
  * @param  BitValue: 要写入的位
  * @retval 无
  */
void Onewire_WriteBit(uint8_t BitValue);

/**
  * @brief  读取1位数据
  * @param  无
  * @retval 读取到的位值
  */
uint8_t Onewire_ReadBit(void);

/**
  * @brief  写入1字节数据
  * @param  Data: 要写入的数据
  * @retval 无
  */
void Onewire_WriteByte(uint8_t Data);

/**
  * @brief  读取1字节数据
  * @param  无
  * @retval 读取到的数据
  */
uint8_t Onewire_ReadByte(void);

/**
  * @brief  发送Skip ROM命令
  * @param  无
  * @retval 无
  */
void Onewire_SkipRom(void);

/**
  * @brief  发送Match ROM命令
  * @param  RomCode: 8字节ROM码
  * @retval 无
  */
void Onewire_MatchRom(const uint8_t RomCode[8]);

/**
  * @brief  计算Dallas/Maxim CRC8
  * @param  Data: 数据缓冲区
  * @param  Length: 数据长度
  * @retval CRC8值
  */
uint8_t Onewire_Crc8(const uint8_t *Data, uint8_t Length);

#ifdef __cplusplus
}
#endif

#endif /* __ONEWIRE_H */
