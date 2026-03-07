#include "mcp_tools.h"
#include "gpio_state.h"
#include "board_config.h"
#include "led_status.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "mcp_tools";

// ── Cached tools/list ────────────────────────────────────────────────────
// Built once at startup from board_config.h tables so that:
//   1. The gpio enum arrays reflect the actual board config.
//   2. The hot path (every tools/list request) does zero allocation.

static char *s_tools_json = NULL;

// Build a cJSON array of safe GPIO numbers, optionally restricted to ADC-capable pins.
static cJSON *make_gpio_enum(bool adc_only)
{
    cJSON *arr = cJSON_CreateArray();
    if (adc_only) {
        for (int i = 0; i < ADC_CAPABLE_COUNT; i++)
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(ADC_CAPABLE_PINS[i]));
    } else {
        for (int i = 0; i < SAFE_DIGITAL_COUNT; i++)
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(SAFE_DIGITAL_PINS[i].gpio));
    }
    return arr;
}

// Build the inputSchema for configure_pins.
// Shape: { "pins": [ { "gpio": <enum>, "mode": <enum> } ] }
static cJSON *schema_configure_pins(void)
{
    cJSON *schema   = cJSON_CreateObject();
    cJSON *props    = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    // pins array
    cJSON *pins_prop  = cJSON_CreateObject();
    cJSON *items      = cJSON_CreateObject();
    cJSON *item_props = cJSON_CreateObject();

    cJSON_AddStringToObject(pins_prop, "type", "array");
    cJSON_AddStringToObject(pins_prop, "description",
        "One object per pin. Each object must have gpio (integer) and mode (string). "
        "Call get_gpio_capabilities first to see which gpios are available.");
    cJSON_AddNumberToObject(pins_prop, "minItems", 1);

    cJSON_AddStringToObject(items, "type", "object");

    // gpio property — enum restricted to safe pins
    cJSON *gpio_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(gpio_prop, "type", "integer");
    cJSON_AddStringToObject(gpio_prop, "description", "GPIO number to configure");
    cJSON_AddItemToObject(gpio_prop, "enum", make_gpio_enum(false));
    cJSON_AddItemToObject(item_props, "gpio", gpio_prop);

    // mode property — ADC mode only valid on ADC-capable pins (enforced by firmware)
    cJSON *mode_prop  = cJSON_CreateObject();
    cJSON *mode_enum  = cJSON_CreateArray();
    cJSON_AddStringToObject(mode_prop, "type", "string");
    cJSON_AddStringToObject(mode_prop, "description",
        "INPUT: digital read with pull-up. "
        "OUTPUT: digital write, starts LOW. "
        "ADC: analog read in millivolts — only valid on GPIOs: "
        // Append ADC pin list inline so the LLM has it in context
#if ADC_CAPABLE_COUNT >= 4
        "0, 1, 2, 3"
#if ADC_CAPABLE_COUNT >= 6
        ", 4, 5"
#endif
#endif
        ". Do NOT request ADC on non-ADC pins.");
    cJSON_AddItemToArray(mode_enum, cJSON_CreateString("INPUT"));
    cJSON_AddItemToArray(mode_enum, cJSON_CreateString("OUTPUT"));
    cJSON_AddItemToArray(mode_enum, cJSON_CreateString("ADC"));
    cJSON_AddItemToObject(mode_prop, "enum", mode_enum);
    cJSON_AddItemToObject(item_props, "mode", mode_prop);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("gpio"));
    cJSON_AddItemToArray(required, cJSON_CreateString("mode"));
    cJSON_AddItemToObject(items, "properties", item_props);
    cJSON_AddItemToObject(items, "required", required);
    cJSON_AddItemToObject(pins_prop, "items", items);
    cJSON_AddItemToObject(props, "pins", pins_prop);

    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("pins"));
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required", req);
    return schema;
}

// Build the inputSchema for write_digital_pins.
// Shape: { "pins": [ { "gpio": <enum>, "level": 0|1 } ] }
static cJSON *schema_write_digital_pins(void)
{
    cJSON *schema   = cJSON_CreateObject();
    cJSON *props    = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *pins_prop  = cJSON_CreateObject();
    cJSON *items      = cJSON_CreateObject();
    cJSON *item_props = cJSON_CreateObject();

    cJSON_AddStringToObject(pins_prop, "type", "array");
    cJSON_AddStringToObject(pins_prop, "description",
        "One object per pin. Pins must already be configured as OUTPUT. "
        "Do NOT write to INPUT or ADC pins.");
    cJSON_AddNumberToObject(pins_prop, "minItems", 1);
    cJSON_AddStringToObject(items, "type", "object");

    cJSON *gpio_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(gpio_prop, "type", "integer");
    cJSON_AddStringToObject(gpio_prop, "description", "GPIO number to write");
    cJSON_AddItemToObject(gpio_prop, "enum", make_gpio_enum(false));
    cJSON_AddItemToObject(item_props, "gpio", gpio_prop);

    cJSON *level_prop = cJSON_CreateObject();
    cJSON *level_enum = cJSON_CreateArray();
    cJSON_AddStringToObject(level_prop, "type", "integer");
    cJSON_AddStringToObject(level_prop, "description", "Logic level: 1 = HIGH (3.3V), 0 = LOW (GND)");
    cJSON_AddItemToArray(level_enum, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(level_enum, cJSON_CreateNumber(1));
    cJSON_AddItemToObject(level_prop, "enum", level_enum);
    cJSON_AddItemToObject(item_props, "level", level_prop);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("gpio"));
    cJSON_AddItemToArray(required, cJSON_CreateString("level"));
    cJSON_AddItemToObject(items, "properties", item_props);
    cJSON_AddItemToObject(items, "required", required);
    cJSON_AddItemToObject(pins_prop, "items", items);
    cJSON_AddItemToObject(props, "pins", pins_prop);

    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("pins"));
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required", req);
    return schema;
}

