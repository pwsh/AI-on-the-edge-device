#include "server_backup.h"

#include "miniz.h"
#include "ClassLogFile.h"
#include "connect_wlan.h"     // getHostname
#include "time_sntp.h"        // getCurrentTimeString
#include "basic_auth.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cctype>
#include <set>
#include <vector>
#include <algorithm>

static const char *TAG = "backup";

#define BACKUP_DIR      "/sdcard/backup"
#define BACKUP_TMP_DL   "/sdcard/backup_dl.zip"
#define BACKUP_KEEP     5      // scheduled backups to retain on the card

// Models shipped with a fresh install: excluded from the backup (a custom model has a
// different filename and IS included, so a restore stays complete on a fresh card).
// Models shipped with a fresh install are excluded from backups (server-side AND the client-side
// backup.html via /defaultmodels) - a restore onto a fresh card already has them; a custom-trained
// model has a different filename and IS backed up.
//
// The authoritative list is the manifest /sdcard/config/default_models.txt, which is generated from
// the shipped sd-card/config/*.tflite at build time and ships with the firmware (so it can never go
// stale). The hardcoded list below is only a fallback for an older card that lacks the manifest.
#define DEFAULT_MODELS_MANIFEST "/sdcard/config/default_models.txt"

static const std::set<std::string> g_defaultModelsFallback = {
    "ana-cont_1300_s2.tflite", "ana-cont_1400_s2_q.tflite", "ana-cont_1500_s2_q.tflite",
    "dig-class100-0173-s2-q.tflite", "dig-class100-0180-s2-q.tflite", "dig-class100-0182-s2_q.tflite",
    "dig-class11_1701_s2.tflite", "dig-class11_1900_s2_q.tflite", "dig-class11_1910_s2_q.tflite",
    "dig-cont_0700_s3_q.tflite", "dig-cont_0712_s3_q.tflite", "dig-cont_0800_s3_q.tflite",
    "dig-cont_0810_s3_q.tflite", "dig-cont_0900_s3_q.tflite"
};

// Lazy-loaded once: read the shipped manifest; fall back to the compiled-in list if it is missing.
static const std::set<std::string>& getDefaultModels()
{
    static std::set<std::string> models;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        FILE *f = fopen(DEFAULT_MODELS_MANIFEST, "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                std::string n = line;
                while (!n.empty() && (n.back() == '\n' || n.back() == '\r' || n.back() == ' ' || n.back() == '\t')) n.pop_back();
                size_t s = n.find_first_not_of(" \t");
                if (s != std::string::npos) n = n.substr(s); else n.clear();
                if (!n.empty() && n[0] != '#') models.insert(n);
            }
            fclose(f);
        }
        if (models.empty()) {
            models = g_defaultModelsFallback;
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "default_models.txt missing/empty - using the built-in fallback model list");
        } else {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Loaded " + std::to_string(models.size()) + " shipped models from default_models.txt");
        }
    }
    return models;
}

static bool isDefaultModel(const std::string &name)
{
    return getDefaultModels().count(name) > 0;
}

// GET /defaultmodels -> newline-separated list of the shipped model filenames, so the client-side
// backup (backup.html) can exclude exactly the same models the server-side backup does.
static esp_err_t handler_default_models(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    std::string out;
    for (const auto &m : getDefaultModels()) out += m + "\n";
    httpd_resp_sendstr(req, out.c_str());
    return ESP_OK;
}

static bool hasSuffix(const std::string &s, const std::string &suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// Recursively add a directory's files to the zip, skipping default model files.
static bool addDirToZip(mz_zip_archive *zip, const std::string &fsDir, const std::string &arcPrefix)
{
    DIR *d = opendir(fsDir.c_str());
    if (!d) return true;   // missing dir -> nothing to add (not an error)
    bool ok = true;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string src = fsDir + "/" + name;
        std::string arc = arcPrefix + name;
        struct stat st;
        if (stat(src.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!addDirToZip(zip, src, arc + "/")) { ok = false; break; }
        } else {
            if ((hasSuffix(name, ".tflite") || hasSuffix(name, ".tfl")) && isDefaultModel(name))
                continue;   // default model -> a fresh install already has it
            if (!mz_zip_writer_add_file(zip, arc.c_str(), src.c_str(), NULL, 0, MZ_BEST_SPEED)) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to add to backup: " + src);
                ok = false; break;
            }
        }
    }
    closedir(d);
    return ok;
}

