#include "ClassFlowControll.h"

#include "connect_wlan.h"
#include "read_wlanini.h"

#include "freertos/task.h"
#include <esp_timer.h>
#include <esp_heap_caps.h>

#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <dirent.h>
#ifdef __cplusplus
}
#endif

#include "ClassLogFile.h"
#include "time_sntp.h"
#include <algorithm>   // std::sort / std::unique (schedule slots)
#include <ctime>       // time / localtime_r (schedule next-time)
#include <cstdlib>     // atoi
#include <cstdio>      // snprintf
#include "Helper.h"
#include "server_ota.h"
#include "server_backup.h"
#ifdef ENABLE_MQTT
    #include "interface_mqtt.h"
    #include "server_mqtt.h"
#endif //ENABLE_MQTT

#include "server_help.h"
#include "MainFlowControl.h"
#include "basic_auth.h"
#include "../../include/defines.h"

static const char* TAG = "FLOWCTRL";

//#define DEBUG_DETAIL_ON

std::string ClassFlowControll::doSingleStep(std::string _stepname, std::string _host)
{
    std::string _classname = "";
    std::string result = "";

    ESP_LOGD(TAG, "Step %s start", _stepname.c_str());

    if ((_stepname.compare("[TakeImage]") == 0) || (_stepname.compare(";[TakeImage]") == 0)) {
        _classname = "ClassFlowTakeImage";
    }
	
    if ((_stepname.compare("[Alignment]") == 0) || (_stepname.compare(";[Alignment]") == 0)) {
        _classname = "ClassFlowAlignment";
    }
	
    if ((_stepname.compare(0, 7, "[Digits") == 0) || (_stepname.compare(0, 8, ";[Digits") == 0)) {
        _classname = "ClassFlowCNNGeneral";
    }
	
    if ((_stepname.compare("[Analog]") == 0) || (_stepname.compare(";[Analog]") == 0)) {
        _classname = "ClassFlowCNNGeneral";
    }
	
    #ifdef ENABLE_MQTT
        if ((_stepname.compare("[MQTT]") == 0) || (_stepname.compare(";[MQTT]") == 0)) {
            _classname = "ClassFlowMQTT";
        }
    #endif //ENABLE_MQTT

    #ifdef ENABLE_INFLUXDB
        if ((_stepname.compare("[InfluxDB]") == 0) || (_stepname.compare(";[InfluxDB]") == 0)) {
            _classname = "ClassFlowInfluxDB";
        }
        if ((_stepname.compare("[InfluxDBv2]") == 0) || (_stepname.compare(";[InfluxDBv2]") == 0)) {
            _classname = "ClassFlowInfluxDBv2";
        }
    #endif //ENABLE_INFLUXDB
	
    #ifdef ENABLE_WEBHOOK
        if ((_stepname.compare("[Webhook]") == 0) || (_stepname.compare(";[Webhook]") == 0)) {
            _classname = "ClassFlowWebhook";
        }
    #endif //ENABLE_WEBHOOK

    for (int i = 0; i < FlowControll.size(); ++i) {
        if (FlowControll[i]->name().compare(_classname) == 0) {
            if (!(FlowControll[i]->name().compare("ClassFlowTakeImage") == 0)) {
                // if it is a TakeImage, the image does not need to be included, this happens automatically with the html query.
                FlowControll[i]->doFlow("");
            }
		
            result = FlowControll[i]->getHTMLSingleStep(_host);
        }
    }

    ESP_LOGD(TAG, "Step %s end", _stepname.c_str());

    return result;
}

std::string ClassFlowControll::TranslateAktstatus(std::string _input)
{
    if (_input.compare("ClassFlowTakeImage") == 0) {
        return ("Take Image");
    }

    if (_input.compare("ClassFlowAlignment") == 0) {
        return ("Aligning");
    }

    if (_input.compare("ClassFlowCNNGeneral") == 0) {
        return ("Digitization of ROIs");
    }

    #ifdef ENABLE_MQTT
        if (_input.compare("ClassFlowMQTT") == 0) {
            return ("Sending MQTT");
        }
    #endif //ENABLE_MQTT
		
    #ifdef ENABLE_INFLUXDB
        if (_input.compare("ClassFlowInfluxDB") == 0) {
            return ("Sending InfluxDB");
        }
		
        if (_input.compare("ClassFlowInfluxDBv2") == 0) {
            return ("Sending InfluxDBv2");
        }
    #endif //ENABLE_INFLUXDB
	
    #ifdef ENABLE_WEBHOOK
        if (_input.compare("ClassFlowWebhook") == 0) {
            return ("Sending Webhook");
        }
    #endif //ENABLE_WEBHOOK
	
    if (_input.compare("ClassFlowPostProcessing") == 0) {
        return ("Post-Processing");
    }

    return "Unkown Status";
}

std::vector<HTMLInfo*> ClassFlowControll::GetAllDigit() 
{
    if (flowdigit) {
        ESP_LOGD(TAG, "ClassFlowControll::GetAllDigit - flowdigit != NULL");
        return flowdigit->GetHTMLInfo();
    }

    std::vector<HTMLInfo*> empty;
    return empty;
}

std::vector<HTMLInfo*> ClassFlowControll::GetAllAnalog()
{
    if (flowanalog) {
        return flowanalog->GetHTMLInfo();
    }

    std::vector<HTMLInfo*> empty;
    return empty;
}

