#include "irc_client.h"

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "irc";

#define IRC_RECV_BUF_SIZE 2048
#define IRC_SEND_BUF_SIZE 1024
#define IRC_TASK_STACK    (8 * 1024)

static irc_config_t   client_config;
static irc_event_cb_t client_callback;
static esp_tls_t*     tls_handle;
static int            plain_sock = -1;
static TaskHandle_t   recv_task_handle;
static bool           running;

static int irc_write(const char* data, size_t len) {
    if (client_config.use_tls) {
        int ret;
        do {
            ret = esp_tls_conn_write(tls_handle, data, len);
        } while (ret == ESP_TLS_ERR_SSL_WANT_READ
                 || ret == ESP_TLS_ERR_SSL_WANT_WRITE);
        return ret;
    }
    return send(plain_sock, data, len, 0);
}

static int irc_read(char* buf, size_t len) {
    if (client_config.use_tls) {
        int ret;
        do {
            ret = esp_tls_conn_read(tls_handle, buf, len);
        } while (ret == ESP_TLS_ERR_SSL_WANT_READ
                 || ret == ESP_TLS_ERR_SSL_WANT_WRITE);
        return ret;
    }
    return recv(plain_sock, buf, len, 0);
}

static void irc_close(void) {
    if (client_config.use_tls && tls_handle) {
        esp_tls_conn_destroy(tls_handle);
        tls_handle = NULL;
    } else if (plain_sock >= 0) {
        close(plain_sock);
        plain_sock = -1;
    }
}

static esp_err_t irc_send_raw(const char* fmt, ...) {
    char buf[IRC_SEND_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0 || len >= (int)sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, ">> %.*s", len - 2, buf);  // strip \r\n
    int written = irc_write(buf, len);
    return (written == len) ? ESP_OK : ESP_FAIL;
}

static void extract_nick(const char* prefix, char* nick, size_t sz) {
    if (!prefix) {
        nick[0] = '\0';
        return;
    }
    const char* bang = strchr(prefix, '!');
    size_t len = bang ? (size_t)(bang - prefix) : strlen(prefix);
    if (len >= sz) len = sz - 1;
    memcpy(nick, prefix, len);
    nick[len] = '\0';
}

static void dispatch_event(irc_event_type_t type, const char* nick,
                           const char* channel, const char* message) {
    if (!client_callback) return;

    irc_event_t event = {.type = type};
    if (nick) {
        strncpy(event.nick, nick, sizeof(event.nick) - 1);
    }
    if (channel) {
        strncpy(event.channel, channel, sizeof(event.channel) - 1);
    }
    if (message) {
        strncpy(event.message, message, sizeof(event.message) - 1);
    }
    client_callback(&event);
}

static void handle_line(char* line) {
    ESP_LOGI(TAG, "<< %s", line);

    char* prefix = NULL;
    char* command;
    char* params;

    if (line[0] == ':') {
        prefix = line + 1;
        char* space = strchr(prefix, ' ');
        if (!space) return;
        *space = '\0';
        line = space + 1;
    }

    command = line;
    char* space = strchr(command, ' ');
    if (space) {
        *space = '\0';
        params = space + 1;
    } else {
        params = "";
    }

    if (strcmp(command, "PING") == 0) {
        irc_send_raw("PONG %s\r\n", params);
        return;
    }

    if (strcmp(command, "001") == 0) {
        ESP_LOGI(TAG, "Joining channel: '%s'", client_config.channel);
        dispatch_event(IRC_EVENT_CONNECTED, NULL, NULL, NULL);
        irc_send_raw("JOIN %s\r\n", client_config.channel);
        return;
    }

    if (strcmp(command, "PRIVMSG") == 0) {
        char nick[32];
        extract_nick(prefix, nick, sizeof(nick));

        char* target = params;
        char* trail = strchr(params, ':');
        if (trail) {
            // Null-terminate target, advance to message text
            char* sp = strchr(target, ' ');
            if (sp) *sp = '\0';
            trail++;
        } else {
            trail = "";
        }

        dispatch_event(IRC_EVENT_MESSAGE, nick, target, trail);
        return;
    }

    if (strcmp(command, "JOIN") == 0) {
        char nick[32];
        extract_nick(prefix, nick, sizeof(nick));
        char* chan = params;
        if (chan[0] == ':') chan++;
        dispatch_event(IRC_EVENT_JOIN, nick, chan, NULL);
        return;
    }

    if (strcmp(command, "PART") == 0) {
        char nick[32];
        extract_nick(prefix, nick, sizeof(nick));
        char* chan = params;
        char* sp = strchr(chan, ' ');
        if (sp) *sp = '\0';
        char* trail = sp ? strchr(sp + 1, ':') : NULL;
        if (trail) trail++;
        dispatch_event(IRC_EVENT_PART, nick, chan,
                       trail ? trail : "");
        return;
    }

    if (strcmp(command, "QUIT") == 0) {
        char nick[32];
        extract_nick(prefix, nick, sizeof(nick));
        char* trail = strchr(params, ':');
        if (trail) trail++;
        dispatch_event(IRC_EVENT_PART, nick, "",
                       trail ? trail : "");
        return;
    }

    if (strcmp(command, "NOTICE") == 0) {
        char nick[32];
        extract_nick(prefix, nick, sizeof(nick));
        char* trail = strchr(params, ':');
        if (trail) trail++;
        else trail = params;
        dispatch_event(IRC_EVENT_SERVER_MSG, nick, NULL, trail);
        return;
    }

    if (strcmp(command, "ERROR") == 0) {
        char* trail = params;
        if (trail[0] == ':') trail++;
        dispatch_event(IRC_EVENT_ERROR, NULL, NULL, trail);
        return;
    }

    // Forward numeric replies as server messages
    if (command[0] >= '0' && command[0] <= '9') {
        char* trail = strchr(params, ':');
        if (trail) trail++;
        else trail = params;
        dispatch_event(IRC_EVENT_SERVER_MSG, NULL, NULL, trail);
        return;
    }
}