// Build the inputSchema for read_pins.
// Shape: { "gpios": [ <int>, ... ] }
// Flat array is intentional — there is nothing to pair, and it is the
// most natural representation the LLM produces reliably.
static cJSON *schema_read_pins(void)
{
    cJSON *schema = cJSON_CreateObject();
    cJSON *props  = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *gpios_prop = cJSON_CreateObject();
    cJSON *items      = cJSON_CreateObject();
    cJSON_AddStringToObject(gpios_prop, "type", "array");
    cJSON_AddStringToObject(gpios_prop, "description",
        "List of GPIO numbers to read. Pins must be configured first. "
        "Returns 0/1 for INPUT/OUTPUT pins and millivolts for ADC pins. "
        "Do NOT poll this in a rapid loop — use it to snapshot current state.");
    cJSON_AddNumberToObject(gpios_prop, "minItems", 1);
    cJSON_AddStringToObject(items, "type", "integer");
    cJSON_AddItemToObject(items, "enum", make_gpio_enum(false));
    cJSON_AddItemToObject(gpios_prop, "items", items);
    cJSON_AddItemToObject(props, "gpios", gpios_prop);

    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("gpios"));
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required", req);
    return schema;
}

// Build the inputSchema for set_pwm_duty.
// Shape: { "pins": [ { "gpio": <enum>, "duty": 0-100 } ] }
static cJSON *schema_set_pwm_duty(void)
{
    cJSON *schema   = cJSON_CreateObject();
    cJSON *props    = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *pins_prop  = cJSON_CreateObject();
    cJSON *items      = cJSON_CreateObject();
    cJSON *item_props = cJSON_CreateObject();

    cJSON_AddStringToObject(pins_prop, "type", "array");
    cJSON_AddStringToObject(pins_prop, "description",
        "One object per pin. Pins must already be configured as PWM. "
        "Do NOT call this on OUTPUT, INPUT, or ADC pins.");
    cJSON_AddNumberToObject(pins_prop, "minItems", 1);
    cJSON_AddStringToObject(items, "type", "object");

    cJSON *gpio_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(gpio_prop, "type", "integer");
    cJSON_AddStringToObject(gpio_prop, "description", "GPIO number (must be in PWM mode)");
    cJSON_AddItemToObject(gpio_prop, "enum", make_gpio_enum(false));
    cJSON_AddItemToObject(item_props, "gpio", gpio_prop);

    cJSON *duty_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(duty_prop, "type", "integer");
    cJSON_AddStringToObject(duty_prop, "description",
        "Duty cycle: 0 = always off (0V), 100 = always on (3.3V), 50 = half brightness");
    cJSON_AddNumberToObject(duty_prop, "minimum", 0);
    cJSON_AddNumberToObject(duty_prop, "maximum", 100);
    cJSON_AddItemToObject(item_props, "duty", duty_prop);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("gpio"));
    cJSON_AddItemToArray(required, cJSON_CreateString("duty"));
    cJSON_AddItemToObject(items, "properties", item_props);
    cJSON_AddItemToObject(items, "required", required);
    cJSON_AddItemToObject(pins_prop, "items", items);
    cJSON_AddItemToObject(props, "pins", pins_prop);

    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("pins"));
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required", req);
    return schema;
}

// Build the inputSchema for i2c_scan.
// Shape: { "sda": <enum>, "scl": <enum> }
static cJSON *schema_i2c_scan(void)
{
    cJSON *schema = cJSON_CreateObject();
    cJSON *props  = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *sda_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(sda_prop, "type", "integer");
    cJSON_AddStringToObject(sda_prop, "description",
        "GPIO to use as I2C SDA. Any safe pin works. "
        "The pin is temporarily used for I2C and released after the scan.");
    cJSON_AddItemToObject(sda_prop, "enum", make_gpio_enum(false));
    cJSON_AddItemToObject(props, "sda", sda_prop);

    cJSON *scl_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(scl_prop, "type", "integer");
    cJSON_AddStringToObject(scl_prop, "description",
        "GPIO to use as I2C SCL. Must be different from sda.");
    cJSON_AddItemToObject(scl_prop, "enum", make_gpio_enum(false));
    cJSON_AddItemToObject(props, "scl", scl_prop);

    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("sda"));
    cJSON_AddItemToArray(req, cJSON_CreateString("scl"));
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required", req);
    return schema;
}

