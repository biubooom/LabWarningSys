#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    KEY_NONE = 0,
    KEY0_PRESS,
    KEY1_PRESS,
    KEY2_PRESS,
    KEY3_PRESS
} KeyValue_t;

uint8_t Key_Scan(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
