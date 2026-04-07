#ifndef __START_TASK_H
#define __START_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

#define START_TASK_NAME         "StartTask"
#define START_TASK_STACK_SIZE   128U
#define START_TASK_PRIORITY     (tskIDLE_PRIORITY + 3)

extern TaskHandle_t StartTaskHandle;

void StartTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* __START_TASK_H */
