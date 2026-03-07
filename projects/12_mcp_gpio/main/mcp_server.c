#include "mcp_server.h"
#include "mcp_tools.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mcp_server";

// ── Wi-Fi ─────────────────────────────────────────────────────────────────

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(char *ip_out, size_t ip_len)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_wifi, inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = CONFIG_GPIO_WIFI_SSID,
            .password = CONFIG_GPIO_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi connection failed");
        return ESP_FAIL;
    }

    // Disable modem sleep — default WIFI_PS_MIN_MODEM adds 100–300 ms per request
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    snprintf(ip_out, ip_len, IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Connected, IP: %s", ip_out);
    return ESP_OK;
}

// ── JSON-RPC helpers ───────────────────────────────────────────────────────

static void send_json_str(httpd_req_t *req, const char *str)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
}

static void send_error(httpd_req_t *req, int id, int code, const char *msg)
{
    cJSON *r   = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    if (id >= 0) cJSON_AddNumberToObject(r, "id", id);
    else         cJSON_AddNullToObject(r, "id");
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code",    code);
    cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(r, "error", err);
    char *str = cJSON_PrintUnformatted(r);
    send_json_str(req, str);
    free(str);
    cJSON_Delete(r);
}

static void send_result(httpd_req_t *req, int id, cJSON *result)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddItemToObject(r, "result", result);
    char *str = cJSON_PrintUnformatted(r);
    send_json_str(req, str);
    free(str);
    cJSON_Delete(r);
}

// ── MCP method router ──────────────────────────────────────────────────────

static bool s_initialized = false;

static void handle_initialize(httpd_req_t *req, int id)
{
    s_initialized = true;
    cJSON *res  = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "protocolVersion", "2024-11-05");
    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name",    "esp32-gpio");
    cJSON_AddStringToObject(info, "version", "1.0.0");
    cJSON_AddItemToObject(res, "serverInfo", info);
    cJSON *caps  = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON_AddItemToObject(caps, "tools", tools);
    cJSON_AddItemToObject(res, "capabilities", caps);
    send_result(req, id, res);
}

// ── HTTP URI handlers ──────────────────────────────────────────────────────

#define RECV_BUF_SIZE 2048

static esp_err_t mcp_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= RECV_BUF_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request size");
        return ESP_OK;
    }
    char buf[RECV_BUF_SIZE];
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
        return ESP_OK;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        send_json_str(req,
            "{\"jsonrpc\":\"2.0\",\"id\":null,"
            "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
        return ESP_OK;
    }

    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    cJSON *method  = cJSON_GetObjectItem(root, "method");
    int    id      = (id_item && cJSON_IsNumber(id_item)) ? (int)id_item->valuedouble : 0;

    if (!method || !cJSON_IsString(method)) {
        send_error(req, id, -32600, "Invalid Request: missing method");
        cJSON_Delete(root);
        return ESP_OK;
    }

    const char *m = method->valuestring;
    if      (strcmp(m, "initialize")  == 0) handle_initialize(req, id);
    else if (strcmp(m, "tools/list")  == 0) handle_tools_list(req, id);
    else if (strcmp(m, "tools/call")  == 0) handle_tools_call(req, id, cJSON_GetObjectItem(root, "params"));
    else                                     send_error(req, id, -32601, "Method not found");

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ping_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"healthy\",\"server\":\"esp32-gpio\"}");
    return ESP_OK;
}

// ── Server start ───────────────────────────────────────────────────────────

esp_err_t mcp_server_start(void)
{
    mcp_tools_init();

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32-gpio"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-C6 GPIO MCP"));
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: esp32-gpio.local");

    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers  = 3;
    config.stack_size        = 8192;   // 4KB default is too small for cJSON ops
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    static const httpd_uri_t mcp_uri = {
        .uri     = "/mcp",
        .method  = HTTP_POST,
        .handler = mcp_post_handler,
    };
    static const httpd_uri_t ping_uri = {
        .uri     = "/ping",
        .method  = HTTP_GET,
        .handler = ping_get_handler,
    };

    httpd_register_uri_handler(server, &mcp_uri);
    httpd_register_uri_handler(server, &ping_uri);

    ESP_LOGI(TAG, "HTTP server started — POST /mcp  GET /ping");
    return ESP_OK;
}
