#include "ui_dashboard.h"

#include <stdio.h>

#define CARD_COUNT               SENSOR_GROUP_COUNT

typedef struct {
    lv_obj_t *card;
    lv_obj_t *group_label;
    lv_obj_t *online_label;
    lv_obj_t *temperature_value;
    lv_obj_t *humidity_value;
    lv_obj_t *smoke_value;
    lv_obj_t *light_value;
} group_card_view_t;

typedef enum {
    PAGE_OVERVIEW = 0,
    PAGE_SETTINGS,
} dashboard_page_t;

static lv_obj_t *s_status_label;
static lv_obj_t *s_timestamp_label;
static lv_obj_t *s_screen;
static lv_obj_t *s_overview_page;
static lv_obj_t *s_settings_page;
static lv_obj_t *s_nav_overview_btn;
static lv_obj_t *s_nav_settings_btn;
static group_card_view_t s_group_views[CARD_COUNT];
static dashboard_page_t s_current_page;

static const lv_font_t *font_title(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_card_heading(void)
{
    return &lv_font_montserrat_16;
}

static const lv_font_t *font_metric_value(void)
{
    return &lv_font_montserrat_20;
}

static const lv_font_t *font_metric_name(void)
{
    return &lv_font_montserrat_16;
}

static const lv_font_t *font_status(void)
{
    return &lv_font_montserrat_14;
}

static const lv_font_t *font_nav(void)
{
    return &lv_font_montserrat_16;
}

static const lv_font_t *font_settings_title(void)
{
    return &lv_font_montserrat_24;
}

static const lv_font_t *font_settings_value(void)
{
    return &lv_font_montserrat_20;
}

static void update_nav_style(lv_obj_t *btn, bool active)
{
    lv_obj_set_style_bg_color(btn, active ? lv_color_hex(0xFDBA74) : lv_color_hex(0x9A3412), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_text_color(btn, active ? lv_color_hex(0x431407) : lv_color_hex(0xFFF7ED), 0);
}

static void show_page(dashboard_page_t page)
{
    s_current_page = page;
    if (s_overview_page != NULL) {
        if (page == PAGE_OVERVIEW) {
            lv_obj_clear_flag(s_overview_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_overview_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_settings_page != NULL) {
        if (page == PAGE_SETTINGS) {
            lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_nav_overview_btn != NULL) {
        update_nav_style(s_nav_overview_btn, page == PAGE_OVERVIEW);
    }
    if (s_nav_settings_btn != NULL) {
        update_nav_style(s_nav_settings_btn, page == PAGE_SETTINGS);
    }
}

static lv_obj_t *create_settings_tile(lv_obj_t *parent, const char *title, const char *value_text, lv_color_t accent)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_t *title_label = lv_label_create(tile);
    lv_obj_t *value_label = lv_label_create(tile);

    lv_obj_remove_style_all(tile);
    lv_obj_set_width(tile, lv_pct(100));
    lv_obj_set_height(tile, 86);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFF7ED), 0);
    lv_obj_set_style_radius(tile, 18, 0);
    lv_obj_set_style_pad_hor(tile, 16, 0);
    lv_obj_set_style_pad_ver(tile, 14, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, accent, 0);

    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, font_metric_name(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x7C2D12), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_label_set_text(value_label, value_text);
    lv_obj_set_style_text_font(value_label, font_settings_value(), 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x431407), 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return tile;
}

static void nav_btn_event_cb(lv_event_t *e)
{
    dashboard_page_t page = (dashboard_page_t)(uintptr_t)lv_event_get_user_data(e);
    show_page(page);
}

static void set_card_online_style(group_card_view_t *view, bool online)
{
    lv_color_t bg_color = online ? lv_color_hex(0xFFF7ED) : lv_color_hex(0xE5E7EB);
    lv_color_t title_color = online ? lv_color_hex(0x9A3412) : lv_color_hex(0x6B7280);
    lv_color_t value_color = online ? lv_color_hex(0x431407) : lv_color_hex(0x4B5563);

    lv_obj_set_style_bg_color(view->card, bg_color, 0);
    lv_obj_set_style_text_color(view->group_label, title_color, 0);
    lv_obj_set_style_text_color(view->online_label, online ? lv_color_hex(0x166534) : lv_color_hex(0x6B7280), 0);
    lv_obj_set_style_text_color(view->temperature_value, value_color, 0);
    lv_obj_set_style_text_color(view->humidity_value, value_color, 0);
    lv_obj_set_style_text_color(view->smoke_value, value_color, 0);
    lv_obj_set_style_text_color(view->light_value, value_color, 0);
}

static lv_obj_t *create_metric_line(lv_obj_t *parent, const char *name, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *name_label = lv_label_create(row);
    lv_obj_t *value = lv_label_create(row);

    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);

    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0x7C2D12), 0);
    lv_obj_set_style_text_font(name_label, font_metric_name(), 0);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, lv_color_hex(0x431407), 0);
    lv_obj_set_style_text_font(value, font_metric_value(), 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

    *value_label = value;
    return row;
}

