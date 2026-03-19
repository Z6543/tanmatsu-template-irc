#include <stdio.h>
#include <string.h>
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "bsp_lvgl.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "hal/lcd_types.h"
#include "nvs_flash.h"
#include "wifi_connection.h"
#include "wifi_remote.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdcard.h"
#include "irc_client.h"
#include "irc_menu.h"
#include "irc_ui.h"

static char const TAG[] = "main";

static esp_lcd_panel_handle_t       display_lcd_panel    = NULL;
static esp_lcd_panel_io_handle_t    display_lcd_panel_io = NULL;
static size_t                       display_h_res        = 0;
static size_t                       display_v_res        = 0;
static lcd_color_rgb_pixel_format_t display_color_format;
static lcd_rgb_data_endian_t        display_data_endian;
static QueueHandle_t                input_event_queue = NULL;

static irc_config_t active_config;
static bool wifi_ready = false;

static void irc_event_handler(irc_event_t* event) {
    char buf[160];
    switch (event->type) {
        case IRC_EVENT_CONNECTED:
            ESP_LOGI(TAG, "IRC connected");
            irc_ui_set_status("Connected");
            break;
        case IRC_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "IRC disconnected");
            irc_ui_set_status("Disconnected");
            break;
        case IRC_EVENT_MESSAGE:
            irc_ui_add_message(event->channel,
                               event->nick, event->message);
            break;
        case IRC_EVENT_JOIN:
            // Add channel tab when we or someone joins
            irc_ui_add_channel(event->channel);
            snprintf(buf, sizeof(buf), "%s joined %s",
                     event->nick, event->channel);
            irc_ui_add_message(event->channel, "", buf);
            break;
        case IRC_EVENT_PART:
            snprintf(buf, sizeof(buf), "%s left", event->nick);
            irc_ui_add_message(event->channel, "", buf);
            // Remove tab if it's us parting
            if (strcmp(event->nick,
                       irc_client_get_nickname()) == 0) {
                irc_ui_remove_channel(event->channel);
            }
            break;
        case IRC_EVENT_ERROR:
            ESP_LOGE(TAG, "IRC error: %s", event->message);
            irc_ui_add_server_message(event->message);
            break;
        case IRC_EVENT_SERVER_MSG:
            irc_ui_add_server_message(event->message);
            break;
    }
}

static void do_connect(const irc_config_t* config);

static void do_back(void) {
    ESP_LOGI(TAG, "Returning to settings menu");
    irc_client_stop();

    lvgl_lock();
    lv_group_t* group = lv_group_get_default();
    if (group) lv_group_remove_all_objs(group);
    lv_obj_t* menu_scr = lv_obj_create(NULL);
    lv_screen_load(menu_scr);
    irc_menu_init(do_connect);
    lvgl_unlock();
}

static void connect_task(void* arg) {
    // Switch to chat UI from this task context
    lvgl_lock();
    lv_group_t* group = lv_group_get_default();
    if (group) lv_group_remove_all_objs(group);
    lv_obj_t* chat_screen = lv_obj_create(NULL);
    lv_screen_load(chat_screen);
    irc_ui_init(do_back);
    lvgl_unlock();

    if (!wifi_ready) {
        irc_ui_set_status("Initializing WiFi...");
        ESP_LOGI(TAG, "Initializing WiFi coprocessor");
        if (wifi_remote_initialize() != ESP_OK) {
            ESP_LOGE(TAG, "WiFi radio not responding");
            irc_ui_set_status("WiFi unavailable");
            vTaskDelete(NULL);
            return;
        }

        irc_ui_set_status("Starting WiFi stack...");
        ESP_LOGI(TAG, "Starting WiFi stack");
        wifi_connection_init_stack();

        irc_ui_set_status("Connecting to WiFi...");
        if (wifi_connect_try_all() == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected");
            wifi_ready = true;
        } else {
            ESP_LOGW(TAG, "WiFi connection failed");
            irc_ui_set_status("WiFi connection failed");
            vTaskDelete(NULL);
            return;
        }
    }

    char status_buf[160];
    snprintf(status_buf, sizeof(status_buf),
             "Connecting to %.127s:%d...",
             active_config.server, active_config.port);
    irc_ui_set_status(status_buf);

    esp_err_t res = irc_client_start(&active_config,
                                      irc_event_handler);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start IRC client");
        irc_ui_set_status("IRC client failed to start");
    }

    vTaskDelete(NULL);
}

static void do_connect(const irc_config_t* config) {
    memcpy(&active_config, config, sizeof(irc_config_t));
    // Spawn background task — don't do UI/WiFi work in event context
    xTaskCreate(connect_task, "irc_connect", 8192, NULL, 5, NULL);
}

void app_main(void) {
    gpio_install_isr_service(0);

    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES
        || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    ESP_ERROR_CHECK(res);

    bsp_configuration_t bsp_config = {};
    ESP_ERROR_CHECK(bsp_device_initialize(&bsp_config));

    res = bsp_display_get_panel(&display_lcd_panel);
    ESP_ERROR_CHECK(res);
    bsp_display_get_panel_io(&display_lcd_panel_io);
    res = bsp_display_get_parameters(&display_h_res, &display_v_res,
                                     &display_color_format,
                                     &display_data_endian);
    ESP_ERROR_CHECK(res);
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));

    lvgl_init(display_h_res, display_v_res, display_color_format,
              display_lcd_panel, display_lcd_panel_io,
              input_event_queue);

    // Mount SD card (non-fatal)
    esp_err_t sd_res = sdcard_init();
    if (sd_res != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available: %s",
                 esp_err_to_name(sd_res));
    }

    // Show settings menu — user presses Connect to proceed
    lvgl_lock();
    irc_menu_init(do_connect);
    lvgl_unlock();
}
