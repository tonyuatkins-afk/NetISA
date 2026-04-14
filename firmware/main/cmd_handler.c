/**
 * cmd_handler.c - Command dispatcher (replaces Phase 0 loopback logic)
 *
 * The Phase 0 ISR reads a command byte from the ISA bus via the register
 * file. The v1 command handler replaces the loopback logic with real
 * command dispatch based on the INT 63h function groups.
 *
 * Communication flow:
 *   1. ISR latches cmd_request_t onto FreeRTOS queue (from ISR context)
 *   2. cmd_handler_task pulls from queue, dispatches to group handler
 *   3. Handler fills cmd_response_t, sets cmd_response_ready flag
 *   4. ISR reads response from shared buffer on next register read
 */

#include "cmd_handler.h"
#include "status.h"
#include "wifi_mgr.h"
#include "http_client.h"
#include "html_parser.h"
#include "ws_client.h"
#include "dns_resolver.h"
#include "nv_config.h"
#include "time_sync.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cmd_handler";

QueueHandle_t cmd_queue = NULL;
volatile cmd_response_t cmd_response;
volatile int cmd_response_ready = 0;
/* F-01 fix: cmd_data_buf/cmd_data_len removed; data now embedded in cmd_request_t */

/* ===== Group 0x00: System ===== */

static void handle_system(const cmd_request_t *req, cmd_response_t *resp)
{
    switch (req->function) {
    case 0x00:  /* NOP / Presence check */
        resp->status = NI_OK;
        resp->data[0] = (NI_SIGNATURE >> 8) & 0xFF;  /* 'C' */
        resp->data[1] = NI_SIGNATURE & 0xFF;          /* 'R' */
        resp->data[2] = FW_VERSION_MAJOR;
        resp->data[3] = FW_VERSION_MINOR;
        resp->data[4] = FW_VERSION_PATCH;
        resp->data_len = 5;
        break;

    case 0x01: {  /* Get card status */
        wifi_state_t ws = wifi_mgr_get_state();
        resp->status = NI_OK;
        resp->data[0] = 0x01;  /* status flags: CMD_READY */
        resp->data[1] = (uint8_t)http_get_active_count();
        resp->data[2] = MAX_SESSIONS;
        resp->data[3] = (ws == WIFI_STATE_CONNECTED) ? NI_NETSTAT_CONNECTED :
                         (ws == WIFI_STATE_CONNECTING) ? NI_NETSTAT_CONNECTING :
                         NI_NETSTAT_DISCONNECTED;
        resp->data[4] = wifi_mgr_get_signal_pct();
        resp->data_len = 5;
        break;
    }

    case 0x02:  /* Reset card */
        resp->status = NI_OK;
        resp->data_len = 0;
        ESP_LOGI(TAG, "Reset requested via command");
        /* Delay restart so response can be sent */
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        break;

    case 0x03: {  /* Get network status */
        wifi_state_t ws = wifi_mgr_get_state();
        resp->status = NI_OK;
        resp->data[0] = (ws == WIFI_STATE_CONNECTED) ? NI_NETSTAT_CONNECTED :
                         (ws == WIFI_STATE_CONNECTING) ? NI_NETSTAT_CONNECTING :
                         NI_NETSTAT_DISCONNECTED;
        resp->data[1] = wifi_mgr_get_signal_pct();
        resp->data_len = 2;
        break;
    }

    case 0x05:  /* Get firmware version */
        resp->status = NI_OK;
        resp->data[0] = FW_VERSION_MAJOR;
        resp->data[1] = FW_VERSION_MINOR;
        resp->data[2] = FW_VERSION_PATCH;
        resp->data_len = 3;
        break;

    case 0x06: {  /* Get MAC address */
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        resp->status = NI_OK;
        memcpy(resp->data, mac, 6);
        resp->data_len = 6;
        break;
    }

    case 0x07: {  /* Get IP address */
        uint32_t ip = 0;
        if (wifi_mgr_get_info(NULL, &ip, NULL) == ESP_OK) {
            resp->status = NI_OK;
            memcpy(resp->data, &ip, 4);
            resp->data_len = 4;
        } else {
            resp->status = NI_ERR_NETWORK_DOWN;
            resp->data_len = 0;
        }
        break;
    }

    default:
        resp->status = NI_ERR_NOT_IMPLEMENTED;
        resp->data_len = 0;
        break;
    }
}

/* ===== Group 0x01: WiFi ===== */