static esp_err_t irc_connect(void) {
    if (client_config.use_tls) {
        esp_tls_cfg_t cfg = {
            .timeout_ms = 10000,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        tls_handle = esp_tls_init();
        if (!tls_handle) return ESP_ERR_NO_MEM;

        int ret = esp_tls_conn_new_sync(
            client_config.server, strlen(client_config.server),
            client_config.port, &cfg, tls_handle);
        if (ret != 1) {
            ESP_LOGE(TAG, "TLS connection failed");
            esp_tls_conn_destroy(tls_handle);
            tls_handle = NULL;
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "TLS connected to %s:%d",
                 client_config.server, client_config.port);
    } else {
        struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
        };
        struct addrinfo* result = NULL;
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", client_config.port);

        int err = getaddrinfo(client_config.server, port_str,
                              &hints, &result);
        if (err != 0 || !result) {
            ESP_LOGE(TAG, "DNS lookup failed: %d", err);
            return ESP_FAIL;
        }

        plain_sock = socket(result->ai_family, result->ai_socktype,
                            result->ai_protocol);
        if (plain_sock < 0) {
            freeaddrinfo(result);
            return ESP_FAIL;
        }

        err = connect(plain_sock, result->ai_addr,
                      result->ai_addrlen);
        freeaddrinfo(result);
        if (err != 0) {
            ESP_LOGE(TAG, "Socket connect failed");
            close(plain_sock);
            plain_sock = -1;
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Connected to %s:%d",
                 client_config.server, client_config.port);
    }

    return ESP_OK;
}

static void irc_recv_task(void* arg) {
    char buf[IRC_RECV_BUF_SIZE];
    size_t buf_pos = 0;

    esp_err_t err = irc_connect();
    if (err != ESP_OK) {
        dispatch_event(IRC_EVENT_ERROR, NULL, NULL,
                       "Connection failed");
        dispatch_event(IRC_EVENT_DISCONNECTED, NULL, NULL, NULL);
        vTaskDelete(NULL);
        return;
    }

    irc_send_raw("NICK %s\r\n", client_config.nickname);
    irc_send_raw("USER %s 0 * :%s\r\n",
                 client_config.nickname, client_config.nickname);

    while (running) {
        int n = irc_read(buf + buf_pos,
                         sizeof(buf) - buf_pos - 1);
        if (n <= 0) {
            ESP_LOGW(TAG, "Connection lost (read=%d)", n);
            break;
        }
        buf_pos += n;
        buf[buf_pos] = '\0';

        char* line_start = buf;
        char* crlf;
        while ((crlf = strstr(line_start, "\r\n")) != NULL) {
            *crlf = '\0';
            handle_line(line_start);
            line_start = crlf + 2;
        }

        size_t remaining = buf_pos - (line_start - buf);
        if (remaining > 0) {
            memmove(buf, line_start, remaining);
        }
        buf_pos = remaining;
    }

    irc_close();
    dispatch_event(IRC_EVENT_DISCONNECTED, NULL, NULL, NULL);
    recv_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t irc_client_start(const irc_config_t* config,
                           irc_event_cb_t callback) {
    if (running) return ESP_ERR_INVALID_STATE;

    memcpy(&client_config, config, sizeof(irc_config_t));
    client_callback = callback;
    running = true;

    BaseType_t ret = xTaskCreate(irc_recv_task, "irc_recv",
                                 IRC_TASK_STACK, NULL, 5,
                                 &recv_task_handle);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t irc_client_send_message(const char* channel,
                                  const char* message) {
    if (!running) return ESP_ERR_INVALID_STATE;
    return irc_send_raw("PRIVMSG %s :%s\r\n", channel, message);
}

esp_err_t irc_client_send_raw_cmd(const char* raw_line) {
    if (!running) return ESP_ERR_INVALID_STATE;
    return irc_send_raw("%s\r\n", raw_line);
}

const char* irc_client_get_nickname(void) {
    return client_config.nickname;
}

void irc_client_stop(void) {
    running = false;
    irc_close();
}
