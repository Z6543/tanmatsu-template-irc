#pragma once

#include "irc_client.h"
#include "lvgl.h"

typedef void (*irc_menu_connect_cb_t)(const irc_config_t* config);

void irc_menu_init(lv_obj_t* screen, irc_menu_connect_cb_t on_connect);
