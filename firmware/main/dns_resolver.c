/**
 * dns_resolver.c - DNS resolution wrapper around lwIP
 */

#include "dns_resolver.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "dns";

esp_err_t dns_resolve(const char *hostname, uint32_t *ip_addr)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    int err;

    if (!hostname || !ip_addr) return ESP_ERR_INVALID_ARG;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS resolve failed for %s: %d", hostname, err);
        if (res) freeaddrinfo(res);
        return ESP_FAIL;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    *ip_addr = addr->sin_addr.s_addr;

    ESP_LOGI(TAG, "Resolved %s -> %d.%d.%d.%d", hostname,
             (*ip_addr) & 0xFF, (*ip_addr >> 8) & 0xFF,
             (*ip_addr >> 16) & 0xFF, (*ip_addr >> 24) & 0xFF);

    freeaddrinfo(res);
    return ESP_OK;
}
