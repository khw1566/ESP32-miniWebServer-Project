#pragma once
#include "m3_runtime.h"

typedef enum { M3_WEB_DOMAIN, M3_WEB_IPV6, M3_WEB_IPV4, M3_WEB_MDNS, M3_WEB_COUNT } m3_web_address_t;

/* Never substitute a different address family when the requested one is unavailable. */
static inline const char *m3_web_address_url(const m3_runtime_state_t *s, unsigned kind) {
    switch (kind) {
        case M3_WEB_DOMAIN: return s->domain_url;
        case M3_WEB_IPV6: return s->web && s->mode == NET_MODE_STA_CONNECTED ? s->ipv6_url : "";
        case M3_WEB_IPV4: return s->web ? s->url : "";
        case M3_WEB_MDNS: return s->web && s->mdns_ready ? s->mdns_url : "";
        default: return "";
    }
}
