#pragma once

#ifndef READ_WLANINI_H
#define READ_WLANINI_H

#include <string>

struct wlan_config {
    std::string ssid = "";
    std::string password = "";
    std::string hostname = "";               // empty -> auto "edgeai-<last6 of MAC>" (see connect_wlan)
    std::string ipaddress = "";
    std::string gateway = "";
    std::string netmask = "";
    std::string dns = "";
    std::string http_username = "";
    std::string http_password = "";
    // Web-password (HTTP basic auth) master switch: 1 = on, 0 = off, -1 = not set in wlan.ini.
    // When unset (legacy files), auth is enabled by the PRESENCE of both credentials, as before.
    int http_auth = -1;
    int rssi_threshold = 0;                 // Default: 0 -> ROAMING disabled
};
extern struct wlan_config wlan_config;


int LoadWlanFromFile(std::string fn);
bool ChangeHostName(std::string fn, std::string _newhostname);
bool ChangeRSSIThreshold(std::string fn, int _newrssithreshold);


#endif //READ_WLANINI_H