t_CNNType ClassFlowControll::GetTypeDigit()
{
    if (flowdigit) {
        return flowdigit->getCNNType();
    }

    return t_CNNType::None;
}

t_CNNType ClassFlowControll::GetTypeAnalog()
{
    if (flowanalog) {
        return flowanalog->getCNNType();
    }

    return t_CNNType::None;
}

#ifdef ALGROI_LOAD_FROM_MEM_AS_JPG
void ClassFlowControll::DigitDrawROI(CImageBasis *_zw)
{
    if (flowdigit) {
        flowdigit->DrawROI(_zw);
    }
}

void ClassFlowControll::AnalogDrawROI(CImageBasis *_zw)
{
    if (flowanalog) {
        flowanalog->DrawROI(_zw);
    }
}
#endif

#ifdef ENABLE_MQTT
bool ClassFlowControll::StartMQTTService() 
{
    /* Start the MQTT service */
    for (int i = 0; i < FlowControll.size(); ++i) {
        if (FlowControll[i]->name().compare("ClassFlowMQTT") == 0) {
            return ((ClassFlowMQTT*) (FlowControll[i]))->Start(AutoInterval);
        }  
    } 
    return false;
}
#endif //ENABLE_MQTT

void ClassFlowControll::SetInitialParameter(void)
{
    AutoStart = true;
    SetupModeActive = false;
    AutoInterval = 10; // Minutes
    scheduleMode = false;
    scheduleMinutes.clear();
    flowdigit = NULL;
    flowanalog = NULL;
    flowpostprocessing = NULL;
    disabled = false;
    aktRunNr = 0;
    aktstatus = "Flow task not yet created";
    aktstatusWithTime = aktstatus;
}

bool ClassFlowControll::getIsAutoStart(void)
{
    //return AutoStart;
    return true; // Flow must always be enabled, else the manual trigger (REST, MQTT) will not work!
}


void ClassFlowControll::setAutoStartInterval(long &_interval)
{
    _interval = AutoInterval * 60 * 1000; // AutoInterval: minutes -> ms
}

// Seconds from now until the next scheduled daily slot (the soonest slot strictly after "now";
// wraps to the first slot tomorrow if none remain today). Assumes the clock is set (caller checks).
long ClassFlowControll::getNextScheduleDelaySec()
{
    if (scheduleMinutes.empty()) return 60;   // nothing scheduled -> retry shortly
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    long nowSecOfDay = (long)lt.tm_hour * 3600 + (long)lt.tm_min * 60 + lt.tm_sec;
    for (size_t i = 0; i < scheduleMinutes.size(); ++i) {
        long slotSec = (long)scheduleMinutes[i] * 60;
        if (slotSec > nowSecOfDay) {
            return slotSec - nowSecOfDay;
        }
    }
    return (86400 - nowSecOfDay) + (long)scheduleMinutes[0] * 60;   // first slot tomorrow
}

