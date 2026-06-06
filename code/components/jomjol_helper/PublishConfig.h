#pragma once

#include <string>

// Central, file-backed control over WHICH parameters are published to each integration (MQTT,
// InfluxDB, Home Assistant) and whether a sequence's reading topics are only sent when its value
// changed. Backed by /sdcard/config/publishing.cfg (flat "key=value" lines), written by the
// "Data Publishing" web page.
//
// Deliberately NOT stored in config.ini: the web config editor only round-trips keys it knows about
// and would silently drop these on the next "Save Config".
namespace PublishConfig {
    // Platform keys used by IsEnabled().
    constexpr const char *MQTT   = "mqtt";
    constexpr const char *INFLUX = "influx";
    constexpr const char *HA     = "ha";

    // (Re)load /sdcard/config/publishing.cfg. Safe to call repeatedly. A missing file => all defaults.
    void Load();

    // Is <platform>.<field> enabled? Falls back to the built-in default table (the v17 baseline:
    // currently-published fields = on, newly-exposed fields = off) when the key is absent from the
    // file, so a device without publishing.cfg behaves exactly as before.
    bool IsEnabled(const char *platform, const std::string &field);

    // mode=changed => only publish a sequence's reading topics when its value changed since last sent
    // (diagnostics/liveness always send). Default false (= always).
    bool SendOnlyChanged();
}
