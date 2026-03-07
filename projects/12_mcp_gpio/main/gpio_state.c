#include "gpio_state.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gpio_state";

static pin_state_t               s_states[SAFE_DIGITAL_COUNT];
static SemaphoreHandle_t         s_mutex;
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t         s_cali_handle;
static bool                      s_cali_ok;

// ── LEDC (PWM) state ──────────────────────────────────────────────────────
// ESP32-C6 only has LEDC_LOW_SPEED_MODE.
// LEDC_TIMER_1 is used to avoid collision with the LCD backlight which uses TIMER_0.
// Up to 8 channels (one per safe pin if needed).
#define PWM_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define PWM_TIMER        LEDC_TIMER_1
#define PWM_FREQ_HZ      5000          // 5 kHz — inaudible, smooth LED dimming
#define PWM_RESOLUTION   LEDC_TIMER_10_BIT   // 1024 steps (0–1023)
#define PWM_MAX_DUTY     1023

static bool           s_ledc_timer_ok = false;
static int8_t         s_pin_ch[SAFE_DIGITAL_COUNT]; // LEDC channel assigned per pin; -1 = none
static uint8_t        s_ch_used = 0;                // bitmask of occupied channels (bits 0–7)

// Allocate the first free LEDC channel. Returns -1 if all 8 are used.
static int ledc_alloc_channel(void)
{
    for (int ch = 0; ch < 8; ch++) {
        if (!(s_ch_used & (1 << ch))) {
            s_ch_used |= (1 << ch);
            return ch;
        }
    }
    return -1;
}

static void ledc_free_channel(int ch)
{
    if (ch >= 0 && ch < 8) s_ch_used &= ~(1 << ch);
}

// ── helpers ───────────────────────────────────────────────────────────────

// Find index into s_states[] for a given gpio number. Returns -1 if not found.
static int find_idx(int gpio)
{
    for (int i = 0; i < SAFE_DIGITAL_COUNT; i++) {
        if (s_states[i].gpio == gpio) return i;
    }
    return -1;
}

// ── public API ────────────────────────────────────────────────────────────

esp_err_t gpio_state_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    // Populate state table from board config — all pins start unconfigured.
    for (int i = 0; i < SAFE_DIGITAL_COUNT; i++) {
        s_states[i].gpio  = SAFE_DIGITAL_PINS[i].gpio;
        s_states[i].mode  = PIN_MODE_UNCONFIGURED;
        s_states[i].value = -1;
    }

    // Create the ADC oneshot unit. ESP32-C6 has only ADC_UNIT_1.
    adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t ret = adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Attempt curve-fitting calibration (supported on ESP32-C6).
    // If unavailable (no eFuse calibration data), raw counts are returned instead.
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK);

    // Initialise LEDC channel tracking — all unassigned.
    memset(s_pin_ch, -1, sizeof(s_pin_ch));

    ESP_LOGI(TAG, "Initialised %d pins, ADC calibration: %s",
             SAFE_DIGITAL_COUNT, s_cali_ok ? "ok (mV)" : "not available (raw counts)");
    return ESP_OK;
}

