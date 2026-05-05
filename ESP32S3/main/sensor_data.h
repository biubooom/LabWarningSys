#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SENSOR_GROUP_COUNT 4U

typedef struct {
    bool online;
    float temperature;
    float humidity;
    float smoke;
    float light;
} sensor_group_data_t;

typedef struct {
    sensor_group_data_t groups[SENSOR_GROUP_COUNT];
    uint32_t last_rx_tick;
    bool link_online;
} sensor_snapshot_t;
