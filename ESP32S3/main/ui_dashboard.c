#include "ui_dashboard.h"
#include "wifi_sta.h"

#include <stdio.h>
#include <string.h>

#define CARD_COUNT SENSOR_GROUP_COUNT

typedef struct {
    lv_obj_t *button;
    lv_obj_t *group_label;
    lv_obj_t *online_label;
} sensor_tile_view_t;

typedef struct {
    lv_obj_t *value;
} detail_metric_view_t;

typedef enum {
    PAGE_HOME = 0,
    PAGE_SETTINGS,
    PAGE_SENSOR_DETAIL,
} dashboard_page_t;

typedef struct {
    lv_color_t bg_color;
    lv_color_t border_color;
    lv_color_t text_color;
} prediction_palette_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_top_bar;
static lv_obj_t *s_title_label;
static lv_obj_t *s_subtitle_label;
static lv_obj_t *s_nav_row;
static lv_obj_t *s_top_back_btn;
static lv_obj_t *s_top_back_label;
static lv_obj_t *s_home_page;
static lv_obj_t *s_settings_page;
static lv_obj_t *s_sensor_detail_page;
static lv_obj_t *s_nav_home_btn;
static lv_obj_t *s_nav_settings_btn;

static lv_obj_t *s_prediction_circle;
static lv_obj_t *s_prediction_state_label;
static lv_obj_t *s_prediction_confidence_label;
static lv_obj_t *s_prediction_hint_label;

static lv_obj_t *s_wifi_state_value;
static lv_obj_t *s_wifi_ssid_value;
static lv_obj_t *s_cloud_link_value;
static lv_obj_t *s_wifi_action_hint;

static lv_obj_t *s_detail_title_label;
static lv_obj_t *s_detail_status_label;
static detail_metric_view_t s_detail_temperature;
static detail_metric_view_t s_detail_humidity;
static detail_metric_view_t s_detail_smoke;
static detail_metric_view_t s_detail_light;

static sensor_tile_view_t s_sensor_tiles[CARD_COUNT];
static dashboard_page_t s_current_page;
static uint32_t s_selected_group_index;
static sensor_snapshot_t s_last_snapshot;
static bool s_has_snapshot;

static const lv_font_t *font_title(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_subtitle(void)
{
    return &lv_font_montserrat_14;
}

static const lv_font_t *font_sensor_tile_title(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_sensor_tile_status(void)
{
    return &lv_font_montserrat_16;
}

static const lv_font_t *font_prediction_state(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_prediction_confidence(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_prediction_hint(void)
{
    return &lv_font_montserrat_14;
}

static const lv_font_t *font_metric_title(void)
{
    return &lv_font_montserrat_16;
}

static const lv_font_t *font_metric_value(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_nav(void)
{
    return &lv_font_montserrat_16;
}

static void format_fixed_1(char *buffer, size_t buffer_size, float value, const char *suffix)
{
    int scaled;
    int integer_part;
    int decimal_part;

    if ((buffer == NULL) || (buffer_size == 0U)) {
        return;
    }

    scaled = (int)((value * 10.0f) + ((value >= 0.0f) ? 0.5f : -0.5f));
    integer_part = scaled / 10;
    decimal_part = scaled >= 0 ? (scaled % 10) : -(scaled % 10);
    lv_snprintf(buffer,
                buffer_size,
                "%d.%d%s",
                integer_part,
                decimal_part,
                (suffix != NULL) ? suffix : "");
}

static const char *sequence_state_summary(const sensor_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return "--";
    }

    if (strcmp(snapshot->sequence_state_label, "STATE_FIRE") == 0) {
        return "FIRE WARN";
    }
    if (strcmp(snapshot->sequence_state_label, "STATE_GAS_LEAK") == 0) {
        return "GAS WARN";
    }
    if (strcmp(snapshot->sequence_state_label, "STATE_HIGH_HUMID") == 0) {
        return "HUMID WARN";
    }
    if (strcmp(snapshot->sequence_state_label, "STATE_NORMAL") == 0) {
        return "NORMAL";
    }

    if (snapshot->sequence_state_label[0] != '\0') {
        return snapshot->sequence_state_label;
    }

    return "--";
}

static const char *threshold_state_summary(const sensor_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return "--";
    }

    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_FIRE") == 0) {
        return "FIRE";
    }
    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_GAS_LEAK") == 0) {
        return "GAS";
    }
    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_HIGH_TEMP") == 0) {
        return "HOT";
    }
    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_LOW_TEMP") == 0) {
        return "COLD";
    }
    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_HIGH_HUMID") == 0) {
        return "HUMID";
    }
    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_NORMAL") == 0) {
        return "NORMAL";
    }

    if (snapshot->threshold_state_label[0] != '\0') {
        return snapshot->threshold_state_label;
    }

    return "--";
}

