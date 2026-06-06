#include "PublishConfig.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <algorithm>

#include "esp_log.h"
#include "ClassLogFile.h"

static const char *TAG = "PUBCFG";
#define PUBLISH_CFG_FILE "/sdcard/config/publishing.cfg"

namespace {
    std::map<std::string, bool> g_overrides;   // "<platform>.<field>" -> on/off (explicit user choices)
    bool g_modeChanged = false;
    bool g_loaded = false;

    std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

    std::string trim(const std::string &s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // The v17 baseline: which <platform>.<field> are published today. Used when the file omits a key,
    // so an existing device (no/partial publishing.cfg) behaves exactly as before this feature.
    bool defaultEnabled(const std::string &platform, const std::string &field) {
        static const std::set<std::string> mqttOn = {
            "value", "raw", "error", "rate", "rate_per_time_unit", "rate_per_digitization_round",
            "timestamp", "leak", "continuous_usage", "json",
            "uptime", "freeMem", "wifiRSSI", "CPUtemp", "processingTime", "analysisType",
            "digitsAnalyzed", "digitsTotal", "fwVersion", "MAC", "IP", "hostname", "interval",
            "connection"   // status/flowstart are Home-Assistant-only entities, not MQTT topics
        };
        static const std::set<std::string> influxOn = {
            // Only fields InfluxDB actually writes today (value + leak + round diagnostics). raw/rate/
            // prevalue/confidence are exposed as opt-in additions on the Data Publishing page (default off).
            "value", "leak", "continuous_usage",
            "processingTime", "analysisType", "digitsAnalyzed"
        };
        static const std::set<std::string> haOn = {
            "value", "raw", "error", "rate_per_time_unit", "rate_per_digitization_round",
            "timestamp", "json", "problem", "leak", "continuous_usage",
            "uptime", "MAC", "fwVersion", "hostname", "freeMem", "wifiRSSI", "CPUtemp",
            "processingTime", "interval", "IP", "status", "flowstart"
        };
        if (platform == "mqtt")   return mqttOn.count(field) > 0;
        if (platform == "influx") return influxOn.count(field) > 0;
        if (platform == "ha")     return haOn.count(field) > 0;
        return false;
    }
}

void PublishConfig::Load() {
    g_overrides.clear();
    g_modeChanged = false;
    g_loaded = true;

    FILE *f = fopen(PUBLISH_CFG_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "No %s - using publish defaults (baseline behaviour)", PUBLISH_CFG_FILE);
        return;
    }

    char buf[200];
    while (fgets(buf, sizeof(buf), f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (lower(key) == "mode")
            g_modeChanged = (lower(val) == "changed");
        else
            g_overrides[key] = (val == "1" || lower(val) == "true" || lower(val) == "on");
    }
    fclose(f);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Loaded publishing.cfg: " + std::to_string(g_overrides.size()) +
            " override(s), mode=" + (g_modeChanged ? "changed" : "always"));
}

bool PublishConfig::IsEnabled(const char *platform, const std::string &field) {
    if (!g_loaded) Load();
    auto it = g_overrides.find(std::string(platform) + "." + field);
    if (it != g_overrides.end()) return it->second;
    return defaultEnabled(platform, field);
}

bool PublishConfig::SendOnlyChanged() {
    if (!g_loaded) Load();
    return g_modeChanged;
}
