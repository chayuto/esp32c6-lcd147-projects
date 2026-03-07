#pragma once

#include "esp_err.h"
#include <stddef.h>

// Connect to Wi-Fi STA. Blocks until connected or fails.
// On success, writes the dotted-decimal IP into ip_out.
esp_err_t wifi_init_sta(char *ip_out, size_t ip_len);

// Build tools JSON, start mDNS and the HTTP server.
// Call after wifi_init_sta() succeeds.
esp_err_t mcp_server_start(void);