// Build the inputSchema for set_rgb_led.
// Shape: { "r": 0-255, "g": 0-255, "b": 0-255 }
static cJSON *schema_set_rgb_led(void)
{
    cJSON *schema = cJSON_CreateObject();
    cJSON *props  = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    const char *ch_names[] = {"r", "g", "b"};
    const char *ch_desc[]  = {
        "Red channel 0-255 (0 = off, 255 = full red)",
        "Green channel 0-255 (0 = off, 255 = full green)",
        "Blue channel 0-255 (0 = off, 255 = full blue)",
    };
    cJSON *req = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddStringToObject(ch, "type",        "integer");
        cJSON_AddStringToObject(ch, "description", ch_desc[i]);
        cJSON_AddNumberToObject(ch, "minimum",     0);
        cJSON_AddNumberToObject(ch, "maximum",     255);
        cJSON_AddItemToObject(props, ch_names[i], ch);
        cJSON_AddItemToArray(req, cJSON_CreateString(ch_names[i]));
    }
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required",   req);
    return schema;
}

void mcp_tools_init(void)
{
    cJSON *tools = cJSON_CreateArray();

    // ── 1. get_gpio_capabilities ─────────────────────────────────────────
    // Discovery tool — the LLM should call this FIRST to understand the board.
    {
        cJSON *t      = cJSON_CreateObject();
        cJSON *schema = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "get_gpio_capabilities");
        cJSON_AddStringToObject(t, "description",
            "Returns the board name, a list of safe GPIO pins with their ADC capability "
            "and current runtime state (mode + value), and a list of reserved pins with "
            "reasons they cannot be used. "
            "Call this FIRST before configure_pins, write_digital_pins, or read_pins. "
            "No parameters required.");
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON_AddItemToObject(schema, "properties", cJSON_CreateObject());
        cJSON_AddItemToObject(t, "inputSchema", schema);
        cJSON_AddItemToArray(tools, t);
    }

    // ── 2. configure_pins ────────────────────────────────────────────────
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "configure_pins");
        cJSON_AddStringToObject(t, "description",
            "Configure one or more GPIO pins for INPUT, OUTPUT, or ADC mode. "
            "Must be called before read_pins or write_digital_pins. "
            "OUTPUT pins start LOW (0V). INPUT pins have an internal pull-up (float → HIGH). "
            "ADC mode is only valid on ADC-capable pins — see get_gpio_capabilities. "
            "Do NOT configure reserved system pins. "
            "Do NOT request ADC mode for non-ADC pins.");
        cJSON_AddItemToObject(t, "inputSchema", schema_configure_pins());
        cJSON_AddItemToArray(tools, t);
    }

    // ── 3. write_digital_pins ────────────────────────────────────────────
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "write_digital_pins");
        cJSON_AddStringToObject(t, "description",
            "Set logic levels on one or more OUTPUT pins simultaneously. "
            "level 1 = HIGH (3.3V), level 0 = LOW (0V). "
            "Pins hold their level until changed — they are not PWM by default. "
            "Do NOT write to a pin configured as INPUT or ADC.");
        cJSON_AddItemToObject(t, "inputSchema", schema_write_digital_pins());
        cJSON_AddItemToArray(tools, t);
    }

    // ── 4. read_pins ─────────────────────────────────────────────────────
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "read_pins");
        cJSON_AddStringToObject(t, "description",
            "Read the current value of one or more configured pins. "
            "INPUT/OUTPUT pins return 0 (LOW) or 1 (HIGH). "
            "ADC pins return millivolts (0–3100 mV on this board's 3.3V rail). "
            "PWM pins return current duty cycle (0–100 percent). "
            "Pins must be configured via configure_pins first. "
            "Do NOT poll this tool in a rapid loop to wait for an event — "
            "use it to snapshot current state.");
        cJSON_AddItemToObject(t, "inputSchema", schema_read_pins());
        cJSON_AddItemToArray(tools, t);
    }

    // ── 5. set_pwm_duty ──────────────────────────────────────────────────
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "set_pwm_duty");
        cJSON_AddStringToObject(t, "description",
            "Set the PWM duty cycle on one or more pins configured as PWM mode. "
            "duty 0 = fully off (0V average), 100 = fully on (3.3V), 50 = half power. "
            "Hardware PWM runs at 5kHz — inaudible, suitable for LED dimming and motor speed. "
            "Pins must first be configured as PWM using configure_pins. "
            "Do NOT call this on OUTPUT, INPUT, or ADC pins.");
        cJSON_AddItemToObject(t, "inputSchema", schema_set_pwm_duty());
        cJSON_AddItemToArray(tools, t);
    }

    // ── 6. i2c_scan ──────────────────────────────────────────────────────
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "i2c_scan");
        cJSON_AddStringToObject(t, "description",
            "Scan the I2C bus on the specified SDA/SCL pins and return a list of "
            "device addresses that respond. "
            "Probes all 126 standard 7-bit addresses at 100kHz. "
            "Takes up to ~2 seconds on an empty bus (devices respond in microseconds). "
            "The two pins are temporarily used for I2C and released after the scan — "
            "any previous configuration on those pins is cleared. "
            "Common addresses: 0x3C = SSD1306 OLED, 0x48 = ADS1115 ADC, "
            "0x76/0x77 = BME280 sensor, 0x27 = PCF8574 I/O expander.");
        cJSON_AddItemToObject(t, "inputSchema", schema_i2c_scan());
        cJSON_AddItemToArray(tools, t);
    }

