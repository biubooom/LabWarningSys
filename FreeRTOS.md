# FreeRTOS

## 一、移植

> 20240603LTS版本、F103RCT6为例

### 1.1 准备文件

需要接入的 FreeRTOS 文件分为四类：

- 内核源码：`tasks.c`、`queue.c`、`list.c`、`timers.c`、`event_groups.c`、`stream_buffer.c`、`croutine.c`
- 头文件目录：`FreeRTOS-Kernel/include`
- 移植层：`portable/GCC/ARM_CM3`
- 堆管理：`portable/MemMang/heap_4.c`

### 1.2 配置 FreeRTOSConfig.h

> FreeRTOS-LTS\FreeRTOS\FreeRTOS-Kernel\ **examples\template_configuration\FreeRTOSConfig.h**

**配置可用的任务优先级数量**

```c
#define configMAX_PRIORITIES                       8
```

**修改FreeRTOS中断优先级**

```c
//add:
#define configPRIO_BITS                             4    /* NVIC 中断优先级位数，STM32 通常为 4 位 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY    15   /* 可设置的最低中断优先级，数值越大优先级越低 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5  /* 可安全调用 FreeRTOS API 的最高中断优先级 */

#define configKERNEL_INTERRUPT_PRIORITY          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) ) /* 内核使用的中断优先级，如 SysTick 和 PendSV */


//modify:
#define configKERNEL_INTERRUPT_PRIORITY          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) ) /* 内核使用的中断优先级，如 SysTick 和 PendSV */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) ) /* 能调用 FreeRTOS 中断安全 API 的最高抢占优先级 */
#define configMAX_API_CALL_INTERRUPT_PRIORITY    configMAX_SYSCALL_INTERRUPT_PRIORITY /* configMAX_SYSCALL_INTERRUPT_PRIORITY 的别名 */
```

**定义端口使用的中断处理程序**

```c
/* Define the interrupt handlers used by the port. */
#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler
```

### 1.3 配置中断处理

> FreeRTOS_test\Core\Src\ s**tm32f1xx_it.c**

1. 先在 CubeMX 中关闭以下两项中断代码生成：

- `System Core -> NVIC -> Code generation -> System service call via SWI instruction`
- `System Core -> NVIC -> Code generation -> Pendable request for system service`

对应目的：

- 关闭 `System service call via SWI instruction`，避免 CubeMX 生成 `SVC_Handler()`
- 关闭 `Pendable request for system service`，避免 CubeMX 生成 `PendSV_Handler()`

重新生成代码后，`stm32f1xx_it.c` 中不应再出现下面两个函数：

```c
void SVC_Handler(void)
{

}
void PendSV_Handler(void)
{

}
```

2. 添加头文件和声明：

```c
#include "FreeRTOS.h"
#include "task.h"
```

声明函数：

```c
//声明xPortSysTickHandler函数，该函数由FreeRTOS提供，用于处理SysTick中断
extern void xPortSysTickHandler( void ); 
```

3. 在 `SysTick_Handler()` 中加入：

```c
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */

  /* USER CODE BEGIN SysTick_IRQn 1 */
  // 当前调度器状态不为未启动时，调用xPortSysTickHandler函数进行系统调度
  if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    xPortSysTickHandler();
  }

  /* USER CODE END SysTick_IRQn 1 */
}
```

说明：

- `SVC_Handler`、`PendSV_Handler` 交给 FreeRTOS 的 `port.c`
- `SysTick_Handler()` 负责驱动 FreeRTOS 节拍和调度

### 1.4 main.c 启动调度器

> `FreeRTOS_test\Core\Src\main.c`

在 `USER CODE BEGIN Includes` 中加入：

```c
#include "start_task.h"
#include "FreeRTOS.h"
#include "task.h"
```

在 `USER CODE BEGIN 2` 中加入：

```c
xTaskCreate(StartTask, START_TASK_NAME, START_TASK_STACK_SIZE, NULL, START_TASK_PRIORITY, &StartTaskHandle);
vTaskStartScheduler();
```

建议：

- `main.c` 只创建 `StartTask`
- 其他任务统一放到 `start_task.c` 中创建

### 1.5 补充必要钩子函数

