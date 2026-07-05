#pragma once

#include <esp_http_server.h>

// (Re-)apply the web-password settings from wlan_config. Safe to call again at runtime after
// wlan.ini changed - enables OR disables the filter to match the new state.
void init_basic_auth();
// True when the basic-auth filter is currently enforcing credentials.
bool basic_auth_enabled();
esp_err_t basic_auth_request_filter(httpd_req_t *req, esp_err_t original_handler(httpd_req_t *));

#define APPLY_BASIC_AUTH_FILTER(method) [](httpd_req_t *req){ return basic_auth_request_filter(req, method); }