// "HH:MM" of the next scheduled slot, for status/log display.
std::string ClassFlowControll::getNextScheduleTimeStr()
{
    if (scheduleMinutes.empty()) return "--:--";
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    long nowSecOfDay = (long)lt.tm_hour * 3600 + (long)lt.tm_min * 60 + lt.tm_sec;
    int slot = scheduleMinutes[0];   // default: first slot (tomorrow)
    for (size_t i = 0; i < scheduleMinutes.size(); ++i) {
        if ((long)scheduleMinutes[i] * 60 > nowSecOfDay) { slot = scheduleMinutes[i]; break; }
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", slot / 60, slot % 60);
    return std::string(buf);
}

ClassFlow* ClassFlowControll::CreateClassFlow(std::string _type)
{
    ClassFlow* cfc = NULL;
    _type = trim(_type);

    if (toUpper(_type).compare("[TAKEIMAGE]") == 0) {
        cfc = new ClassFlowTakeImage(&FlowControll);
        flowtakeimage = (ClassFlowTakeImage*) cfc;
    }
	
    if (toUpper(_type).compare("[ALIGNMENT]") == 0) {
        cfc = new ClassFlowAlignment(&FlowControll);
        flowalignment = (ClassFlowAlignment*) cfc;
    }
	
    if (toUpper(_type).compare("[ANALOG]") == 0) {
        cfc = new ClassFlowCNNGeneral(flowalignment);
        flowanalog = (ClassFlowCNNGeneral*) cfc;
    }
	
    if (toUpper(_type).compare(0, 7, "[DIGITS") == 0) {
        cfc = new ClassFlowCNNGeneral(flowalignment);
        flowdigit = (ClassFlowCNNGeneral*) cfc;
    }
	
    #ifdef ENABLE_MQTT
        if (toUpper(_type).compare("[MQTT]") == 0) {
            cfc = new ClassFlowMQTT(&FlowControll);
        }
    #endif //ENABLE_MQTT
	
    #ifdef ENABLE_INFLUXDB
        if (toUpper(_type).compare("[INFLUXDB]") == 0) {
            cfc = new ClassFlowInfluxDB(&FlowControll);
        }

        if (toUpper(_type).compare("[INFLUXDBV2]") == 0) {
            cfc = new ClassFlowInfluxDBv2(&FlowControll);
        }
    #endif //ENABLE_INFLUXDB
	
    #ifdef ENABLE_WEBHOOK
        if (toUpper(_type).compare("[WEBHOOK]") == 0) {
            cfc = new ClassFlowWebhook(&FlowControll);
        }
    #endif //ENABLE_WEBHOOK

    if (toUpper(_type).compare("[POSTPROCESSING]") == 0) {
        cfc = new ClassFlowPostProcessing(&FlowControll, flowanalog, flowdigit); 
        flowpostprocessing = (ClassFlowPostProcessing*) cfc;
    }

    if (cfc) {                           
        // Attached only if it is not [AutoTimer], because this is for FlowControll
        FlowControll.push_back(cfc);
    }

    if (toUpper(_type).compare("[AUTOTIMER]") == 0) {
        cfc = this;
    }

    if (toUpper(_type).compare("[DATALOGGING]") == 0) {
        cfc = this;
    }

    if (toUpper(_type).compare("[DEBUG]") == 0) {
        cfc = this;
    }

    if (toUpper(_type).compare("[SYSTEM]") == 0) {
        cfc = this;
    }

    return cfc;
}

// Parse an "Interval = <number> [unit]" value to minutes. Unit (seconds/minutes/hours/days or
// s/m/h/d) is optional; a bare number stays minutes (back-compat). Zero/negative is guarded to 5 min.
float ClassFlowControll::parseIntervalToMinutes(const std::vector<std::string>& splitted)
{
    float _val = std::stof(splitted[1]);
    float _minutes = _val;   // default unit: minutes
    if (splitted.size() > 2) {
        std::string _unit = toUpper(splitted[2]);
        if (_unit == "S" || _unit == "SEC" || _unit == "SECOND" || _unit == "SECONDS")
            _minutes = _val / 60.0f;
        else if (_unit == "H" || _unit == "HR" || _unit == "HOUR" || _unit == "HOURS")
            _minutes = _val * 60.0f;
        else if (_unit == "D" || _unit == "DAY" || _unit == "DAYS")
            _minutes = _val * 1440.0f;
    }
    if (_minutes <= 0.0f) _minutes = 5.0f;
    return _minutes;
}

// Re-read just the [AutoTimer] Interval from config.ini and apply it live (no reboot). Safe: it is a
// single scalar used only for the inter-round sleep; the running flow objects/models are untouched.
// Returns true if a valid interval was found and applied.
bool ClassFlowControll::ReloadLiveInterval()
{
    FILE* pf = fopen(FormatFileName(std::string(CONFIG_FILE)).c_str(), "r");
    if (!pf) return false;
    char buf[256];
    bool inAutoTimer = false;
    float newInterval = -1.0f;
    while (fgets(buf, sizeof(buf), pf)) {
        std::string line = trim(std::string(buf));
        if (line.size() == 0 || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { inAutoTimer = (toUpper(line).find("[AUTOTIMER]") != std::string::npos); continue; }
        if (inAutoTimer) {
            std::vector<std::string> sp = ZerlegeZeile(line, " =");
            if (sp.size() > 1 && toUpper(sp[0]) == "INTERVAL" && isStringNumeric(sp[1])) {
                newInterval = parseIntervalToMinutes(sp);
            }
        }
    }
    fclose(pf);
    if (newInterval > 0.0f) {
        AutoInterval = newInterval;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live config: processing interval set to " +
                std::to_string(AutoInterval) + " min (applies on the next round, no reboot)");
        return true;
    }
    return false;
}

void ClassFlowControll::InitFlow(std::string config)
{
    aktstatus = "Initialization";
    aktstatusWithTime = aktstatus;

    //#ifdef ENABLE_MQTT
        //MQTTPublish(mqttServer_getMainTopic() + "/" + "status", "Initialization", 1, false); // Right now, not possible -> MQTT Service is going to be started later
    //#endif //ENABLE_MQTT
    
    string line;
    flowpostprocessing = NULL;

    ClassFlow* cfc;
    FILE* pFile;
    config = FormatFileName(config);
    pFile = fopen(config.c_str(), "r");

    line = "";

    char zw[1024];
	
    if (pFile != NULL) {
        fgets(zw, 1024, pFile);
        ESP_LOGD(TAG, "%s", zw);
        line = std::string(zw);
    }

    while ((line.size() > 0) && !(feof(pFile))) {
        cfc = CreateClassFlow(line);
        // printf("Name: %s\n", cfc->name().c_str());
	    
        if (cfc) {
            ESP_LOGE(TAG, "Start ReadParameter (%s)", line.c_str());
            cfc->ReadParameter(pFile, line);
        }
        else {
            line = "";
		
            if (fgets(zw, 1024, pFile) && !feof(pFile)) {
                ESP_LOGD(TAG, "Read: %s", zw);
                line = std::string(zw);
            }
        }
    }

    fclose(pFile);

    // Once all sections are read: if the alignment crop is enabled, shift the CNN ROI coordinates
    // into crop space so cut + draw line up with the repacked (cropped) frame.
    if (flowalignment && flowalignment->IsCropEnabled()) {
        int dx = flowalignment->GetCropOffsetX();
        int dy = flowalignment->GetCropOffsetY();
        if (flowanalog) {
            flowanalog->ShiftROIs(dx, dy);
        }
        if (flowdigit) {
            flowdigit->ShiftROIs(dx, dy);
        }
    }
}

std::string* ClassFlowControll::getActStatusWithTime()
{
    return &aktstatusWithTime;
}

std::string* ClassFlowControll::getActStatus()
{
    return &aktstatus;
}

void ClassFlowControll::setActStatus(std::string _aktstatus)
{
    aktstatus = _aktstatus;
    aktstatusWithTime = aktstatus;
}

string ClassFlowControll::getDigitMatrixJson()
{
    string json = "{";
    if (flowdigit) {
        flowdigit->AppendDigitMatrixJson(json);
    }
    json += "}";
    return json;
}

string ClassFlowControll::ExamineCutRoi(bool isAnalog, const std::string &cutOrgPath, const std::string &displayPath, bool ccw)
{
    ClassFlowCNNGeneral *flow = isAnalog ? flowanalog : flowdigit;
    if (!flow) {
        return "\"error\":\"no " + std::string(isAnalog ? "analog" : "digit") + " model is configured\"";
    }
    return flow->ExamineCut(cutOrgPath, displayPath, ccw);
}

bool ClassFlowControll::SaveFreshAlignedImage(const std::string &path)
{
    if (!flowalignment) {
        return false;
    }
    CAlignAndCutImage *aligned = flowalignment->GetAlignAndCutImage();
    if (!aligned || !aligned->ImageOkay()) {
        return false;
    }
    aligned->SaveToFile(FormatFileName(path));
    return true;
}

void ClassFlowControll::doFlowTakeImageOnly(string time)
{
    std::string zw_time;

    for (int i = 0; i < FlowControll.size(); ++i) {
        if (FlowControll[i]->name() == "ClassFlowTakeImage") {
            zw_time = getCurrentTimeString("%H:%M:%S");
            aktstatus = TranslateAktstatus(FlowControll[i]->name());
            aktstatusWithTime = aktstatus + " (" + zw_time + ")";
            #ifdef ENABLE_MQTT
                MQTTPublish(mqttServer_getMainTopic() + "/" + "status", aktstatus, 1, false);
            #endif //ENABLE_MQTT

            FlowControll[i]->doFlow(time);
        }
    }
}

// Map a flow-step class name to a status-LED ProcessingStage (Helper.h).
static int stageFromFlowName(const std::string& name)
{
    if (name == "ClassFlowTakeImage")      return PROC_STAGE_TAKEIMAGE;
    if (name == "ClassFlowAlignment")      return PROC_STAGE_ALIGN;
    if (name == "ClassFlowCNNGeneral")     return PROC_STAGE_DIGITIZE;
    if (name == "ClassFlowPostProcessing") return PROC_STAGE_POSTPROC;
    if (name == "ClassFlowMQTT" || name == "ClassFlowInfluxDB" ||
        name == "ClassFlowInfluxDBv2" || name == "ClassFlowWebhook")
        return PROC_STAGE_TRANSMIT;
    return PROC_STAGE_IDLE;
}

bool ClassFlowControll::doFlow(string time)
{
    bool result = true;
    std::string zw_time;
    int repeat = 0;
    int qos = 1;

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("ClassFlowControll::doFlow - Start");
    #endif

    /* Check if we have a valid date/time and if not restart the NTP client */
   /* if (! getTimeIsSet()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Time not set, restarting NTP Client!");
        restartNtpClient();
    }*/

    //checkNtpStatus(0);

    // Diagnostics: total round timing for performance monitoring (DEBUG level).
    int64_t round_start_us = esp_timer_get_time();

    for (int i = 0; i < FlowControll.size(); ++i) {
        zw_time = getCurrentTimeString("%H:%M:%S");
        aktstatus = TranslateAktstatus(FlowControll[i]->name());
        aktstatusWithTime = aktstatus + " (" + zw_time + ")";
        setProcessingStage(stageFromFlowName(FlowControll[i]->name()));   // status LED
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Status: " + aktstatusWithTime);
        #ifdef ENABLE_MQTT
            MQTTPublish(mqttServer_getMainTopic() + "/" + "status", aktstatus, qos, false);
        #endif //ENABLE_MQTT

        #ifdef DEBUG_DETAIL_ON
            string zw = "FlowControll.doFlow - " + FlowControll[i]->name();
            LogFile.WriteHeapInfo(zw);
        #endif

        // Diagnostics: per-step duration + heap usage (DEBUG level), so slow
        // steps and memory pressure can be spotted without a special build.
        int64_t step_start_us = esp_timer_get_time();
        size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        if (!FlowControll[i]->doFlow(time)) {
            // The alignment step does best-effort marker matching and only reports failure when the
            // regenerable align.txt cache can't be read back (missing/corrupt) - a rare, recoverable
            // condition, not a stuck device. Don't retry it into the 5x-failure reboot below: end the
            // round gracefully and let the next round try again (it self-heals once the cache is
            // rewritten). All other steps keep the retry/reboot watchdog behaviour.
            //
            // The downstream steps that normally report to MQTT / InfluxDB / the REST API are
            // skipped on this round, so surface the failure here instead of silently ending as
            // "Flow finished": set the round status (REST /statusflow + MQTT <topic>/status) and
            // raise the MQTT error topic (drives the Home Assistant "problem" binary sensor). The
            // next successful round republishes "no error" and clears it. InfluxDB only stores the
            // numeric reading, so a skipped round is simply a gap in the series (no datapoint).
            // ClassFlowTakeImage joins this graceful-skip branch for the same reason: its only
            // soft failure is "the shared PSRAM region was too small to decode this frame" (a wedged
            // camera already reboots inside CaptureToBasisImage). A reboot cannot grow the region, so
            // retrying into the 5x-failure reboot below would just boot-loop - skip the round and
            // retry next time instead.
            const std::string &failedStep = FlowControll[i]->name();
            if ((failedStep == "ClassFlowAlignment") || (failedStep == "ClassFlowTakeImage")) {
                const char *stepLabel = (failedStep == "ClassFlowTakeImage") ? "Take Image" : "Alignment";
                setProcessingStage(PROC_STAGE_ERROR);
                aktstatus = std::string(stepLabel) + " step could not complete this round";
                aktstatusWithTime = aktstatus + " (" + getCurrentTimeString("%H:%M:%S") + ")";
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, failedStep + " failed - reporting status/error "
                    "to REST + MQTT and skipping the rest of this round without rebooting; will retry "
                    "on the next round.");
                #ifdef ENABLE_MQTT
                    MQTTPublish(mqttServer_getMainTopic() + "/" + "status", aktstatus, qos, false);
                    MQTTPublish(mqttServer_getMainTopic() + "/" + "error",
                                std::string(stepLabel) + " step could not complete", qos, false);
                #endif //ENABLE_MQTT
                return false;   // skip the post-loop "Flow finished" so the failure status persists
            }
            repeat++;
            setProcessingStage(PROC_STAGE_ERROR);   // status LED: step failed / retrying
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Fehler im vorheriger Schritt - wird zum " + to_string(repeat) + ". Mal wiederholt");
            if (i) { i -= 1; }   // vPrevious step must be repeated (probably take pictures)
            result = false;
            if (repeat > 5) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wiederholung 5x nicht erfolgreich --> reboot");
                doReboot();
                //Step was repeated 5x --> reboot
            }
        }
        else {
            result = true;
        }

        // Diagnostics (DEBUG): step duration in ms and internal/PSRAM heap delta.
        int64_t step_ms = (esp_timer_get_time() - step_start_us) / 1000;
        long heap_delta = (long)heap_before - (long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        long psram_delta = (long)psram_before - (long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Diag: " + FlowControll[i]->name() +
            " took " + to_string(step_ms) + " ms, heap " + to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) +
            " KB (d" + to_string(heap_delta) + "), psram " + to_string(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) +
            " KB (d" + to_string(psram_delta) + ")");

        #ifdef DEBUG_DETAIL_ON
            LogFile.WriteHeapInfo("ClassFlowControll::doFlow");
        #endif
    }

    int64_t round_ms = (esp_timer_get_time() - round_start_us) / 1000;
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Diag: full flow round took " + to_string(round_ms) + " ms");

    zw_time = getCurrentTimeString("%H:%M:%S");
    aktstatus = "Flow finished";
    aktstatusWithTime = aktstatus + " (" + zw_time + ")";
    setProcessingStage(PROC_STAGE_IDLE);   // status LED: idle until next round
    //LogFile.WriteToFile(ESP_LOG_INFO, TAG, aktstatusWithTime);
    #ifdef ENABLE_MQTT
        MQTTPublish(mqttServer_getMainTopic() + "/" + "status", aktstatus, qos, false);
    #endif //ENABLE_MQTT

    return result;
}


