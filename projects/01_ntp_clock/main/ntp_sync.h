#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t ntp_sync_start(const char *server, const char *timezone, uint32_t timeout_ms);
bool      ntp_is_synced(void);
void      ntp_get_local_time(struct tm *out);
