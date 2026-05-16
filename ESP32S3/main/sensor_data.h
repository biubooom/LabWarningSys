#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SENSOR_GROUP_COUNT 4U
#define SENSOR_SEQUENCE_STATE_LABEL_MAX_LEN 32U
#define SENSOR_SEQUENCE_DISPLAY_NAME_MAX_LEN 32U
#define SENSOR_THRESHOLD_STATE_LABEL_MAX_LEN 32U
#define SENSOR_THRESHOLD_DISPLAY_NAME_MAX_LEN 32U

typedef struct {
    bool online;
    bool alarm;
    float temperature;
    float humidity;
    float smoke;
    float light;
} sensor_group_data_t;

typedef struct {
    sensor_group_data_t groups[SENSOR_GROUP_COUNT];
    uint32_t last_rx_tick;
    bool link_online;
    bool system_alarm;
    bool threshold_state_valid;
    bool sequence_ready;
    bool sequence_prediction_valid;
    uint8_t sequence_length;
    float sequence_confidence;
    char threshold_state_label[SENSOR_THRESHOLD_STATE_LABEL_MAX_LEN];
    char threshold_display_name[SENSOR_THRESHOLD_DISPLAY_NAME_MAX_LEN];
    char sequence_state_label[SENSOR_SEQUENCE_STATE_LABEL_MAX_LEN];
    char sequence_display_name[SENSOR_SEQUENCE_DISPLAY_NAME_MAX_LEN];
} sensor_snapshot_t;