string ClassFlowControll::getReadoutAll(int _type)
{
    std::string out = "";
	
    if (flowpostprocessing) {
        std::vector<NumberPost*> *numbers = flowpostprocessing->GetNumbers();

        for (int i = 0; i < (*numbers).size(); ++i) {
            out = out + (*numbers)[i]->name + "\t";
		
            switch (_type) {
                case READOUT_TYPE_VALUE:
                    out = out + (*numbers)[i]->ReturnValue;
                    break;
                case READOUT_TYPE_PREVALUE:
                    if (flowpostprocessing->PreValueUse) {
                        if ((*numbers)[i]->PreValueOkay) {
                            out = out + (*numbers)[i]->ReturnPreValue;
                        }
                        else {
                            out = out + "PreValue too old"; 
                        }
                    }
                    else {
                        out = out + "PreValue deactivated";
                    }
                    break;
                case READOUT_TYPE_RAWVALUE:
                    out = out + (*numbers)[i]->ReturnRawValue;
                    break;
                case READOUT_TYPE_ERROR:
                    out = out + (*numbers)[i]->ErrorMessageText;
                    break;
            }
		
            if (i < (*numbers).size()-1) {
                out = out + "\r\n";
            }
        }
    // ESP_LOGD(TAG, "OUT: %s", out.c_str());
    }

    return out;
}	

