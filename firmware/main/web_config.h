/**
 * web_config.h - HTTP server for web configuration UI
 */

#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include "esp_err.h"

esp_err_t web_config_start(void);
esp_err_t web_config_stop(void);

#endif /* WEB_CONFIG_H */