esp_err_t gpio_state_configure(int gpio, pin_mode_t mode)
{
    if (!board_is_safe_pin(gpio)) {
        ESP_LOGW(TAG, "configure: GPIO %d not in safe pin list", gpio);
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == PIN_MODE_ADC && !board_is_adc_pin(gpio)) {
        ESP_LOGW(TAG, "configure: GPIO %d is not ADC-capable", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    int idx = find_idx(gpio);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    // If the pin is currently PWM, stop the LEDC channel before reconfiguring.
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        bool was_pwm = (s_states[idx].mode == PIN_MODE_PWM);
        int  old_ch  = s_pin_ch[idx];
        xSemaphoreGive(s_mutex);
        if (was_pwm && old_ch >= 0) {
            ledc_stop(PWM_SPEED_MODE, (ledc_channel_t)old_ch, 0);
            ledc_free_channel(old_ch);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_pin_ch[idx] = -1;
            xSemaphoreGive(s_mutex);
        }
    }

    esp_err_t ret = ESP_OK;

    if (mode == PIN_MODE_PWM) {
        // Initialise the shared LEDC timer once.
        if (!s_ledc_timer_ok) {
            ledc_timer_config_t tcfg = {
                .speed_mode      = PWM_SPEED_MODE,
                .duty_resolution = PWM_RESOLUTION,
                .timer_num       = PWM_TIMER,
                .freq_hz         = PWM_FREQ_HZ,
                .clk_cfg         = LEDC_AUTO_CLK,
            };
            ret = ledc_timer_config(&tcfg);
            if (ret != ESP_OK) return ret;
            s_ledc_timer_ok = true;
        }
        int ch = ledc_alloc_channel();
        if (ch < 0) {
            ESP_LOGW(TAG, "configure PWM: no free LEDC channels");
            return ESP_ERR_NO_MEM;
        }
        ledc_channel_config_t ccfg = {
            .gpio_num   = gpio,
            .speed_mode = PWM_SPEED_MODE,
            .channel    = (ledc_channel_t)ch,
            .timer_sel  = PWM_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };
        ret = ledc_channel_config(&ccfg);
        if (ret != ESP_OK) { ledc_free_channel(ch); return ret; }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_pin_ch[idx]   = (int8_t)ch;
        s_states[idx].mode  = PIN_MODE_PWM;
        s_states[idx].value = 0;
        xSemaphoreGive(s_mutex);

        ESP_LOGI(TAG, "GPIO %d → PWM (ch %d, %dHz, 10-bit)", gpio, ch, PWM_FREQ_HZ);
        return ESP_OK;
    } else if (mode == PIN_MODE_ADC) {
        // Release GPIO matrix before handing pin to ADC peripheral.
        gpio_reset_pin(gpio);
        adc_oneshot_chan_cfg_t ch_cfg = {
            .atten    = ADC_ATTENUATION,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_oneshot_config_channel(s_adc_handle,
                                         (adc_channel_t)board_adc_channel(gpio),
                                         &ch_cfg);
    } else if (mode == PIN_MODE_UNCONFIGURED) {
        // Return to high-impedance input — safest resting state.
        gpio_reset_pin(gpio);
    } else {
        gpio_config_t gcfg = {
            .pin_bit_mask = 1ULL << gpio,
            .mode         = (mode == PIN_MODE_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
            // Pull-up on INPUT makes floating pins read HIGH instead of floating.
            .pull_up_en   = (mode == PIN_MODE_INPUT) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&gcfg);
        if (ret == ESP_OK && mode == PIN_MODE_OUTPUT) {
            gpio_set_level(gpio, 0);  // start LOW — safest default for outputs
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "configure: GPIO %d hardware config failed: %s",
                 gpio, esp_err_to_name(ret));
        return ret;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_states[idx].mode  = mode;
    s_states[idx].value = (mode == PIN_MODE_OUTPUT) ? 0 : -1;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "GPIO %d → %s", gpio, pin_mode_str(mode));
    return ESP_OK;
}

esp_err_t gpio_state_write(int gpio, int level)
{
    int idx = find_idx(gpio);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    pin_mode_t mode = s_states[idx].mode;
    xSemaphoreGive(s_mutex);

    if (mode != PIN_MODE_OUTPUT) {
        ESP_LOGW(TAG, "write: GPIO %d is not OUTPUT (mode=%s)",
                 gpio, pin_mode_str(mode));
        return ESP_ERR_INVALID_STATE;
    }

    int lv = level ? 1 : 0;
    gpio_set_level(gpio, lv);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_states[idx].value = lv;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t gpio_state_read(int gpio, int *value_out)
{
    int idx = find_idx(gpio);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    pin_mode_t mode = s_states[idx].mode;
    xSemaphoreGive(s_mutex);

    if (mode == PIN_MODE_UNCONFIGURED) {
        ESP_LOGW(TAG, "read: GPIO %d is UNCONFIGURED", gpio);
        return ESP_ERR_INVALID_STATE;
    }

    // PWM pins: return current duty% from state table (no hardware read needed).
    if (mode == PIN_MODE_PWM) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        *value_out = s_states[idx].value;
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    int val;
    if (mode == PIN_MODE_ADC) {
        int raw;
        int ch = board_adc_channel(gpio);
        esp_err_t ret = adc_oneshot_read(s_adc_handle, (adc_channel_t)ch, &raw);
        if (ret != ESP_OK) return ret;
        if (s_cali_ok) {
            adc_cali_raw_to_voltage(s_cali_handle, raw, &val);
        } else {
            val = raw;  // raw 12-bit count (0-4095) when calibration unavailable
        }
    } else {
        // gpio_get_level() reads the input register for INPUT, and the output
        // latch register for OUTPUT — both are valid "current value" reads.
        val = gpio_get_level(gpio);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_states[idx].value = val;
    xSemaphoreGive(s_mutex);

    *value_out = val;
    return ESP_OK;
}

void gpio_state_snapshot(pin_state_t *out, int max_count, int *count_out)
{
    int n = (max_count < SAFE_DIGITAL_COUNT) ? max_count : SAFE_DIGITAL_COUNT;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, s_states, n * sizeof(pin_state_t));
    xSemaphoreGive(s_mutex);
    *count_out = n;
}

void gpio_state_poll_digital(pin_state_t *out, int max_count, int *count_out)
{
    // Fast path for the display timer: refresh INPUT and OUTPUT levels with a
    // direct register read (< 1 µs each).
    // ADC and PWM pins keep their last-known value — no hardware reads needed here.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < SAFE_DIGITAL_COUNT; i++) {
        if (s_states[i].mode == PIN_MODE_INPUT ||
            s_states[i].mode == PIN_MODE_OUTPUT) {
            s_states[i].value = gpio_get_level(s_states[i].gpio);
        }
    }
    int n = (max_count < SAFE_DIGITAL_COUNT) ? max_count : SAFE_DIGITAL_COUNT;
    memcpy(out, s_states, n * sizeof(pin_state_t));
    xSemaphoreGive(s_mutex);
    *count_out = n;
}

esp_err_t gpio_state_set_pwm_duty(int gpio, int duty_percent)
{
    int idx = find_idx(gpio);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    pin_mode_t mode = s_states[idx].mode;
    int        ch   = s_pin_ch[idx];
    xSemaphoreGive(s_mutex);

    if (mode != PIN_MODE_PWM || ch < 0) {
        ESP_LOGW(TAG, "set_pwm_duty: GPIO %d is not in PWM mode", gpio);
        return ESP_ERR_INVALID_STATE;
    }

    int duty_pct = duty_percent < 0 ? 0 : duty_percent > 100 ? 100 : duty_percent;
    uint32_t raw = (uint32_t)((duty_pct * PWM_MAX_DUTY) / 100);
    ledc_set_duty(PWM_SPEED_MODE, (ledc_channel_t)ch, raw);
    ledc_update_duty(PWM_SPEED_MODE, (ledc_channel_t)ch);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_states[idx].value = duty_pct;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "GPIO %d PWM duty → %d%% (raw %lu)", gpio, duty_pct, (unsigned long)raw);
    return ESP_OK;
}

const char *pin_mode_str(pin_mode_t mode)
{
    switch (mode) {
        case PIN_MODE_INPUT:        return "INPUT";
        case PIN_MODE_OUTPUT:       return "OUTPUT";
        case PIN_MODE_ADC:          return "ADC";
        case PIN_MODE_PWM:          return "PWM";
        case PIN_MODE_UNCONFIGURED: // fall-through
        default:                    return "---";
    }
}
