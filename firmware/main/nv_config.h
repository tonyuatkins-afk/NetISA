/**
 * nv_config.h - NVS storage for WiFi credentials and settings
 */

#ifndef NV_CONFIG_H
#define NV_CONFIG_H

#include "esp_err.h"
#include <stddef.h>

esp_err_t nv_config_init(void);
esp_err_t nv_config_set_wifi(const char *ssid, const char *password);
esp_err_t nv_config_get_wifi(char *ssid, char *password);
esp_err_t nv_config_clear_wifi(void);
esp_err_t nv_config_set_string(const char *key, const char *value);
esp_err_t nv_config_get_string(const char *key, char *value, size_t max_len);

#endif /* NV_CONFIG_H */
