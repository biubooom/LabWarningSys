#include "IIC.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* 软件IIC引脚控制宏 */
#define IIC_SCL_HIGH() HAL_GPIO_WritePin(IIC_SCL_GPIO_Port, IIC_SCL_Pin, GPIO_PIN_SET)
#define IIC_SCL_LOW()  HAL_GPIO_WritePin(IIC_SCL_GPIO_Port, IIC_SCL_Pin, GPIO_PIN_RESET)
#define IIC_SDA_HIGH() HAL_GPIO_WritePin(IIC_SDA_GPIO_Port, IIC_SDA_Pin, GPIO_PIN_SET)
#define IIC_SDA_LOW()  HAL_GPIO_WritePin(IIC_SDA_GPIO_Port, IIC_SDA_Pin, GPIO_PIN_RESET)
#define IIC_SDA_READ() HAL_GPIO_ReadPin(IIC_SDA_GPIO_Port, IIC_SDA_Pin)

static SemaphoreHandle_t IIC_Mutex;
static uint8_t IIC_DelayReady;

/**
  * @brief  使能DWT计数器，用于微秒级延时
  * @param  无
  * @retval 无
  */
static void IIC_EnableDelayCounter(void)
{
    if (IIC_DelayReady == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        IIC_DelayReady = 1U;
    }
}

/**
  * @brief  软件IIC微秒延时
  * @param  DelayUs: 延时时间，单位微秒
  * @retval 无
  */
static void IIC_DelayUs(uint32_t DelayUs)
{
    uint32_t start_cycle;
    uint32_t delay_cycle;

    IIC_EnableDelayCounter();

    start_cycle = DWT->CYCCNT;
    delay_cycle = DelayUs * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start_cycle) < delay_cycle)
    {
    }
}

/**
  * @brief  进入IIC临界区，避免通信时序被任务切换打断
  * @param  无
  * @retval 无
  */
static void IIC_EnterCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskENTER_CRITICAL();
    }
}

/**
  * @brief  退出IIC临界区
  * @param  无
  * @retval 无
  */
static void IIC_ExitCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskEXIT_CRITICAL();
    }
}

/**
  * @brief  发送IIC起始信号
  * @param  无
  * @retval 无
  */
static void IIC_StartCondition(void)
{
    IIC_EnterCritical();
    IIC_SDA_HIGH();
    IIC_SCL_HIGH();
    IIC_DelayUs(4U);
    IIC_SDA_LOW();
    IIC_DelayUs(4U);
    IIC_SCL_LOW();
    IIC_ExitCritical();
}

/**
  * @brief  发送IIC停止信号
  * @param  无
  * @retval 无
  */
static void IIC_StopCondition(void)
{
    IIC_EnterCritical();
    IIC_SCL_LOW();
    IIC_SDA_LOW();
    IIC_DelayUs(4U);
    IIC_SCL_HIGH();
    IIC_DelayUs(4U);
    IIC_SDA_HIGH();
    IIC_DelayUs(4U);
    IIC_ExitCritical();
}

/**
  * @brief  等待从机应答
  * @param  无
  * @retval 1: 收到应答
  *         0: 未收到应答
  */
static uint8_t IIC_WaitAck(void)
{
    uint8_t ack;

    IIC_EnterCritical();
    IIC_SDA_HIGH();
    IIC_DelayUs(2U);
    IIC_SCL_HIGH();
    IIC_DelayUs(4U);
    ack = (uint8_t)(IIC_SDA_READ() == GPIO_PIN_RESET);
    IIC_SCL_LOW();
    IIC_ExitCritical();

    return ack;
}

/**
  * @brief  主机发送应答位
  * @param  AckValue: 0发送ACK，1发送NACK
  * @retval 无
  */
static void IIC_SendAck(uint8_t AckValue)
{
    IIC_EnterCritical();
    IIC_SCL_LOW();
    if (AckValue == 0U)
    {
        IIC_SDA_LOW();
    }
    else
    {
        IIC_SDA_HIGH();
    }

    IIC_DelayUs(2U);
    IIC_SCL_HIGH();
    IIC_DelayUs(4U);
    IIC_SCL_LOW();
    IIC_SDA_HIGH();
    IIC_ExitCritical();
}

