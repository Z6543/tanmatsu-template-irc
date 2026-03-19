#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char server[128];
    uint16_t port;
    bool use_tls;
    char nickname[32];
    char channel[64];
} irc_config_t;

typedef enum {
    IRC_EVENT_CONNECTED,
    IRC_EVENT_DISCONNECTED,
    IRC_EVENT_MESSAGE,
    IRC_EVENT_JOIN,
    IRC_EVENT_PART,
    IRC_EVENT_ERROR,
    IRC_EVENT_SERVER_MSG,
} irc_event_type_t;

typedef struct {
    irc_event_type_t type;
    char nick[32];
    char channel[64];
    char message[512];
} irc_event_t;

typedef void (*irc_event_cb_t)(irc_event_t* event);

esp_err_t irc_client_start(const irc_config_t* config,
                           irc_event_cb_t callback);
esp_err_t irc_client_send_message(const char* channel,
                                  const char* message);
esp_err_t irc_client_send_raw_cmd(const char* raw_line);
const char* irc_client_get_nickname(void);
void irc_client_stop(void);