#if BOARD_RGB_LED_GPIO >= 0
    // ── 7. set_rgb_led ───────────────────────────────────────────────────
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "set_rgb_led");
        cJSON_AddStringToObject(t, "description",
            "Set the colour of the onboard WS2812 RGB LED (GPIO 8). "
            "Each channel is 0-255. Use this for demos without any external hardware — "
            "the LED is always available. "
            "Examples: red=(255,0,0), green=(0,255,0), blue=(0,0,255), "
            "white=(255,255,255), off=(0,0,0), warm amber=(200,80,0). "
            "Tip: keep values below 80 — the LED is bright at full power.");
        cJSON_AddItemToObject(t, "inputSchema", schema_set_rgb_led());
        cJSON_AddItemToArray(tools, t);
    }
#endif

    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "tools", tools);
    s_tools_json = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    ESP_LOGI(TAG, "Tools JSON cached (%zu bytes)", strlen(s_tools_json));
}

// ── JSON-RPC response helpers ─────────────────────────────────────────────

static void send_json_str(httpd_req_t *req, const char *str)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
}

static void send_json_obj(httpd_req_t *req, cJSON *obj)
{
    char *str = cJSON_PrintUnformatted(obj);
    send_json_str(req, str);
    free(str);
    cJSON_Delete(obj);
}

static void send_result(httpd_req_t *req, int id, cJSON *result)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddItemToObject(r, "result", result);
    send_json_obj(req, r);
}

// Wrap a text string in MCP content format with optional isError flag.
static cJSON *text_content(const char *msg, bool is_error)
{
    cJSON *res     = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item    = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", msg);
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(res, "content", content);
    if (is_error) cJSON_AddTrueToObject(res, "isError");
    return res;
}

// ── Tool handlers ─────────────────────────────────────────────────────────

static void handle_get_gpio_capabilities(httpd_req_t *req, int id)
{
    // Snapshot current pin state (no hardware reads — shows last-known values)
    pin_state_t snap[SAFE_DIGITAL_COUNT];
    int count;
    gpio_state_snapshot(snap, SAFE_DIGITAL_COUNT, &count);

    // Build response JSON as a pretty-printed string inside the text content.
    cJSON *caps = cJSON_CreateObject();
    cJSON_AddStringToObject(caps, "board", BOARD_NAME);

    // safe_pins array — one entry per exposed GPIO
    cJSON *pins_arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "gpio",        snap[i].gpio);
        cJSON_AddBoolToObject(p,  "adc_capable",  board_is_adc_pin(snap[i].gpio));
        const char *note = board_pin_note(snap[i].gpio);
        if (note) cJSON_AddStringToObject(p, "note", note);
        cJSON_AddStringToObject(p, "mode",        pin_mode_str(snap[i].mode));
        if (snap[i].value < 0) {
            cJSON_AddNullToObject(p, "value");
        } else if (snap[i].mode == PIN_MODE_ADC) {
            char mv[16];
            snprintf(mv, sizeof(mv), "%dmV", snap[i].value);
            cJSON_AddStringToObject(p, "value", mv);
        } else {
            cJSON_AddStringToObject(p, "value",
                snap[i].value ? "HIGH" : "LOW");
        }
        cJSON_AddItemToArray(pins_arr, p);
    }
    cJSON_AddItemToObject(caps, "safe_pins", pins_arr);

#if BOARD_RGB_LED_GPIO >= 0
    // Onboard addressable RGB LED — controlled via set_rgb_led tool (not configure_pins)
    cJSON *rgb = cJSON_CreateObject();
    cJSON_AddNumberToObject(rgb, "gpio",        BOARD_RGB_LED_GPIO);
    cJSON_AddStringToObject(rgb, "type",        "WS2812 addressable LED");
    cJSON_AddStringToObject(rgb, "tool",        "set_rgb_led");
    cJSON_AddStringToObject(rgb, "description",
        "Onboard RGB LED. Use set_rgb_led with r/g/b 0-255 to change colour. "
        "Great for testing without any external hardware.");
    cJSON_AddItemToObject(caps, "onboard_rgb_led", rgb);
