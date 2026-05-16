#pragma once

#include "esp_err.h"

#include "sensor_data.h"

typedef struct {
    sensor_group_data_t groups[SENSOR_GROUP_COUNT];
    bool link_online;
    bool system_alarm;
} cloud_report_snapshot_t;

typedef struct {
    bool valid;
    bool alarm_valid;
    bool sequence_valid;
    bool threshold_valid;
    bool link_online;
    bool system_alarm;
    bool sequence_ready;
    bool group_alarm[SENSOR_GROUP_COUNT];
    uint8_t sequence_length;
    float sequence_confidence;
    char threshold_state_label[SENSOR_THRESHOLD_STATE_LABEL_MAX_LEN];
    char threshold_display_name[SENSOR_THRESHOLD_DISPLAY_NAME_MAX_LEN];
    char sequence_state_label[SENSOR_SEQUENCE_STATE_LABEL_MAX_LEN];
    char sequence_display_name[SENSOR_SEQUENCE_DISPLAY_NAME_MAX_LEN];
} cloud_alarm_state_t;

esp_err_t cloud_report_start(void);
esp_err_t cloud_report_publish_snapshot(const cloud_report_snapshot_t *snapshot, cloud_alarm_state_t *alarm_state);
