#pragma once

#include <esp_http_server.h>
#include <string>

// Create a backup zip of everything needed to restore this device onto a fresh
// install: the whole /config folder (config.ini, reference images, align.txt,
// prevalue, certs, ...) plus /wlan.ini and any CUSTOM model (default models are
// skipped - a fresh install already ships them). Returns true on success.
bool CreateBackupZip(const std::string &outPath);

// "<hostname>_<YYYYMMDD-HHMMSS>.zip"
std::string GetBackupFileName();

// Registers GET /backup (creates a zip and streams it as a download).
void register_server_backup_uri(httpd_handle_t server);

// Called periodically from the flow loop. If scheduled backup is enabled and due,
// writes a backup to /sdcard/backup/<hostname>_<date>.zip (keeping the newest few).
void CheckScheduledBackup(void);

// Set the scheduled-backup interval in days (0 = disabled); called by the config layer.
void setBackupAutoIntervalDays(int days);
