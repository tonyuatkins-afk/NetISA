/**
 * wifi_mgr.h - WiFi station + AP mode manager
 */

#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdint.h>

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_ERROR
} wifi_state_t;

esp_err_t wifi_mgr_init(void);
esp_err_t wifi_mgr_scan(wifi_ap_record_t *results, uint16_t *count);
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);
esp_err_t wifi_mgr_disconnect(void);
wifi_state_t wifi_mgr_get_state(void);
esp_err_t wifi_mgr_get_info(char *ssid, uint32_t *ip, int8_t *rssi);
uint8_t wifi_mgr_get_signal_pct(void);

#endif /* WIFI_MGR_H */