static lv_color_t mix_with_white(lv_color_t base, uint8_t strength)
{
    return lv_color_mix(base, lv_color_hex(0xFFF8F1), strength);
}

static prediction_palette_t resolve_prediction_palette(const sensor_snapshot_t *snapshot)
{
    prediction_palette_t palette = {
        .bg_color = lv_color_hex(0xCBD5E1),
        .border_color = lv_color_hex(0x64748B),
        .text_color = lv_color_hex(0x1E293B),
    };
    lv_color_t base = lv_color_hex(0x64748B);
    uint8_t intensity = 96U;

    if ((snapshot == NULL) || (!snapshot->threshold_state_valid && !snapshot->sequence_prediction_valid)) {
        palette.bg_color = lv_color_hex(0xE2E8F0);
        palette.border_color = lv_color_hex(0x94A3B8);
        palette.text_color = lv_color_hex(0x334155);
        return palette;
    }

    if (strcmp(snapshot->threshold_state_label, "THRESHOLD_FIRE") == 0) {
        base = lv_color_hex(0xB91C1C);
    } else if (strcmp(snapshot->threshold_state_label, "THRESHOLD_GAS_LEAK") == 0) {
        base = lv_color_hex(0xC2410C);
    } else if (strcmp(snapshot->threshold_state_label, "THRESHOLD_HIGH_TEMP") == 0) {
        base = lv_color_hex(0xDC2626);
    } else if (strcmp(snapshot->threshold_state_label, "THRESHOLD_LOW_TEMP") == 0) {
        base = lv_color_hex(0x1D4ED8);
    } else if (strcmp(snapshot->threshold_state_label, "THRESHOLD_HIGH_HUMID") == 0) {
        base = lv_color_hex(0x0369A1);
    } else if (strcmp(snapshot->sequence_state_label, "STATE_FIRE") == 0) {
        base = lv_color_hex(0xB91C1C);
    } else if (strcmp(snapshot->sequence_state_label, "STATE_GAS_LEAK") == 0) {
        base = lv_color_hex(0xC2410C);
    } else if (strcmp(snapshot->sequence_state_label, "STATE_HIGH_HUMID") == 0) {
        base = lv_color_hex(0x0369A1);
    } else if (strcmp(snapshot->sequence_state_label, "STATE_NORMAL") == 0) {
        base = lv_color_hex(0x15803D);
    } else {
        base = lv_color_hex(0x475569);
    }

    if (snapshot->sequence_confidence <= 0.25f) {
        intensity = 70U;
    } else if (snapshot->sequence_confidence <= 0.5f) {
        intensity = 110U;
    } else if (snapshot->sequence_confidence <= 0.75f) {
        intensity = 160U;
    } else {
        intensity = 220U;
    }

    palette.bg_color = mix_with_white(base, intensity);
    palette.border_color = base;
    palette.text_color = (intensity >= 150U) ? lv_color_hex(0xFFF7ED) : lv_color_hex(0x3F1D0B);
    return palette;
}