static lv_obj_t *create_group_card(lv_obj_t *parent, uint32_t group_index, group_card_view_t *view)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_t *metrics = lv_obj_create(card);
    char group_text[8];

    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFF7ED), 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0xC2410C), 0);
    lv_obj_set_style_shadow_width(card, 14, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);

    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);

    lv_obj_remove_style_all(metrics);
    lv_obj_set_width(metrics, lv_pct(100));
    lv_obj_set_height(metrics, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(metrics, 6, 0);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(metrics, LV_LAYOUT_FLEX);

    view->group_label = lv_label_create(header);
    lv_snprintf(group_text, sizeof(group_text), "G%u", (unsigned int)(group_index + 1U));
    lv_label_set_text(view->group_label, group_text);
    lv_obj_set_style_text_color(view->group_label, lv_color_hex(0x9A3412), 0);
    lv_obj_set_style_text_font(view->group_label, font_card_heading(), 0);

    view->online_label = lv_label_create(header);
    lv_label_set_text(view->online_label, "OFFLINE");
    lv_obj_set_style_text_color(view->online_label, lv_color_hex(0x6B7280), 0);
    lv_obj_set_style_text_font(view->online_label, font_metric_name(), 0);
    lv_obj_align(view->online_label, LV_ALIGN_RIGHT_MID, 0, 0);

    create_metric_line(metrics, "Temperature", &view->temperature_value);
    create_metric_line(metrics, "Humidity", &view->humidity_value);
    create_metric_line(metrics, "Smoke", &view->smoke_value);
    create_metric_line(metrics, "Light", &view->light_value);

    view->card = card;
    set_card_online_style(view, false);
    return card;
}

static lv_obj_t *create_nav_button(lv_obj_t *parent, const char *text, dashboard_page_t page)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_size(btn, 144, 42);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, nav_btn_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)page);
    update_nav_style(btn, false);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font_nav(), 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_settings_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *title = lv_label_create(page);
    lv_obj_t *hint = lv_label_create(page);
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

    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, font_settings_title(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7C2D12), 0);

    lv_label_set_text(hint, "Current device configuration overview");
    lv_obj_set_style_text_font(hint, font_card_heading(), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9A3412), 0);

    lv_obj_remove_style_all(section);
    lv_obj_set_width(section, lv_pct(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(section, 12, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);

    create_settings_tile(section, "UART Link", "USART1 -> ESP32", lv_color_hex(0xF97316));
    create_settings_tile(section, "Display", "ST7796 8-bit / 480x320", lv_color_hex(0x0EA5E9));
    create_settings_tile(section, "Touch", "FT6336 enabled", lv_color_hex(0x22C55E));
    create_settings_tile(section, "Alarm Engine", "Handled by ESP32", lv_color_hex(0xA855F7));

    lv_label_set_text(footer, "Use this page as the device settings summary.");
    lv_obj_set_style_text_font(footer, font_metric_name(), 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x7C2D12), 0);

    return page;
}

