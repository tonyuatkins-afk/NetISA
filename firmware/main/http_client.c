/**
 * http_client.c - HTTPS GET/POST with session management
 *
 * Manages up to 4 concurrent TLS sessions using esp_http_client.
 * Uses the ESP-IDF certificate bundle for public CA coverage.
 */

#include "http_client.h"
#include "status.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "http_client";

typedef struct {
    esp_http_client_handle_t handle;
    bool in_use;
    int status_code;
    int content_length;
    int bytes_read;
    bool headers_fetched;
} http_session_t;

static http_session_t sessions[MAX_SESSIONS];

void http_client_init(void)
{
    memset(sessions, 0, sizeof(sessions));
    ESP_LOGI(TAG, "HTTP client initialized (%d session slots)", MAX_SESSIONS);
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].in_use) return i;
    }
    return -1;
}

static int open_session(const char *url, esp_http_client_method_t method,
                        const char *body, int body_len)
{
    int slot = find_free_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "No free session slots");
        return -1;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return -1;
    }

    if (body && body_len > 0) {
        esp_http_client_set_post_field(client, body, body_len);
        esp_http_client_set_header(client, "Content-Type",
                                   "application/x-www-form-urlencoded");
    }

    /* Open connection and send request */
    esp_err_t err = esp_http_client_open(client, body_len > 0 ? body_len : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    /* Write body if POST */
    if (body && body_len > 0) {
        int written = esp_http_client_write(client, body, body_len);
        if (written < 0) {
            ESP_LOGE(TAG, "HTTP write failed");
            esp_http_client_cleanup(client);
            return -1;
        }
    }

    /* Fetch response headers */
    int content_length = esp_http_client_fetch_headers(client);

    sessions[slot].handle = client;
    sessions[slot].in_use = true;
    sessions[slot].status_code = esp_http_client_get_status_code(client);
    sessions[slot].content_length = content_length;
    sessions[slot].bytes_read = 0;
    sessions[slot].headers_fetched = true;

    ESP_LOGI(TAG, "Session %d opened: %s -> HTTP %d (content-length: %d)",
             slot, url, sessions[slot].status_code, content_length);

    return slot;
}

int http_open_get(const char *url)
{
    if (!url) return -1;
    return open_session(url, HTTP_METHOD_GET, NULL, 0);
}

int http_open_post(const char *url, const char *body, int body_len)
{
    if (!url) return -1;
    return open_session(url, HTTP_METHOD_POST, body, body_len);
}

int http_read(int session_id, char *buf, int bufsize)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return -1;
    if (!sessions[session_id].in_use) return -1;
    if (!buf || bufsize <= 0) return -1;

    int read = esp_http_client_read(sessions[session_id].handle, buf, bufsize);
    if (read > 0) {
        sessions[session_id].bytes_read += read;
    }
    return read;
}

int http_get_status(int session_id)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return -1;
    if (!sessions[session_id].in_use) return -1;
    return sessions[session_id].status_code;
}

void http_close(int session_id)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return;
    if (!sessions[session_id].in_use) return;

    esp_http_client_close(sessions[session_id].handle);
    esp_http_client_cleanup(sessions[session_id].handle);
    sessions[session_id].handle = NULL;
    sessions[session_id].in_use = false;

    ESP_LOGI(TAG, "Session %d closed (%d bytes read)",
             session_id, sessions[session_id].bytes_read);
}

int http_get_active_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].in_use) count++;
    }
    return count;
}