string ClassFlowControll::getReadout(bool _rawvalue = false, bool _noerror = false, int _number = 0)
{
    if (flowpostprocessing) {
        return flowpostprocessing->getReadoutParam(_rawvalue, _noerror, _number);
    }

    return std::string("");
}

string ClassFlowControll::GetPrevalue(std::string _number)	
{
    if (flowpostprocessing) {
        return flowpostprocessing->GetPreValue(_number);   
    }

    return std::string("");    
}

bool ClassFlowControll::UpdatePrevalue(std::string _newvalue, std::string _numbers, bool _extern)
{
    double newvalueAsDouble;
    char* p;

    _newvalue = trim(_newvalue);
    // ESP_LOGD(TAG, "Input UpdatePreValue: %s", _newvalue.c_str());

    if (_newvalue.substr(0,8).compare("0.000000") == 0 || _newvalue.compare("0.0") == 0 || _newvalue.compare("0") == 0) {
        newvalueAsDouble = 0;   // preset to value = 0
    }
    else {
        newvalueAsDouble = strtod(_newvalue.c_str(), &p);
        if (newvalueAsDouble == 0) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "UpdatePrevalue: No valid value for processing: " + _newvalue);
            return false;
        }
    }
    
    if (flowpostprocessing) {
        if (flowpostprocessing->SetPreValue(newvalueAsDouble, _numbers, _extern)) {
            return true;
        }
        else {
            return false;
        }
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "UpdatePrevalue: ERROR - Class Post-Processing not initialized");
        return false;
    }
}