当 `FreeRTOSConfig.h` 开启栈溢出检测时，需要提供：

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;

  taskDISABLE_INTERRUPTS();
  while (1)
  {
  }
}
```

建议放在 `main.c` 的 `USER CODE BEGIN 4` 区域中，避免被 CubeMX 覆盖。

### 1.6 CMake 接入与防覆盖

推荐做法：

- `cmake/stm32cubemx/CMakeLists.txt` 保持给 CubeMX 管理
- 自定义增量配置写到 `cmake/user-project.cmake`
- 在顶层 `CMakeLists.txt` 中引入它

> `FreeRTOS_test\CMakeLists.txt`

```cmake
add_executable(${CMAKE_PROJECT_NAME})

add_subdirectory(cmake/stm32cubemx)
include(cmake/user-project.cmake)
```

> `FreeRTOS_test\cmake\user-project.cmake`

```cmake
set(USER_APP_SRC_DIR "${CMAKE_SOURCE_DIR}/Core/Src")
set(USER_HARDWARE_INC_DIR "${CMAKE_SOURCE_DIR}/Drivers/Hardware/Inc")
set(USER_HARDWARE_SRC_DIR "${CMAKE_SOURCE_DIR}/Drivers/Hardware/Src")
set(USER_FREERTOS_DIR "${CMAKE_SOURCE_DIR}/FreeRTOS/Source")
set(USER_FREERTOS_PORT_DIR "${USER_FREERTOS_DIR}/portable/GCC/ARM_CM3")
set(USER_FREERTOS_HEAP_FILE "${USER_FREERTOS_DIR}/portable/MemMang/heap_4.c")
set(USER_START_TASK_FILE "${USER_APP_SRC_DIR}/start_task.c")

