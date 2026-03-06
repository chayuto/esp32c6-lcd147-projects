#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    BTN_SHORT_PRESS,
    BTN_LONG_PRESS,
} btn_event_t;

QueueHandle_t button_create_queue(void);
void          button_init(QueueHandle_t event_queue);