#endif

    // reserved_pins array — from board_config.h g_reserved_pins[]
    cJSON *res_arr = cJSON_CreateArray();
    for (int i = 0; i < (int)RESERVED_PINS_COUNT; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "gpio",   g_reserved_pins[i].gpio);
        cJSON_AddStringToObject(r, "reason", g_reserved_pins[i].reason);
        cJSON_AddItemToArray(res_arr, r);
    }
    cJSON_AddItemToObject(caps, "reserved_pins", res_arr);
    cJSON_AddStringToObject(caps, "adc_voltage_range", "0-3100mV (12dB attenuation, 3.3V rail)");

    char *caps_str = cJSON_Print(caps);    // pretty-printed for LLM readability
    cJSON_Delete(caps);

    cJSON *result = text_content(caps_str, false);
    free(caps_str);
    send_result(req, id, result);
}

static void handle_configure_pins(httpd_req_t *req, int id, cJSON *args)
{
    cJSON *pins_arr = args ? cJSON_GetObjectItem(args, "pins") : NULL;
    if (!pins_arr || !cJSON_IsArray(pins_arr) || cJSON_GetArraySize(pins_arr) == 0) {
        send_result(req, id, text_content(
            "configure_pins: 'pins' must be a non-empty array of {gpio, mode} objects",
            true));
        return;
    }

    cJSON *configured = cJSON_CreateArray();
    cJSON *errors     = cJSON_CreateArray();
    bool   had_error  = false;

    int n = cJSON_GetArraySize(pins_arr);
    for (int i = 0; i < n; i++) {
        cJSON *entry    = cJSON_GetArrayItem(pins_arr, i);
        cJSON *gpio_item = entry ? cJSON_GetObjectItem(entry, "gpio") : NULL;
        cJSON *mode_item = entry ? cJSON_GetObjectItem(entry, "mode") : NULL;

        if (!gpio_item || !cJSON_IsNumber(gpio_item) ||
            !mode_item || !cJSON_IsString(mode_item)) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "index", i);
            cJSON_AddStringToObject(e, "error", "each pin entry must have integer 'gpio' and string 'mode'");
            cJSON_AddItemToArray(errors, e);
            had_error = true;
            continue;
        }

        int gpio = (int)gpio_item->valuedouble;
        const char *mode_str = mode_item->valuestring;

        pin_mode_t mode;
        if      (strcmp(mode_str, "INPUT")  == 0) mode = PIN_MODE_INPUT;
        else if (strcmp(mode_str, "OUTPUT") == 0) mode = PIN_MODE_OUTPUT;
        else if (strcmp(mode_str, "ADC")    == 0) mode = PIN_MODE_ADC;
        else if (strcmp(mode_str, "PWM")    == 0) mode = PIN_MODE_PWM;
        else {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "gpio",  gpio);
            cJSON_AddStringToObject(e, "error", "mode must be INPUT, OUTPUT, ADC, or PWM");
            cJSON_AddItemToArray(errors, e);
            had_error = true;
            continue;
        }

        esp_err_t ret = gpio_state_configure(gpio, mode);
        if (ret == ESP_OK) {
            cJSON *ok = cJSON_CreateObject();
            cJSON_AddNumberToObject(ok, "gpio", gpio);
            cJSON_AddStringToObject(ok, "mode", mode_str);
            cJSON_AddItemToArray(configured, ok);
        } else {
            char errbuf[128];
            if (!board_is_safe_pin(gpio)) {
                snprintf(errbuf, sizeof(errbuf),
                         "GPIO %d is not in safe pin list — see get_gpio_capabilities", gpio);
            } else if (mode == PIN_MODE_ADC && !board_is_adc_pin(gpio)) {
                snprintf(errbuf, sizeof(errbuf),
                         "GPIO %d is not ADC-capable. ADC-capable pins: ", gpio);
                // Append the list of ADC pins
                int pos = strlen(errbuf);
                for (int j = 0; j < ADC_CAPABLE_COUNT && pos < 120; j++) {
                    pos += snprintf(errbuf + pos, sizeof(errbuf) - pos,
                                    j ? ", %d" : "%d", ADC_CAPABLE_PINS[j]);
                }
            } else {
                snprintf(errbuf, sizeof(errbuf), "hardware error: %s", esp_err_to_name(ret));
            }
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "gpio",  gpio);
            cJSON_AddStringToObject(e, "error", errbuf);
            cJSON_AddItemToArray(errors, e);
            had_error = true;
        }
    }

    // Build result object
    cJSON *res_obj = cJSON_CreateObject();
    cJSON_AddItemToObject(res_obj, "configured", configured);
    cJSON_AddItemToObject(res_obj, "errors",     errors);

    char *res_str  = cJSON_PrintUnformatted(res_obj);
    cJSON_Delete(res_obj);
    cJSON *result  = text_content(res_str, had_error);
    free(res_str);
    send_result(req, id, result);
}

