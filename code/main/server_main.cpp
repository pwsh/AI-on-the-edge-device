#include "server_main.h"

#include <string>

#include "server_help.h"
#include "ClassLogFile.h"

#include "time_sntp.h"

#include "connect_wlan.h"
#include "read_wlanini.h"

#include "version.h"

#include "esp_wifi.h"
#include <netdb.h>

#include "MainFlowControl.h"
#include "esp_log.h"
#include "basic_auth.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "ClassControllCamera.h"

#include <stdio.h>

#include "Helper.h"

httpd_handle_t server = NULL;   
std::string starttime = "";

static const char *TAG = "MAIN SERVER";

/* An HTTP GET handler */
esp_err_t info_get_handler(httpd_req_t *req)
{
#ifdef DEBUG_DETAIL_ON      
    LogFile.WriteHeapInfo("info_get_handler - Start");    
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "info_get_handler");    
    char _query[200];
    char _valuechar[30];    
    std::string _task;

    if (httpd_req_get_url_query_str(req, _query, 200) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid query string");
    }

    ESP_LOGD(TAG, "Query: %s", _query);

    if (httpd_query_key_value(_query, "type", _valuechar, 30) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing or invalid 'type' query parameter (too long value?)");
    }

    ESP_LOGD(TAG, "type is found: %s", _valuechar);
    _task = std::string(_valuechar);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (_task.compare("GitBranch") == 0)
    {
        httpd_resp_sendstr(req, libfive_git_branch());
        return ESP_OK;        
    }
    else if (_task.compare("GitTag") == 0)
    {
        httpd_resp_sendstr(req, libfive_git_version());
        return ESP_OK;        
    }
    else if (_task.compare("GitRevision") == 0)
    {
        httpd_resp_sendstr(req, libfive_git_revision());
        return ESP_OK;        
    }
    else if (_task.compare("BuildTime") == 0)
    {
        httpd_resp_sendstr(req, build_time());
        return ESP_OK;        
    }
    else if (_task.compare("FirmwareVersion") == 0)
    {
        httpd_resp_sendstr(req, getFwVersion().c_str());
        return ESP_OK;        
    }
    else if (_task.compare("HTMLVersion") == 0)
    {
        httpd_resp_sendstr(req, getHTMLversion().c_str());
        return ESP_OK;        
    }
    else if (_task.compare("Hostname") == 0)
    {
        std::string zw;
        zw = std::string(wlan_config.hostname);
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("IP") == 0)
    {
        std::string *zw;
        zw = getIPAddress();
        httpd_resp_sendstr(req, zw->c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SSID") == 0)
    {
        std::string *zw;
        zw = getSSID();
        httpd_resp_sendstr(req, zw->c_str());
        return ESP_OK;        
    }
    else if (_task.compare("FlowStatus") == 0)
    {
        std::string zw;
        zw = std::string("FlowStatus");
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("Round") == 0)
    {
        char formated[10] = "";
        snprintf(formated, sizeof(formated), "%d", getCountFlowRounds());
        httpd_resp_sendstr(req, formated);
        return ESP_OK;
    }
    else if (_task.compare("Ready") == 0)   // 1 once init is done and the flow is ready to run
    {
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_sendstr(req, getSystemReady() ? "1" : "0");
        return ESP_OK;
    }
    else if (_task.compare("SystemStatus") == 0)   // bitmask of SystemStatusFlag_t (0 = healthy)
    {
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_sendstr(req, to_string(getSystemStatus()).c_str());
        return ESP_OK;
    }
    else if (_task.compare("SDCardPartitionSize") == 0)
    {
        std::string zw;
        zw = getSDCardPartitionSize();
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SDCardFreePartitionSpace") == 0)
    {
        std::string zw;
        zw = getSDCardFreePartitionSpace();
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SDCardPartitionAllocationSize") == 0)
    {
        std::string zw;
        zw = getSDCardPartitionAllocationSize();
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SDCardManufacturer") == 0)
    {
        std::string zw;
        zw = getSDCardManufacturer(); 
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SDCardName") == 0)
    {
        std::string zw;
        zw = getSDCardName(); 
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SDCardCapacity") == 0)
    {
        std::string zw;
        zw = getSDCardCapacity();
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("SDCardSectorSize") == 0)
    {
        std::string zw;
        zw = getSDCardSectorSize();
        httpd_resp_sendstr(req, zw.c_str());
        return ESP_OK;        
    }
    else if (_task.compare("ChipCores") == 0)
    {
        esp_chip_info_t chipInfo;
        esp_chip_info(&chipInfo);
        httpd_resp_sendstr(req, to_string(chipInfo.cores).c_str());
        return ESP_OK;        
    }
    else if (_task.compare("ChipRevision") == 0)
    {
        esp_chip_info_t chipInfo;
        esp_chip_info(&chipInfo);
        httpd_resp_sendstr(req, to_string(chipInfo.revision).c_str());
        return ESP_OK;        
    }
    else if (_task.compare("ChipFeatures") == 0)
    {
        esp_chip_info_t chipInfo;
        esp_chip_info(&chipInfo);
        httpd_resp_sendstr(req, to_string(chipInfo.features).c_str());
        return ESP_OK;
    }
    else if (_task.compare("ChipModel") == 0)
    {
        esp_chip_info_t chipInfo;
        esp_chip_info(&chipInfo);
        std::string m = (chipInfo.model == CHIP_ESP32) ? "ESP32" :
                        (chipInfo.model == CHIP_ESP32S3) ? "ESP32-S3" :
                        (chipInfo.model == CHIP_ESP32S2) ? "ESP32-S2" : "unknown";
        httpd_resp_sendstr(req, m.c_str());
        return ESP_OK;
    }
    else if (_task.compare("CPUFrequency") == 0)
    {
        httpd_resp_sendstr(req, (to_string(esp_clk_cpu_freq() / 1000000) + " MHz").c_str());
        return ESP_OK;
    }
    else if (_task.compare("PSRAMSize") == 0)   // total external PSRAM chip size (bytes)
    {
        httpd_resp_sendstr(req, to_string((unsigned long) esp_psram_get_size()).c_str());
        return ESP_OK;
    }
    else if (_task.compare("PSRAMFree") == 0)   // currently free PSRAM (bytes)
    {
        httpd_resp_sendstr(req, to_string((unsigned long) heap_caps_get_free_size(MALLOC_CAP_SPIRAM)).c_str());
        return ESP_OK;
    }
    else if (_task.compare("FlashSize") == 0)   // on-device SPI flash chip size (bytes)
    {
        uint32_t flashSize = 0;
        esp_flash_get_size(NULL, &flashSize);
        httpd_resp_sendstr(req, to_string((unsigned long) flashSize).c_str());
        return ESP_OK;
    }
    else if (_task.compare("CPUTemperature") == 0)
    {
        httpd_resp_sendstr(req, to_string((int)temperatureRead()).c_str());
        return ESP_OK;
    }
    else if (_task.compare("CameraModel") == 0)
    {
        std::string camModel;
        switch (CCstatus.CamSensor_id)
        {
            case OV2640_PID: camModel = "OV2640"; break;
            case OV3660_PID: camModel = "OV3660"; break;
            case OV5640_PID: camModel = "OV5640"; break;
            default:         camModel = "unknown"; break;
        }
        httpd_resp_sendstr(req, camModel.c_str());
        return ESP_OK;
    }
    else if (_task.compare("CameraResolution") == 0)
    {
        httpd_resp_sendstr(req, (to_string(CCstatus.ImageWidth) + "x" + to_string(CCstatus.ImageHeight)).c_str());
        return ESP_OK;
    }
    else if (_task.compare("WifiRSSI") == 0)
    {
        wifi_ap_record_t ap;
        std::string r = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? (to_string(ap.rssi) + " dBm") : "-";
        httpd_resp_sendstr(req, r.c_str());
        return ESP_OK;
    }
    else if (_task.compare("WifiChannel") == 0)
    {
        wifi_ap_record_t ap;
        std::string c = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? to_string(ap.primary) : "-";
        httpd_resp_sendstr(req, c.c_str());
        return ESP_OK;
    }
    else
    {
        char formatted[256];
        snprintf(formatted, sizeof(formatted), "Unknown value for parameter info 'type': '%s'\n", _task.c_str());
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, formatted);
    }

    return ESP_OK;
}


esp_err_t starttime_get_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    httpd_resp_send(req, starttime.c_str(), starttime.length()); 

    return ESP_OK;
}


esp_err_t hello_main_handler(httpd_req_t *req)
{
#ifdef DEBUG_DETAIL_ON      
    LogFile.WriteHeapInfo("hello_main_handler - Start");
#endif

    char filepath[50];
    ESP_LOGD(TAG, "uri: %s\n", req->uri);
    int _pos;
    esp_err_t res;

    char *base_path = (char*) req->user_ctx;
    std::string filetosend(base_path);

    const char *filename = get_path_from_uri(filepath, base_path,
                                             req->uri - 1, sizeof(filepath));    
    ESP_LOGD(TAG, "1 uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);

    if ((strcmp(req->uri, "/") == 0))
    {
        {
            filetosend = filetosend + "/html/index.html";
        }
    }
    else
    {
        filetosend = filetosend + "/html" + std::string(req->uri);
        _pos = filetosend.find("?");
        if (_pos > -1){
            filetosend = filetosend.substr(0, _pos);
        }
    }

    if (filetosend == "/sdcard/html/index.html") {
        bool criticalError = isSetSystemStatusFlag(SYSTEM_STATUS_PSRAM_BAD) ||
                             isSetSystemStatusFlag(SYSTEM_STATUS_HEAP_TOO_SMALL) ||
                             isSetSystemStatusFlag(SYSTEM_STATUS_CAM_BAD) ||
                             isSetSystemStatusFlag(SYSTEM_STATUS_SDCARD_CHECK_BAD) ||
                             isSetSystemStatusFlag(SYSTEM_STATUS_FOLDER_CHECK_BAD);

        // Don't hand out the full UI until the device is actually ready to run (camera up,
        // models loaded, boot/recovery delays done) - or to explain a critical error. The
        // startup page below polls the status endpoints and auto-switches to the full UI.
        if (criticalError || !getSystemReady()) {
            if (criticalError)
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Serving startup page: a critical error is present");

            static const char STARTUP_PAGE[] = R"STARTUP(<!DOCTYPE html><html lang='en'><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>AI on the Edge - starting up</title><style>
:root{--bg:#fafbfc;--fg:#1c2024;--muted:#5a636b;--card:#fff;--bd:#d8dee4;--accent:#2c7be5;--err:#c0392b;--ok:#1f9d55;}
html[data-theme=dark]{--bg:#1e1e1e;--fg:#dcdcdc;--muted:#9a9a9a;--card:#252526;--bd:#3e3e42;}
body{margin:0;font-family:Arial,Helvetica,sans-serif;background:var(--bg);color:var(--fg);min-height:100vh;display:flex;align-items:center;justify-content:center;}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;max-width:580px;width:92%;padding:22px 24px;box-shadow:0 2px 10px rgba(0,0,0,.12);box-sizing:border-box;}
h1{font-size:1.25em;margin:0 0 4px;display:flex;align-items:center;gap:10px;}
.spin{width:22px;height:22px;border:3px solid rgba(127,127,127,.3);border-top-color:var(--accent);border-radius:50%;animation:s .9s linear infinite;flex:0 0 auto;}
@keyframes s{to{transform:rotate(360deg)}}
.sub{color:var(--muted);margin:0 0 14px;font-size:.92em;}
.row{display:flex;justify-content:space-between;gap:12px;padding:7px 0;border-bottom:1px solid var(--bd);font-size:.95em;}
.k{color:var(--muted);}.v{font-weight:600;text-align:right;}
.errbox{margin:12px 0 0;padding:10px 12px;border-radius:8px;background:rgba(192,57,43,.12);border:1px solid rgba(192,57,43,.4);color:var(--err);font-size:.9em;display:none;}
.why{margin:12px 0 0;font-size:.88em;color:var(--muted);}
.btns{margin-top:16px;display:flex;gap:8px;flex-wrap:wrap;}
button{padding:7px 12px;border:1px solid var(--bd);border-radius:6px;background:var(--card);color:var(--fg);cursor:pointer;font-size:.9em;}
button:hover{filter:brightness(.97);}
</style><script>
try{var s=localStorage.getItem('aiotedge-theme');var d=s?(s==='dark'):(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches);if(d)document.documentElement.setAttribute('data-theme','dark');}catch(e){}
var FLAGS=[[0x1,'PSRAM not usable - needs at least 4MB'],[0x2,'Internal heap too small'],[0x4,'Camera not detected / init failed - check the ribbon cable is fully seated'],[0x8,'SD card read/write check failed'],[0x10,'Required folders/files missing on the SD card'],[0x100,'Camera framebuffer issue (non-critical)'],[0x200,'Time sync (NTP) failed (non-critical)']];
function el(i){return document.getElementById(i);}
function g(u,cb){var x=new XMLHttpRequest();x.onreadystatechange=function(){if(x.readyState==4){cb(x.status==200?x.responseText:null);}};try{x.open('GET',u+(u.indexOf('?')>-1?'&':'?')+'_='+Date.now(),true);x.send();}catch(e){cb(null);}}
function friendly(s){if(!s)return 'Starting...';if(/delayed/i.test(s))return 'Recovering from a previous crash - waiting up to 5 min before retrying (you can reboot or update now)';if(/not yet created/i.test(s))return 'Initialising - loading recognition models...';return s;}
function showErrors(mask){var box=el('errbox');var out=[];for(var i=0;i<FLAGS.length;i++){if(mask&FLAGS[i][0])out.push(FLAGS[i][1]);}if(out.length){box.style.display='block';box.innerHTML='<b>Problem(s) detected:</b><br>'+out.join('<br>');}else{box.style.display='none';}el('why').innerHTML=mask?'The full interface stays hidden until these are resolved, so you do not get a half-working UI. Fix the issue, then reboot.':'The full interface loads automatically once the camera is initialised, the models are loaded, and the first processing cycle is ready - this avoids showing a UI that cannot actually run.';}
function poll(){
 g('/info?type=Ready',function(r){if(r!=null&&r.trim()==='1'){el('status').textContent='Ready - loading the interface...';location.href='index.html?ready=1&_='+Date.now();}});
 g('/statusflow',function(r){if(r!=null)el('status').textContent=friendly(r.trim());});
 g('/info?type=SystemStatus',function(r){showErrors(r==null?0:(parseInt(r,10)||0));});
 g('/sysinfo',function(r){if(r){try{var j=JSON.parse(r)[0];el('camera').textContent=j.cameraModel||'-';el('uptime').textContent=j.uptime||'-';}catch(e){}}});
 setTimeout(poll,2000);
}
window.addEventListener('load',function(){poll();});
</script></head><body><div class='card'>
<h1><span class='spin'></span> AI on the Edge - starting up</h1>
<p class='sub'>Getting the device ready. This page switches to the full interface automatically when everything is up.</p>
<div class='row'><span class='k'>Current step</span><span class='v' id='status'>Starting...</span></div>
<div class='row'><span class='k'>Camera</span><span class='v' id='camera'>-</span></div>
<div class='row'><span class='k'>Uptime</span><span class='v' id='uptime'>-</span></div>
<div class='errbox' id='errbox'></div>
<p class='why' id='why'></p>
<div class='btns'>
<button onclick="window.open('/log.html')">Log viewer</button>
<button onclick="window.open('/info.html')">System info</button>
<button onclick="window.open('/ota_page.html')">OTA update</button>
<button onclick="location.href='/reboot'">Reboot</button>
</div></div></body></html>)STARTUP";

            httpd_resp_set_hdr(req, "Cache-Control", "no-store");
            httpd_resp_set_type(req, "text/html");
            httpd_resp_send(req, STARTUP_PAGE, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        else if (isSetupModusActive()) {
            ESP_LOGD(TAG, "System is in setup mode --> index.html --> setup.html");
            filetosend = "/sdcard/html/setup.html";
        }
    }

    ESP_LOGD(TAG, "Filename: %s", filename);
    
    ESP_LOGD(TAG, "File requested: %s", filetosend.c_str());

    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 414 Error */
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
        return ESP_FAIL;
    }

    res = send_file(req, filetosend);
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);

    if (res != ESP_OK)
        return res;

    /* Respond with an empty chunk to signal HTTP response completion */
//    httpd_resp_sendstr(req, "");
//    httpd_resp_send_chunk(req, NULL, 0);

#ifdef DEBUG_DETAIL_ON      
    LogFile.WriteHeapInfo("hello_main_handler - Stop");   
#endif

    return ESP_OK;
}


esp_err_t img_tmp_handler(httpd_req_t *req)
{
    char filepath[50];
    ESP_LOGD(TAG, "uri: %s", req->uri);

    char *base_path = (char*) req->user_ctx;
    std::string filetosend(base_path);

    const char *filename = get_path_from_uri(filepath, base_path,
                                             req->uri  + sizeof("/img_tmp/") - 1, sizeof(filepath));    
    ESP_LOGD(TAG, "1 uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);

    filetosend = filetosend + "/img_tmp/" + std::string(filename);
    ESP_LOGD(TAG, "File to upload: %s", filetosend.c_str());

    esp_err_t res = send_file(req, filetosend); 
    if (res != ESP_OK)
        return res;

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


esp_err_t img_tmp_virtual_handler(httpd_req_t *req)
{
    #ifdef DEBUG_DETAIL_ON      
        LogFile.WriteHeapInfo("img_tmp_virtual_handler - Start");  
    #endif

    char filepath[50];

    ESP_LOGD(TAG, "uri: %s", req->uri);

    char *base_path = (char*) req->user_ctx;
    std::string filetosend(base_path);

    const char *filename = get_path_from_uri(filepath, base_path,
                                             req->uri  + sizeof("/img_tmp/") - 1, sizeof(filepath));    
    ESP_LOGD(TAG, "1 uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);

    filetosend = std::string(filename);
    ESP_LOGD(TAG, "File to upload: %s", filetosend.c_str());

    // Serve raw.jpg
    if (filetosend == "raw.jpg")
        return GetRawJPG(req); 

    // Serve alg.jpg, alg_roi.jpg or digit and analog ROIs
    if (ESP_OK == GetJPG(filetosend, req))
        return ESP_OK;

    #ifdef DEBUG_DETAIL_ON      
        LogFile.WriteHeapInfo("img_tmp_virtual_handler - Done");   
    #endif

    // File was not served already --> serve with img_tmp_handler
    return img_tmp_handler(req);
}


esp_err_t sysinfo_handler(httpd_req_t *req)
{
    std::string zw;
    std::string cputemp = std::to_string((int)temperatureRead());
    std::string gitversion = libfive_git_version();
    std::string buildtime = build_time();
    std::string gitbranch = libfive_git_branch();
    std::string gittag = libfive_git_version();
    std::string gitrevision = libfive_git_revision();
    std::string htmlversion = getHTMLversion();
    char freeheapmem[11];
    sprintf(freeheapmem, "%lu", (long) getESPHeapSize());

    // CPU / chip
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    std::string cpuFreq = std::to_string(esp_clk_cpu_freq() / 1000000);     // MHz
    std::string cpuCores = std::to_string(chip.cores);
    std::string chipModel = (chip.model == CHIP_ESP32) ? "ESP32" :
                            (chip.model == CHIP_ESP32S3) ? "ESP32-S3" :
                            (chip.model == CHIP_ESP32S2) ? "ESP32-S2" : "unknown";

    // Uptime (since boot)
    int64_t up = esp_timer_get_time() / 1000000;       // seconds
    char upbuf[48];
    snprintf(upbuf, sizeof(upbuf), "%dd %02dh %02dm %02ds",
             (int)(up / 86400), (int)((up % 86400) / 3600), (int)((up % 3600) / 60), (int)(up % 60));

    // Wi-Fi (station)
    std::string wifiSsid = "", wifiRssi = "-", wifiChannel = "-";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        wifiSsid = std::string(reinterpret_cast<char *>(ap.ssid));
        wifiRssi = std::to_string(ap.rssi);
        wifiChannel = std::to_string(ap.primary);
    }

    // Camera
    std::string camModel;
    switch (CCstatus.CamSensor_id)
    {
        case OV2640_PID: camModel = "OV2640"; break;
        case OV3660_PID: camModel = "OV3660"; break;
        case OV5640_PID: camModel = "OV5640"; break;
        default:         camModel = "unknown"; break;
    }
    std::string camResolution = std::to_string(CCstatus.ImageWidth) + "x" + std::to_string(CCstatus.ImageHeight);

    // SD card / filesystem (MB)
    std::string sdTotalMB = getSDCardPartitionSize();
    std::string sdFreeMB = getSDCardFreePartitionSpace();

    // On-device memory / storage: external PSRAM, internal heap, SPI flash chip (bytes)
    size_t psramSize    = esp_psram_get_size();
    size_t psramFree    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t intHeapFree  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t intHeapTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    uint32_t flashSize  = 0;
    esp_flash_get_size(NULL, &flashSize);

    zw = string("[{") +
        "\"firmware\": \"" + gitversion + "\"," +
        "\"buildtime\": \"" + buildtime + "\"," +
        "\"gitbranch\": \"" + gitbranch + "\"," +
        "\"gittag\": \"" + gittag + "\"," +
        "\"gitrevision\": \"" + gitrevision + "\"," +
        "\"html\": \"" + htmlversion + "\"," +
        "\"cputemp\": \"" + cputemp + "\"," +
        "\"cpuFrequencyMHz\": \"" + cpuFreq + "\"," +
        "\"chipModel\": \"" + chipModel + "\"," +
        "\"cpuCores\": \"" + cpuCores + "\"," +
        "\"uptime\": \"" + std::string(upbuf) + "\"," +
        "\"uptimeSeconds\": \"" + std::to_string(up) + "\"," +
        "\"wifiSSID\": \"" + wifiSsid + "\"," +
        "\"wifiRSSI\": \"" + wifiRssi + "\"," +
        "\"wifiChannel\": \"" + wifiChannel + "\"," +
        "\"cameraModel\": \"" + camModel + "\"," +
        "\"cameraResolution\": \"" + camResolution + "\"," +
        "\"sdCardTotalMB\": \"" + sdTotalMB + "\"," +
        "\"sdCardFreeMB\": \"" + sdFreeMB + "\"," +
        "\"psramSize\": \"" + std::to_string((unsigned long) psramSize) + "\"," +
        "\"psramFree\": \"" + std::to_string((unsigned long) psramFree) + "\"," +
        "\"psramLargestFreeBlock\": \"" + std::to_string((unsigned long) psramLargest) + "\"," +
        "\"flashSize\": \"" + std::to_string((unsigned long) flashSize) + "\"," +
        "\"internalHeapFree\": \"" + std::to_string((unsigned long) intHeapFree) + "\"," +
        "\"internalHeapTotal\": \"" + std::to_string((unsigned long) intHeapTotal) + "\"," +
        "\"hostname\": \"" + *getHostname() + "\"," +
        "\"IPv4\": \"" + *getIPAddress() + "\"," +
        "\"freeHeapMem\": \"" + freeheapmem + "\"" +
        "}]";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, zw.c_str(), zw.length());

    return ESP_OK;
}


void register_server_main_uri(httpd_handle_t server, const char *base_path)
{
    httpd_uri_t info_get_handle = {
        .uri       = "/info",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(info_get_handler),
        .user_ctx  = (void*) base_path    // Pass server data as context
    };
    httpd_register_uri_handler(server, &info_get_handle);

    httpd_uri_t sysinfo_handle = {
        .uri       = "/sysinfo",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(sysinfo_handler),
        .user_ctx  = (void*) base_path    // Pass server data as context
    };
    httpd_register_uri_handler(server, &sysinfo_handle);

    httpd_uri_t starttime_tmp_handle = {
        .uri       = "/starttime",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(starttime_get_handler),
        .user_ctx  = NULL    // Pass server data as context
    };
    httpd_register_uri_handler(server, &starttime_tmp_handle);

    httpd_uri_t img_tmp_handle = {
        .uri       = "/img_tmp/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(img_tmp_virtual_handler),
        .user_ctx  = (void*) base_path    // Pass server data as context
    };
    httpd_register_uri_handler(server, &img_tmp_handle);

    httpd_uri_t main_rest_handle = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(hello_main_handler),
        .user_ctx  = (void*) base_path    // Pass server data as context
    };
    httpd_register_uri_handler(server, &main_rest_handle);

}


httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.task_priority = tskIDLE_PRIORITY+3; // previously -> 2022-12-11: tskIDLE_PRIORITY+1; 2021-09-24: tskIDLE_PRIORITY+5
    config.stack_size = 12288; // previously -> 2023-01-02: 32768
    config.core_id = 1; // previously -> 2023-01-02: 0, 2022-12-11: tskNO_AFFINITY;
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 5; //20210921 --> previously 7   
    config.max_uri_handlers = 54; // Make sure this fits all URI handlers (was 42, exactly full -> adding /backup
                                  // overflowed it and the LAST-registered handler, the "/*" html catch-all,
                                  // silently failed to register, 404'ing every page). Memory: 6*max_uri_handlers bytes.
    config.max_resp_headers = 8;                        
    config.backlog_conn = 5;                        
    config.lru_purge_enable = true; // this cuts old connections if new ones are needed.               
    config.recv_wait_timeout = 5; // default: 5 20210924 --> previously 30              
    config.send_wait_timeout = 5; // default: 5 20210924 --> previously 30                    
    config.global_user_ctx = NULL;                        
    config.global_user_ctx_free_fn = NULL;                
    config.global_transport_ctx = NULL;                   
    config.global_transport_ctx_free_fn = NULL;           
    config.open_fn = NULL;                                
    config.close_fn = NULL;     
//    config.uri_match_fn = NULL;                            
    config.uri_match_fn = httpd_uri_match_wildcard;

    starttime = getCurrentTimeString("%Y%m%d-%H%M%S");

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}


void stop_webserver(httpd_handle_t server)
{
    httpd_stop(server);
}


void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}


void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
