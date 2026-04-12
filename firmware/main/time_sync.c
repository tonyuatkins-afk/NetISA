/**
 * time_sync.c - SNTP time synchronization
 */

#include "time_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "time_sync";

esp_err_t time_sync_init(void)
{
    ESP_LOGI(TAG, "Initializing SNTP with pool.ntp.org");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    /* Set timezone to UTC */
    setenv("TZ", "UTC0", 1);
    tzset();

    return ESP_OK;
}

esp_err_t time_sync_get(int *year, int *month, int *day,
                        int *hour, int *min, int *sec)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    gmtime_r(&now, &timeinfo);

    /* Check if time has been synchronized (year > 2020 as heuristic) */
    if (timeinfo.tm_year < (2020 - 1900)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (year)  *year  = timeinfo.tm_year + 1900;
    if (month) *month = timeinfo.tm_mon + 1;
    if (day)   *day   = timeinfo.tm_mday;
    if (hour)  *hour  = timeinfo.tm_hour;
    if (min)   *min   = timeinfo.tm_min;
    if (sec)   *sec   = timeinfo.tm_sec;

    return ESP_OK;
}
