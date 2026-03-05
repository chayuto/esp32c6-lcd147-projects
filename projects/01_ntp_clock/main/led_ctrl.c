#include "led_ctrl.h"
#include "theme.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "led_strip.h"
#include "esp_log.h"

#define TAG         "led"
#define LED_GPIO    8
#define FADE_STEP_MS 20

typedef struct {
    uint8_t  r, g, b;
    uint32_t fade_ms;
    bool     repeat_breathing; // for boot breathing loop
} led_cmd_t;

static QueueHandle_t    s_queue;
static led_strip_handle_t s_strip;
static bool             s_enabled = true;
static uint8_t          s_cur_r, s_cur_g, s_cur_b;

static void set_raw(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_enabled) {
        led_strip_set_pixel(s_strip, 0, 0, 0, 0);
    } else {
        led_strip_set_pixel(s_strip, 0, r, g, b);
    }
    led_strip_refresh(s_strip);
    s_cur_r = r; s_cur_g = g; s_cur_b = b;
}

static void fade_to(uint8_t tr, uint8_t tg, uint8_t tb, uint32_t ms)
{
    int steps = ms / FADE_STEP_MS;
    if (steps < 1) { set_raw(tr, tg, tb); return; }

    for (int i = 1; i <= steps; i++) {
        uint8_t r = s_cur_r + (int)(tr - s_cur_r) * i / steps;
        uint8_t g = s_cur_g + (int)(tg - s_cur_g) * i / steps;
        uint8_t b = s_cur_b + (int)(tb - s_cur_b) * i / steps;
        set_raw(r, g, b);
        vTaskDelay(pdMS_TO_TICKS(FADE_STEP_MS));
    }
    set_raw(tr, tg, tb);
}

static void led_task(void *arg)
{
    led_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY)) {
            fade_to(cmd.r, cmd.g, cmd.b, cmd.fade_ms);
        }
    }
}

void led_ctrl_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);

    s_queue = xQueueCreate(8, sizeof(led_cmd_t));
    xTaskCreate(led_task, "led_task", 2048, NULL, 3, NULL);
}

void led_ctrl_set(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_ms)
{
    led_cmd_t cmd = {r, g, b, fade_ms, false};
    xQueueSend(s_queue, &cmd, 0);
}

void led_ctrl_set_theme(void)
{
    const theme_t *t = theme_current();
    led_ctrl_set(t->led_r, t->led_g, t->led_b, 500);
}

void led_ctrl_pulse_once(void)
{
    // Enqueue a brief bright pulse then back to theme — led_task handles timing
    const theme_t *t = theme_current();
    uint8_t pr = (uint8_t)(t->led_r * 2.5f);
    uint8_t pg = (uint8_t)(t->led_g * 2.5f);
    uint8_t pb = (uint8_t)(t->led_b * 2.5f);
    led_ctrl_set(pr, pg, pb, 150);
    led_ctrl_set_theme();  // queued after pulse — no vTaskDelay from caller
}

void led_ctrl_flash_white(void)
{
    led_ctrl_set(180, 180, 180, 50);
    led_ctrl_set_theme();  // queued after flash — no vTaskDelay from caller
}

void led_ctrl_boot_breathing(void)
{
    // Single breath cycle — call repeatedly from boot screen timer
    led_ctrl_set(40, 40, 40, 800);
    vTaskDelay(pdMS_TO_TICKS(1000));
    led_ctrl_set(0, 0, 0, 800);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void led_ctrl_ntp_flash(void)
{
    led_ctrl_set(0, 80, 30, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_ctrl_set_theme();
}

void led_ctrl_error_pulse(void)
{
    led_ctrl_set(60, 0, 0, 400);
    vTaskDelay(pdMS_TO_TICKS(500));
    led_ctrl_set(0, 0, 0, 400);
    vTaskDelay(pdMS_TO_TICKS(500));
}

void led_ctrl_set_enabled(bool en)
{
    s_enabled = en;
    if (en) {
        led_ctrl_set_theme();
    } else {
        led_strip_set_pixel(s_strip, 0, 0, 0, 0);
        led_strip_refresh(s_strip);
    }
}
