/**
 * wifi_mgr.c - WiFi station + AP mode + provisioning
 *
 * Initializes WiFi in APSTA mode. If saved credentials exist, auto-connects
 * as station. Otherwise starts AP mode ("NetISA-Setup", open) for initial
 * configuration via the web config UI.
 */

#include "wifi_mgr.h"
#include "nv_config.h"
#include "time_sync.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY           5

static EventGroupHandle_t wifi_event_group;
static wifi_state_t current_state = WIFI_STATE_IDLE;
static int retry_count = 0;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (retry_count < MAX_RETRY) {
                retry_count++;
                ESP_LOGI(TAG, "Retry %d/%d...", retry_count, MAX_RETRY);
                esp_wifi_connect();
            } else {
                current_state = WIFI_STATE_ERROR;
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGE(TAG, "Connection failed after %d retries", MAX_RETRY);
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event =
                (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "AP client connected, AID=%d", event->aid);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event =
                (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "AP client disconnected, AID=%d", event->aid);
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        current_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

        /* Start SNTP after getting IP */
        time_sync_init();
    }
}

static esp_err_t start_ap_mode(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "NetISA-Setup",
            .ssid_len = 12,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    current_state = WIFI_STATE_AP_MODE;
    ESP_LOGI(TAG, "AP started: NetISA-Setup (no password)");
    return ESP_OK;
}

esp_err_t wifi_mgr_init(void)
{
    char ssid[33] = {0};
    char password[65] = {0};
    esp_err_t ret;

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* Check for saved credentials */
    ret = nv_config_get_wifi(ssid, password);
    if (ret == ESP_OK && strlen(ssid) > 0) {
        /* Auto-connect as station */
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        current_state = WIFI_STATE_CONNECTING;

        ESP_LOGI(TAG, "Saved credentials found, connecting to: %s", ssid);
    } else {
        ESP_LOGI(TAG, "No saved credentials, starting AP mode");
    }

    /* Always start AP for config access */
    start_ap_mode();

    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_ap_record_t *results, uint16_t *count)
{
    esp_err_t ret;
    uint16_t max_count;

    if (!results || !count || *count == 0) return ESP_ERR_INVALID_ARG;

    max_count = *count;

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = { .min = 100, .max = 300 },
        },
    };

    /* S4 fix: save state before scan so we can restore it afterward */
    wifi_state_t saved_state = current_state;
    current_state = WIFI_STATE_SCANNING;
    ret = esp_wifi_scan_start(&scan_config, true);  /* blocking */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        current_state = saved_state;
        return ret;
    }

    ret = esp_wifi_scan_get_ap_records(&max_count, results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan get records failed: %s", esp_err_to_name(ret));
        current_state = saved_state;
        return ret;
    }

    *count = max_count;
    ESP_LOGI(TAG, "Scan found %d networks", max_count);

    /* S4 fix: restore previous state (e.g. CONNECTED) instead of IDLE */
    if (current_state == WIFI_STATE_SCANNING) {
        current_state = saved_state;
    }

    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    wifi_config_t sta_config = {0};

    if (!ssid) return ESP_ERR_INVALID_ARG;

    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta_config.sta.password, password,
                sizeof(sta_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    retry_count = 0;
    current_state = WIFI_STATE_CONNECTING;

    /* Connect with new config; ESP-IDF internally handles disconnecting
     * any existing connection, avoiding the double-connect race that
     * would occur if we called esp_wifi_disconnect() first (the async
     * DISCONNECTED event would trigger a retry connect in the handler). */
    esp_wifi_connect();

    /* Wait for connection with 10-second timeout */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        /* Save credentials on successful connect */
        nv_config_set_wifi(ssid, password ? password : "");
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    current_state = WIFI_STATE_ERROR;
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_mgr_disconnect(void)
{
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        current_state = WIFI_STATE_IDLE;
        nv_config_clear_wifi();
        ESP_LOGI(TAG, "Disconnected and credentials cleared");
    }
    return ret;
}

wifi_state_t wifi_mgr_get_state(void)
{
    return current_state;
}

esp_err_t wifi_mgr_get_info(char *ssid, uint32_t *ip, int8_t *rssi)
{
    wifi_ap_record_t ap_info;
    esp_netif_ip_info_t ip_info;

    if (current_state != WIFI_STATE_CONNECTED) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    if (ssid) {
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
        if (ret == ESP_OK) {
            strncpy(ssid, (char *)ap_info.ssid, 32);
            ssid[32] = '\0';
            if (rssi) *rssi = ap_info.rssi;
        }
    }

    if (ip) {
        esp_netif_get_ip_info(sta_netif, &ip_info);
        *ip = ip_info.ip.addr;
    }

    return ESP_OK;
}

uint8_t wifi_mgr_get_signal_pct(void)
{
    wifi_ap_record_t ap_info;

    if (current_state != WIFI_STATE_CONNECTED) return 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) return 0;

    /* Convert RSSI (typically -100 to -30 dBm) to 0-100% */
    int rssi = ap_info.rssi;
    if (rssi >= -30) return 100;
    if (rssi <= -100) return 0;
    return (uint8_t)((rssi + 100) * 100 / 70);
}