static void update_nav_style(lv_obj_t *btn, bool active)
{
    lv_obj_set_style_bg_color(btn, active ? lv_color_hex(0xFDBA74) : lv_color_hex(0x9A3412), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_text_color(btn, active ? lv_color_hex(0x431407) : lv_color_hex(0xFFF7ED), 0);
}

static void update_top_bar_for_page(dashboard_page_t page)
{
    if ((s_title_label == NULL) || (s_subtitle_label == NULL)) {
        return;
    }

    if (page == PAGE_HOME) {
        lv_label_set_text(s_title_label, "Lab Monitor");
        lv_label_set_text(s_subtitle_label, "");
        lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 0, -10);
        lv_obj_align(s_subtitle_label, LV_ALIGN_LEFT_MID, 0, 12);
    } else if (page == PAGE_SETTINGS) {
        lv_label_set_text(s_title_label, "Settings");
        lv_label_set_text(s_subtitle_label, "");
        lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 0, -10);
        lv_obj_align(s_subtitle_label, LV_ALIGN_LEFT_MID, 0, 12);
    } else {
        char title_text[24];

        lv_snprintf(title_text, sizeof(title_text), "G%u", (unsigned int)(s_selected_group_index + 1U));
        lv_label_set_text(s_title_label, title_text);
        lv_label_set_text(s_subtitle_label, "");
        lv_obj_align(s_title_label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_align(s_subtitle_label, LV_ALIGN_RIGHT_MID, 0, 18);
    }
}

