#ifndef __IIC_H
#define __IIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    IIC_OK = 0,
    IIC_ERROR
} IIC_Status_t;

/**
  * @brief  初始化软件IIC总线
  * @param  无
  * @retval IIC_OK: 初始化成功
  *         IIC_ERROR: 初始化失败
  */
IIC_Status_t IIC_Init(void);

/**
  * @brief  获取IIC总线互斥锁
  * @param  无
  * @retval 无
  */
void IIC_Lock(void);

/**
  * @brief  释放IIC总线互斥锁
  * @param  无
  * @retval 无
  */
void IIC_Unlock(void);

/**
  * @brief  向7位地址设备写入连续数据
  * @param  DeviceAddress: 7位设备地址
  * @param  Data: 数据缓冲区
  * @param  Size: 数据长度
  * @retval IIC_OK: 写入成功
  *         IIC_ERROR: 写入失败
  */
IIC_Status_t IIC_Write(uint8_t DeviceAddress, const uint8_t *Data, uint16_t Size);

/**
  * @brief  从7位地址设备读取连续数据
  * @param  DeviceAddress: 7位设备地址
  * @param  Data: 数据缓冲区
  * @param  Size: 数据长度
  * @retval IIC_OK: 读取成功
  *         IIC_ERROR: 读取失败
  */
IIC_Status_t IIC_Read(uint8_t DeviceAddress, uint8_t *Data, uint16_t Size);

/**
  * @brief  向设备寄存器写入连续数据
  * @param  DeviceAddress: 7位设备地址
  * @param  RegisterAddress: 寄存器地址
  * @param  Data: 数据缓冲区
  * @param  Size: 数据长度
  * @retval IIC_OK: 写入成功
  *         IIC_ERROR: 写入失败
  */
IIC_Status_t IIC_WriteReg(uint8_t DeviceAddress, uint8_t RegisterAddress, const uint8_t *Data, uint16_t Size);

/**
  * @brief  从设备寄存器读取连续数据
  * @param  DeviceAddress: 7位设备地址
  * @param  RegisterAddress: 寄存器地址
  * @param  Data: 数据缓冲区
  * @param  Size: 数据长度
  * @retval IIC_OK: 读取成功
  *         IIC_ERROR: 读取失败
  */
IIC_Status_t IIC_ReadReg(uint8_t DeviceAddress, uint8_t RegisterAddress, uint8_t *Data, uint16_t Size);

#ifdef __cplusplus
}
#endif

#endif /* __IIC_H */
