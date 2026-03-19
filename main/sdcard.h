#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sdcard_init(void);
bool sdcard_is_mounted(void);
