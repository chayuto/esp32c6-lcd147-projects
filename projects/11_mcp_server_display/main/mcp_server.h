#pragma once

#include <stddef.h>
#include "esp_err.h"

// Initializes Wi-Fi STA, blocks until connected (up to 30 s).
// Writes dotted-decimal IP into ip_out (caller supplies >= 16 bytes).
// Disables modem sleep after connect to minimise HTTP latency.
esp_err_t wifi_init_sta(char *ip_out, size_t ip_len);

// Builds cached tools/list JSON, starts mDNS + HTTP server.
// Call after wifi_init_sta succeeds.
esp_err_t mcp_server_start(void);