/**
  * @brief  向总线发送1字节数据
  * @param  Data: 要发送的数据
  * @retval 1: 从机应答成功
  *         0: 从机未应答
  */
static uint8_t IIC_SendByte(uint8_t Data)
{
    uint8_t i;

    IIC_EnterCritical();
    IIC_SCL_LOW();
    for (i = 0U; i < 8U; i++)
    {
        if ((Data & 0x80U) != 0U)
        {
            IIC_SDA_HIGH();
        }
        else
        {
            IIC_SDA_LOW();
        }

        Data <<= 1U;
        IIC_DelayUs(2U);
        IIC_SCL_HIGH();
        IIC_DelayUs(4U);
        IIC_SCL_LOW();
        IIC_DelayUs(2U);
    }

    IIC_SDA_HIGH();
    IIC_ExitCritical();

    return IIC_WaitAck();
}

/**
  * @brief  从总线读取1字节数据
  * @param  AckValue: 0发送ACK，1发送NACK
  * @retval 读取到的数据
  */
static uint8_t IIC_ReadByteInternal(uint8_t AckValue)
{
    uint8_t i;
    uint8_t data = 0U;

    IIC_EnterCritical();
    IIC_SDA_HIGH();
    for (i = 0U; i < 8U; i++)
    {
        data <<= 1U;
        IIC_SCL_LOW();
        IIC_DelayUs(2U);
        IIC_SCL_HIGH();
        IIC_DelayUs(2U);

        if (IIC_SDA_READ() == GPIO_PIN_SET)
        {
            data |= 0x01U;
        }

        IIC_DelayUs(2U);
    }
    IIC_SCL_LOW();
    IIC_ExitCritical();

    IIC_SendAck(AckValue);

    return data;
}

/**
  * @brief  初始化软件IIC总线
  * @param  无
  * @retval IIC_OK: 初始化成功
  *         IIC_ERROR: 初始化失败
  */
IIC_Status_t IIC_Init(void)
{
    if (IIC_Mutex == NULL)
    {
        IIC_Mutex = xSemaphoreCreateMutex();
        if (IIC_Mutex == NULL)
        {
            return IIC_ERROR;
        }
    }

    IIC_SCL_HIGH();
    IIC_SDA_HIGH();
    IIC_EnableDelayCounter();

    return IIC_OK;
}

/**
  * @brief  获取IIC总线互斥锁，避免多任务同时访问总线
  * @param  无
  * @retval 无
  */
void IIC_Lock(void)
{
    if (IIC_Mutex != NULL)
    {
        (void)xSemaphoreTake(IIC_Mutex, portMAX_DELAY);
    }
}

/**
  * @brief  释放IIC总线互斥锁
  * @param  无
  * @retval 无
  */
void IIC_Unlock(void)
{
    if (IIC_Mutex != NULL)
    {
        (void)xSemaphoreGive(IIC_Mutex);
    }
}

/**
  * @brief  向指定设备连续写入数据
  * @param  DeviceAddress: 7位设备地址
  * @param  Data: 待写入数据缓冲区
  * @param  Size: 写入长度
  * @retval IIC_OK: 写入成功
  *         IIC_ERROR: 写入失败
  */
IIC_Status_t IIC_Write(uint8_t DeviceAddress, const uint8_t *Data, uint16_t Size)
{
    uint16_t i;
    IIC_Status_t status = IIC_OK;

    if ((Data == NULL) && (Size > 0U))
    {
        return IIC_ERROR;
    }

    IIC_Lock();
    IIC_StartCondition();

    if (IIC_SendByte((uint8_t)(DeviceAddress << 1U)) == 0U)
    {
        status = IIC_ERROR;
    }

    for (i = 0U; (i < Size) && (status == IIC_OK); i++)
    {
        if (IIC_SendByte(Data[i]) == 0U)
        {
            status = IIC_ERROR;
        }
    }

    IIC_StopCondition();
    IIC_Unlock();

    return status;
}