bool ClassFlowControll::ReadParameter(FILE* pfile, string& aktparamgraph)
{
    std::vector<string> splitted;
    aktparamgraph = trim(aktparamgraph);

    if (aktparamgraph.size() == 0) {
        if (!this->GetNextParagraph(pfile, aktparamgraph)) {
            return false;
        }
    }

    if ((toUpper(aktparamgraph).compare("[AUTOTIMER]") != 0) && (toUpper(aktparamgraph).compare("[DEBUG]") != 0) &&
        (toUpper(aktparamgraph).compare("[SYSTEM]") != 0 && (toUpper(aktparamgraph).compare("[DATALOGGING]") != 0))) {     
        // Paragraph does not match Debug or DataLogging
        return false;
    }

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph)) {
        splitted = ZerlegeZeile(aktparamgraph, " =");

        if ((toUpper(splitted[0]) == "INTERVAL") && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                AutoInterval = parseIntervalToMinutes(splitted);
            }
        }

        if ((toUpper(splitted[0]) == "TRIGGERMODE") && (splitted.size() > 1)) {
            // interval (default) = fixed cadence; schedule = run at the listed daily times.
            scheduleMode = (toUpper(splitted[1]) == "SCHEDULE");
        }

        if ((toUpper(splitted[0]) == "SCHEDULE") && (splitted.size() > 1)) {
            // Schedule = HH:MM,HH:MM,...  Daily times a round runs in schedule mode. Multiple slots.
            scheduleMinutes.clear();
            std::vector<std::string> slots = ZerlegeZeile(splitted[1], ",");
            for (size_t si = 0; si < slots.size(); ++si) {
                std::string t = trim(slots[si]);
                size_t c = t.find(':');
                if (c == std::string::npos) continue;
                int hh = atoi(t.substr(0, c).c_str());
                int mm = atoi(t.substr(c + 1).c_str());
                if (hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
                    scheduleMinutes.push_back(hh * 60 + mm);
                }
            }
            std::sort(scheduleMinutes.begin(), scheduleMinutes.end());
            scheduleMinutes.erase(std::unique(scheduleMinutes.begin(), scheduleMinutes.end()), scheduleMinutes.end());
        }

        if ((toUpper(splitted[0]) == "DATALOGACTIVE") && (splitted.size() > 1)) {
            LogFile.SetDataLogToSD(alphanumericToBoolean(splitted[1]));
        }

        if ((toUpper(splitted[0]) == "DATAFILESRETENTION") && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                LogFile.SetDataLogRetention(std::stoi(splitted[1]));
            }
        }

        if ((toUpper(splitted[0]) == "LOGLEVEL") && (splitted.size() > 1)) {
            /* matches esp_log_level_t */
            if ((toUpper(splitted[1]) == "TRUE") || (toUpper(splitted[1]) == "2")) {
                LogFile.setLogLevel(ESP_LOG_WARN);
            }
            else if ((toUpper(splitted[1]) == "FALSE") || (toUpper(splitted[1]) == "0") || (toUpper(splitted[1]) == "1")) {
                LogFile.setLogLevel(ESP_LOG_ERROR);
            }
            else if (toUpper(splitted[1]) == "3") {
                LogFile.setLogLevel(ESP_LOG_INFO);
            }
            else if (toUpper(splitted[1]) == "4") {
                LogFile.setLogLevel(ESP_LOG_DEBUG);
            }

            /* If system reboot was not triggered by user and reboot was caused by execption -> keep log level to DEBUG */
            if (!getIsPlannedReboot() && (esp_reset_reason() == ESP_RST_PANIC)) {
                LogFile.setLogLevel(ESP_LOG_DEBUG);
            }
        }
	    
        if ((toUpper(splitted[0]) == "LOGFILESRETENTION") && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                LogFile.SetLogFileRetention(std::stoi(splitted[1]));
            }
        }

        /* TimeServer and TimeZone got already read from the config, see setupTime () */
        
        #if (defined WLAN_USE_ROAMING_BY_SCANNING || (defined WLAN_USE_MESH_ROAMING && defined WLAN_USE_MESH_ROAMING_ACTIVATE_CLIENT_TRIGGERED_QUERIES))
        if ((toUpper(splitted[0]) == "RSSITHRESHOLD") && (splitted.size() > 1)) {
            int RSSIThresholdTMP = atoi(splitted[1].c_str());
            RSSIThresholdTMP = min(0, max(-100, RSSIThresholdTMP)); // Verify input limits (-100 - 0)
            
            if (ChangeRSSIThreshold(WLAN_CONFIG_FILE, RSSIThresholdTMP)) {
                // reboot necessary so that the new wlan.ini is also used !!!
                fclose(pfile);
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Rebooting to activate new RSSITHRESHOLD ...");
                doReboot();
            }
        }
        #endif

        if ((toUpper(splitted[0]) == "HOSTNAME") && (splitted.size() > 1)) {
            if (ChangeHostName(WLAN_CONFIG_FILE, splitted[1])) {
                // reboot necessary so that the new wlan.ini is also used !!!
                fclose(pfile);
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Rebooting to activate new HOSTNAME...");             
                doReboot();
            }
        }

        if ((toUpper(splitted[0]) == "SETUPMODE") && (splitted.size() > 1)) {
            SetupModeActive = alphanumericToBoolean(splitted[1]);
        }

        if ((toUpper(splitted[0]) == "BACKUPINTERVAL") && (splitted.size() > 1)) {
            // Scheduled backup to /sdcard/backup, in days. 0 (or non-numeric) = disabled.
            int _days = isStringNumeric(splitted[1]) ? atoi(splitted[1].c_str()) : 0;
            if (_days < 0) _days = 0;
            setBackupAutoIntervalDays(_days);
        }
    }
    return true;
}

