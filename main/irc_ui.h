#pragma once

#include "lvgl.h"

typedef void (*irc_ui_back_cb_t)(void);

void irc_ui_init(lv_obj_t* screen, irc_ui_back_cb_t back_cb);
void irc_ui_add_message(const char* channel,
                        const char* nick, const char* text);
void irc_ui_add_server_message(const char* text);
void irc_ui_set_status(const char* status);
void irc_ui_add_channel(const char* channel);
void irc_ui_remove_channel(const char* channel);
void irc_ui_set_active_channel(const char* channel);
const char* irc_ui_get_active_channel(void);
void irc_ui_hide_spinner(void);
