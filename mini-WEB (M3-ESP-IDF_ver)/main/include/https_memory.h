#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The persistent relay yields its TLS heap before an AI request allocates
 * its JSON and provider connection. Only the owning task releases a lease. */
bool https_memory_relay_enter(void);
void https_memory_relay_leave(void);
bool https_memory_exclusive_pending(void);
bool https_memory_exclusive_begin(uint32_t timeout_ms);
void https_memory_exclusive_end(void);