static void handle_wifi(const cmd_request_t *req, cmd_response_t *resp)
{
    switch (req->function) {
    case 0x02: {  /* Connect WiFi (SSID and password passed via data registers) */
        /* In v1, SSID/password are set via web config or NVS.
         * This command triggers connection with saved credentials. */
        char ssid[33], pass[65];
        if (nv_config_get_wifi(ssid, pass) == ESP_OK) {
            esp_err_t err = wifi_mgr_connect(ssid, pass);
            resp->status = (err == ESP_OK) ? NI_OK : NI_ERR_CONNECT_FAILED;
        } else {
            resp->status = NI_ERR_NETWORK_DOWN;
        }
        resp->data_len = 0;
        break;
    }

    case 0x03:  /* Disconnect WiFi */
        wifi_mgr_disconnect();
        resp->status = NI_OK;
        resp->data_len = 0;
        break;

    case 0x04: {  /* Scan WiFi networks */
        wifi_ap_record_t records[20];
        uint16_t count = 20;
        esp_err_t err = wifi_mgr_scan(records, &count);
        if (err != ESP_OK) {
            resp->status = NI_ERR_NOT_READY;
            resp->data_len = 0;
            break;
        }
        /* Pack scan results: count (1 byte) + per-AP: ssid(33) + rssi(1) + auth(1) = 35 bytes */
        resp->status = NI_OK;
        int offset = 1;
        int packed = 0;
        for (int i = 0; i < count && offset + 35 <= CMD_RESP_MAX; i++) {
            memcpy(&resp->data[offset], records[i].ssid, 33);
            resp->data[offset + 33] = (uint8_t)(int8_t)records[i].rssi;
            resp->data[offset + 34] = (uint8_t)records[i].authmode;
            offset += 35;
            packed++;
        }
        resp->data[0] = (uint8_t)packed;
        resp->data_len = offset;
        break;
    }

    default:
        resp->status = NI_ERR_NOT_IMPLEMENTED;
        resp->data_len = 0;
        break;
    }
}

/* ===== Group 0x02: HTTP ===== */

static void handle_http(const cmd_request_t *req, cmd_response_t *resp)
{
    switch (req->function) {
    case 0x00: {  /* HTTP GET - URL passed via staging buffer (F-01: per-request data) */
        char url[256];
        int url_len = req->data_len;
        if (url_len > 255) url_len = 255;
        memcpy(url, req->data, url_len);
        url[url_len] = '\0';

        int session = http_open_get(url);
        if (session >= 0) {
            resp->status = NI_OK;
            resp->data[0] = (uint8_t)session;
            resp->data_len = 1;
        } else {
            resp->status = NI_ERR_CONNECT_FAILED;
            resp->data_len = 0;
        }
        break;
    }

    case 0x01: {  /* HTTP POST */
        resp->status = NI_ERR_NOT_IMPLEMENTED;
        resp->data_len = 0;
        break;
    }

    case 0x02: {  /* HTTP read */
        if (req->param_len < 1) {
            resp->status = NI_ERR_INVALID_PARAM;
            resp->data_len = 0;
            break;
        }
        int session_id = req->params[0];
        int max_read = CMD_RESP_MAX - 4;
        int bytes = http_read(session_id, (char *)&resp->data[4], max_read);
        if (bytes >= 0) {
            resp->status = NI_OK;
            resp->data[0] = (uint8_t)(bytes & 0xFF);
            resp->data[1] = (uint8_t)((bytes >> 8) & 0xFF);
            resp->data[2] = (uint8_t)(http_get_status(session_id) & 0xFF);
            resp->data[3] = (uint8_t)((http_get_status(session_id) >> 8) & 0xFF);
            resp->data_len = 4 + bytes;
        } else {
            resp->status = NI_ERR_DISCONNECTED;
            resp->data_len = 0;
        }
        break;
    }

    case 0x03: {  /* HTTP close */
        if (req->param_len < 1) {
            resp->status = NI_ERR_INVALID_PARAM;
            resp->data_len = 0;
            break;
        }
        int session_id = req->params[0];
        http_close(session_id);
        resp->status = NI_OK;
        resp->data_len = 0;
        break;
    }

    default:
        resp->status = NI_ERR_NOT_IMPLEMENTED;
        resp->data_len = 0;
        break;
    }
}

/* ===== Group 0x03: WebSocket (stub) ===== */

static void handle_ws(const cmd_request_t *req, cmd_response_t *resp)
{
    (void)req;
    resp->status = NI_ERR_NOT_IMPLEMENTED;
    resp->data_len = 0;
}

/* ===== Group 0x04: Certificates (stub) ===== */

static void handle_cert(const cmd_request_t *req, cmd_response_t *resp)
{
    (void)req;
    resp->status = NI_ERR_NOT_IMPLEMENTED;
    resp->data_len = 0;
}

/* ===== Group 0x05: DNS ===== */

static void handle_dns(const cmd_request_t *req, cmd_response_t *resp)
{
    switch (req->function) {
    case 0x00: {  /* Resolve hostname (IPv4) - F-01: per-request data */
        char hostname[128];
        int len = req->data_len;
        if (len > 127) len = 127;
        memcpy(hostname, req->data, len);
        hostname[len] = '\0';

        uint32_t ip;
        if (dns_resolve(hostname, &ip) == ESP_OK) {
            resp->status = NI_OK;
            memcpy(resp->data, &ip, 4);
            resp->data_len = 4;
        } else {
            resp->status = NI_ERR_DNS_FAILED;
            resp->data_len = 0;
        }
        break;
    }

    default:
        resp->status = NI_ERR_NOT_IMPLEMENTED;
        resp->data_len = 0;
        break;
    }
}