file(GLOB USER_HARDWARE_SOURCES CONFIGURE_DEPENDS
    ${USER_HARDWARE_SRC_DIR}/*.c
)

set(USER_APP_SOURCES
    ${USER_START_TASK_FILE}
    ${USER_HARDWARE_SOURCES}
)

set(USER_FREERTOS_SOURCES
    ${USER_FREERTOS_DIR}/croutine.c
    ${USER_FREERTOS_DIR}/event_groups.c
    ${USER_FREERTOS_DIR}/list.c
    ${USER_FREERTOS_DIR}/queue.c
    ${USER_FREERTOS_DIR}/stream_buffer.c
    ${USER_FREERTOS_DIR}/tasks.c
    ${USER_FREERTOS_DIR}/timers.c
    ${USER_FREERTOS_PORT_DIR}/port.c
    ${USER_FREERTOS_HEAP_FILE}
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${USER_APP_SOURCES}
    ${USER_FREERTOS_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${USER_HARDWARE_INC_DIR}
    ${USER_FREERTOS_DIR}/include
    ${USER_FREERTOS_PORT_DIR}
)
```

这样做的目的只有一个：CubeMX 重新生成后，自定义 CMake 配置尽量不丢。

### 1.7 任务创建失败检查

在 `start_task.c` 中调用 `xTaskCreate()` 时，建议检查返回值：

```c
BaseType_t task_status;

task_status = xTaskCreate(...);
if (task_status != pdPASS)
{
    Error_Handler();
}
```

这样当堆空间不足或配置异常导致任务创建失败时，不会静默出错。

### 1.8 CubeMX重新生成后的检查项

每次用 CubeMX 重新生成工程后，建议至少检查以下内容：

#### 1.8.1 main.c

- `USER CODE BEGIN Includes` 中的  
  `#include "start_task.h"`  
  `#include "FreeRTOS.h"`  
  `#include "task.h"` 是否还在
- `USER CODE BEGIN 2` 中的  
  `xTaskCreate(StartTask, ...)`  
  `vTaskStartScheduler()` 是否还在
- `USER CODE BEGIN 4` 中的  
  `vApplicationStackOverflowHook()` 是否还在

#### 1.8.2 stm32f1xx_it.c

- `FreeRTOS.h`、`task.h`、`xPortSysTickHandler` 声明是否还在
- CubeMX 中 `System service call via SWI instruction`、`Pendable request for system service` 是否已关闭
- `stm32f1xx_it.c` 中是否没有再生成 `SVC_Handler`、`PendSV_Handler`
- `SysTick_Handler()` 是否仍然调用 `xPortSysTickHandler()`

#### 1.8.3 CMake

- 顶层 `CMakeLists.txt` 是否仍然包含：

```cmake
include(cmake/user-project.cmake)
```

- `cmake/user-project.cmake` 是否仍存在
- `cmake/stm32cubemx/CMakeLists.txt` 即使被覆盖，只要顶层仍然引入 `user-project.cmake`，自定义 FreeRTOS 和硬件驱动接入通常仍可保留

这些文件不应依赖 CubeMX 生成；如果路径不变，重新生成工程后通常不会丢失。

## 二、任务状态

![image-20260407142248077](../AppData/Roaming/Typora/typora-user-images/image-20260407142248077.png)

FreeRTOS 中任务常见状态只有四种，理解它们的切换关系即可。

### 2.1 运行态（Running）

- 当前正在占用 CPU 执行的任务
- 任意时刻只能有一个任务处于运行态

### 2.2 就绪态（Ready）

- 任务已经具备运行条件
- 只是暂时没有获得 CPU
- 调度器会从就绪列表中选择优先级最高的任务运行

### 2.3 阻塞态（Blocked）

- 任务在等待某个事件，因此暂时不参与调度
- 常见等待对象：`vTaskDelay()`、队列、信号量、互斥量、事件标志组
- 等待条件满足后，任务会自动回到就绪态

### 2.4 挂起态（Suspended）

- 任务被显式暂停，完全脱离调度
- 不会因时间到或事件到而自动恢复
- 必须由 `vTaskResume()` 或 `xTaskResumeFromISR()` 恢复

## 三、任务创建

FreeRTOS 创建任务有两种方式：动态创建和静态创建。两者差别主要在于任务栈与 TCB 的内存由谁提供。

### 3.1 动态创建

- 内存由 FreeRTOS 堆管理器分配
- 删除任务后，相关内存可回收到堆中
- 使用简单，最常见
- 风险是堆不足时会创建失败

**配置宏**

```c
#define configSUPPORT_DYNAMIC_ALLOCATION 1
```

**函数原型**

```c
BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,                 // 任务函数入口
    const char * const pcName,                 // 任务名，主要用于调试
    const configSTACK_DEPTH_TYPE uxStackDepth, // 栈深度，单位是字，不是字节
    void * const pvParameters,                 // 传给任务函数的参数
    UBaseType_t uxPriority,                    // 任务优先级
    TaskHandle_t * const pxCreatedTask         // 输出任务句柄，可传 NULL
);
```

**最小示例**

```c
TaskHandle_t LedTaskHandle = NULL; // 保存任务句柄，后续可用于挂起、恢复、删除等操作

void LedTask(void *argument) // 任务函数必须是这种固定原型
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // 延时 100ms，让出 CPU
    }
}

void AppStart(void)
{
    // 动态创建任务：栈和 TCB 由 FreeRTOS 堆分配
    if (xTaskCreate(LedTask, "LedTask", 128, NULL, 2, &LedTaskHandle) != pdPASS)
    {
        Error_Handler(); // 创建失败，一般是堆空间不足或参数错误
    }
}
```

### 3.2 静态创建

- 栈和 TCB 由用户自己提供
- 不依赖堆，内存占用在编译期就确定
- 更适合资源受控、稳定性要求高的场景
- 删除任务后，这块静态内存也不会自动回收

**配置宏**

```c
#define configSUPPORT_STATIC_ALLOCATION 1
```

**函数原型**

```c
TaskHandle_t xTaskCreateStatic(
    TaskFunction_t pxTaskCode,                 // 任务函数入口
    const char * const pcName,                 // 任务名，主要用于调试
    const configSTACK_DEPTH_TYPE uxStackDepth, // 栈深度，单位是字
    void * const pvParameters,                 // 传给任务函数的参数
    UBaseType_t uxPriority,                    // 任务优先级
    StackType_t * const puxStackBuffer,        // 用户提供的任务栈
    StaticTask_t * const pxTaskBuffer          // 用户提供的任务控制块 TCB
);
```

**最小示例**

```c
#define LED_TASK_STACK_SIZE 128 // 栈深度，单位是字

static StackType_t LedTaskStack[LED_TASK_STACK_SIZE]; // 静态任务栈
static StaticTask_t LedTaskTCB;                       // 静态任务控制块
static TaskHandle_t LedTaskHandle = NULL;             // 任务句柄

void LedTask(void *argument)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // 周期性执行任务
    }
}

void AppStart(void)
{
    // 静态创建任务：栈和 TCB 由用户自己提供
    LedTaskHandle = xTaskCreateStatic(
        LedTask,            // 任务函数
        "LedTask",          // 任务名
        LED_TASK_STACK_SIZE,// 栈深度
        NULL,               // 任务参数
        2,                  // 优先级
        LedTaskStack,       // 用户提供的任务栈
        &LedTaskTCB         // 用户提供的 TCB
    );
}
```

### 3.3 什么时候选动态，什么时候选静态

- 想快速开发、任务数量不多：优先用动态创建
- 想严格控制 RAM、避免堆碎片：优先用静态创建
- 只要用了静态创建，就要同时提供空闲任务内存；启用软件定时器时，还要提供定时器任务内存

```c
void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,  // 返回空闲任务的 TCB 地址
    StackType_t **ppxIdleTaskStackBuffer, // 返回空闲任务栈起始地址
    uint32_t *pulIdleTaskStackSize);      // 返回空闲任务栈深度

void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCBBuffer,  // 返回定时器任务的 TCB 地址
    StackType_t **ppxTimerTaskStackBuffer, // 返回定时器任务栈起始地址
    uint32_t *pulTimerTaskStackSize);      // 返回定时器任务栈深度
```

## 四、任务与调度器的挂起/恢复

### 4.1 相关配置

```c
#define INCLUDE_vTaskSuspend        1
#define INCLUDE_xTaskResumeFromISR  1
```

### 4.2 任务挂起与恢复

任务挂起针对某一个任务本身。

```c
void vTaskSuspend(TaskHandle_t xTaskToSuspend);          // 挂起指定任务
void vTaskResume(TaskHandle_t xTaskToResume);            // 在任务中恢复指定任务
BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume); // 在中断中恢复指定任务
```

- `vTaskSuspend(NULL)`：挂起当前任务自己
- `vTaskResume()`：在任务上下文中恢复目标任务
- `xTaskResumeFromISR()`：在中断中恢复任务

适用场景：

- 某任务暂时不允许运行
- 必须由其他任务或中断“手动唤醒”

### 4.3 调度器挂起与恢复

调度器挂起影响的是任务切换，不是中断开关。

```c
vTaskSuspendAll(); // 暂停任务调度，不再切换任务
xTaskResumeAll();  // 恢复任务调度
```

- 挂起后，中断仍然会响应
- 只是暂时不进行任务切换
- 适合保护一小段不希望被切换打断的代码
- 不适合长时间占用，否则会影响系统实时性

## 五、FreeRTOS 中断管理

这部分最重要的结论只有两条：

- 高优先级中断可以更实时，但不能调用任何 FreeRTOS API
- 能调用 `xxxFromISR()` 的中断，优先级必须低于或等于 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 允许的范围

### 5.1 优先级分界

> 相关宏见 1.2 节

| 类型 | 中断优先级范围 | 是否会被 FreeRTOS 临界区屏蔽 | 是否能调用 FreeRTOS API |
| --- | --- | --- | --- |
| 高优先级中断 | `0 ~ configMAX_SYSCALL_INTERRUPT_PRIORITY - 1` | 不会 | 不能 |
| 可管理中断 | `configMAX_SYSCALL_INTERRUPT_PRIORITY ~ 15` | 会 | 只能调用 `xxxFromISR()` |

### 5.2 中断开关

```c
portDISABLE_INTERRUPTS(); // 屏蔽 FreeRTOS 可管理的中断
portENABLE_INTERRUPTS();  // 恢复 FreeRTOS 可管理的中断
```

这里关闭的不是所有中断，而是 FreeRTOS 可管理的那部分中断。

### 5.3 临界区

```c
taskENTER_CRITICAL(); // 进入临界区，防止任务切换或相关中断打断
// 访问共享资源
taskEXIT_CRITICAL();  // 退出临界区
```

在中断里如果也要保护共享资源，应使用中断版本：

```c
UBaseType_t state;

state = portSET_INTERRUPT_MASK_FROM_ISR(); // 保存当前屏蔽状态，并临时屏蔽可管理中断
// 访问共享资源
portCLEAR_INTERRUPT_MASK_FROM_ISR(state);  // 恢复进入前的中断屏蔽状态
```

使用建议：

- 临界区尽量短
- 临界区里不要做阻塞操作
- 中断里只调用 `FromISR` 版本 API

### 5.4 中断与任务同步的一般写法

典型流程如下：

1. 中断接收数据或检测到事件
2. 中断中调用 `xQueueSendFromISR()`、`xSemaphoreGiveFromISR()` 等接口通知任务
3. 如果需要切换任务，调用 `portYIELD_FROM_ISR()`

```c
BaseType_t xHigherPriorityTaskWoken = pdFALSE; // 记录是否唤醒了更高优先级任务

xSemaphoreGiveFromISR(xSem, &xHigherPriorityTaskWoken); // 在中断中释放信号量，通知任务
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);           // 如果需要，立即切换到更高优先级任务
```

## 六、消息队列

消息队列用于任务与任务、任务与中断之间传递数据。它的核心特点是：**既能同步，又能带数据**。

### 6.1 常用场景

- 按键任务把键值发给业务任务
- 串口中断把接收到的数据或帧头通知给解析任务
- 多个生产者向一个消费者发送消息

### 6.2 常用接口

```c
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize); // 创建队列

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);   // 发送一项数据到队列
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);           // 从队列中接收一项数据

BaseType_t xQueueSendFromISR(
    QueueHandle_t xQueue,                    // 目标队列
    const void *pvItemToQueue,               // 要发送的数据地址
    BaseType_t *pxHigherPriorityTaskWoken    // 若唤醒高优先级任务，则置位
);
```

### 6.3 使用要点

- 队列按“项”存数据，不是按字节流
- 创建队列时要明确每一项的大小
- 队列满时发送会失败或阻塞
- 队列空时接收会失败或阻塞

**示例**

```c
typedef struct
{
    uint8_t key;   // 按键编号
    uint8_t state; // 按键状态：按下/松开
} KeyMsg_t;

QueueHandle_t KeyQueue; // 按键消息队列句柄

void AppInit(void)
{
    KeyQueue = xQueueCreate(8, sizeof(KeyMsg_t)); // 队列长度为 8，每项是一个 KeyMsg_t
}
```

## 七、信号量

信号量用于**同步**或**资源管理**，通常不关注传递的数据内容。

### 7.1 二值信号量

作用类似“事件通知”。

适合场景：

- 中断通知任务处理数据
- 一个任务通知另一个任务开始执行

```c
SemaphoreHandle_t xSemaphoreCreateBinary(void); // 创建二值信号量
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait); // 获取信号量
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore); // 释放信号量
BaseType_t xSemaphoreGiveFromISR(
    SemaphoreHandle_t xSemaphore,              // 要释放的信号量
    BaseType_t *pxHigherPriorityTaskWoken      // 若唤醒高优先级任务，则置位
);
```

### 7.2 计数信号量

适合表示“有多少个资源”或“发生了多少次事件”。

```c
SemaphoreHandle_t xSemaphoreCreateCounting(
    UBaseType_t uxMaxCount,     // 最大计数值
    UBaseType_t uxInitialCount  // 初始计数值
);
```

常见用途：

- 统计缓冲区中可处理的数据块数量
- 统计中断事件发生次数

### 7.3 互斥量

互斥量本质上也是一种信号量，但它专门用于保护临界资源，并带有优先级继承机制。

适合场景：

- 多个任务共用串口
- 多个任务共用 I2C、SPI、显示屏等外设

```c
SemaphoreHandle_t xSemaphoreCreateMutex(void); // 创建互斥量，用于保护共享资源
```

注意：

- 互斥量主要用于任务与任务之间
- 不要在中断中使用互斥量
- 中断与任务同步优先用二值信号量或队列


## 八、ADC + DMA + FreeRTOS

### 8.1 推荐方案

多通道采集推荐直接使用：

- `ADC1`
- `Scan Conversion Mode = Enabled`
- `Continuous Conversion Mode = Enabled`
- `DMA = Enable`
- `DMA Mode = Circular`

适合场景：

- 多通道连续采样
- FreeRTOS 任务周期读取 ADC 数据

### 8.2 CubeMX 配置

#### 8.2.1 引脚

把需要采集的引脚配置成 `ADC1_INx`。  
例如：

- `PA0 -> ADC1_IN0`
- `PA1 -> ADC1_IN1`
- `PA2 -> ADC1_IN2`
- `PA3 -> ADC1_IN3`

#### 8.2.2 ADC1 参数

在 `ADC1 -> Parameter Settings` 中配置：

- `Mode = Independent mode`
  含义：
  ADC1 单独工作，不和 ADC2 组成双 ADC 模式。普通工程默认就选这个。

- `Data Alignment = Right alignment`
  含义：
  12 位 ADC 结果放在数据寄存器低位，高位补 0。这样读出来后直接放进 `uint16_t` 使用最方便。
  如果选 `Left alignment`，结果会左移，不利于直接计算。

- `Scan Conversion Mode = Enabled`
  含义：
  允许 ADC 按顺序转换多个规则通道。
  多通道采集必须开启。
  如果关闭，ADC 每次只会按当前配置处理一个规则通道。

- `Continuous Conversion Mode = Enabled`
  含义：
  一轮规则组转换完成后，ADC 自动开始下一轮，不需要软件反复启动。
  适合持续采样。
  如果关闭，则每次只执行一轮，之后需要软件再次启动。

- `Discontinuous Conversion Mode = Disabled`
  含义：
  不把规则组拆成多次触发执行。
  多通道连续采样一般关闭。
  如果开启，规则组会被分段执行，适合特殊触发场景，普通 ADC + DMA 不建议用。

- `Enable Regular Conversions = Enable`
  含义：
  启用规则通道组转换。
  平时做电压采样、传感器采样，基本都用规则组。

- `Number Of Conversion = 通道数`
  含义：
  一轮规则组里一共要转换几个通道。
  例如你启用了 `IN0~IN3` 四个通道，这里就必须填 `4`。
  这个值必须和实际 Rank 数量一致，否则 DMA 数据顺序会错。

- `External Trigger Conversion Source = Regular Conversion launched by software`
  含义：
  规则组转换由软件触发启动。
  代码里通常通过 `HAL_ADC_Start_DMA()` 或 `HAL_ADC_Start()` 触发。
  如果后面想用定时器定时采样，可以把这里改成某个定时器触发源。

- `Enable Injected Conversions = Disable`
  含义：
  不使用注入通道组。
  注入通道一般用于优先级更高、单独触发的采样任务。普通多通道 DMA 采集先不用。

- `Enable Analog WatchDog Mode = Disable`
  含义：
  不启用模拟看门狗功能。
  模拟看门狗用于监控 ADC 值是否超出上下阈值，普通采集先关闭，后续需要做过压/欠压检测时再开。

#### 8.2.3 Rank

给每个通道排顺序，例如：

- `Rank1 = IN0`
- `Rank2 = IN1`
- `Rank3 = IN2`
- `Rank4 = IN3`

DMA 缓冲区顺序与 Rank 一致：

- `adc_buf[0] -> Rank1`
- `adc_buf[1] -> Rank2`
- `adc_buf[2] -> Rank3`
- `adc_buf[3] -> Rank4`

#### 8.2.4 Sampling Time

每个通道建议先配：

- `55.5 cycles`
或
- `71.5 cycles`

含义：

- 采样时间越长，越适合高阻抗输入

#### 8.2.5 DMA 参数

在 `ADC1 -> DMA Settings` 中添加 `ADC1`，推荐：

- `Direction = Peripheral to Memory`
  含义：
  数据从 ADC 外设寄存器搬运到内存。
  ADC 采样时，源地址是 ADC 数据寄存器，目标地址是你的数组缓冲区，所以方向必须是这个。

- `Mode = Circular`
  含义：
  DMA 写到缓冲区末尾后，会自动从头开始继续写，不需要软件重新启动。
  适合连续采样。
  如果选 `Normal`，DMA 传完一轮就停，后续要软件再次启动。

- `Peripheral Increment = Disable`
  含义：
  外设地址不递增。
  因为 ADC 采样结果始终都从同一个数据寄存器读出，所以必须关闭。
  如果错误开启，会导致 DMA 访问错误地址。

- `Memory Increment = Enable`
  含义：
  每搬运一个 ADC 结果，内存地址自动加 1 个数据单位，写到下一个数组元素。
  多通道采样必须开启，否则每次结果都会覆盖到同一个内存位置。
  单通道、只想保存一个最新值时，可以关闭。

- `Peripheral Data Width = Half Word`
  含义：
  ADC 数据寄存器按 16 位读取。
  STM32F1 的 ADC 结果虽然只有 12 位，但通常存放在 16 位寄存器中，因此这里选 `Half Word` 最合适。

- `Memory Data Width = Half Word`
  含义：
  内存缓冲区每个元素按 16 位存储。
  这意味着代码中缓冲区类型通常写成 `uint16_t adc_buf[]`。
  如果这里配置成 `Word` 或 `Byte`，会和 ADC 结果宽度不匹配。

说明：

- 多通道采样时，`Memory Increment` 必须开
- 数据类型一般用 `uint16_t`
- 多通道 + 连续采样最常见组合是：
  `Peripheral to Memory + Circular + Peripheral Increment Disable + Memory Increment Enable + Half Word`

#### 8.2.6 时钟

> 时钟树Clock Configuration

推荐：

- `ADC Clock = PCLK2 / 6`

72MHz 主频下约为：

- `12MHz`

### 8.3 生成代码后

CubeMX 生成后通常会有：

- `hadc1`
- `hdma_adc1`
- `MX_ADC1_Init()`
- `MX_DMA_Init()`

初始化顺序建议：

```c
MX_GPIO_Init();
MX_DMA_Init();
MX_ADC1_Init();
```

### 8.4 FreeRTOS 中使用

#### 8.4.1 DMA 缓冲区

```c
#define ADC_CHANNEL_COUNT 4

uint16_t g_adcBuffer[ADC_CHANNEL_COUNT];
```

#### 8.4.2 启动 ADC + DMA

```c
HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adcBuffer, ADC_CHANNEL_COUNT);
```

#### 8.4.3 最简单任务例程

```c
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#define ADC_CHANNEL_COUNT 4

uint16_t g_adcBuffer[ADC_CHANNEL_COUNT];

static void AdcTask(void *pvParameters)
{
    uint16_t ch0;
    uint16_t ch1;
    uint16_t ch2;
    uint16_t ch3;

    (void)pvParameters;

    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adcBuffer, ADC_CHANNEL_COUNT);

    while (1)
    {
        ch0 = g_adcBuffer[0];
        ch1 = g_adcBuffer[1];
        ch2 = g_adcBuffer[2];
        ch3 = g_adcBuffer[3];

        /* 在这里处理 ADC 数据 */

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

#### 8.4.4 创建任务

```c
TaskHandle_t AdcTaskHandle = NULL;

#define ADC_TASK_NAME        "ADC_Task"
#define ADC_TASK_STACK_SIZE  256U
#define ADC_TASK_PRIORITY    (tskIDLE_PRIORITY + 2)

void AppCreateTasks(void)
{
    if (xTaskCreate(AdcTask,
                    ADC_TASK_NAME,
                    ADC_TASK_STACK_SIZE,
                    NULL,
                    ADC_TASK_PRIORITY,
                    &AdcTaskHandle) != pdPASS)
    {
        Error_Handler();
    }
}
```

### 8.5 ADC 值转电压

12 位 ADC：

- 范围 `0 ~ 4095`

参考电压 `3.3V` 时：

```c
float voltage = (3.3f * adc_value) / 4095.0f;
```

### 8.6 常见问题

- 多通道却没开 `Scan`
  结果只会像单通道

- `Number Of Conversion` 与实际通道数不一致
  DMA 数据顺序会乱

- `Memory Increment` 没开
  多通道结果会反复覆盖同一个元素

- `Rank` 配错
  `adc_buf[i]` 对应关系会错

- 采样时间太短
  高阻抗输入下数据容易不稳

### 8.7 推荐最终配置

- `ADC1`
- `Independent mode`
- `Right alignment`
- `Scan Conversion Mode = Enabled`
- `Continuous Conversion Mode = Enabled`
- `Number Of Conversion = 通道数`
- `Software Start`
- `Rank` 按通道顺序配置
- `Sampling Time = 55.5` 或 `71.5 cycles`
- `DMA1 Channel1`
- `Circular`
- `Memory Increment = Enable`
- `Half Word`
- `ADC Clock = PCLK2 / 6`