void ui_dashboard_create(lv_display_t *display)
{
    lv_obj_t *top_bar;
    lv_obj_t *title_label;
    lv_obj_t *nav_row;
    static int32_t column_desc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_desc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    s_screen = lv_obj_create(NULL);
    top_bar = lv_obj_create(s_screen);
    nav_row = lv_obj_create(top_bar);
    s_overview_page = lv_obj_create(s_screen);

    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFF8F1), 0);

    lv_obj_remove_style_all(top_bar);
    lv_obj_set_width(top_bar, lv_pct(100));
    lv_obj_set_height(top_bar, 64);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x7C2D12), 0);
    lv_obj_set_style_pad_hor(top_bar, 18, 0);
    lv_obj_set_style_pad_ver(top_bar, 10, 0);

    title_label = lv_label_create(top_bar);

    lv_label_set_text(title_label, "");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, font_title(), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_status_label = lv_label_create(top_bar);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_text_font(s_status_label, font_status(), 0);
    lv_obj_align(s_status_label, LV_ALIGN_RIGHT_MID, 0, -8);

    s_timestamp_label = lv_label_create(top_bar);
    lv_label_set_text(s_timestamp_label, "");
    lv_obj_set_style_text_color(s_timestamp_label, lv_color_hex(0xFED7AA), 0);
    lv_obj_set_style_text_font(s_timestamp_label, font_status(), 0);
    lv_obj_align(s_timestamp_label, LV_ALIGN_RIGHT_MID, 0, 10);

    lv_obj_remove_style_all(nav_row);
    lv_obj_set_size(nav_row, 300, 42);
    lv_obj_set_style_bg_opa(nav_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_column(nav_row, 10, 0);
    lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_layout(nav_row, LV_LAYOUT_FLEX);
    lv_obj_align(nav_row, LV_ALIGN_CENTER, 0, 0);

    s_nav_overview_btn = create_nav_button(nav_row, "Overview", PAGE_OVERVIEW);
    s_nav_settings_btn = create_nav_button(nav_row, "Settings", PAGE_SETTINGS);

    lv_obj_remove_style_all(s_overview_page);
    lv_obj_set_size(s_overview_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_overview_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(s_overview_page, 82, 0);
    lv_obj_set_style_pad_bottom(s_overview_page, 14, 0);
    lv_obj_set_style_pad_left(s_overview_page, 14, 0);
    lv_obj_set_style_pad_right(s_overview_page, 14, 0);
    lv_obj_set_style_pad_row(s_overview_page, 12, 0);
    lv_obj_set_style_pad_column(s_overview_page, 12, 0);
    lv_obj_set_layout(s_overview_page, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(s_overview_page, column_desc, row_desc);

    for (uint32_t i = 0; i < CARD_COUNT; ++i) {
        lv_obj_t *card = create_group_card(s_overview_page, i, &s_group_views[i]);
        lv_obj_set_grid_cell(card,
                             LV_GRID_ALIGN_STRETCH,
                             (int32_t)(i % 2U),
                             1,
                             LV_GRID_ALIGN_STRETCH,
                             (int32_t)(i / 2U),
                             1);
    }

    s_settings_page = create_settings_page(s_screen);
    /* Keep the top navigation bar above the full-screen pages so touch can reach the buttons. */
    lv_obj_move_foreground(top_bar);
    lv_obj_move_foreground(nav_row);
    show_page(PAGE_OVERVIEW);
    lv_screen_load(s_screen);
    (void)display;
}

void ui_dashboard_update(const sensor_snapshot_t *snapshot)
{
    char text[32];

    if (snapshot == NULL) {
        return;
    }

    if (snapshot->link_online) {
        lv_label_set_text(s_status_label, "");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xBBF7D0), 0);
        if (snapshot->last_rx_tick == 0U) {
            lv_label_set_text(s_timestamp_label, "");
        } else {
            lv_label_set_text(s_timestamp_label, "");
        }
    } else {
        lv_label_set_text(s_status_label, "");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFDE68A), 0);
        lv_label_set_text(s_timestamp_label, "");
    }

    for (uint32_t i = 0; i < CARD_COUNT; ++i) {
        const sensor_group_data_t *group = &snapshot->groups[i];
        group_card_view_t *view = &s_group_views[i];

        if (group->online) {
            lv_label_set_text(view->online_label, "ONLINE");
            lv_obj_set_style_text_color(view->online_label, lv_color_hex(0x166534), 0);
            snprintf(text, sizeof(text), "%.1f C", (double)group->temperature);
            lv_label_set_text(view->temperature_value, text);
            snprintf(text, sizeof(text), "%.1f %%RH", (double)group->humidity);
            lv_label_set_text(view->humidity_value, text);
            snprintf(text, sizeof(text), "%.1f %%", (double)group->smoke);
            lv_label_set_text(view->smoke_value, text);
            snprintf(text, sizeof(text), "%.1f %%", (double)group->light);
            lv_label_set_text(view->light_value, text);
            set_card_online_style(view, true);
        } else {
            lv_label_set_text(view->online_label, "OFFLINE");
            lv_label_set_text(view->temperature_value, "--");
            lv_label_set_text(view->humidity_value, "--");
            lv_label_set_text(view->smoke_value, "--");
            lv_label_set_text(view->light_value, "--");
            set_card_online_style(view, false);
        }
    }
}

void ui_dashboard_touch_debug_update(bool pressed, uint16_t x, uint16_t y)
{
    (void)pressed;
    (void)x;
    (void)y;
}