bool CreateBackupZip(const std::string &outPath)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    remove(outPath.c_str());

    if (!mz_zip_writer_init_file(&zip, outPath.c_str(), 0)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Could not create backup zip: " + outPath);
        return false;
    }

    bool ok = addDirToZip(&zip, "/sdcard/config", "config/");

    // WLAN credentials live at the card root and are needed to rejoin Wi-Fi after a restore.
    struct stat st;
    if (ok && stat("/sdcard/wlan.ini", &st) == 0)
        ok = mz_zip_writer_add_file(&zip, "wlan.ini", "/sdcard/wlan.ini", NULL, 0, MZ_BEST_SPEED);

    if (ok) ok = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    if (!ok) {
        remove(outPath.c_str());
        return false;
    }
    return true;
}

std::string GetBackupFileName(void)
{
    std::string host = *getHostname();
    if (host.empty()) host = "ai-on-the-edge";
    for (char &c : host)
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) c = '_';

    std::string ts = getCurrentTimeString("%Y%m%d-%H%M%S");
    if (ts.empty()) ts = "nodate";
    return host + "_" + ts + ".zip";
}

// ---- GET /backup : build a zip and stream it as a download -----------------
static esp_err_t handler_backup(httpd_req_t *req)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Creating backup zip for download...");
    if (!CreateBackupZip(BACKUP_TMP_DL)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create backup");
        return ESP_FAIL;
    }

    std::string fname = GetBackupFileName();
    char dispo[160];
    snprintf(dispo, sizeof(dispo), "attachment; filename=\"%s\"", fname.c_str());
    httpd_resp_set_type(req, "application/zip");
    httpd_resp_set_hdr(req, "Content-Disposition", dispo);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    FILE *f = fopen(BACKUP_TMP_DL, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Backup open failed");
        return ESP_FAIL;
    }
    char *buf = (char *) malloc(8192);
    esp_err_t res = ESP_OK;
    if (!buf) {
        res = ESP_FAIL;
    } else {
        size_t n;
        while ((n = fread(buf, 1, 8192, f)) > 0) {
            if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) { res = ESP_FAIL; break; }
        }
        free(buf);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);   // signal end of response
    remove(BACKUP_TMP_DL);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Backup downloaded: " + fname);
    return res;
}

void register_server_backup_uri(httpd_handle_t server)
{
    httpd_uri_t u = { };
    u.uri      = "/backup";
    u.method   = HTTP_GET;
    u.handler  = APPLY_BASIC_AUTH_FILTER(handler_backup);
    u.user_ctx = (void *) "Backup";
    httpd_register_uri_handler(server, &u);

    httpd_uri_t dm = { };
    dm.uri      = "/defaultmodels";
    dm.method   = HTTP_GET;
    dm.handler  = APPLY_BASIC_AUTH_FILTER(handler_default_models);
    dm.user_ctx = (void *) "DefaultModels";
    httpd_register_uri_handler(server, &dm);
}

// ---- Scheduled backup to the SD card ---------------------------------------
// Trim the backup folder to the newest BACKUP_KEEP zips.
static void pruneOldBackups(void)
{
    DIR *d = opendir(BACKUP_DIR);
    if (!d) return;
    std::vector<std::string> zips;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (hasSuffix(name, ".zip")) zips.push_back(name);
    }
    closedir(d);
    if (zips.size() <= BACKUP_KEEP) return;
    std::sort(zips.begin(), zips.end());   // names start with the timestamp-bearing scheme -> oldest first by string
    for (size_t i = 0; i + BACKUP_KEEP < zips.size(); ++i)
        remove((std::string(BACKUP_DIR) + "/" + zips[i]).c_str());
}

// Set by the config layer (main.cpp, from [System] BackupInterval). 0 = disabled.
static int s_backupIntervalDays = 0;
void setBackupAutoIntervalDays(int days) { s_backupIntervalDays = days; }

void CheckScheduledBackup(void)
{
    int intervalDays = s_backupIntervalDays;
    if (intervalDays <= 0) return;                 // scheduled backup disabled

    static int64_t lastBackupUs = 0;
    int64_t nowUs = esp_timer_get_time();
    int64_t periodUs = (int64_t) intervalDays * 24 * 3600 * 1000000LL;
    if (lastBackupUs != 0 && (nowUs - lastBackupUs) < periodUs) return;

    mkdir(BACKUP_DIR, 0777);
    std::string out = std::string(BACKUP_DIR) + "/" + GetBackupFileName();
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Scheduled backup -> " + out);
    if (CreateBackupZip(out)) {
        lastBackupUs = nowUs;
        pruneOldBackups();
    } else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Scheduled backup failed");
    }
}
