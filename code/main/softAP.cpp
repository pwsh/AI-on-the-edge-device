#ifdef ENABLE_SOFTAP
//if ENABLE_SOFTAP = disabled, set CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n in sdkconfig.defaults to save 28k of flash
#include "../../include/defines.h"


#include "softAP.h"

/*  WiFi softAP Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "stdio.h"

#include "ClassLogFile.h"
#include "server_help.h"
#include "defines.h"
#include "Helper.h"
#include "statusled.h"
#include "server_ota.h"
#include "basic_auth.h"
#include "server_GPIO.h"   // driveSystemStatusWs281x() - onboard RGB status (S3)

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

/* The examples use WiFi configuration that you can set via project configuration menu.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

bool isConfigINI = false;
bool isWlanINI = false;

static const char *TAG = "WIFI AP";

static volatile int s_apClientCount = 0;   // stations currently connected to our AP
bool s_apForcedReconfig = false;           // AP started because Wi-Fi connect failed (not missing setup)

// Seconds with no client connected to the AP before we reboot to retry the configured Wi-Fi (only
// when the AP was entered due to a Wi-Fi connection failure, i.e. settings exist but didn't work).
#define WIFI_AP_RETRY_SECONDS 300

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        s_apClientCount++;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d (clients=%d)",
                 MAC2STR(event->mac), event->aid, s_apClientCount);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        if (s_apClientCount > 0) s_apClientCount--;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d (clients=%d)",
                 MAC2STR(event->mac), event->aid, s_apClientCount);
    }
}


void wifi_init_softAP(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = { };

    strcpy((char*)wifi_config.ap.ssid, (const char*) EXAMPLE_ESP_WIFI_SSID);
    strcpy((char*)wifi_config.ap.password, (const char*) EXAMPLE_ESP_WIFI_PASS);
    wifi_config.ap.channel = EXAMPLE_ESP_WIFI_CHANNEL;
    wifi_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "started with SSID \"%s\", password: \"%s\", channel: %d. Connect to AP and open http://192.168.4.1",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);

    // Captive portal, part 1: make the AP's DHCP server hand out *ourselves* (192.168.4.1) as the DNS
    // server. Without this the client has no DNS in AP mode, so its connectivity-check lookup never
    // reaches us and the "Sign in to network" page never pops. dhcps must be stopped to change options.
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != NULL) {
        esp_netif_dhcps_stop(ap_netif);   // ignore "already stopped"
        esp_netif_dns_info_t dns_info = {};
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton("192.168.4.1");
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        uint8_t dns_offer = 0x02;   // OFFER_DNS - tell dhcps to include the DNS-server option (6)
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                               &dns_offer, sizeof(dns_offer));
        esp_netif_dhcps_start(ap_netif);
    }
}


// Captive portal, part 2: a minimal DNS server that answers *every* A query with the AP's own IP
// (192.168.4.1). Combined with the DHCP DNS offer above, this hijacks the client's connectivity-check
// lookup (Android connectivitycheck.gstatic.com, Apple captive.apple.com, Windows msftconnecttest.com)
// onto our HTTP server, which then serves/redirects to the setup page - triggering the OS captive-portal
// prompt automatically. AP-mode only; the task lives until the device reboots out of AP mode.
#define CAPTIVE_DNS_PORT 53
#define CAPTIVE_DNS_MAXLEN 256

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} captive_dns_header_t;

static void captive_dns_task(void *pvParameters)
{
    uint8_t rx[CAPTIVE_DNS_MAXLEN];
    uint8_t tx[CAPTIVE_DNS_MAXLEN];

    uint32_t ap_ip;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info;
    if (ap_netif != NULL && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        ap_ip = ip_info.ip.addr;            // already in network byte order
    } else {
        ap_ip = esp_ip4addr_aton("192.168.4.1");
    }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(CAPTIVE_DNS_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Captive DNS: socket() failed (errno %d)", errno);
        vTaskDelete(NULL);
        return;
    }
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Captive DNS: bind() failed (errno %d)", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Captive DNS server up on :53 -> 192.168.4.1 (resolves every name to the portal)");

    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&client, &clen);
        if (len < 0) {
            // Transient socket error (e.g. a Wi-Fi blip): back off briefly and keep serving rather than
            // killing the captive responder for the rest of the AP session.
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        if (len < (int)sizeof(captive_dns_header_t)) {
            continue;
        }

        captive_dns_header_t *qh = (captive_dns_header_t *)rx;
        if ((ntohs(qh->flags) & 0x8000) != 0 || ntohs(qh->qd_count) < 1) {
            continue;   // not a standard query
        }

        // Walk past the first QNAME (sequence of length-prefixed labels, terminated by a 0 byte).
        int p = sizeof(captive_dns_header_t);
        while (p < len && rx[p] != 0) {
            p += rx[p] + 1;
        }
        p += 1;                 // skip the terminating zero
        int q_end = p + 4;      // + QTYPE(2) + QCLASS(2)
        if (q_end > len || q_end + 16 > (int)sizeof(tx)) {
            continue;
        }

        // Build the reply: original header+question, then a single A answer pointing at the AP IP.
        memcpy(tx, rx, q_end);
        captive_dns_header_t *rh = (captive_dns_header_t *)tx;
        rh->flags    = htons(0x8180);   // QR=1, RD copied, RA=1, RCODE=0
        rh->an_count = htons(1);
        rh->ns_count = 0;
        rh->ar_count = 0;

        int w = q_end;
        tx[w++] = 0xC0; tx[w++] = 0x0C;                 // NAME -> pointer to the question at offset 12
        tx[w++] = 0x00; tx[w++] = 0x01;                 // TYPE  = A
        tx[w++] = 0x00; tx[w++] = 0x01;                 // CLASS = IN
        tx[w++] = 0x00; tx[w++] = 0x00; tx[w++] = 0x00; tx[w++] = 0x0A;   // TTL = 10s
        tx[w++] = 0x00; tx[w++] = 0x04;                 // RDLENGTH = 4
        memcpy(tx + w, &ap_ip, 4); w += 4;              // RDATA = AP IP (network byte order)

        sendto(sock, tx, w, 0, (struct sockaddr *)&client, clen);
    }

    close(sock);
    ESP_LOGW(TAG, "Captive DNS server stopped");
    vTaskDelete(NULL);
}

static void start_captive_dns(void)
{
    xTaskCreate(captive_dns_task, "captive_dns", 4096, NULL, 5, NULL);
}


void SendHTTPResponse(httpd_req_t *req)
{
    // Device identity + a reboot control, always shown at the top of the AP page.
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macbuf[24];
    snprintf(macbuf, sizeof(macbuf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char hnbuf[20];
    snprintf(hnbuf, sizeof(hnbuf), "edgeai-%02x%02x%02x", mac[3], mac[4], mac[5]);

    std::string message = "<h1>AI-on-the-edge - BASIC SETUP</h1>";
    message += "<p><b>Device MAC:</b> " + std::string(macbuf) +
               " &nbsp;&middot;&nbsp; <b>Default hostname:</b> " + std::string(hnbuf) + "</p>";
    message += "<button class=\"button\" type=\"button\" onclick=\"if(confirm('Reboot the device now?')){fetch('/reboot');setTimeout(function(){location.reload();},2000);}\">Reboot device</button>";
    message += "<hr>";
    if (s_apForcedReconfig) {
        message += "<p style=\"color:#b00\"><b>The device could not connect to the configured Wi-Fi.</b> "
                   "Re-enter the network credentials below, then reboot. (It also retries the saved network "
                   "automatically every few minutes while no one is connected here.)</p>";
    }
    message += "<p>This is an access point with a minimal server to setup the minimum required files and information on the device and the SD-card. ";
    message += "This mode is started when /wlan.ini or /config/config.ini is missing, or when the device could not connect to the configured Wi-Fi.<p>";
    message += "The setup is done in 3 steps: 1. upload full inital configuration (sd-card content), 2. store WLAN access information, 3. reboot (and connect to WLANs)<p><p>";
    message += "Please follow the below instructions.<p>";
    httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

    isWlanINI = FileExists(WLAN_CONFIG_FILE);

    if (!isConfigINI)
    {
        message = "<h3>1. Upload initial configuration to sd-card</h3><p>";
        message += "The configuration file config.ini is missing and most propably the full configuration and html folder on the sd-card. ";
        message += "This is normal after the first flashing of the firmware and an empty sd-card. Please upload \"remote_setup.zip\", which contains a full inital configuration.<p>";
        message += "<input id=\"newfile\" type=\"file\"><br>";
        message += "<button class=\"button\" style=\"width:300px\" id=\"doUpdate\" type=\"button\" onclick=\"upload()\">Upload File</button><p>";
        message += "The upload might take up to 60s. After a successful upload the page will be updated.";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

        message = "<script language=\"JavaScript\">";
        message += "function upload() {";
        message += "var xhttp = new XMLHttpRequest();";
        message += "xhttp.onreadystatechange = function() {if (xhttp.readyState == 4) {if (xhttp.status == 200) {location.reload();}}};";
        message += "var filePath = document.getElementById(\"newfile\").value.split(/[\\\\/]/).pop();";
        message += "var file = document.getElementById(\"newfile\").files[0];";
        message += "if (!file.name.includes(\"remote-setup\")){if (!confirm(\"The zip file name should contain '...remote-setup...'. Are you sure that you have downloaded the correct file?\"))return;};";
        message += "var upload_path = \"/upload/firmware/\" + filePath; xhttp.open(\"POST\", upload_path, true); xhttp.send(file);document.reload();";
        message += "document.getElementById(\"doUpdate\").disabled = true;}";
        message += "</script>";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));
        return;
    }
    if (!isWlanINI || s_apForcedReconfig)
    {
        message = "<h3>2. WLAN access credentials</h3><p>";
        message += "<table>";
        message += "<tr><td>WLAN-SSID</td><td><input type=\"text\" name=\"ssid\" id=\"ssid\"></td><td>SSID of the WLAN</td></tr>";
        message += "<tr><td>WLAN-Password</td><td><input type=\"text\" name=\"password\" id=\"password\"></td><td>ATTENTION: the password will not be encrypted during the sending.</td><tr>";
        message += "</table><p>";
        message += "<h4>ATTENTION:<h4>Be sure about the WLAN settings. They cannot be reset afterwards. If ssid or password is wrong, you need to take out the sd-card and manually change them in \"wlan.ini\"!<p>";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

//        message = "</tr><tr><td> Hostname</td><td><input type=\"text\" name=\"hostname\" id=\"hostname\"></td><td></td>";
//        message += "</tr><tr><td>Fixed IP</td><td><input type=\"text\" name=\"ip\" id=\"ip\"></td><td>Leave emtpy if set by router (DHCP)</td></tr>";
//        message += "<tr><td>Gateway</td><td><input type=\"text\" name=\"gateway\" id=\"gateway\"></td><td>Leave emtpy if set by router (DHCP)</td></tr>";
//        message += "<tr><td>Netmask</td><td><input type=\"text\" name=\"netmask\" id=\"netmask\"></td><td>Leave emtpy if set by router (DHCP)</td>";
//        message += "</tr><tr><td>DNS</td><td><input type=\"text\" name=\"dns\" id=\"dns\"></td><td>Leave emtpy if set by router (DHCP)</td></tr>";
//        message += "<tr><td>RSSI Threshold</td><td><input type=\"number\" name=\"name\" id=\"threshold\" min=\"-100\"  max=\"0\" step=\"1\" value = \"0\"></td><td>WLAN Mesh Parameter: Threshold for RSSI value to check for start switching access point in a mesh system (if actual RSSI is lower). Possible values: -100 to 0, 0 = disabled - Value will be transfered to wlan.ini at next startup)</td></tr>";
//        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));


        message = "<button class=\"button\" type=\"button\" onclick=\"wr()\">Write wlan.ini</button>";
        message += "<script language=\"JavaScript\">async function wr(){";
        message += "api = \"/config?\"+\"ssid=\"+document.getElementById(\"ssid\").value+\"&pwd=\"+document.getElementById(\"password\").value;";
//        message += "api = \"/config?\"+\"ssid=\"+document.getElementById(\"ssid\").value+\"&pwd=\"+document.getElementById(\"password\").value+\"&hn=\"+document.getElementById(\"hostname\").value+\"&ip=\"+document.getElementById(\"ip\").value+\"&gw=\"+document.getElementById(\"gateway\").value+\"&nm=\"+document.getElementById(\"netmask\").value+\"&dns=\"+document.getElementById(\"dns\").value+\"&rssithreshold=\"+document.getElementById(\"threshold\").value;";
        message += "fetch(api);await new Promise(resolve => setTimeout(resolve, 1000));location.reload();}</script>";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));
        return;
    }

    message = "<h3>3. Reboot</h3><p>";
    message += "After triggering the reboot, the zip-files gets extracted and written to the sd-card.<br>The ESP32 will restart two times and then connect to your access point. Please find the IP in your router settings and access it with the new ip-address.<p>";
    message += "The first update and initialization process can take up to 3 minutes before you find it in the wlan. Error logs can be found on the console / serial logout.<p>Have fun!<p>";
    message += "<button class=\"button\" type=\"button\" onclick=\"rb()\")>Reboot to first setup.</button>";
    message += "<script language=\"JavaScript\">async function rb(){";
    message += "api = \"/reboot\";";
    message += "fetch(api);await new Promise(resolve => setTimeout(resolve, 1000));location.reload();}</script>";
    httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));
}


esp_err_t test_handler(httpd_req_t *req)
{
    // Captive portal, part 3: the OS connectivity probes (Android /generate_204, Apple
    // /hotspot-detect.html, Windows /ncsi.txt, ...) land here via the DNS hijack. Answering anything
    // other than their expected body makes the OS conclude it's behind a captive portal and open the
    // sign-in page. We 302-redirect every path except the portal root itself to http://192.168.4.1/,
    // so that sign-in page is our setup form. (The page's own /config, /reboot and /upload calls are
    // registered separately and never reach this catch-all.)
    if (strcmp(req->uri, "/") != 0 && strcmp(req->uri, "/test") != 0 &&
        strncmp(req->uri, "/index", 6) != 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    SendHTTPResponse(req);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


esp_err_t reboot_handlerAP(httpd_req_t *req)
{
#ifdef DEBUG_DETAIL_ON     
    LogFile.WriteHeapInfo("handler_ota_update - Start");    
#endif
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Trigger reboot due to firmware update.");
    doRebootOTA();
    return ESP_OK;
}


esp_err_t config_ini_handler(httpd_req_t *req)
{
#ifdef DEBUG_DETAIL_ON     
    LogFile.WriteHeapInfo("handler_ota_update - Start");    
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "config_ini_handler");
    char _query[400];
    char _valuechar[100];    
    std::string fn = "/sdcard/firmware/";
    std::string _task = "";
    std::string ssid = "";
    std::string pwd = "";
    std::string hn = "";    // hostname
    std::string ip = "";
    std::string gw = "";    // gateway
    std::string nm = "";    // netmask
    std::string dns = "";
    std::string rssithreshold = ""; //rssi threshold for WIFI roaming
    std::string text = "";


    if (httpd_req_get_url_query_str(req, _query, 400) == ESP_OK)
    {
        ESP_LOGD(TAG, "Query: %s", _query);
        
        if (httpd_query_key_value(_query, "ssid", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "ssid is found: %s", _valuechar);
            ssid = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "pwd", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "pwd is found: %s", _valuechar);
            pwd = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "ssid", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "ssid is found: %s", _valuechar);
            ssid = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "hn", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "hostname is found: %s", _valuechar);
            hn = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "ip", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "ip is found: %s", _valuechar);
            ip = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "gw", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "gateway is found: %s", _valuechar);
            gw = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "nm", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "netmask is found: %s", _valuechar);
            nm = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "dns", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "dns is found: %s", _valuechar);
            dns = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "rssithreshold", _valuechar, 100) == ESP_OK)
        {
            ESP_LOGD(TAG, "rssithreshold is found: %s", _valuechar);
            rssithreshold = UrlDecode(std::string(_valuechar));
        }
    }

    FILE* configfilehandle = fopen(WLAN_CONFIG_FILE, "w");

    text  = ";++++++++++++++++++++++++++++++++++\n";
    text += "; AI on the edge - WLAN configuration\n";
    text += "; ssid: Name of WLAN network (mandatory), e.g. \"WLAN-SSID\"\n";
    text += "; password: Password of WLAN network (mandatory), e.g. \"PASSWORD\"\n\n";
    fputs(text.c_str(), configfilehandle);
    
    if (ssid.length())
        ssid = "ssid = \"" + ssid + "\"\n";
    else
        ssid = "ssid = \"\"\n";
    fputs(ssid.c_str(), configfilehandle);

    if (pwd.length())
        pwd = "password = \"" + pwd + "\"\n";
    else
        pwd = "password = \"\"\n";
    fputs(pwd.c_str(), configfilehandle);

    text  = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; Hostname: Name of device in network\n";
    text += "; This parameter can be configured via WebUI configuration\n";
    text += "; Default: \"watermeter\", if nothing is configured\n\n";
    fputs(text.c_str(), configfilehandle);

    if (hn.length())
        hn = "hostname = \"" + hn + "\"\n";
    else
        hn = ";hostname = \"watermeter\"\n";
    fputs(hn.c_str(), configfilehandle);

    text  = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; Fixed IP: If you like to use fixed IP instead of DHCP (default), the following\n";
    text += "; parameters needs to be configured: ip, gateway, netmask are mandatory, dns optional\n\n";
    fputs(text.c_str(), configfilehandle);

    if (ip.length())
        ip = "ip = \"" + ip + "\"\n";
    else
        ip = ";ip = \"xxx.xxx.xxx.xxx\"\n";
    fputs(ip.c_str(), configfilehandle);

    if (gw.length())
        gw = "gateway = \"" + gw + "\"\n";
    else
        gw = ";gateway = \"xxx.xxx.xxx.xxx\"\n";
    fputs(gw.c_str(), configfilehandle);

    if (nm.length())
        nm = "netmask = \"" + nm + "\"\n";
    else
        nm = ";netmask = \"xxx.xxx.xxx.xxx\"\n";
    fputs(nm.c_str(), configfilehandle);

    text  = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; DNS server (optional, if no DNS is configured, gateway address will be used)\n\n";
    fputs(text.c_str(), configfilehandle);

    if (dns.length())
        dns = "dns = \"" + dns + "\"\n";
    else
        dns = ";dns = \"xxx.xxx.xxx.xxx\"\n";
    fputs(dns.c_str(), configfilehandle);

    text  = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; WIFI Roaming:\n";
    text += "; Network assisted roaming protocol is activated by default\n";
    text += "; AP / mesh system needs to support roaming protocol 802.11k/v\n";
    text += ";\n";
    text += "; Optional feature (usually not necessary):\n";
    text += "; RSSI Threshold for client requested roaming query (RSSI < RSSIThreshold)\n";
    text += "; Note: This parameter can be configured via WebUI configuration\n";
    text += "; Default: 0 = Disable client requested roaming query\n\n";
    fputs(text.c_str(), configfilehandle);

    if (rssithreshold.length())
        rssithreshold = "RSSIThreshold = " + rssithreshold + "\n";
    else
        rssithreshold = "RSSIThreshold = 0\n";
    fputs(rssithreshold.c_str(), configfilehandle);

    fflush(configfilehandle);
    fclose(configfilehandle);

    std::string zw = "ota without parameter - should not be the case!";
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, zw.c_str(), zw.length()); 

    ESP_LOGD(TAG, "end config.ini");

    return ESP_OK;
}


esp_err_t upload_post_handlerAP(httpd_req_t *req)
{
    printf("Start des Post Handlers\n");
    MakeDir("/sdcard/config");
    MakeDir("/sdcard/firmware");
    MakeDir("/sdcard/html");
    MakeDir("/sdcard/img_tmp");
    MakeDir("/sdcard/log");
    MakeDir("/sdcard/demo");
    printf("Nach Start des Post Handlers\n");

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "upload_post_handlerAP");
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;

    const char *filename = get_path_from_uri(filepath, "/sdcard",
                                             req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
        return ESP_FAIL;
    }

    printf("filepath: %s, filename: %s\n", filepath, filename);

    DeleteFile(std::string(filepath));



    fd = fopen(filepath, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file: %s...", filename);

    char buf[1024];
    int received;

    int remaining = req->content_len;

    printf("remaining: %d\n", remaining);



    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size: %d", remaining);
        if ((received = httpd_req_recv(req, buf, MIN(remaining, 1024))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }

            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        if (received && (received != fwrite(buf, 1, received, fd))) {
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        remaining -= received;
    }
    fclose(fd);
    isConfigINI = true;

    FILE* pfile = fopen("/sdcard/update.txt", "w");
    std::string _s_zw= "/sdcard" + std::string(filename);
    fwrite(_s_zw.c_str(), strlen(_s_zw.c_str()), 1, pfile);
    fclose(pfile);


    ESP_LOGI(TAG, "File reception complete");
    httpd_resp_set_hdr(req, "Location", "/test");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/test");
    httpd_resp_send_chunk(req, NULL, 0);

    ESP_LOGI(TAG, "Update page send out");

    return ESP_OK;
}


httpd_handle_t start_webserverAP(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        // Do something
    }

    httpd_uri_t reboot_handle = {
        .uri       = "/reboot",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(reboot_handlerAP),
        .user_ctx  = NULL    // Pass server data as context
    };
    httpd_register_uri_handler(server, &reboot_handle);

    httpd_uri_t config_ini_handle = {
        .uri       = "/config",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(config_ini_handler),
        .user_ctx  = NULL    // Pass server data as context
    };
    httpd_register_uri_handler(server, &config_ini_handle);

    /* URI handler for uploading files to server */
    httpd_uri_t file_uploadAP = {
        .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler = APPLY_BASIC_AUTH_FILTER(upload_post_handlerAP),
        .user_ctx  = NULL    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_uploadAP);

    httpd_uri_t test_uri = {
        .uri      = "*",
        .method   = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(test_handler),
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &test_uri);

    return NULL;
}