/* ===== Group 0x06: Events (stub) ===== */

static void handle_event(const cmd_request_t *req, cmd_response_t *resp)
{
    (void)req;
    resp->status = NI_ERR_NOT_IMPLEMENTED;
    resp->data_len = 0;
}

/* ===== Group 0x07: Diagnostics / Time ===== */

static void handle_diag(const cmd_request_t *req, cmd_response_t *resp)
{
    switch (req->function) {
    case 0x00: {  /* Get uptime */
        uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);
        resp->status = NI_OK;
        memcpy(resp->data, &uptime_sec, 4);
        resp->data_len = 4;
        break;
    }

    case 0x01: {  /* Get memory info */
        uint32_t free_heap = (uint32_t)esp_get_free_heap_size();
        uint32_t largest_block = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        resp->status = NI_OK;
        memcpy(&resp->data[0], &free_heap, 4);
        memcpy(&resp->data[4], &largest_block, 4);
        resp->data_len = 8;
        break;
    }

    case 0x06: {  /* Bus loopback test (preserved from Phase 0) */
        /* Echo params back as response */
        resp->status = NI_OK;
        memcpy(resp->data, req->params, req->param_len);
        resp->data_len = req->param_len;
        break;
    }

    case 0x07: {  /* SNTP time */
        int year, month, day, hour, min, sec;
        if (time_sync_get(&year, &month, &day, &hour, &min, &sec) == ESP_OK) {
            resp->status = NI_OK;
            resp->data[0] = (uint8_t)(year >> 8);
            resp->data[1] = (uint8_t)(year & 0xFF);
            resp->data[2] = (uint8_t)month;
            resp->data[3] = (uint8_t)day;
            resp->data[4] = (uint8_t)hour;
            resp->data[5] = (uint8_t)min;
            resp->data[6] = (uint8_t)sec;
            resp->data_len = 7;
        } else {
            resp->status = NI_ERR_NOT_READY;
            resp->data_len = 0;
        }
        break;
    }

    case 0x0B: {  /* Random bytes (CSPRNG) */
        if (req->param_len < 1) {
            resp->status = NI_ERR_INVALID_PARAM;
            resp->data_len = 0;
            break;
        }
        int count = req->params[0];
        if (count == 0) count = 1;
        if (count > CMD_RESP_MAX) count = CMD_RESP_MAX;
        esp_fill_random(resp->data, count);
        resp->status = NI_OK;
        resp->data_len = count;
        break;
    }

    default:
        resp->status = NI_ERR_NOT_IMPLEMENTED;
        resp->data_len = 0;
        break;
    }
}

/* ===== Dispatch table ===== */

static const cmd_handler_fn handlers[8] = {
    handle_system,  /* 0x00 */
    handle_wifi,    /* 0x01 */
    handle_http,    /* 0x02 */
    handle_ws,      /* 0x03 - stub */
    handle_cert,    /* 0x04 - stub */
    handle_dns,     /* 0x05 */
    handle_event,   /* 0x06 - stub */
    handle_diag,    /* 0x07 */
};

/* ===== Command handler task ===== */

void cmd_handler_init(void)
{
    /* F-01 fix: reduced from 8 to 4 entries; each entry is now ~277 bytes */
    cmd_queue = xQueueCreate(4, sizeof(cmd_request_t));
    if (!cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return;
    }
    cmd_response_ready = 0;
    http_client_init();
    ESP_LOGI(TAG, "Command handler initialized");
}

void cmd_handler_task(void *arg)
{
    cmd_request_t req;
    cmd_response_t resp;

    (void)arg;
    ESP_LOGI(TAG, "Command handler task started on core %d", xPortGetCoreID());

    while (1) {
        if (xQueueReceive(cmd_queue, &req, portMAX_DELAY) == pdTRUE) {
            memset(&resp, 0, sizeof(resp));

            if (req.group < 8) {
                ESP_LOGD(TAG, "CMD: group=0x%02X func=0x%02X params=%d",
                         req.group, req.function, req.param_len);
                handlers[req.group](&req, &resp);
            } else {
                ESP_LOGW(TAG, "Unknown command group: 0x%02X", req.group);
                resp.status = NI_ERR_INVALID_PARAM;
                resp.data_len = 0;
            }

            /* Copy response to shared buffer and signal ISR.
             * Memory barrier ensures ISR on core 0 sees data before flag. */
            memcpy((void *)&cmd_response, &resp, sizeof(resp));
            __sync_synchronize();
            cmd_response_ready = 1;

            ESP_LOGD(TAG, "RESP: status=0x%02X data_len=%d",
                     resp.status, resp.data_len);
        }
    }
}
