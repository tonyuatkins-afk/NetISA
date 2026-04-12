/**
 * time_sync.h - SNTP time synchronization
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include "esp_err.h"

esp_err_t time_sync_init(void);
esp_err_t time_sync_get(int *year, int *month, int *day,
                        int *hour, int *min, int *sec);

#endif /* TIME_SYNC_H */
