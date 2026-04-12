/**
 * dns_resolver.h - DNS resolution wrapper
 */

#ifndef DNS_RESOLVER_H
#define DNS_RESOLVER_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t dns_resolve(const char *hostname, uint32_t *ip_addr);

#endif /* DNS_RESOLVER_H */
