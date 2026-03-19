// SPDX-FileCopyrightText: 2025 Orange-Murker
// SPDX-License-Identifier: MIT

#pragma once

#include "display/lv_display.h"
#include "esp_lcd_types.h"
#include "freertos/idf_additions.h"
#include "hal/lcd_types.h"
#include "misc/lv_types.h"

// Custom key codes for function keys not in lv_key_t
#define BSP_KEY_F3  0xF3

typedef struct {
    uint32_t   key;
    lv_state_t state;
} lvgl_key_event_t;

void lvgl_lock(void);
void lvgl_unlock(void);
lv_display_t* lvgl_get_display(void);

void lvgl_init(int32_t hres, int32_t vres,
               lcd_color_rgb_pixel_format_t colour_fmt,
               esp_lcd_panel_handle_t lcd_panel_handle,
               esp_lcd_panel_io_handle_t lcd_panel_io_handle,
               QueueHandle_t input_event_queue);

lv_display_rotation_t lvgl_get_default_rotation(void);
lv_display_rotation_t lvgl_rotation_relative_to_default(
    lv_display_rotation_t rotation);
