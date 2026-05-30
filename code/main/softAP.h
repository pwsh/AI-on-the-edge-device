#ifdef ENABLE_SOFTAP

#ifndef SOFTAP_H
#define SOFTAP_H

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
// NOTE: "protocol_examples_common.h" used to be included here but nothing in softAP.cpp uses it
// (only esp_netif_create_default_wifi_ap() from esp_netif). Dropping it avoids pulling the IDF
// example component into the build just to satisfy an unused include.
#include "esp_tls_crypto.h"
#include <esp_http_server.h>

void CheckStartAPMode();

#endif  //SOFTAP_H

#endif //#ifdef ENABLE_SOFTAP