#pragma once

#include <esp_http_server.h>

// Registers the cleanup endpoints:
//   GET  /cleanup?task=list    -> JSON of files on the SD card that are NOT part of the current
//                                 deployment (manifest html/deployment.lst) and NOT protected
//                                 (config / reference images / backups / models / wlan.ini / logs).
//   POST /cleanup?task=delete  -> body = newline-separated relative paths; deletes only files that
//                                 re-validate as cleanup candidates (never protected/deployment).
void register_server_cleanup_uri(httpd_handle_t server);