static void show_page(dashboard_page_t page)
{
    s_current_page = page;

    if (s_home_page != NULL) {
        if (page == PAGE_HOME) {
            lv_obj_clear_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_settings_page != NULL) {
        if (page == PAGE_SETTINGS) {
            lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_sensor_detail_page != NULL) {
        if (page == PAGE_SENSOR_DETAIL) {
            lv_obj_clear_flag(s_sensor_detail_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_sensor_detail_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_nav_home_btn != NULL) {
        update_nav_style(s_nav_home_btn, page == PAGE_HOME);
    }
    if (s_nav_settings_btn != NULL) {
        update_nav_style(s_nav_settings_btn, page == PAGE_SETTINGS);
    }
    if (s_nav_row != NULL) {
        if (page == PAGE_SENSOR_DETAIL) {
            lv_obj_add_flag(s_nav_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_nav_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_top_back_btn != NULL) {
        if (page == PAGE_SENSOR_DETAIL) {
            lv_obj_clear_flag(s_top_back_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_top_back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    update_top_bar_for_page(page);
}

static lv_obj_t *create_metric_tile(lv_obj_t *parent,
                                    const char *title,
                                    const char *value_text,
                                    lv_color_t accent,
                                    detail_metric_view_t *metric_view)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_t *title_label = lv_label_create(tile);
    lv_obj_t *value_label = lv_label_create(tile);

    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, lv_pct(48), 102);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFF7ED), 0);
    lv_obj_set_style_radius(tile, 18, 0);
    lv_obj_set_style_pad_hor(tile, 16, 0);
    lv_obj_set_style_pad_ver(tile, 14, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, accent, 0);

    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, font_metric_title(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x7C2D12), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_label_set_text(value_label, value_text);
    lv_obj_set_style_text_font(value_label, font_metric_value(), 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x431407), 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (metric_view != NULL) {
        metric_view->value = value_label;
    }

    return tile;
}

static lv_obj_t *create_settings_tile_with_value(lv_obj_t *parent,
                                                 const char *title,
                                                 const char *value_text,
                                                 lv_color_t accent,
                                                 lv_obj_t **value_label_out)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_t *title_label = lv_label_create(tile);
    lv_obj_t *value_label = lv_label_create(tile);

    lv_obj_remove_style_all(tile);
    lv_obj_set_width(tile, lv_pct(100));
    lv_obj_set_height(tile, 76);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFF7ED), 0);
    lv_obj_set_style_radius(tile, 16, 0);
    lv_obj_set_style_pad_hor(tile, 16, 0);
    lv_obj_set_style_pad_ver(tile, 14, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, accent, 0);

    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, font_metric_title(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x7C2D12), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_label_set_text(value_label, value_text);
    lv_obj_set_style_text_font(value_label, font_metric_value(), 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x431407), 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (value_label_out != NULL) {
        *value_label_out = value_label;
    }

    return tile;
}

static void nav_btn_event_cb(lv_event_t *e)
{
    dashboard_page_t page = (dashboard_page_t)(uintptr_t)lv_event_get_user_data(e);
    show_page(page);
}

static void wifi_reconnect_btn_event_cb(lv_event_t *e)
{
    (void)e;
    wifi_sta_request_reconnect();
    if (s_wifi_action_hint != NULL) {
        lv_label_set_text(s_wifi_action_hint, "Reconnect requested, waiting for IP...");
    }
}

static void back_to_home_event_cb(lv_event_t *e)
{
    (void)e;
    show_page(PAGE_HOME);
}

static void update_sensor_detail_page(const sensor_snapshot_t *snapshot)
{
    char text[32];
    const sensor_group_data_t *group = NULL;

    if ((snapshot == NULL) || (s_selected_group_index >= CARD_COUNT)) {
        return;
    }

    group = &snapshot->groups[s_selected_group_index];

    if (s_detail_title_label != NULL) {
        lv_snprintf(text, sizeof(text), "Sensor Group G%u", (unsigned int)(s_selected_group_index + 1U));
        lv_label_set_text(s_detail_title_label, text);
    }

    if (s_detail_status_label != NULL) {
        if (!group->online) {
            lv_label_set_text(s_detail_status_label, "OFFLINE");
            lv_obj_set_style_text_color(s_detail_status_label, lv_color_hex(0x64748B), 0);
        } else if (group->alarm) {
            lv_label_set_text(s_detail_status_label, "ONLINE  |  ALARM");
            lv_obj_set_style_text_color(s_detail_status_label, lv_color_hex(0xB91C1C), 0);
        } else {
            lv_label_set_text(s_detail_status_label, "ONLINE  |  NORMAL");
            lv_obj_set_style_text_color(s_detail_status_label, lv_color_hex(0x166534), 0);
        }
    }

    if (group->online) {
        if (s_detail_temperature.value != NULL) {
            format_fixed_1(text, sizeof(text), group->temperature, " C");
            lv_label_set_text(s_detail_temperature.value, text);
        }
        if (s_detail_humidity.value != NULL) {
            format_fixed_1(text, sizeof(text), group->humidity, " %RH");
            lv_label_set_text(s_detail_humidity.value, text);
        }
        if (s_detail_smoke.value != NULL) {
            format_fixed_1(text, sizeof(text), group->smoke, " %");
            lv_label_set_text(s_detail_smoke.value, text);
        }
        if (s_detail_light.value != NULL) {
            format_fixed_1(text, sizeof(text), group->light, " %");
            lv_label_set_text(s_detail_light.value, text);
        }
    } else {
        if (s_detail_temperature.value != NULL) {
            lv_label_set_text(s_detail_temperature.value, "--");
        }
        if (s_detail_humidity.value != NULL) {
            lv_label_set_text(s_detail_humidity.value, "--");
        }
        if (s_detail_smoke.value != NULL) {
            lv_label_set_text(s_detail_smoke.value, "--");
        }
        if (s_detail_light.value != NULL) {
            lv_label_set_text(s_detail_light.value, "--");
        }
    }
}

static void sensor_tile_event_cb(lv_event_t *e)
{
    uint32_t group_index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (group_index >= CARD_COUNT) {
        return;
    }

    s_selected_group_index = group_index;
    if (s_has_snapshot) {
        update_sensor_detail_page(&s_last_snapshot);
    }
    show_page(PAGE_SENSOR_DETAIL);
}

static void style_sensor_tile(sensor_tile_view_t *view, bool online)
{
    if (view == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(view->button, online ? lv_color_hex(0xD1FAE5) : lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_border_color(view->button, online ? lv_color_hex(0x059669) : lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_color(view->group_label, online ? lv_color_hex(0x065F46) : lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_color(view->online_label, online ? lv_color_hex(0x166534) : lv_color_hex(0x64748B), 0);
}

static lv_obj_t *create_sensor_tile(lv_obj_t *parent,
                                    uint32_t group_index,
                                    lv_align_t align,
                                    lv_coord_t x_ofs,
                                    lv_coord_t y_ofs,
                                    sensor_tile_view_t *view)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *group_label = lv_label_create(btn);
    lv_obj_t *online_label = lv_label_create(btn);
    char group_text[8];

    lv_obj_set_size(btn, lv_pct(50), lv_pct(50));
    lv_obj_set_align(btn, align);
    lv_obj_set_pos(btn, x_ofs, y_ofs);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 10, 0);
    lv_obj_add_event_cb(btn, sensor_tile_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)group_index);

    lv_snprintf(group_text, sizeof(group_text), "G%u", (unsigned int)(group_index + 1U));
    lv_label_set_text(group_label, group_text);
    lv_obj_set_style_text_font(group_label, font_sensor_tile_title(), 0);
    lv_obj_align(group_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_label_set_text(online_label, "OFFLINE");
    lv_obj_set_style_text_font(online_label, font_sensor_tile_status(), 0);
    lv_obj_align(online_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    view->button = btn;
    view->group_label = group_label;
    view->online_label = online_label;
    style_sensor_tile(view, false);
    return btn;
}

static lv_obj_t *create_prediction_circle(lv_obj_t *parent)
{
    lv_obj_t *circle = lv_obj_create(parent);

    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 176, 176);
    lv_obj_set_align(circle, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_border_width(circle, 4, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_pad_all(circle, 12, 0);

    s_prediction_state_label = lv_label_create(circle);
    lv_label_set_text(s_prediction_state_label, "WAIT");
    lv_obj_set_style_text_font(s_prediction_state_label, font_prediction_state(), 0);
    lv_obj_set_style_text_color(s_prediction_state_label, lv_color_hex(0x334155), 0);
    lv_obj_align(s_prediction_state_label, LV_ALIGN_TOP_MID, 0, 28);

    s_prediction_confidence_label = lv_label_create(circle);
    lv_label_set_text(s_prediction_confidence_label, "--");
    lv_obj_set_style_text_font(s_prediction_confidence_label, font_prediction_confidence(), 0);
    lv_obj_set_style_text_color(s_prediction_confidence_label, lv_color_hex(0x334155), 0);
    lv_obj_align(s_prediction_confidence_label, LV_ALIGN_CENTER, 0, 6);

    s_prediction_hint_label = lv_label_create(circle);
    lv_label_set_text(s_prediction_hint_label, "WAIT 0/16");
    lv_obj_set_style_text_font(s_prediction_hint_label, font_prediction_hint(), 0);
    lv_obj_set_style_text_color(s_prediction_hint_label, lv_color_hex(0x475569), 0);
    lv_obj_align(s_prediction_hint_label, LV_ALIGN_BOTTOM_MID, 0, -24);

    return circle;
}

static lv_obj_t *create_home_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *content = lv_obj_create(page);

    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(page, 72, 0);

    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_align(content, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xFFF8F1), 0);

    s_prediction_circle = create_prediction_circle(content);
    create_sensor_tile(content, 0U, LV_ALIGN_TOP_RIGHT, 0, 0, &s_sensor_tiles[0]);
    create_sensor_tile(content, 1U, LV_ALIGN_TOP_LEFT, 0, 0, &s_sensor_tiles[1]);
    create_sensor_tile(content, 2U, LV_ALIGN_BOTTOM_RIGHT, 0, 0, &s_sensor_tiles[2]);
    create_sensor_tile(content, 3U, LV_ALIGN_BOTTOM_LEFT, 0, 0, &s_sensor_tiles[3]);
    lv_obj_move_foreground(s_prediction_circle);

    return page;
}

static lv_obj_t *create_settings_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *section = lv_obj_create(page);
    lv_obj_t *footer = lv_label_create(page);

    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(page, 88, 0);
    lv_obj_set_style_pad_bottom(page, 24, 0);
    lv_obj_set_style_pad_left(page, 18, 0);
    lv_obj_set_style_pad_right(page, 18, 0);
    lv_obj_set_style_pad_row(page, 14, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(page, LV_LAYOUT_FLEX);

    lv_obj_remove_style_all(section);
    lv_obj_set_width(section, lv_pct(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(section, 12, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);

    create_settings_tile_with_value(section, "Wi-Fi Status", "Connecting...", lv_color_hex(0x2563EB), &s_wifi_state_value);
    create_settings_tile_with_value(section, "Wi-Fi SSID", "--", lv_color_hex(0x0EA5E9), &s_wifi_ssid_value);
    create_settings_tile_with_value(section, "Cloud Link", "Waiting...", lv_color_hex(0x059669), &s_cloud_link_value);

    {
        lv_obj_t *action_row = lv_obj_create(section);
        lv_obj_t *btn = lv_button_create(action_row);
        lv_obj_t *btn_label = lv_label_create(btn);

        lv_obj_remove_style_all(action_row);
        lv_obj_set_width(action_row, lv_pct(100));
        lv_obj_set_height(action_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(action_row, LV_OPA_TRANSP, 0);

        lv_obj_set_size(btn, lv_pct(100), 48);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2563EB), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, wifi_reconnect_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_label_set_text(btn_label, "Reconnect Wi-Fi");
        lv_obj_set_style_text_font(btn_label, font_nav(), 0);
        lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(btn_label);
    }

    s_wifi_action_hint = footer;
    lv_label_set_text(footer, "Tap the button to reconnect the configured hotspot.");
    lv_obj_set_style_text_font(footer, font_metric_title(), 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x7C2D12), 0);

    return page;
}

static lv_obj_t *create_sensor_detail_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *header_row = lv_obj_create(page);
    lv_obj_t *metrics = lv_obj_create(page);

    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(page, 82, 0);
    lv_obj_set_style_pad_bottom(page, 18, 0);
    lv_obj_set_style_pad_left(page, 18, 0);
    lv_obj_set_style_pad_right(page, 18, 0);
    lv_obj_set_style_pad_row(page, 14, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(page, LV_LAYOUT_FLEX);

    lv_obj_remove_style_all(header_row);
    lv_obj_set_width(header_row, lv_pct(100));
    lv_obj_set_height(header_row, 32);
    lv_obj_set_style_bg_opa(header_row, LV_OPA_TRANSP, 0);

    s_detail_status_label = lv_label_create(header_row);
    lv_label_set_text(s_detail_status_label, "OFFLINE");
    lv_obj_set_style_text_font(s_detail_status_label, font_subtitle(), 0);
    lv_obj_set_style_text_color(s_detail_status_label, lv_color_hex(0x64748B), 0);
    lv_obj_align(s_detail_status_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_remove_style_all(metrics);
    lv_obj_set_width(metrics, lv_pct(100));
    lv_obj_set_height(metrics, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(metrics, 12, 0);
    lv_obj_set_style_pad_column(metrics, 12, 0);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_layout(metrics, LV_LAYOUT_FLEX);

    create_metric_tile(metrics, "Temperature", "--", lv_color_hex(0xEF4444), &s_detail_temperature);
    create_metric_tile(metrics, "Humidity", "--", lv_color_hex(0x0EA5E9), &s_detail_humidity);
    create_metric_tile(metrics, "Smoke", "--", lv_color_hex(0xF97316), &s_detail_smoke);
    create_metric_tile(metrics, "Light", "--", lv_color_hex(0xEAB308), &s_detail_light);

    return page;
}

static void refresh_prediction_circle(const sensor_snapshot_t *snapshot)
{
    char text[32];
    prediction_palette_t palette = resolve_prediction_palette(snapshot);
    bool threshold_active = false;
    bool warning_active = false;

    if ((s_prediction_circle == NULL) ||
        (s_prediction_state_label == NULL) ||
        (s_prediction_confidence_label == NULL) ||
        (s_prediction_hint_label == NULL)) {
        return;
    }

    lv_obj_set_style_bg_color(s_prediction_circle, palette.bg_color, 0);
    lv_obj_set_style_border_color(s_prediction_circle, palette.border_color, 0);
    lv_obj_set_style_text_color(s_prediction_state_label, palette.text_color, 0);
    lv_obj_set_style_text_color(s_prediction_confidence_label, palette.text_color, 0);
    lv_obj_set_style_text_color(s_prediction_hint_label, palette.text_color, 0);

    if (snapshot != NULL) {
        threshold_active = snapshot->threshold_state_valid &&
                           (strcmp(snapshot->threshold_state_label, "THRESHOLD_NORMAL") != 0);
        warning_active = snapshot->sequence_prediction_valid &&
                         (strcmp(snapshot->sequence_state_label, "STATE_NORMAL") != 0);
    }

    if ((snapshot == NULL) || !snapshot->sequence_ready) {
        lv_obj_align(s_prediction_state_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_confidence_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_hint_label, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_label_set_text(s_prediction_state_label, threshold_active ? threshold_state_summary(snapshot) : "NORMAL");
        if (threshold_active) {
            lv_label_set_text(s_prediction_confidence_label, "");
            lv_label_set_text(s_prediction_hint_label, "");
        } else {
            lv_label_set_text(s_prediction_confidence_label, "");
            lv_snprintf(text, sizeof(text), "WAIT %u/16", snapshot != NULL ? (unsigned int)snapshot->sequence_length : 0U);
            lv_label_set_text(s_prediction_hint_label, text);
        }
        return;
    }

    if (!threshold_active && !warning_active) {
        lv_obj_align(s_prediction_state_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_confidence_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_hint_label, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_label_set_text(s_prediction_state_label, "NORMAL");
        lv_label_set_text(s_prediction_confidence_label, "");
        lv_label_set_text(s_prediction_hint_label, "");
        return;
    }

    if (threshold_active && !warning_active) {
        lv_obj_align(s_prediction_state_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_confidence_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_hint_label, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_label_set_text(s_prediction_state_label, threshold_state_summary(snapshot));
        lv_label_set_text(s_prediction_confidence_label, "");
        lv_label_set_text(s_prediction_hint_label, "");
        return;
    }

    if (!threshold_active && warning_active) {
        lv_obj_align(s_prediction_state_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_confidence_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(s_prediction_hint_label, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_label_set_text(s_prediction_state_label, sequence_state_summary(snapshot));
        lv_label_set_text(s_prediction_confidence_label, "");
        lv_label_set_text(s_prediction_hint_label, "");
        return;
    }

    lv_obj_align(s_prediction_state_label, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_align(s_prediction_confidence_label, LV_ALIGN_CENTER, 0, 6);
    lv_obj_align(s_prediction_hint_label, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_label_set_text(s_prediction_state_label, threshold_state_summary(snapshot));
    lv_label_set_text(s_prediction_confidence_label, sequence_state_summary(snapshot));
    lv_label_set_text(s_prediction_hint_label, "");
}

static void refresh_sensor_tiles(const sensor_snapshot_t *snapshot)
{
    for (uint32_t i = 0; i < CARD_COUNT; ++i) {
        const sensor_group_data_t *group = NULL;

        if ((snapshot == NULL) || (s_sensor_tiles[i].button == NULL)) {
            continue;
        }

        group = &snapshot->groups[i];
        style_sensor_tile(&s_sensor_tiles[i], group->online);
        lv_label_set_text(s_sensor_tiles[i].online_label, group->online ? "ONLINE" : "OFFLINE");
    }
}

void ui_dashboard_create(lv_display_t *display)
{
    s_screen = lv_obj_create(NULL);
    s_top_bar = lv_obj_create(s_screen);
    s_title_label = lv_label_create(s_top_bar);
    s_subtitle_label = lv_label_create(s_top_bar);
    s_top_back_btn = lv_button_create(s_top_bar);
    s_top_back_label = lv_label_create(s_top_back_btn);
    s_nav_row = lv_obj_create(s_top_bar);

    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFF8F1), 0);

    lv_obj_remove_style_all(s_top_bar);
    lv_obj_set_width(s_top_bar, lv_pct(100));
    lv_obj_set_height(s_top_bar, 72);
    lv_obj_set_style_bg_opa(s_top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_top_bar, lv_color_hex(0x7C2D12), 0);
    lv_obj_set_style_pad_hor(s_top_bar, 18, 0);
    lv_obj_set_style_pad_ver(s_top_bar, 10, 0);

    lv_label_set_text(s_title_label, "Lab Monitor");
    lv_obj_set_style_text_font(s_title_label, font_title(), 0);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 0, -10);

    lv_label_set_text(s_subtitle_label, "");
    lv_obj_set_style_text_font(s_subtitle_label, font_subtitle(), 0);
    lv_obj_set_style_text_color(s_subtitle_label, lv_color_hex(0xFED7AA), 0);
    lv_obj_align(s_subtitle_label, LV_ALIGN_LEFT_MID, 0, 12);

    lv_obj_set_size(s_top_back_btn, 48, 48);
    lv_obj_set_style_bg_color(s_top_back_btn, lv_color_hex(0xB45309), 0);
    lv_obj_set_style_bg_opa(s_top_back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_top_back_btn, 24, 0);
    lv_obj_set_style_border_width(s_top_back_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_top_back_btn, 0, 0);
    lv_obj_align(s_top_back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(s_top_back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_top_back_btn, back_to_home_event_cb, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(s_top_back_label, "<");
    lv_obj_set_style_text_font(s_top_back_label, font_prediction_state(), 0);
    lv_obj_set_style_text_color(s_top_back_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_top_back_label);

    lv_obj_remove_style_all(s_nav_row);
    lv_obj_set_size(s_nav_row, 230, 42);
    lv_obj_set_style_bg_opa(s_nav_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_column(s_nav_row, 10, 0);
    lv_obj_set_flex_flow(s_nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_layout(s_nav_row, LV_LAYOUT_FLEX);
    lv_obj_align(s_nav_row, LV_ALIGN_RIGHT_MID, 0, 0);

    s_nav_home_btn = lv_button_create(s_nav_row);
    {
        lv_obj_t *label = lv_label_create(s_nav_home_btn);
        lv_obj_set_size(s_nav_home_btn, 108, 42);
        lv_obj_set_style_shadow_width(s_nav_home_btn, 0, 0);
        lv_obj_set_style_pad_all(s_nav_home_btn, 0, 0);
        lv_obj_add_event_cb(s_nav_home_btn, nav_btn_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)PAGE_HOME);
        lv_label_set_text(label, "Home");
        lv_obj_set_style_text_font(label, font_nav(), 0);
        lv_obj_center(label);
    }

    s_nav_settings_btn = lv_button_create(s_nav_row);
    {
        lv_obj_t *label = lv_label_create(s_nav_settings_btn);
        lv_obj_set_size(s_nav_settings_btn, 108, 42);
        lv_obj_set_style_shadow_width(s_nav_settings_btn, 0, 0);
        lv_obj_set_style_pad_all(s_nav_settings_btn, 0, 0);
        lv_obj_add_event_cb(s_nav_settings_btn, nav_btn_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)PAGE_SETTINGS);
        lv_label_set_text(label, "Settings");
        lv_obj_set_style_text_font(label, font_nav(), 0);
        lv_obj_center(label);
    }

    s_home_page = create_home_page(s_screen);
    s_settings_page = create_settings_page(s_screen);
    s_sensor_detail_page = create_sensor_detail_page(s_screen);

    lv_obj_move_foreground(s_top_bar);
    lv_obj_move_foreground(s_nav_row);
    show_page(PAGE_HOME);
    lv_screen_load(s_screen);
    (void)display;
}

void ui_dashboard_update(const sensor_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    s_last_snapshot = *snapshot;
    s_has_snapshot = true;

    refresh_sensor_tiles(snapshot);
    refresh_prediction_circle(snapshot);
    update_sensor_detail_page(snapshot);
}

void ui_dashboard_update_wifi(bool connected, const char *ssid, const char *ip)
{
    (void)ip;

    if (s_wifi_state_value != NULL) {
        lv_label_set_text(s_wifi_state_value, connected ? "Connected" : "Disconnected");
        lv_obj_set_style_text_color(s_wifi_state_value,
                                    connected ? lv_color_hex(0x166534) : lv_color_hex(0xB91C1C),
                                    0);
    }
    if (s_wifi_ssid_value != NULL) {
        lv_label_set_text(s_wifi_ssid_value, (ssid != NULL) ? ssid : "--");
    }
    if (s_wifi_action_hint != NULL) {
        lv_label_set_text(s_wifi_action_hint,
                          connected ? "Wi-Fi connected. Device can reach the cloud service."
                                    : "Wi-Fi disconnected. Tap the button to reconnect.");
    }
}

void ui_dashboard_update_cloud(bool connected)
{
    if (s_cloud_link_value != NULL) {
        lv_label_set_text(s_cloud_link_value, connected ? "Online" : "Offline");
        lv_obj_set_style_text_color(s_cloud_link_value,
                                    connected ? lv_color_hex(0x166534) : lv_color_hex(0xB91C1C),
                                    0);
    }
}

void ui_dashboard_touch_debug_update(bool pressed, uint16_t x, uint16_t y)
{
    (void)pressed;
    (void)x;
    (void)y;
}