// Start the configuration access point and block until reboot. forcedReconfig == true means the
// configured Wi-Fi exists but is unusable (couldn't connect, or invalid/empty credentials), so the AP
// periodically reboots to retry; false means initial setup (files missing), so it waits indefinitely.
void StartAPModeAndWait(bool forcedReconfig)
{
    s_apForcedReconfig = forcedReconfig;

    ESP_LOGI(TAG, "Starting access point for remote configuration");
    StatusLED(AP_OR_OTA, 2, true);
    driveSystemStatusWs281x(0, 0, 60);   // RGB blue = AP setup / reconfiguration mode
    wifi_init_softAP();
    start_webserverAP();
    start_captive_dns();   // hijack DNS so the OS shows the setup page as a captive portal

    // If we reached AP mode on a freshly-OTA'd *trial* image, confirm it now. The normal post-init
    // confirmation (main, after the STA web server is up) never runs in AP mode, so without this a
    // user who configures Wi-Fi here and reboots would have the bootloader roll the new firmware back
    // as an unconfirmed trial. The web server (AP) is up at this point, satisfying diagnostic().
    ConfirmOTAUpdateAfterInit();

    int idleSeconds = 0;
    while(1) { // wait until reboot (within task_do_Update_ZIP, the reboot button, or the retry below)
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (s_apForcedReconfig) {
            if (s_apClientCount > 0) {
                idleSeconds = 0;   // someone is connected and configuring -> don't disrupt them
            }
            else if (++idleSeconds >= WIFI_AP_RETRY_SECONDS) {
                ESP_LOGW(TAG, "No client on AP for a while -> rebooting to retry the configured Wi-Fi");
                esp_restart();
            }
        }
    }
}

void CheckStartAPMode()
{
    isConfigINI = FileExists(CONFIG_FILE);
    isWlanINI = FileExists(WLAN_CONFIG_FILE);

    // A previous boot could not connect to the configured Wi-Fi and left this marker, asking us to
    // come up in AP mode so the settings can be fixed. One-shot: remove it so the *next* boot tries
    // the configured network again (this AP session periodically reboots to retry too - see below).
    bool forceAP = FileExists("/sdcard/.force_ap");
    if (forceAP) {
        DeleteFile("/sdcard/.force_ap");
        ESP_LOGW(TAG, "Previous Wi-Fi connection failed -> starting AP mode for reconfiguration");
    }

    if (!isConfigINI)
        ESP_LOGW(TAG, "config.ini not found!");

    if (!isWlanINI)
        ESP_LOGW(TAG, "wlan.ini not found!");

    if (!isConfigINI || !isWlanINI || forceAP)
    {
        // Distinguish "couldn't connect" (settings exist) from "needs initial setup" (files missing):
        // only the former periodically retries the configured Wi-Fi.
        StartAPModeAndWait(forceAP && isConfigINI && isWlanINI);
    }
}

#endif //#ifdef ENABLE_SOFTAP
