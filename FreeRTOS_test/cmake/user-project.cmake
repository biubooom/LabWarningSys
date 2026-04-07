# 用户自维护的 CMake 增量配置。
# 将工程自定义源码、头文件路径和 FreeRTOS 接入统一放在这里，
# 避免 STM32CubeMX 重新生成时覆盖这些项目配置。

# 应用层源码目录，适合放任务编排、系统启动等文件。
set(USER_APP_SRC_DIR "${CMAKE_SOURCE_DIR}/Core/Src")

# 用户硬件驱动目录，适合放 key/led/oled 等驱动。
set(USER_HARDWARE_INC_DIR "${CMAKE_SOURCE_DIR}/Drivers/Hardware/Inc")
set(USER_HARDWARE_SRC_DIR "${CMAKE_SOURCE_DIR}/Drivers/Hardware/Src")

# FreeRTOS 源码目录以及当前 Cortex-M 端口目录。
set(USER_FREERTOS_DIR "${CMAKE_SOURCE_DIR}/FreeRTOS/Source")
set(USER_FREERTOS_PORT_DIR "${USER_FREERTOS_DIR}/portable/GCC/ARM_CM3")

# FreeRTOS 堆管理实现文件。
# 如果你想切换为 heap_1/2/3/5，可只改这一行。
set(USER_FREERTOS_HEAP_FILE "${USER_FREERTOS_DIR}/portable/MemMang/heap_4.c")

# 启动任务源码文件。
# 如果你的启动任务文件名不是 start_task.c，只需修改这里。
set(USER_START_TASK_FILE "${USER_APP_SRC_DIR}/start_task.c")

# 自动收集用户硬件驱动源码。
file(GLOB USER_HARDWARE_SOURCES CONFIGURE_DEPENDS
    ${USER_HARDWARE_SRC_DIR}/*.c
)

# 工程自定义应用层源码，CubeMX 不负责管理这些文件。
set(USER_APP_SOURCES
    ${USER_START_TASK_FILE}
    ${USER_HARDWARE_SOURCES}
)

# 当前工程所需的 FreeRTOS 内核最小源码集合。
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

# 将用户自定义源码追加到工程目标中。
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${USER_APP_SOURCES}
    ${USER_FREERTOS_SOURCES}
)

# 添加用户驱动和 FreeRTOS 所需的头文件路径。
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${USER_HARDWARE_INC_DIR}
    ${USER_FREERTOS_DIR}/include
    ${USER_FREERTOS_PORT_DIR}
)
