#pragma once

#include "irc_client.h"
#include "esp_err.h"

esp_err_t irc_settings_load(irc_config_t* config);
esp_err_t irc_settings_save(const irc_config_t* config);
