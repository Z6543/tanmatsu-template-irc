#include "irc_menu.h"
#include "irc_settings.h"
#include "bsp_lvgl.h"
#include "esp_log.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "irc-menu";

static irc_config_t          menu_config;
static irc_menu_connect_cb_t connect_cb;

static lv_obj_t* ta_server;
static lv_obj_t* ta_port;
static lv_obj_t* ta_nickname;
static lv_obj_t* ta_channel;
static lv_obj_t* sw_tls;
static lv_obj_t* btn_connect;
static lv_obj_t* menu_screen;

static lv_obj_t* create_field_row(lv_obj_t* parent,
                                  const char* label_text,
                                  lv_obj_t** out_input,
                                  bool one_line) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row, 8, 0);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_set_width(label, 120);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, one_line);
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_set_height(ta, LV_SIZE_CONTENT);

    *out_input = ta;
    return row;
}

static void populate_fields(void) {
    lv_textarea_set_text(ta_server, menu_config.server);

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", menu_config.port);
    lv_textarea_set_text(ta_port, port_str);

    lv_textarea_set_text(ta_nickname, menu_config.nickname);
    lv_textarea_set_text(ta_channel, menu_config.channel);

    if (menu_config.use_tls) {
        lv_obj_add_state(sw_tls, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(sw_tls, LV_STATE_CHECKED);
    }
}

static void read_fields(void) {
    strncpy(menu_config.server, lv_textarea_get_text(ta_server),
            sizeof(menu_config.server) - 1);

    const char* port_text = lv_textarea_get_text(ta_port);
    int port = atoi(port_text);
    if (port > 0 && port <= 65535) {
        menu_config.port = (uint16_t)port;
    }

    strncpy(menu_config.nickname, lv_textarea_get_text(ta_nickname),
            sizeof(menu_config.nickname) - 1);
    strncpy(menu_config.channel, lv_textarea_get_text(ta_channel),
            sizeof(menu_config.channel) - 1);

    menu_config.use_tls =
        lv_obj_has_state(sw_tls, LV_STATE_CHECKED);
}

static void on_connect_clicked(lv_event_t* e) {
    (void)e;
    read_fields();
    irc_settings_save(&menu_config);
    ESP_LOGI(TAG, "Connect: %s:%d %s %s (TLS=%d)",
             menu_config.server, menu_config.port,
             menu_config.nickname, menu_config.channel,
             menu_config.use_tls);

    if (connect_cb) {
        connect_cb(&menu_config);
    }
}

void irc_menu_init(irc_menu_connect_cb_t on_connect) {
    connect_cb = on_connect;
    irc_settings_load(&menu_config);

    menu_screen = lv_screen_active();
    lv_obj_set_flex_flow(menu_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(menu_screen, 8, 0);
    lv_obj_set_style_pad_gap(menu_screen, 6, 0);

    // Title
    lv_obj_t* title = lv_label_create(menu_screen);
    lv_label_set_text(title, "IRC Connection");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Scrollable form container
    lv_obj_t* form = lv_obj_create(menu_screen);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(form, 1);
    lv_obj_set_width(form, lv_pct(100));
    lv_obj_set_style_pad_all(form, 8, 0);
    lv_obj_set_style_pad_gap(form, 6, 0);
    lv_obj_set_style_border_width(form, 0, 0);
    lv_obj_add_flag(form, LV_OBJ_FLAG_SCROLLABLE);

    // Fields
    create_field_row(form, "Server:", &ta_server, true);
    create_field_row(form, "Port:", &ta_port, true);
    lv_textarea_set_accepted_chars(ta_port, "0123456789");
    lv_textarea_set_max_length(ta_port, 5);

    create_field_row(form, "Nickname:", &ta_nickname, true);
    create_field_row(form, "Channel:", &ta_channel, true);

    // TLS toggle row
    lv_obj_t* tls_row = lv_obj_create(form);
    lv_obj_remove_style_all(tls_row);
    lv_obj_set_width(tls_row, lv_pct(100));
    lv_obj_set_height(tls_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tls_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tls_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(tls_row, 8, 0);

    lv_obj_t* tls_label = lv_label_create(tls_row);
    lv_label_set_text(tls_label, "Use TLS:");
    lv_obj_set_width(tls_label, 120);
    lv_obj_set_style_text_align(tls_label, LV_TEXT_ALIGN_RIGHT, 0);

    sw_tls = lv_switch_create(tls_row);

    // Connect button
    btn_connect = lv_button_create(menu_screen);
    lv_obj_set_width(btn_connect, lv_pct(100));
    lv_obj_set_height(btn_connect, 44);
    lv_obj_set_style_bg_color(btn_connect,
                              lv_color_hex(0x2196F3), 0);

    lv_obj_t* btn_label = lv_label_create(btn_connect);
    lv_label_set_text(btn_label, "Connect");
    lv_obj_center(btn_label);
    lv_obj_set_style_text_color(btn_label,
                                lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(btn_connect, on_connect_clicked,
                        LV_EVENT_CLICKED, NULL);

    populate_fields();

    // Add all focusable widgets to the default group
    lv_group_t* group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, ta_server);
        lv_group_add_obj(group, ta_port);
        lv_group_add_obj(group, ta_nickname);
        lv_group_add_obj(group, ta_channel);
        lv_group_add_obj(group, sw_tls);
        lv_group_add_obj(group, btn_connect);
        lv_group_focus_obj(ta_server);
    }

    ESP_LOGI(TAG, "Menu initialized");
}
