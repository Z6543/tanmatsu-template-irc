#include "irc_ui.h"
#include "irc_client.h"
#include "bsp_lvgl.h"
#include "esp_log.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "irc-ui";

#define MAX_CHANNELS 8
#define MAX_MESSAGES 100

typedef struct {
    char name[64];
    lv_obj_t* msg_container;
    lv_obj_t* tab_btn;
    int msg_count;
} channel_window_t;

static channel_window_t channels[MAX_CHANNELS];
static int num_channels;
static int active_idx = -1;

static lv_obj_t* tab_bar;
static lv_obj_t* status_label;
static lv_obj_t* server_label;
static lv_obj_t* msg_panel;
static lv_obj_t* input_textarea;
static lv_obj_t* spinner_overlay;

static irc_ui_back_cb_t back_callback;

static int find_channel(const char* name) {
    for (int i = 0; i < num_channels; i++) {
        if (strcasecmp(channels[i].name, name) == 0) return i;
    }
    return -1;
}

static void switch_to_channel(int idx) {
    if (idx < 0 || idx >= num_channels) return;
    if (idx == active_idx) return;

    if (active_idx >= 0 && channels[active_idx].msg_container) {
        lv_obj_add_flag(channels[active_idx].msg_container,
                        LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(channels[active_idx].tab_btn,
                            LV_STATE_CHECKED);
    }

    active_idx = idx;
    lv_obj_remove_flag(channels[idx].msg_container,
                       LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(channels[idx].tab_btn, LV_STATE_CHECKED);

    char status[128];
    snprintf(status, sizeof(status), "%s", channels[idx].name);
    lv_label_set_text(status_label, status);

    lv_obj_scroll_to_y(channels[idx].msg_container,
                       LV_COORD_MAX, LV_ANIM_OFF);
}

static void on_tab_clicked(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    for (int i = 0; i < num_channels; i++) {
        if (channels[i].tab_btn == btn) {
            switch_to_channel(i);
            // Refocus input after tab click
            lv_group_focus_obj(input_textarea);
            return;
        }
    }
}

static lv_obj_t* create_msg_container(void) {
    lv_obj_t* cont = lv_obj_create(msg_panel);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_style_pad_gap(cont, 2, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

static void prune_messages(channel_window_t* ch) {
    while (ch->msg_count > MAX_MESSAGES) {
        lv_obj_t* first = lv_obj_get_child(ch->msg_container, 0);
        if (first) {
            lv_obj_delete(first);
            ch->msg_count--;
        } else {
            break;
        }
    }
}

static void add_msg_to_channel(int idx, const char* nick,
                               const char* text) {
    if (idx < 0 || idx >= num_channels) return;
    channel_window_t* ch = &channels[idx];

    char line[576];
    if (nick && nick[0] != '\0') {
        snprintf(line, sizeof(line), "<%s> %s", nick, text);
    } else {
        snprintf(line, sizeof(line), "* %s", text);
    }

    lv_obj_t* label = lv_label_create(ch->msg_container);
    lv_label_set_text(label, line);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    ch->msg_count++;

    prune_messages(ch);

    if (idx == active_idx) {
        lv_obj_scroll_to_y(ch->msg_container,
                           LV_COORD_MAX, LV_ANIM_ON);
    }
}

// Async call data structures
typedef struct {
    char channel[64];
    char nick[32];
    char text[512];
} pending_msg_t;

typedef struct {
    char text[128];
} pending_text_t;

typedef struct {
    char name[64];
} pending_channel_t;

static void add_message_async(void* data) {
    pending_msg_t* msg = (pending_msg_t*)data;
    int idx = find_channel(msg->channel);
    if (idx >= 0) {
        add_msg_to_channel(idx, msg->nick, msg->text);
    }
    free(msg);
}

static void add_server_message_async(void* data) {
    pending_text_t* t = (pending_text_t*)data;
    lv_label_set_text(server_label, t->text);
    // Also add to "status" channel (index 0) if it exists
    if (num_channels > 0) {
        add_msg_to_channel(0, NULL, t->text);
    }
    free(t);
}

static void set_status_async(void* data) {
    pending_text_t* s = (pending_text_t*)data;
    lv_label_set_text(status_label, s->text);
    if (spinner_overlay) {
        lv_obj_t* lbl = lv_obj_get_child(spinner_overlay, 1);
        if (lbl) lv_label_set_text(lbl, s->text);
    }
    free(s);
}

static void hide_spinner_async(void* data) {
    (void)data;
    if (spinner_overlay) {
        lv_obj_delete(spinner_overlay);
        spinner_overlay = NULL;
    }
}

static void add_channel_async(void* data) {
    pending_channel_t* pc = (pending_channel_t*)data;

    if (find_channel(pc->name) >= 0 || num_channels >= MAX_CHANNELS) {
        free(pc);
        return;
    }

    int idx = num_channels;
    channel_window_t* ch = &channels[idx];
    strncpy(ch->name, pc->name, sizeof(ch->name) - 1);
    ch->msg_count = 0;

    ch->msg_container = create_msg_container();

    ch->tab_btn = lv_button_create(tab_bar);
    lv_obj_set_height(ch->tab_btn, 28);
    lv_obj_set_style_pad_hor(ch->tab_btn, 8, 0);
    lv_obj_set_style_pad_ver(ch->tab_btn, 2, 0);
    lv_obj_set_style_min_width(ch->tab_btn, 60, 0);
    lv_obj_add_flag(ch->tab_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(ch->tab_btn, on_tab_clicked,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(ch->tab_btn);
    lv_label_set_text(lbl, ch->name);
    lv_obj_center(lbl);

    num_channels++;

    // Auto-switch to the new channel if it's the first real one
    // (idx 0 is "status", switch on first join)
    if (idx == 1 || active_idx < 0) {
        switch_to_channel(idx);
    }

    ESP_LOGI(TAG, "Added channel tab: %s (idx=%d)", ch->name, idx);
    free(pc);
}

static void remove_channel_async(void* data) {
    pending_channel_t* pc = (pending_channel_t*)data;
    int idx = find_channel(pc->name);
    if (idx <= 0) {  // Don't remove "status" (idx 0)
        free(pc);
        return;
    }

    // Delete UI objects
    lv_obj_delete(channels[idx].msg_container);
    lv_obj_delete(channels[idx].tab_btn);

    // Shift remaining channels down
    for (int i = idx; i < num_channels - 1; i++) {
        channels[i] = channels[i + 1];
    }
    num_channels--;
    memset(&channels[num_channels], 0, sizeof(channel_window_t));

    // Fix active index
    if (active_idx == idx) {
        active_idx = -1;
        switch_to_channel(num_channels > 1 ? 1 : 0);
    } else if (active_idx > idx) {
        active_idx--;
    }

    free(pc);
}

static void on_key(lv_event_t* e) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_HOME && back_callback) {
        ESP_LOGI(TAG, "F1 pressed, returning to menu");
        back_callback();
    }
}

static void on_input_ready(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    const char* text = lv_textarea_get_text(ta);

    if (!text || text[0] == '\0') return;

    char msg_copy[512];
    strncpy(msg_copy, text, sizeof(msg_copy) - 1);
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    lv_textarea_set_text(ta, "");

    if (msg_copy[0] == '/') {
        const char* cmd = msg_copy + 1;
        ESP_LOGI(TAG, "Sending raw command: %s", cmd);

        // Handle /join locally to add channel tab
        if (strncasecmp(cmd, "join ", 5) == 0) {
            const char* chan = cmd + 5;
            while (*chan == ' ') chan++;
            irc_client_send_raw_cmd(cmd);
            return;
        }

        esp_err_t err = irc_client_send_raw_cmd(cmd);
        if (err != ESP_OK) {
            irc_ui_add_server_message("Failed to send command");
        }
        return;
    }

    const char* active = irc_ui_get_active_channel();
    if (active && active[0] == '#') {
        esp_err_t err = irc_client_send_message(active, msg_copy);
        if (err == ESP_OK) {
            const char* nick = irc_client_get_nickname();
            irc_ui_add_message(active, nick ? nick : "me",
                               msg_copy);
        } else {
            irc_ui_add_server_message("Failed to send message");
        }
    }
}

void irc_ui_init(lv_obj_t* screen, irc_ui_back_cb_t back_cb) {
    back_callback = back_cb;
    num_channels = 0;
    active_idx = -1;
    memset(channels, 0, sizeof(channels));
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 2, 0);
    lv_obj_set_style_pad_gap(screen, 2, 0);

    // Channel tab bar (horizontal scrollable)
    tab_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(tab_bar);
    lv_obj_set_width(tab_bar, lv_pct(100));
    lv_obj_set_height(tab_bar, 32);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab_bar, 4, 0);
    lv_obj_set_style_pad_all(tab_bar, 2, 0);
    lv_obj_add_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tab_bar, LV_DIR_HOR);
    lv_obj_remove_flag(tab_bar, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // Status line (channel name / connection status)
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Connecting...");
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_bg_color(status_label,
                              lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(status_label,
                                lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(status_label, 3, 0);
    lv_obj_set_height(status_label, 24);

    // Message panel (holds all channel containers stacked)
    msg_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(msg_panel);
    lv_obj_set_flex_grow(msg_panel, 1);
    lv_obj_set_width(msg_panel, lv_pct(100));
    lv_obj_set_style_border_width(msg_panel, 1, 0);
    lv_obj_set_style_border_color(msg_panel,
                                  lv_color_hex(0x888888), 0);
    lv_obj_remove_flag(msg_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Spinner overlay (shown during connection)
    spinner_overlay = lv_obj_create(msg_panel);
    lv_obj_remove_style_all(spinner_overlay);
    lv_obj_set_size(spinner_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(spinner_overlay,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(spinner_overlay, LV_OPA_70, 0);
    lv_obj_set_flex_flow(spinner_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(spinner_overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(spinner_overlay, 12, 0);
    lv_obj_remove_flag(spinner_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* spinner = lv_spinner_create(spinner_overlay);
    lv_spinner_set_anim_params(spinner, 1000, 270);
    lv_obj_set_size(spinner, 48, 48);

    lv_obj_t* spin_label = lv_label_create(spinner_overlay);
    lv_label_set_text(spin_label, "Connecting...");
    lv_obj_set_style_text_color(spin_label,
                                lv_color_hex(0xCCCCCC), 0);

    // Server message line
    server_label = lv_label_create(screen);
    lv_label_set_text(server_label, "");
    lv_obj_set_width(server_label, lv_pct(100));
    lv_label_set_long_mode(server_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_bg_color(server_label,
                              lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(server_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(server_label,
                                lv_color_hex(0x88CCFF), 0);
    lv_obj_set_style_pad_all(server_label, 3, 0);
    lv_obj_set_height(server_label, 22);

    // Text input
    input_textarea = lv_textarea_create(screen);
    lv_textarea_set_one_line(input_textarea, true);
    lv_textarea_set_placeholder_text(input_textarea,
                                     "Type message...");
    lv_obj_set_width(input_textarea, lv_pct(100));
    lv_obj_set_height(input_textarea, 36);
    lv_obj_add_event_cb(input_textarea, on_input_ready,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(input_textarea, on_key,
                        LV_EVENT_KEY, NULL);

    lv_group_t* group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, input_textarea);
        lv_group_focus_obj(input_textarea);
    }

    // Create "status" channel as first tab
    irc_ui_add_channel("status");

    ESP_LOGI(TAG, "IRC UI initialized (multi-channel)");
}

void irc_ui_add_message(const char* channel,
                        const char* nick, const char* text) {
    pending_msg_t* msg = calloc(1, sizeof(pending_msg_t));
    if (!msg) return;

    if (channel) {
        strncpy(msg->channel, channel, sizeof(msg->channel) - 1);
    }
    if (nick) {
        strncpy(msg->nick, nick, sizeof(msg->nick) - 1);
    }
    if (text) {
        strncpy(msg->text, text, sizeof(msg->text) - 1);
    }

    lvgl_lock();
    lv_async_call(add_message_async, msg);
    lvgl_unlock();
}

void irc_ui_add_server_message(const char* text) {
    pending_text_t* t = calloc(1, sizeof(pending_text_t));
    if (!t) return;
    if (text) {
        strncpy(t->text, text, sizeof(t->text) - 1);
    }
    lvgl_lock();
    lv_async_call(add_server_message_async, t);
    lvgl_unlock();
}

void irc_ui_set_status(const char* status) {
    pending_text_t* s = calloc(1, sizeof(pending_text_t));
    if (!s) return;
    strncpy(s->text, status, sizeof(s->text) - 1);
    lvgl_lock();
    lv_async_call(set_status_async, s);
    lvgl_unlock();
}

void irc_ui_add_channel(const char* channel) {
    pending_channel_t* pc = calloc(1, sizeof(pending_channel_t));
    if (!pc) return;
    strncpy(pc->name, channel, sizeof(pc->name) - 1);
    lvgl_lock();
    lv_async_call(add_channel_async, pc);
    lvgl_unlock();
}

void irc_ui_remove_channel(const char* channel) {
    pending_channel_t* pc = calloc(1, sizeof(pending_channel_t));
    if (!pc) return;
    strncpy(pc->name, channel, sizeof(pc->name) - 1);
    lvgl_lock();
    lv_async_call(remove_channel_async, pc);
    lvgl_unlock();
}

void irc_ui_set_active_channel(const char* channel) {
    lvgl_lock();
    int idx = find_channel(channel);
    if (idx >= 0) switch_to_channel(idx);
    lvgl_unlock();
}

const char* irc_ui_get_active_channel(void) {
    if (active_idx >= 0 && active_idx < num_channels) {
        return channels[active_idx].name;
    }
    return NULL;
}

void irc_ui_hide_spinner(void) {
    lvgl_lock();
    lv_async_call(hide_spinner_async, NULL);
    lvgl_unlock();
}
