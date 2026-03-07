#pragma once

#include "esp_http_server.h"
#include "cJSON.h"

// Called once at startup — builds and caches the tools/list JSON string.
void mcp_tools_init(void);

// Serve the cached tools/list response, wrapped in the JSON-RPC envelope.
void handle_tools_list(httpd_req_t *req, int id);

// Dispatch a tools/call request. Reads "name" and "arguments" from params.
void handle_tools_call(httpd_req_t *req, int id, cJSON *params);
