#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef enum {
    APP_STATE_BOOT,
    APP_STATE_WIFI_CONNECTING,
    APP_STATE_WIFI_ERROR,
    APP_STATE_NTP_SYNCING,
    APP_STATE_NTP_ERROR,
    APP_STATE_CLOCK_RUNNING,
} app_state_t;

extern volatile app_state_t g_app_state;
extern SemaphoreHandle_t    g_state_mutex;

static inline app_state_t app_state_get(void) {
    app_state_t s;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    s = g_app_state;
    xSemaphoreGive(g_state_mutex);
    return s;
}

static inline void app_state_set(app_state_t new_state) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_app_state = new_state;
    xSemaphoreGive(g_state_mutex);
}
