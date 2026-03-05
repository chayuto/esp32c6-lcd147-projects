#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms);
bool      wifi_is_connected(void);
void      wifi_get_ip_str(char *buf, size_t len);