static void handle_write_digital_pins(httpd_req_t *req, int id, cJSON *args)
{
    cJSON *pins_arr = args ? cJSON_GetObjectItem(args, "pins") : NULL;
    if (!pins_arr || !cJSON_IsArray(pins_arr) || cJSON_GetArraySize(pins_arr) == 0) {
        send_result(req, id, text_content(
            "write_digital_pins: 'pins' must be a non-empty array of {gpio, level} objects",
            true));
        return;
    }

    cJSON *written    = cJSON_CreateArray();
    cJSON *errors     = cJSON_CreateArray();
    bool   had_error  = false;

    int n = cJSON_GetArraySize(pins_arr);
    for (int i = 0; i < n; i++) {
        cJSON *entry     = cJSON_GetArrayItem(pins_arr, i);
        cJSON *gpio_item = entry ? cJSON_GetObjectItem(entry, "gpio")  : NULL;
        cJSON *lv_item   = entry ? cJSON_GetObjectItem(entry, "level") : NULL;

        if (!gpio_item || !cJSON_IsNumber(gpio_item) ||
            !lv_item   || !cJSON_IsNumber(lv_item)) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "index", i);
            cJSON_AddStringToObject(e, "error", "each entry must have integer 'gpio' and integer 'level' (0 or 1)");
            cJSON_AddItemToArray(errors, e);
            had_error = true;
            continue;
        }

        int gpio  = (int)gpio_item->valuedouble;
        int level = ((int)lv_item->valuedouble) ? 1 : 0;

        esp_err_t ret = gpio_state_write(gpio, level);
        if (ret == ESP_OK) {
            cJSON *ok = cJSON_CreateObject();
            cJSON_AddNumberToObject(ok, "gpio",  gpio);
            cJSON_AddNumberToObject(ok, "level", level);
            cJSON_AddItemToArray(written, ok);
        } else {
            char errbuf[128];
            if (ret == ESP_ERR_INVALID_STATE) {
                snprintf(errbuf, sizeof(errbuf),
                         "GPIO %d is not configured as OUTPUT — call configure_pins first", gpio);
            } else {
                snprintf(errbuf, sizeof(errbuf), "error: %s", esp_err_to_name(ret));
            }
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "gpio",  gpio);
            cJSON_AddStringToObject(e, "error", errbuf);
            cJSON_AddItemToArray(errors, e);
            had_error = true;
        }
    }

    cJSON *res_obj = cJSON_CreateObject();
    cJSON_AddItemToObject(res_obj, "written", written);
    cJSON_AddItemToObject(res_obj, "errors",  errors);

    char *res_str = cJSON_PrintUnformatted(res_obj);
    cJSON_Delete(res_obj);
    cJSON *result = text_content(res_str, had_error);
    free(res_str);
    send_result(req, id, result);
}

static void handle_read_pins(httpd_req_t *req, int id, cJSON *args)
{
    cJSON *gpios_arr = args ? cJSON_GetObjectItem(args, "gpios") : NULL;
    if (!gpios_arr || !cJSON_IsArray(gpios_arr) || cJSON_GetArraySize(gpios_arr) == 0) {
        send_result(req, id, text_content(
            "read_pins: 'gpios' must be a non-empty array of GPIO integers",
            true));
        return;
    }

    cJSON *readings  = cJSON_CreateArray();
    cJSON *errors    = cJSON_CreateArray();
    bool   had_error = false;

    int n = cJSON_GetArraySize(gpios_arr);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(gpios_arr, i);
        if (!item || !cJSON_IsNumber(item)) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "index", i);
            cJSON_AddStringToObject(e, "error", "each entry must be an integer GPIO number");
            cJSON_AddItemToArray(errors, e);
            had_error = true;
            continue;
        }

        int gpio = (int)item->valuedouble;
        int val  = 0;
        esp_err_t ret = gpio_state_read(gpio, &val);

        if (ret == ESP_OK) {
            // Look up current mode for the response
            pin_state_t snap[SAFE_DIGITAL_COUNT];
            int cnt;
            gpio_state_snapshot(snap, SAFE_DIGITAL_COUNT, &cnt);
            pin_mode_t mode = PIN_MODE_UNCONFIGURED;
            for (int j = 0; j < cnt; j++) {
                if (snap[j].gpio == gpio) { mode = snap[j].mode; break; }
            }

            cJSON *r = cJSON_CreateObject();
            cJSON_AddNumberToObject(r, "gpio", gpio);
            cJSON_AddStringToObject(r, "mode", pin_mode_str(mode));
            if (mode == PIN_MODE_ADC) {
                cJSON_AddNumberToObject(r, "value", val);
                cJSON_AddStringToObject(r, "unit",  "mV");
            } else {
                cJSON_AddNumberToObject(r, "value", val);
                cJSON_AddStringToObject(r, "unit",  "digital");
                cJSON_AddStringToObject(r, "level", val ? "HIGH" : "LOW");
            }
            cJSON_AddItemToArray(readings, r);
        } else {
            char errbuf[128];
            if (ret == ESP_ERR_INVALID_STATE) {
                snprintf(errbuf, sizeof(errbuf),
                         "GPIO %d is not configured — call configure_pins first", gpio);
            } else if (!board_is_safe_pin(gpio)) {
                snprintf(errbuf, sizeof(errbuf),
                         "GPIO %d is not in safe pin list", gpio);
            } else {
                snprintf(errbuf, sizeof(errbuf), "read error: %s", esp_err_to_name(ret));
            }
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "gpio",  gpio);
            cJSON_AddStringToObject(e, "error", errbuf);
            cJSON_AddItemToArray(errors, e);
            had_error = true;
        }
    }

    cJSON *res_obj = cJSON_CreateObject();
    cJSON_AddItemToObject(res_obj, "readings", readings);
    cJSON_AddItemToObject(res_obj, "errors",   errors);

    char *res_str = cJSON_PrintUnformatted(res_obj);
    cJSON_Delete(res_obj);
    cJSON *result = text_content(res_str, had_error);
    free(res_str);
    send_result(req, id, result);
}

