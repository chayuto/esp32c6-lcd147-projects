#include "led_status.h"

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define LED_GPIO    8
#define BLINK_MS    400   // half-period for connecting blink

static const char *TAG = "led_status";

static led_strip_handle_t s_strip;

typedef enum {
    STATE_OFF        = 0,
    STATE_CONNECTING,   // blink amber
    STATE_READY,        // solid green
    STATE_ERROR,        // solid red
    STATE_CUSTOM,       // MCP-controlled colour
} led_state_t;

static volatile led_state_t s_state    = STATE_OFF;
static volatile uint8_t     s_custom_r = 0;
static volatile uint8_t     s_custom_g = 0;
static volatile uint8_t     s_custom_b = 0;

static void set_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    // WS2812 on this board has R and G channels physically swapped
    led_strip_set_pixel(s_strip, 0, g, r, b);
    led_strip_refresh(s_strip);
}

static void led_task(void *arg)
{
    bool on = true;
    while (1) {
        switch (s_state) {
            case STATE_CONNECTING:
                // Blink amber (warm yellow: R+G, no B)
                set_pixel(on ? 60 : 0, on ? 30 : 0, 0);
                on = !on;
                vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
                break;

            case STATE_READY:
                set_pixel(0, 40, 0);   // solid green — dim enough for a room
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case STATE_ERROR:
                set_pixel(60, 0, 0);   // solid red
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case STATE_CUSTOM:
                set_pixel(s_custom_r, s_custom_g, s_custom_b);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            default:
                set_pixel(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
        }
    }
}

void led_status_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds       = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz  = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);

    xTaskCreate(led_task, "led_status", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "LED status task started");
}

void led_status_connecting(void)
{
    s_state = STATE_CONNECTING;
}

void led_status_ready(void)
{
    s_state = STATE_READY;
}

void led_status_error(void)
{
    s_state = STATE_ERROR;
}

void led_status_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    s_custom_r = r;
    s_custom_g = g;
    s_custom_b = b;
    s_state    = STATE_CUSTOM;
}
