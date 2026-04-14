/**
 * nv_config.c - NVS storage for WiFi credentials and settings
 */

#include "nv_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nv_config";
static const char *NVS_NAMESPACE = "netisa";

/* TODO: Enable NVS encryption for credential protection.
 * ESP-IDF supports encrypted NVS via nvs_flash_secure_init()
 * with a key partition. See ESP-IDF docs: "NVS Encryption".
 * For now, credentials are stored in plaintext NVS. */
esp_err_t nv_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition full or outdated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    }
    return ret;
}

esp_err_t nv_config_set_wifi(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (!ssid) return ESP_ERR_INVALID_ARG;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(handle, "wifi_ssid", ssid);
    if (ret == ESP_OK && password) {
        ret = nvs_set_str(handle, "wifi_pass", password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", ssid);
    }
    return ret;
}

esp_err_t nv_config_get_wifi(char *ssid, char *password)
{
    nvs_handle_t handle;
    esp_err_t ret;
    size_t len;

    if (!ssid || !password) return ESP_ERR_INVALID_ARG;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return ret;

    len = 33;
    ret = nvs_get_str(handle, "wifi_ssid", ssid, &len);
    if (ret == ESP_OK) {
        len = 65;
        ret = nvs_get_str(handle, "wifi_pass", password, &len);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t nv_config_clear_wifi(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_erase_key(handle, "wifi_ssid");
    nvs_erase_key(handle, "wifi_pass");
    ret = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ret;
}

esp_err_t nv_config_set_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (!key || !value) return ESP_ERR_INVALID_ARG;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t nv_config_get_string(const char *key, char *value, size_t max_len)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (!key || !value || max_len == 0) return ESP_ERR_INVALID_ARG;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_str(handle, key, value, &max_len);
    nvs_close(handle);
    return ret;
}