static void handle_set_pwm_duty(httpd_req_t *req, int id, cJSON *args)
{
    cJSON *pins_arr = args ? cJSON_GetObjectItem(args, "pins") : NULL;
    if (!pins_arr || !cJSON_IsArray(pins_arr) || cJSON_GetArraySize(pins_arr) == 0) {
        send_result(req, id, text_content(
            "set_pwm_duty: 'pins' must be a non-empty array of {gpio, duty} objects", true));
        return;
    }

    cJSON *updated   = cJSON_CreateArray();
    cJSON *errors    = cJSON_CreateArray();
    bool   had_error = false;

    int n = cJSON_GetArraySize(pins_arr);
    for (int i = 0; i < n; i++) {
        cJSON *entry     = cJSON_GetArrayItem(pins_arr, i);
        cJSON *gpio_item = entry ? cJSON_GetObjectItem(entry, "gpio") : NULL;
        cJSON *duty_item = entry ? cJSON_GetObjectItem(entry, "duty") : NULL;

        if (!gpio_item || !cJSON_IsNumber(gpio_item) ||
            !duty_item || !cJSON_IsNumber(duty_item)) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "index", i);
            cJSON_AddStringToObject(e, "error", "each entry must have integer 'gpio' and integer 'duty' (0-100)");
            cJSON_AddItemToArray(errors, e);
            had_error = true;
            continue;
        }

        int gpio = (int)gpio_item->valuedouble;
        int duty = (int)duty_item->valuedouble;

        esp_err_t ret = gpio_state_set_pwm_duty(gpio, duty);
        if (ret == ESP_OK) {
            cJSON *ok = cJSON_CreateObject();
            cJSON_AddNumberToObject(ok, "gpio", gpio);
            cJSON_AddNumberToObject(ok, "duty", duty < 0 ? 0 : duty > 100 ? 100 : duty);
            cJSON_AddItemToArray(updated, ok);
        } else {
            char errbuf[128];
            if (ret == ESP_ERR_INVALID_STATE) {
                snprintf(errbuf, sizeof(errbuf),
                         "GPIO %d is not in PWM mode — call configure_pins with mode PWM first", gpio);
            } else {
                snprintf(errbuf, sizeof(errbuf), "error: %s", esp_err_to_name(ret));
            }
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "gpio",  gpio);
            cJSON_AddStringToObject(e, "error", errbuf);
            cJSON_AddItemToArray(errors, e);
            had_error = true;
        }
    }

    cJSON *res_obj = cJSON_CreateObject();
    cJSON_AddItemToObject(res_obj, "updated", updated);
    cJSON_AddItemToObject(res_obj, "errors",  errors);
    char *res_str = cJSON_PrintUnformatted(res_obj);
    cJSON_Delete(res_obj);
    send_result(req, id, text_content(res_str, had_error));
    free(res_str);
}