int ClassFlowControll::CleanTempFolder() {
    const char* folderPath = "/sdcard/img_tmp";
    
    ESP_LOGD(TAG, "Clean up temporary folder to avoid damage of sdcard sectors: %s", folderPath);
    DIR *dir = opendir(folderPath);
	
    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir: %s", folderPath);
        return -1;
    }

    struct dirent *entry;
    int deleted = 0;
	
    while ((entry = readdir(dir)) != NULL) {
        std::string path = string(folderPath) + "/" + entry->d_name;
        if (entry->d_type == DT_REG) {
            if (unlink(path.c_str()) == 0) {
                deleted ++;
            } 
            else {
                ESP_LOGE(TAG, "can't delete file: %s", path.c_str());
            }
        } 
        else if (entry->d_type == DT_DIR) {
            deleted += removeFolder(path.c_str(), TAG);
        }
    }
	
    closedir(dir);
    ESP_LOGD(TAG, "%d files deleted", deleted);
    
    return 0;
}

esp_err_t ClassFlowControll::SendRawJPG(httpd_req_t *req)
{
    return flowtakeimage != NULL ? flowtakeimage->SendRawJPG(req) : ESP_FAIL;
}

esp_err_t ClassFlowControll::GetJPGStream(std::string _fn, httpd_req_t *req)
{
    ESP_LOGD(TAG, "ClassFlowControll::GetJPGStream %s", _fn.c_str());

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("ClassFlowControll::GetJPGStream - Start");
    #endif

    CImageBasis *_send = NULL;
    esp_err_t result = ESP_FAIL;
    bool _sendDelete = false;

    if (_fn == "alg.jpg") {
        if (flowalignment && flowalignment->ImageBasis && flowalignment->ImageBasis->ImageOkay()) {
            _send = flowalignment->ImageBasis;
        }
        else {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ClassFlowControll::GetJPGStream: alg.jpg cannot be served");
            return ESP_FAIL;
        }
    }
    else if (_fn == "alg_roi.jpg") {
        #ifdef ALGROI_LOAD_FROM_MEM_AS_JPG      // no CImageBasis needed to create alg_roi.jpg (ca. 790kB less RAM)
            if (aktstatus.find("Initialization (delayed)") != -1) {
                std::string filename = "/sdcard/html/Flowstate_initialization_delayed.jpg";
                result = send_file(req, filename);
                /*    
                FILE* file = fopen("/sdcard/html/Flowstate_initialization_delayed.jpg", "rb"); 

                if (!file) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File /sdcard/html/Flowstate_initialization_delayed.jpg not found");
                    return ESP_FAIL;
                }

                fseek(file, 0, SEEK_END);
                long fileSize = ftell(file); // how long is the file ?
                fseek(file, 0, SEEK_SET); // reset

                unsigned char* fileBuffer = (unsigned char*) malloc(fileSize);

                if (!fileBuffer) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ClassFlowControll::GetJPGStream: Not enough memory to create fileBuffer: " + std::to_string(fileSize));
                    fclose(file);  
                    return ESP_FAIL;
                }

                fread(fileBuffer, fileSize, 1, file);
                fclose(file);

                httpd_resp_set_type(req, "image/jpeg");
                result = httpd_resp_send(req, (const char *)fileBuffer, fileSize); 
                free(fileBuffer);
                */
            }
            else if (aktstatus.find("Initialization") != -1) {
                std::string filename = "/sdcard/html/Flowstate_initialization.jpg";
                result = send_file(req, filename);
                /*
                FILE* file = fopen("/sdcard/html/Flowstate_initialization.jpg", "rb"); 

                if (!file) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File /sdcard/html/Flowstate_initialization.jpg not found");
                    return ESP_FAIL;
                }

                fseek(file, 0, SEEK_END);
                long fileSize = ftell(file); // how long is the file ?
                fseek(file, 0, SEEK_SET); // reset

                unsigned char* fileBuffer = (unsigned char*) malloc(fileSize);

                if (!fileBuffer) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ClassFlowControll::GetJPGStream: Not enough memory to create fileBuffer: " + std::to_string(fileSize));
                    fclose(file);  
                    return ESP_FAIL;
                }

                fread(fileBuffer, fileSize, 1, file);
                fclose(file);

                httpd_resp_set_type(req, "image/jpeg");
                result = httpd_resp_send(req, (const char *)fileBuffer, fileSize); 
                free(fileBuffer);
                */
            }
            else if (aktstatus.find("Take Image") != -1) {
                if (flowalignment && flowalignment->AlgROI) {
                    std::string filename = "/sdcard/html/Flowstate_take_image.jpg";
                    result = send_file(req, filename);
                    /*
                    FILE* file = fopen("/sdcard/html/Flowstate_take_image.jpg", "rb");    

                    if (!file) {
                        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File /sdcard/html/Flowstate_take_image.jpg not found");
                        return ESP_FAIL;
                    }

                    fseek(file, 0, SEEK_END);
                    flowalignment->AlgROI->size = ftell(file); // how long is the file ?
                    fseek(file, 0, SEEK_SET); // reset
                    
                    if (flowalignment->AlgROI->size > MAX_JPG_SIZE) {
                        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File /sdcard/html/Flowstate_take_image.jpg (" + std::to_string(flowalignment->AlgROI->size) +
                                                                ") > allocated buffer (" + std::to_string(MAX_JPG_SIZE) + ")");
                        fclose(file);
                        return ESP_FAIL;
                    }

                    fread(flowalignment->AlgROI->data, flowalignment->AlgROI->size, 1, file);
                    fclose(file);

                    httpd_resp_set_type(req, "image/jpeg");
                    result = httpd_resp_send(req, (const char *)flowalignment->AlgROI->data, flowalignment->AlgROI->size);
                    */
                }
                else {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ClassFlowControll::GetJPGStream: alg_roi.jpg cannot be served -> alg.jpg is going to be served!");
                    if (flowalignment && flowalignment->ImageBasis && flowalignment->ImageBasis->ImageOkay()) {
                        _send = flowalignment->ImageBasis;
                    }
                    else {
                        httpd_resp_send(req, NULL, 0);
                        return ESP_OK;
                    }
                }
            }
            else {
                if (flowalignment && flowalignment->AlgROI) {
                    httpd_resp_set_type(req, "image/jpeg");
                    result = httpd_resp_send(req, (const char *)flowalignment->AlgROI->data, flowalignment->AlgROI->size);
                }
                else {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ClassFlowControll::GetJPGStream: alg_roi.jpg cannot be served -> alg.jpg is going to be served!");
                    if (flowalignment && flowalignment->ImageBasis && flowalignment->ImageBasis->ImageOkay()) {
                        _send = flowalignment->ImageBasis;
                    }
                    else {
                        httpd_resp_send(req, NULL, 0);
                        return ESP_OK;
                    }
                }
            }
        #else
            if (!flowalignment) {
                ESP_LOGD(TAG, "ClassFloDControll::GetJPGStream: FlowAlignment is not (yet) initialized. Interrupt serving!");
                httpd_resp_send(req, NULL, 0);
                return ESP_FAIL;
            }

            _send = new CImageBasis("alg_roi", flowalignment->ImageBasis);
			
            if (_send->ImageOkay()) {
                if (flowalignment) { flowalignment->DrawRef(_send); }
                if (flowdigit) { flowdigit->DrawROI(_send); }
                if (flowanalog) { flowanalog->DrawROI(_send); }
                _sendDelete = true; // delete temporary _send element after sending
            }
            else {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "ClassFlowControll::GetJPGStream: Not enough memory to create alg_roi.jpg -> alg.jpg is going to be served!");
                
                if (flowalignment && flowalignment->ImageBasis && flowalignment->ImageBasis->ImageOkay()) {
                    _send = flowalignment->ImageBasis;
                }
                else {
                    httpd_resp_send(req, NULL, 0);
                    return ESP_OK;
                }
            }
        #endif
    }
    else {
        std::vector<HTMLInfo*> htmlinfo;
    
        htmlinfo = GetAllDigit();
        ESP_LOGD(TAG, "After getClassFlowControll::GetAllDigit");

        for (int i = 0; i < htmlinfo.size(); ++i) {
            if (_fn == htmlinfo[i]->filename) {
                if (htmlinfo[i]->image) {
                    _send = htmlinfo[i]->image;
                }
            }

            if (_fn == htmlinfo[i]->filename_org) {
                if (htmlinfo[i]->image_org) {
                    _send = htmlinfo[i]->image_org;
                }
            }
            delete htmlinfo[i];
        }
        htmlinfo.clear();

        if (!_send) {
            htmlinfo = GetAllAnalog();
            ESP_LOGD(TAG, "After getClassFlowControll::GetAllAnalog");
	        
            for (int i = 0; i < htmlinfo.size(); ++i) {
                if (_fn == htmlinfo[i]->filename) {
                    if (htmlinfo[i]->image) {
                        _send = htmlinfo[i]->image;
                    }
                }

                if (_fn == htmlinfo[i]->filename_org) {
                    if (htmlinfo[i]->image_org) {
                        _send = htmlinfo[i]->image_org;
                    }
                }
                delete htmlinfo[i];
            }
            htmlinfo.clear();
        }
    }

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("ClassFlowControll::GetJPGStream - before send");
    #endif

    if (_send) {
        ESP_LOGD(TAG, "Sending file: %s ...", _fn.c_str());
        set_content_type_from_file(req, _fn.c_str());
        result = _send->SendJPGtoHTTP(req);
	
        /* Respond with an empty chunk to signal HTTP response completion */
        httpd_resp_send_chunk(req, NULL, 0);
        ESP_LOGD(TAG, "File sending complete");

        if (_sendDelete) {
            delete _send;
        }
            
        _send = NULL;  
    }

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("ClassFlowControll::GetJPGStream - done");
    #endif

    return result;
}

string ClassFlowControll::getNumbersName()
{
    return flowpostprocessing->getNumbersName();
}

string ClassFlowControll::getJSON()
{
    return flowpostprocessing->GetJSON();
}

/** 
 * @returns a vector of all current sequences
 **/
const std::vector<NumberPost*> &ClassFlowControll::getNumbers()
{
    return *flowpostprocessing->GetNumbers();
}