/**
  * @brief  从指定设备连续读取数据
  * @param  DeviceAddress: 7位设备地址
  * @param  Data: 读取数据缓冲区
  * @param  Size: 读取长度
  * @retval IIC_OK: 读取成功
  *         IIC_ERROR: 读取失败
  */
IIC_Status_t IIC_Read(uint8_t DeviceAddress, uint8_t *Data, uint16_t Size)
{
    uint16_t i;

    if ((Data == NULL) || (Size == 0U))
    {
        return IIC_ERROR;
    }

    IIC_Lock();
    IIC_StartCondition();

    if (IIC_SendByte((uint8_t)((DeviceAddress << 1U) | 0x01U)) == 0U)
    {
        IIC_StopCondition();
        IIC_Unlock();
        return IIC_ERROR;
    }

    for (i = 0U; i < Size; i++)
    {
        Data[i] = IIC_ReadByteInternal((uint8_t)(i == (Size - 1U)));
    }

    IIC_StopCondition();
    IIC_Unlock();

    return IIC_OK;
}

/**
  * @brief  向设备寄存器连续写入数据
  * @param  DeviceAddress: 7位设备地址
  * @param  RegisterAddress: 寄存器地址
  * @param  Data: 待写入数据缓冲区
  * @param  Size: 写入长度
  * @retval IIC_OK: 写入成功
  *         IIC_ERROR: 写入失败
  */
IIC_Status_t IIC_WriteReg(uint8_t DeviceAddress, uint8_t RegisterAddress, const uint8_t *Data, uint16_t Size)
{
    uint16_t i;
    IIC_Status_t status = IIC_OK;

    if ((Data == NULL) && (Size > 0U))
    {
        return IIC_ERROR;
    }

    IIC_Lock();
    IIC_StartCondition();

    if (IIC_SendByte((uint8_t)(DeviceAddress << 1U)) == 0U)
    {
        status = IIC_ERROR;
    }

    if ((status == IIC_OK) && (IIC_SendByte(RegisterAddress) == 0U))
    {
        status = IIC_ERROR;
    }

    for (i = 0U; (i < Size) && (status == IIC_OK); i++)
    {
        if (IIC_SendByte(Data[i]) == 0U)
        {
            status = IIC_ERROR;
        }
    }

    IIC_StopCondition();
    IIC_Unlock();

    return status;
}

/**
  * @brief  从设备寄存器连续读取数据
  * @param  DeviceAddress: 7位设备地址
  * @param  RegisterAddress: 寄存器地址
  * @param  Data: 读取数据缓冲区
  * @param  Size: 读取长度
  * @retval IIC_OK: 读取成功
  *         IIC_ERROR: 读取失败
  */
IIC_Status_t IIC_ReadReg(uint8_t DeviceAddress, uint8_t RegisterAddress, uint8_t *Data, uint16_t Size)
{
    uint16_t i;

    if ((Data == NULL) || (Size == 0U))
    {
        return IIC_ERROR;
    }

    IIC_Lock();
    IIC_StartCondition();

    if (IIC_SendByte((uint8_t)(DeviceAddress << 1U)) == 0U)
    {
        IIC_StopCondition();
        IIC_Unlock();
        return IIC_ERROR;
    }

    if (IIC_SendByte(RegisterAddress) == 0U)
    {
        IIC_StopCondition();
        IIC_Unlock();
        return IIC_ERROR;
    }

    IIC_StartCondition();
    if (IIC_SendByte((uint8_t)((DeviceAddress << 1U) | 0x01U)) == 0U)
    {
        IIC_StopCondition();
        IIC_Unlock();
        return IIC_ERROR;
    }

    for (i = 0U; i < Size; i++)
    {
        Data[i] = IIC_ReadByteInternal((uint8_t)(i == (Size - 1U)));
    }

    IIC_StopCondition();
    IIC_Unlock();

    return IIC_OK;
}
