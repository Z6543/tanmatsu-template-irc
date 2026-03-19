#include "irc_settings.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char* TAG = "irc-settings";
static const char* NVS_NAMESPACE = "irc";

esp_err_t irc_settings_load(irc_config_t* config) {
    // Start with Kconfig defaults
    memset(config, 0, sizeof(irc_config_t));
    strncpy(config->server, CONFIG_IRC_SERVER_HOST,
            sizeof(config->server) - 1);
    config->port = CONFIG_IRC_SERVER_PORT;
    config->use_tls = CONFIG_IRC_USE_TLS;
    strncpy(config->nickname, CONFIG_IRC_NICKNAME,
            sizeof(config->nickname) - 1);
    strncpy(config->channel, CONFIG_IRC_CHANNEL,
            sizeof(config->channel) - 1);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved settings, using defaults");
        return ESP_OK;
    }

    size_t len;

    len = sizeof(config->server);
    nvs_get_str(nvs, "server", config->server, &len);

    uint16_t port;
    if (nvs_get_u16(nvs, "port", &port) == ESP_OK) {
        config->port = port;
    }

    uint8_t tls;
    if (nvs_get_u8(nvs, "tls", &tls) == ESP_OK) {
        config->use_tls = tls;
    }

    len = sizeof(config->nickname);
    nvs_get_str(nvs, "nickname", config->nickname, &len);

    len = sizeof(config->channel);
    nvs_get_str(nvs, "channel", config->channel, &len);

    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded settings: %s:%d %s %s",
             config->server, config->port,
             config->nickname, config->channel);
    return ESP_OK;
}

esp_err_t irc_settings_save(const irc_config_t* config) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s",
                 esp_err_to_name(err));
        return err;
    }

    nvs_set_str(nvs, "server", config->server);
    nvs_set_u16(nvs, "port", config->port);
    nvs_set_u8(nvs, "tls", config->use_tls ? 1 : 0);
    nvs_set_str(nvs, "nickname", config->nickname);
    nvs_set_str(nvs, "channel", config->channel);

    err = nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings saved");
    return err;
}
