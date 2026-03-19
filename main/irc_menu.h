#pragma once

#include "irc_client.h"

typedef void (*irc_menu_connect_cb_t)(const irc_config_t* config);

void irc_menu_init(irc_menu_connect_cb_t on_connect);