static void handle_i2c_scan(httpd_req_t *req, int id, cJSON *args)
{
    cJSON *sda_item = args ? cJSON_GetObjectItem(args, "sda") : NULL;
    cJSON *scl_item = args ? cJSON_GetObjectItem(args, "scl") : NULL;

    if (!sda_item || !cJSON_IsNumber(sda_item) ||
        !scl_item || !cJSON_IsNumber(scl_item)) {
        send_result(req, id, text_content(
            "i2c_scan: 'sda' and 'scl' must be integer GPIO numbers", true));
        return;
    }

    int sda = (int)sda_item->valuedouble;
    int scl = (int)scl_item->valuedouble;

    if (sda == scl) {
        send_result(req, id, text_content("i2c_scan: sda and scl must be different pins", true));
        return;
    }
    if (!board_is_safe_pin(sda) || !board_is_safe_pin(scl)) {
        send_result(req, id, text_content(
            "i2c_scan: both sda and scl must be in the safe pin list", true));
        return;
    }

    // Create I2C master bus — internal pull-ups enabled, 100kHz standard mode.
    i2c_master_bus_config_t bus_cfg = {
        .clk_source                  = I2C_CLK_SRC_DEFAULT,
        .i2c_port                    = I2C_NUM_0,
        .scl_io_num                  = scl,
        .sda_io_num                  = sda,
        .glitch_ignore_cnt           = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus);
    if (ret != ESP_OK) {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "i2c_scan: failed to init I2C bus: %s",
                 esp_err_to_name(ret));
        send_result(req, id, text_content(errbuf, true));
        return;
    }

    // Probe all 126 standard 7-bit addresses.
    cJSON *found = cJSON_CreateArray();
    int found_count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2c_master_probe(bus, addr, 10) == ESP_OK) {
            cJSON *dev = cJSON_CreateObject();
            char hex[8];
            snprintf(hex, sizeof(hex), "0x%02X", addr);
            cJSON_AddStringToObject(dev, "address_hex", hex);
            cJSON_AddNumberToObject(dev, "address_dec", addr);
            cJSON_AddItemToArray(found, dev);
            found_count++;
            ESP_LOGI(TAG, "I2C device at 0x%02X", addr);
        }
    }

    i2c_del_master_bus(bus);

    // Release the pins back to unconfigured state — the I2C driver has
    // reconfigured them; gpio_state must reflect reality.
    gpio_state_configure(sda, PIN_MODE_UNCONFIGURED);
    gpio_state_configure(scl, PIN_MODE_UNCONFIGURED);

    cJSON *res_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(res_obj, "sda",     sda);
    cJSON_AddNumberToObject(res_obj, "scl",     scl);
    cJSON_AddNumberToObject(res_obj, "freq_hz", 100000);
    cJSON_AddNumberToObject(res_obj, "scanned", 126);
    cJSON_AddNumberToObject(res_obj, "found_count", found_count);
    cJSON_AddItemToObject(res_obj, "found", found);
    if (found_count == 0) {
        cJSON_AddStringToObject(res_obj, "hint",
            "No devices detected. Check wiring and pull-up resistors (4.7kΩ to 3.3V on SDA+SCL).");
    }

    char *res_str = cJSON_PrintUnformatted(res_obj);
    cJSON_Delete(res_obj);
    send_result(req, id, text_content(res_str, false));
    free(res_str);
}

static void handle_set_rgb_led(httpd_req_t *req, int id, cJSON *args)
{
    cJSON *r_item = args ? cJSON_GetObjectItem(args, "r") : NULL;
    cJSON *g_item = args ? cJSON_GetObjectItem(args, "g") : NULL;
    cJSON *b_item = args ? cJSON_GetObjectItem(args, "b") : NULL;

    if (!r_item || !cJSON_IsNumber(r_item) ||
        !g_item || !cJSON_IsNumber(g_item) ||
        !b_item || !cJSON_IsNumber(b_item)) {
        send_result(req, id, text_content(
            "set_rgb_led: 'r', 'g', 'b' must all be integers 0-255", true));
        return;
    }

    int r = (int)r_item->valuedouble;
    int g = (int)g_item->valuedouble;
    int b = (int)b_item->valuedouble;

    // Clamp to valid range
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;

    led_status_set_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "r", r);
    cJSON_AddNumberToObject(res, "g", g);
    cJSON_AddNumberToObject(res, "b", b);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    send_result(req, id, text_content(res_str, false));
    free(res_str);
}

// ── Public dispatch ────────────────────────────────────────────────────────

void handle_tools_list(httpd_req_t *req, int id)
{
    char *resp = NULL;
    asprintf(&resp, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, s_tools_json);
    if (resp) { send_json_str(req, resp); free(resp); }
    else      {
        // OOM — send minimal error inline
        send_json_str(req,
            "{\"jsonrpc\":\"2.0\",\"id\":0,\"error\":{\"code\":-32603,\"message\":\"OOM\"}}");
    }
}

void handle_tools_call(httpd_req_t *req, int id, cJSON *params)
{
    cJSON *name_item = params ? cJSON_GetObjectItem(params, "name")      : NULL;
    cJSON *args      = params ? cJSON_GetObjectItem(params, "arguments") : NULL;

    if (!name_item || !cJSON_IsString(name_item)) {
        // Protocol-level error — missing tool name
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(r, "id", id);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code",    -32602);
        cJSON_AddStringToObject(err, "message", "tools/call: missing 'name'");
        cJSON_AddItemToObject(r, "error", err);
        send_json_obj(req, r);
        return;
    }

    const char *tool = name_item->valuestring;
    if      (strcmp(tool, "get_gpio_capabilities") == 0) handle_get_gpio_capabilities(req, id);
    else if (strcmp(tool, "configure_pins")         == 0) handle_configure_pins(req, id, args);
    else if (strcmp(tool, "write_digital_pins")     == 0) handle_write_digital_pins(req, id, args);
    else if (strcmp(tool, "read_pins")              == 0) handle_read_pins(req, id, args);
    else if (strcmp(tool, "set_pwm_duty")           == 0) handle_set_pwm_duty(req, id, args);
    else if (strcmp(tool, "i2c_scan")               == 0) handle_i2c_scan(req, id, args);
    else if (strcmp(tool, "set_rgb_led")            == 0) handle_set_rgb_led(req, id, args);
    else {
        cJSON *r   = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(r, "id", id);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code",    -32601);
        cJSON_AddStringToObject(err, "message", "Unknown tool");
        cJSON_AddItemToObject(r, "error", err);
        send_json_obj(req, r);
    }
}
