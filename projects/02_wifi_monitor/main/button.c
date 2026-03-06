#include "button.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define BTN_GPIO      9
#define DEBOUNCE_MS   50
#define LONG_PRESS_MS 1500

static QueueHandle_t s_queue;

static void button_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    bool last_state = true;
    TickType_t press_start = 0;

    while (1) {
        bool state = gpio_get_level(BTN_GPIO);

        if (last_state && !state) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (!gpio_get_level(BTN_GPIO)) {
                press_start = xTaskGetTickCount();
            }
        } else if (!last_state && state) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (gpio_get_level(BTN_GPIO)) {
                uint32_t held_ms = pdTICKS_TO_MS(xTaskGetTickCount() - press_start);
                btn_event_t evt = (held_ms >= LONG_PRESS_MS)
                                  ? BTN_LONG_PRESS : BTN_SHORT_PRESS;
                xQueueSend(s_queue, &evt, 0);
            }
        }

        last_state = state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

QueueHandle_t button_create_queue(void)
{
    s_queue = xQueueCreate(4, sizeof(btn_event_t));
    return s_queue;
}

void button_init(QueueHandle_t event_queue)
{
    s_queue = event_queue;
    xTaskCreate(button_task, "btn_task", 2048, NULL, 5, NULL);
}
