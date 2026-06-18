#include <string>
#include <functional>
#include <cstdlib>   // strtol (exception-free int parse; build is -fno-exceptions)
#include <cerrno>    // errno / ERANGE
#include "string.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "esp_log.h"

#include <sys/stat.h>
#include <vector>

#include "../../include/defines.h"

#include "server_GPIO.h"

#include "ClassLogFile.h"
#include "configFile.h"
#include "Helper.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#include "server_mqtt.h"
#endif //ENABLE_MQTT

#include "basic_auth.h"

static const char *TAG = "GPIO";
QueueHandle_t gpio_queue_handle = NULL;

GpioPin::GpioPin(gpio_num_t gpio, const char* name, gpio_pin_mode_t mode, gpio_int_type_t interruptType, uint8_t dutyResolution, std::string mqttTopic, bool httpEnable) 
{
    _gpio = gpio;
    _name = name; 
    _mode = mode;
    _interruptType = interruptType;    
    _mqttTopic = mqttTopic;
}

GpioPin::~GpioPin()
{
    ESP_LOGD(TAG,"reset GPIO pin %d", _gpio);
    if (_interruptType != GPIO_INTR_DISABLE) {
        //hook isr handler for specific gpio pin
        gpio_isr_handler_remove(_gpio);
    }
    gpio_reset_pin(_gpio);
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    GpioResult gpioResult;
    gpioResult.gpio = *(gpio_num_t*) arg;
    gpioResult.value = gpio_get_level(gpioResult.gpio);
    BaseType_t ContextSwitchRequest = pdFALSE;
 
    xQueueSendToBackFromISR(gpio_queue_handle,(void*)&gpioResult,&ContextSwitchRequest);
   
    if(ContextSwitchRequest){
        taskYIELD();
    }
}

static void gpioHandlerTask(void *arg) {
    ESP_LOGD(TAG,"start interrupt task");
    while(1){
        if(uxQueueMessagesWaiting(gpio_queue_handle)){
            while(uxQueueMessagesWaiting(gpio_queue_handle)){
                GpioResult gpioResult;
                xQueueReceive(gpio_queue_handle,(void*)&gpioResult,10);
                ESP_LOGD(TAG,"gpio: %d state: %d", gpioResult.gpio, gpioResult.value);
                ((GpioHandler*)arg)->gpioInterrupt(&gpioResult);
            }  
        }

        ((GpioHandler*)arg)->taskHandler();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void GpioPin::gpioInterrupt(int value) {
#ifdef ENABLE_MQTT    
    if (_mqttTopic.compare("") != 0) {
        ESP_LOGD(TAG, "gpioInterrupt %s %d", _mqttTopic.c_str(), value);

        MQTTPublish(_mqttTopic, value ? "true" : "false", 1);        
    }
#endif //ENABLE_MQTT
    currentState = value;
}

void GpioPin::init()
{
    gpio_config_t io_conf;
    //set interrupt
    io_conf.intr_type = _interruptType;
    //set as output mode
    io_conf.mode = (_mode == GPIO_PIN_MODE_OUTPUT) || (_mode == GPIO_PIN_MODE_BUILT_IN_FLASH_LED) ? gpio_mode_t::GPIO_MODE_OUTPUT : gpio_mode_t::GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = (1ULL << _gpio);
    //set pull-down mode
    io_conf.pull_down_en = _mode == GPIO_PIN_MODE_INPUT_PULLDOWN ? gpio_pulldown_t::GPIO_PULLDOWN_ENABLE : gpio_pulldown_t::GPIO_PULLDOWN_DISABLE;
    //set pull-up mode
    io_conf.pull_up_en = _mode == GPIO_PIN_MODE_INPUT_PULLDOWN ? gpio_pullup_t::GPIO_PULLUP_ENABLE : gpio_pullup_t::GPIO_PULLUP_DISABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

//    if (_interruptType != GPIO_INTR_DISABLE) {                // without GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X; when that is used, the handler here should not be initialized either, since it is then handled via SmartLED.
    // Note: GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X is a gpio_pin_mode_t value compared numerically
    // against the gpio_int_type_t interrupt type; cast to int to keep the original behavior
    // (GCC 15 rejects the cross-enum comparison under -Werror=enum-compare).
    if ((_interruptType != GPIO_INTR_DISABLE) && ((int)_interruptType != (int)GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X)) {
        //hook isr handler for specific gpio pin
        ESP_LOGD(TAG, "GpioPin::init add isr handler for GPIO %d", _gpio);
        gpio_isr_handler_add(_gpio, gpio_isr_handler, (void*)&_gpio);
    }

#ifdef ENABLE_MQTT
    if ((_mqttTopic.compare("") != 0) && ((_mode == GPIO_PIN_MODE_OUTPUT) || (_mode == GPIO_PIN_MODE_OUTPUT_PWM) || (_mode == GPIO_PIN_MODE_BUILT_IN_FLASH_LED))) {
        std::function<bool(std::string, char*, int)> f = std::bind(&GpioPin::handleMQTT, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        MQTTregisterSubscribeFunction(_mqttTopic, f);
    }
#endif //ENABLE_MQTT
}

bool GpioPin::getValue(std::string* errorText)
{   
    if ((_mode != GPIO_PIN_MODE_INPUT) && (_mode != GPIO_PIN_MODE_INPUT_PULLUP) && (_mode != GPIO_PIN_MODE_INPUT_PULLDOWN)) {
        (*errorText) = "GPIO is not in input mode";
    }

    return gpio_get_level(_gpio) == 1;
}

void GpioPin::setValue(bool value, gpio_set_source setSource, std::string* errorText)
{
    ESP_LOGD(TAG, "GpioPin::setValue %d", value);

    if ((_mode != GPIO_PIN_MODE_OUTPUT) && (_mode != GPIO_PIN_MODE_OUTPUT_PWM) && (_mode != GPIO_PIN_MODE_BUILT_IN_FLASH_LED)) {
        (*errorText) = "GPIO is not in output mode";
    } else {
        gpio_set_level(_gpio, value);

#ifdef ENABLE_MQTT
        if ((_mqttTopic.compare("") != 0) && (setSource != GPIO_SET_SOURCE_MQTT)) {
            MQTTPublish(_mqttTopic, value ? "true" : "false", 1);
        }
#endif //ENABLE_MQTT
    }
}

void GpioPin::publishState() {
    int newState = gpio_get_level(_gpio);
    if (newState != currentState) {
        ESP_LOGD(TAG,"publish state of GPIO %d new state %d", _gpio, newState);
#ifdef ENABLE_MQTT
    if (_mqttTopic.compare("") != 0)
        MQTTPublish(_mqttTopic, newState ? "true" : "false", 1);
#endif //ENABLE_MQTT
        currentState = newState;
    }
}

#ifdef ENABLE_MQTT
bool GpioPin::handleMQTT(std::string, char* data, int data_len) {
    ESP_LOGD(TAG, "GpioPin::handleMQTT data %.*s", data_len, data);

    std::string dataStr(data, data_len);
    dataStr = toLower(dataStr);
    std::string errorText = "";
    if ((dataStr == "true") || (dataStr == "1")) {
        setValue(true, GPIO_SET_SOURCE_MQTT, &errorText);
    } else if ((dataStr == "false") || (dataStr == "0")) {
        setValue(false, GPIO_SET_SOURCE_MQTT, &errorText);    
    } else {
        errorText = "wrong value ";
        errorText.append(data, data_len);
    }

    if (errorText != "") {
        ESP_LOGE(TAG, "%s", errorText.c_str());
    }

    return (errorText == "");
}
#endif //ENABLE_MQTT

esp_err_t callHandleHttpRequest(httpd_req_t *req)
{
    ESP_LOGD(TAG,"callHandleHttpRequest");

    GpioHandler *gpioHandler = (GpioHandler*)req->user_ctx;
    return gpioHandler->handleHttpRequest(req);
}

esp_err_t callHandleLedBrightness(httpd_req_t *req)
{
    GpioHandler *gpioHandler = (GpioHandler*)req->user_ctx;
    return gpioHandler->handleLedBrightnessRequest(req);
}

void taskGpioHandler(void *pvParameter)
{
    ESP_LOGD(TAG,"taskGpioHandler");
    ((GpioHandler*)pvParameter)->init();
}

GpioHandler::GpioHandler(std::string configFile, httpd_handle_t httpServer) 
{
    ESP_LOGI(TAG,"start GpioHandler");
    _configFile = configFile;
    _httpServer = httpServer;

    ESP_LOGI(TAG, "register GPIO Uri");
    registerGpioUri();
}

GpioHandler::~GpioHandler()  {
    if (gpioMap != NULL) {
        clear();
        delete gpioMap;
    }
}

static void statusLedStageTrampoline(int stage);   // defined below

void GpioHandler::init()
{
    // TickType_t xDelay = 60000 / portTICK_PERIOD_MS;
    // ESP_LOGD(TAG, "wait before start %ldms", (long) xDelay);
    // vTaskDelay( xDelay );

    ESP_LOGD(TAG, "*************** Start GPIOHandler_Init *****************");

    initStatusLedDefaults();   // set defaults; config (if present) overrides in readConfig()
    registerStatusLedStageCallback(statusLedStageTrampoline);   // let the flow drive the status LED

    if (gpioMap == NULL) {
        gpioMap = new std::map<gpio_num_t, GpioPin*>();
    } else {
        clear();
    }
    
    
    ESP_LOGI(TAG, "read GPIO config and init GPIO");
    if (!readConfig()) {
        clear();
        delete gpioMap;
        gpioMap = NULL;
        ESP_LOGI(TAG, "GPIO init completed, handler is disabled");
        return;
    }


    for(std::map<gpio_num_t, GpioPin*>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it) {
        it->second->init();
    }

#ifdef ENABLE_MQTT
    std::function<void()> f = std::bind(&GpioHandler::handleMQTTconnect, this);
    MQTTregisterConnectFunction("gpio-handler", f);
#endif //ENABLE_MQTT

    if (xHandleTaskGpio == NULL) {
        gpio_queue_handle = xQueueCreate(10,sizeof(GpioResult));
        BaseType_t  xReturned = xTaskCreate(&gpioHandlerTask, "gpio_int", 3 * 1024, (void *)this, tskIDLE_PRIORITY + 4, &xHandleTaskGpio);
        if(xReturned == pdPASS ) {
            ESP_LOGD(TAG, "xHandletaskGpioHandler started");
        } else {
            ESP_LOGD(TAG, "xHandletaskGpioHandler not started %d ", (int)xHandleTaskGpio);
        }
    }

    ESP_LOGI(TAG, "GPIO init completed, is enabled");
}

void GpioHandler::taskHandler() {
    if (gpioMap != NULL) {
        for(std::map<gpio_num_t, GpioPin*>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it) {
            if ((it->second->getInterruptType() == GPIO_INTR_DISABLE))
                it->second->publishState();
        }
    }
}

#ifdef ENABLE_MQTT
void GpioHandler::handleMQTTconnect()
{
    if (gpioMap != NULL) {
        for(std::map<gpio_num_t, GpioPin*>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it) {
            if ((it->second->getMode() == GPIO_PIN_MODE_INPUT) || (it->second->getMode() == GPIO_PIN_MODE_INPUT_PULLDOWN) || (it->second->getMode() == GPIO_PIN_MODE_INPUT_PULLUP))
                it->second->publishState();
        }
    }
}
#endif //ENABLE_MQTT

void GpioHandler::deinit() {
#ifdef ENABLE_MQTT
    MQTTunregisterConnectFunction("gpio-handler");
#endif //ENABLE_MQTT
    clear();
    if (xHandleTaskGpio != NULL) {
        vTaskDelete(xHandleTaskGpio);
        xHandleTaskGpio = NULL;
    }
}

void GpioHandler::gpioInterrupt(GpioResult* gpioResult) {
    if ((gpioMap != NULL) && (gpioMap->find(gpioResult->gpio) != gpioMap->end())) {
        (*gpioMap)[gpioResult->gpio]->gpioInterrupt(gpioResult->value);
    }
}

// True if a GPIO is wired to the camera (DVP data/clock/I2C) or the SD card on this board. Driving
// such a pin as an LED reconfigures it away from the camera/SD, which breaks image capture (the
// camera-failure watchdog then reboots the device) - so we refuse to use these for flash/LED output.
static bool isReservedCameraSdPin(gpio_num_t g)
{
    if (g == GPIO_NUM_NC) return false;
    static const gpio_num_t reserved[] = {
#ifdef CAM_PIN_XCLK
        CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC,
        CAM_PIN_D0, CAM_PIN_D1, CAM_PIN_D2, CAM_PIN_D3,
        CAM_PIN_D4, CAM_PIN_D5, CAM_PIN_D6, CAM_PIN_D7,
        CAM_PIN_VSYNC, CAM_PIN_HREF, CAM_PIN_PCLK,
#endif
#ifdef GPIO_SDCARD_CLK
        GPIO_SDCARD_CLK, GPIO_SDCARD_CMD, GPIO_SDCARD_D0,
        // SD data lines D1-D3 are only wired in 4-bit SDMMC mode (GPIO_NUM_NC otherwise, and skipped by
        // the GPIO_NUM_NC check below). Reserve them so an LED can't be placed on e.g. GPIO 4/12/13 and
        // silently break the SD card - matching the LEDPin error message's "never 4/12/13" warning.
        GPIO_SDCARD_D1, GPIO_SDCARD_D2, GPIO_SDCARD_D3,
#endif
    };
    for (unsigned i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        if (reserved[i] != GPIO_NUM_NC && reserved[i] == g) return true;
    }
    return false;
}

// Parse an int from a config value, tolerating a malformed/empty string. The firmware is built with
// -fno-exceptions, so std::stoi() aborts the whole device on bad input (a config-induced brick on every
// boot). Parse defensively with strtol so a hand-edited/corrupted value degrades to the default instead.
static int cfgStoi(const std::string &s, int def)
{
    if (s.empty()) return def;
    char *end = nullptr;
    errno = 0;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || errno == ERANGE) return def;   // no digits parsed / out of range
    return (int)v;
}

bool GpioHandler::readConfig()
{
    if (!gpioMap->empty())
        clear();

    ConfigFile configFile = ConfigFile(_configFile); 

    std::vector<std::string> splitted;
    std::string line = "";
    bool disabledLine = false;
    bool eof = false;
    gpio_num_t gpioExtLED = (gpio_num_t) 0;
    
//    ESP_LOGD(TAG, "readConfig - Start 1");
        
    while ((!configFile.GetNextParagraph(line, disabledLine, eof) || (line.compare("[GPIO]") != 0)) && !eof) {}
    if (eof)
        return false;

//    ESP_LOGD(TAG, "readConfig - Start 2 line: %s, disabbledLine: %d", line.c_str(), (int) disabledLine);


    _isEnabled = !disabledLine;

    if (!_isEnabled)
        return false;

//    ESP_LOGD(TAG, "readConfig - Start 3");

#ifdef ENABLE_MQTT
//    std::string mainTopicMQTT = "";
    std::string mainTopicMQTT = mqttServer_getMainTopic();
    if (mainTopicMQTT.length() > 0)
    {
        mainTopicMQTT = mainTopicMQTT + "/GPIO";
        ESP_LOGD(TAG, "MAINTOPICMQTT found");
    }
#endif // ENABLE_MQTT
    bool registerISR = false;
    while (configFile.getNextLine(&line, disabledLine, eof) && !configFile.isNewParagraph(line))
    {
        splitted = ZerlegeZeile(line);
        // const std::regex pieces_regex("IO([0-9]{1,2})");
        // std::smatch pieces_match;
        // if (std::regex_match(splitted[0], pieces_match, pieces_regex) && (pieces_match.size() == 2))
        // {
        //     std::string gpioStr = pieces_match[1];
        ESP_LOGD(TAG, "conf param %s", toUpper(splitted[0]).c_str());
        if (toUpper(splitted[0]) == "MAINTOPICMQTT") {
//            ESP_LOGD(TAG, "MAINTOPICMQTT found");
//            mainTopicMQTT = splitted[1];
        } else if ((splitted[0].rfind("IO", 0) == 0) && (splitted.size() >= 6))
        {
            ESP_LOGI(TAG,"Enable GP%s in %s mode", splitted[0].c_str(), splitted[1].c_str());
            std::string gpioStr = splitted[0].substr(2, 2);
            gpio_num_t gpioNr = (gpio_num_t)atoi(gpioStr.c_str());
            gpio_pin_mode_t pinMode = resolvePinMode(toLower(splitted[1]));
            gpio_int_type_t intType = resolveIntType(toLower(splitted[2]));
            uint16_t dutyResolution = (uint8_t)atoi(splitted[3].c_str());
#ifdef ENABLE_MQTT 
            bool mqttEnabled = (toLower(splitted[4]) == "true");
#endif // ENABLE_MQTT
            bool httpEnabled = (toLower(splitted[5]) == "true");
            char gpioName[100];
            if (splitted.size() >= 7) {
                strcpy(gpioName, trim(splitted[6]).c_str());
            } else {
                sprintf(gpioName, "GPIO%d", gpioNr);
            }
#ifdef ENABLE_MQTT            
            std::string mqttTopic = mqttEnabled ? (mainTopicMQTT + "/" + gpioName) : "";
#else // ENABLE_MQTT
            std::string mqttTopic = "";
#endif // ENABLE_MQTT
            // Safety: an LED/flash pin must not be a camera or SD pin, or driving it breaks capture
            // and the device crash-loops. Refuse and warn instead of bricking the device.
            if ((pinMode == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X || pinMode == GPIO_PIN_MODE_BUILT_IN_FLASH_LED)
                && isReservedCameraSdPin(gpioNr))
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "GPIO" + std::to_string((int)gpioNr) +
                    " is a camera/SD pin and cannot be used for an LED/flash - skipping it (driving it "
                    "would break the camera and reboot the device). Move the LED to a free GPIO.");
                continue;
            }

            GpioPin* gpioPin = new GpioPin(gpioNr, gpioName, pinMode, intType,dutyResolution, mqttTopic, httpEnabled);
            (*gpioMap)[gpioNr] = gpioPin;

            if (pinMode == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X)
            {
                ESP_LOGD(TAG, "Set WS2812 to GPIO %d", gpioNr);
                gpioExtLED = gpioNr;
            }

            if (intType != GPIO_INTR_DISABLE) {
                registerISR = true;
            }
        }
        if (toUpper(splitted[0]) == "LEDNUMBERS")
        {
            LEDNumbers = cfgStoi(splitted[1], LEDNumbers);
        }
        // External WS281x data pin (the simple way to place the strip on any free GPIO, instead of an
        // IOxx=external-flash-ws281x line). Set up after the loop so it can see whether an IOxx already
        // claimed the WS281x role.
        if (toUpper(splitted[0]) == "LEDPIN" && splitted.size() > 1)
        {
            ledPin = cfgStoi(splitted[1], ledPin);
        }
        if (toUpper(splitted[0]) == "LEDCOLOR" && splitted.size() > 3)
        {
            uint8_t _r, _g, _b;
            _r = (uint8_t)cfgStoi(splitted[1], 0);
            _g = (uint8_t)cfgStoi(splitted[2], 0);
            _b = (uint8_t)cfgStoi(splitted[3], 0);

            LEDColor = Rgb{_r, _g, _b};
        }
        if (toUpper(splitted[0]) == "LEDTYPE")
        {
            if (splitted[1] == "WS2812")
                LEDType = LED_WS2812;
            if (splitted[1] == "WS2812B")
                LEDType = LED_WS2812B;
            if (splitted[1] == "SK6812")
                LEDType = LED_SK6812;
            if (splitted[1] == "WS2813")
                LEDType = LED_WS2813;
        }
        // External LED master enable/disable.
        if (toUpper(splitted[0]) == "EXTERNALLED" && splitted.size() > 1)
        {
            externalLedEnabled = alphanumericToBoolean(splitted[1]);
        }
        // External LED output % (0-100), clamped to the 5V current budget below.
        if (toUpper(splitted[0]) == "LEDBRIGHTNESS" && splitted.size() > 1)
        {
            externalLedBrightnessPct = std::min(std::max(cfgStoi(splitted[1], externalLedBrightnessPct), 0), 100);
        }
        // External 5V power injection: when on, use LEDMaxCurrent as the budget instead of the board default.
        if (toUpper(splitted[0]) == "LEDPOWERINJECTION" && splitted.size() > 1)
        {
            externalPowerInjection = alphanumericToBoolean(splitted[1]);
        }
        // Injected supply current budget in mA (only honoured when LEDPowerInjection is true).
        if (toUpper(splitted[0]) == "LEDMAXCURRENT" && splitted.size() > 1)
        {
            externalMaxCurrentMa = std::max(cfgStoi(splitted[1], externalMaxCurrentMa), 0);
        }
        // Onboard status RGB (S3 GPIO48) on/off.
        if (toUpper(splitted[0]) == "ONBOARDLED" && splitted.size() > 1)
        {
            onboardLedEnabled = alphanumericToBoolean(splitted[1]);
        }
        // ---- Status LED: per-stage colours (show current processing step) ----
        if (toUpper(splitted[0]) == "STATUSLED" && splitted.size() > 1)
        {
            statusLedEnabled = alphanumericToBoolean(splitted[1]);
        }
        {
            // map "StatusLEDColor<Stage>" key -> ProcessingStage index
            static const struct { const char* key; int stage; } _stageKeys[] = {
                { "STATUSLEDIDLE",      PROC_STAGE_IDLE },
                { "STATUSLEDTAKEIMAGE", PROC_STAGE_TAKEIMAGE },
                { "STATUSLEDALIGN",     PROC_STAGE_ALIGN },
                { "STATUSLEDDIGITIZE",  PROC_STAGE_DIGITIZE },
                { "STATUSLEDPOSTPROC",  PROC_STAGE_POSTPROC },
                { "STATUSLEDTRANSMIT",  PROC_STAGE_TRANSMIT },
                { "STATUSLEDERROR",     PROC_STAGE_ERROR },
            };
            std::string _k = toUpper(splitted[0]);
            for (unsigned _i = 0; _i < sizeof(_stageKeys)/sizeof(_stageKeys[0]); ++_i) {
                if (_k == _stageKeys[_i].key && splitted.size() >= 4) {
                    statusLedColors[_stageKeys[_i].stage] = Rgb{ (uint8_t)cfgStoi(splitted[1], 0),
                                                                 (uint8_t)cfgStoi(splitted[2], 0),
                                                                 (uint8_t)cfgStoi(splitted[3], 0) };
                    break;
                }
            }
        }
    }

    // LEDPin: configure the external WS281x strip on the given GPIO, unless an IOxx line already
    // claimed the WS281x role (gpioExtLED set). Reject camera/SD pins (would break capture + reboot).
    if (ledPin > 0 && gpioExtLED == 0 && externalLedEnabled)
    {
        if (isReservedCameraSdPin((gpio_num_t)ledPin)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LEDPin " + std::to_string(ledPin) +
                " is a camera/SD pin and cannot drive the LED - ignored. Pick a free GPIO "
                "(ESP32-S3: e.g. 21 or 47; never 4/12/13).");
        } else {
            GpioPin* ledGpio = new GpioPin((gpio_num_t)ledPin, "ExternalLED",
                GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X, GPIO_INTR_DISABLE, 10, "", false);
            (*gpioMap)[(gpio_num_t)ledPin] = ledGpio;
            gpioExtLED = (gpio_num_t)ledPin;
            ESP_LOGI(TAG, "External WS281x strip configured on GPIO%d via LEDPin", ledPin);
        }
    }

    if (registerISR) {
        //install gpio isr service
        gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM);
    }

    setOnboardLedEnabled(onboardLedEnabled);   // apply the on/off toggle for the onboard RGB (S3)

    if (gpioExtLED > 0)
    {
    //     LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Startsequence 06");      // Nremove
//        vTaskDelay( xDelay );   
//        xDelay = 5000 / portTICK_PERIOD_MS;
//        ESP_LOGD(TAG, "main: sleep for: %ldms", (long) xDelay);

//        SmartLed leds( LED_WS2812, 2, GPIO_NUM_12, 0, DoubleBuffer );


//        leds[ 0 ] = Rgb{ 255, 0, 0 };
//        leds[ 1 ] = Rgb{ 255, 255, 255 };
//        leds.show();    
//        SmartLed leds = new SmartLed(LEDType, LEDNumbers, gpioExtLED, 0, DoubleBuffer);
//        _SmartLED = new SmartLed( LED_WS2812, 2, GPIO_NUM_12, 0, DoubleBuffer );
    }

    return true;
}

void GpioHandler::clear() 
{
    ESP_LOGD(TAG, "GpioHandler::clear");

    if (gpioMap != NULL) {
        for(std::map<gpio_num_t, GpioPin*>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it) {
            delete it->second;
        }
        gpioMap->clear();
    }

    // gpio_uninstall_isr_service(); can't uninstall, isr service is used by camera
}
 
void GpioHandler::registerGpioUri() 
{
    ESP_LOGI(TAG, "server_GPIO - Registering URI handlers");
    
    httpd_uri_t camuri = { };
    camuri.method    = HTTP_GET;
    camuri.uri       = "/GPIO";
    camuri.handler   = APPLY_BASIC_AUTH_FILTER(callHandleHttpRequest);
    camuri.user_ctx  = (void*)this;
    httpd_register_uri_handler(_httpServer, &camuri);

    // Live external-LED brightness: GET /ledbrightness?value=0..100 - applies immediately (no reboot)
    // so the config page can preview the strip dimming as the slider moves.
    httpd_uri_t leduri = { };
    leduri.method    = HTTP_GET;
    leduri.uri       = "/ledbrightness";
    leduri.handler   = APPLY_BASIC_AUTH_FILTER(callHandleLedBrightness);
    leduri.user_ctx  = (void*)this;
    httpd_register_uri_handler(_httpServer, &leduri);
}

esp_err_t GpioHandler::handleHttpRequest(httpd_req_t *req)
{
    ESP_LOGD(TAG, "handleHttpRequest");

    if (gpioMap == NULL) {
        std::string resp_str = "GPIO handler not initialized";
        httpd_resp_send(req, resp_str.c_str(), resp_str.length());    
        return ESP_OK;
    }

#ifdef DEBUG_DETAIL_ON 
    LogFile.WriteHeapInfo("handler_switch_GPIO - Start");    
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handler_switch_GPIO");
    char _query[200];
    char _valueGPIO[30];    
    char _valueStatus[30];    
    std::string gpio, status;

    if (httpd_req_get_url_query_str(req, _query, 200) == ESP_OK) {
        ESP_LOGD(TAG, "Query: %s", _query);
        
        if (httpd_query_key_value(_query, "GPIO", _valueGPIO, 30) == ESP_OK)
        {
            ESP_LOGD(TAG, "GPIO is found %s", _valueGPIO); 
            gpio = std::string(_valueGPIO);
        } else {
            std::string resp_str = "GPIO No is not defined";
            httpd_resp_send(req, resp_str.c_str(), resp_str.length());    
            return ESP_OK;
        }
        if (httpd_query_key_value(_query, "Status", _valueStatus, 30) == ESP_OK)
        {
            ESP_LOGD(TAG, "Status is found %s", _valueStatus); 
            status = std::string(_valueStatus);
        }
    } else {
        const char* resp_str = "Error in call. Use /GPIO?GPIO=12&Status=high";
        httpd_resp_send(req, resp_str, strlen(resp_str));    
        return ESP_OK;
    }

    status = toUpper(status);
    if ((status != "HIGH") && (status != "LOW") && (status != "TRUE") && (status != "FALSE") && (status != "0") && (status != "1") && (status != ""))
    {
        std::string zw = "Status not valid: " + status;
        httpd_resp_sendstr_chunk(req, zw.c_str());
        httpd_resp_sendstr_chunk(req, NULL);          
        return ESP_OK;    
    }

    int gpionum = stoi(gpio);

    // free: 16; 12-15; 2; 4  // only 12 and 13 work. 2: reboot, 4: flash LED, 15: PSRAM, 14/15: DMA for SD card ???
    gpio_num_t gpio_num = resolvePinNr(gpionum);
    if (gpio_num == GPIO_NUM_NC)
    {
        std::string zw = "GPIO" + std::to_string(gpionum) + " unsupported - only 12 & 13 free";
            httpd_resp_sendstr_chunk(req, zw.c_str());
            httpd_resp_sendstr_chunk(req, NULL);          
            return ESP_OK;
    }

    if (gpioMap->count(gpio_num) == 0) {
        char resp_str [30];
        sprintf(resp_str, "GPIO%d is not registred", gpio_num);
        httpd_resp_send(req, resp_str, strlen(resp_str));  
        return ESP_OK;     
    }
    
    if (status == "") 
    {
        std::string resp_str = "";
        status = (*gpioMap)[gpio_num]->getValue(&resp_str) ? "HIGH" : "LOW";
        if (resp_str == "") {
            resp_str = status;
        }
        httpd_resp_sendstr_chunk(req, resp_str.c_str());
        httpd_resp_sendstr_chunk(req, NULL);
    }
    else
    {
        std::string resp_str = "";
        (*gpioMap)[gpio_num]->setValue((status == "HIGH") || (status == "TRUE") || (status == "1"), GPIO_SET_SOURCE_HTTP, &resp_str);
        if (resp_str == "") {
            resp_str = "GPIO" + std::to_string(gpionum) + " switched to " + status;
        }
        httpd_resp_sendstr_chunk(req, resp_str.c_str());
        httpd_resp_sendstr_chunk(req, NULL);
    }
          
    return ESP_OK;    
};

// Meaningful default colours per processing stage (moderate brightness to avoid glare).
void GpioHandler::initStatusLedDefaults()
{
    statusLedColors[PROC_STAGE_IDLE]      = Rgb{   0,  10,   0 };   // dim green  : idle / ready
    statusLedColors[PROC_STAGE_TAKEIMAGE] = Rgb{   0,   0,  80 };   // blue       : capturing image
    statusLedColors[PROC_STAGE_ALIGN]     = Rgb{  90,  30,   0 };   // orange     : aligning
    statusLedColors[PROC_STAGE_DIGITIZE]  = Rgb{  80,  80,   0 };   // yellow     : digit/analog CNN
    statusLedColors[PROC_STAGE_POSTPROC]  = Rgb{  60,   0,  80 };   // purple     : post-processing
    statusLedColors[PROC_STAGE_TRANSMIT]  = Rgb{   0,  80,  80 };   // cyan       : sending
    statusLedColors[PROC_STAGE_ERROR]     = Rgb{ 120,   0,   0 };   // red        : error / retry
}

// Apply the external-LED output % (optional) and clamp the colour so the whole WS281x chain's
// estimated 5V draw stays under the active budget. The estimate uses LED_MA_PER_CHANNEL_FULL mA per
// colour channel at value 255 (~60 mA/pixel at full white). Budget = the user's injected-supply limit
// when external power injection is enabled, else the board default EXTERNAL_LED_5V_BUDGET_MA. This is
// the single guard that keeps a long/bright chain from browning out the board.
Rgb GpioHandler::scaleExtLedColor(Rgb base, bool applyBrightness)
{
    float r = base.r, g = base.g, b = base.b;

    if (applyBrightness) {
        // Gamma ~2 so the percentage tracks *perceived* brightness. WS2812 light output is very
        // non-linear: 20% linear PWM still looks bright, which makes a linear scale feel like it
        // "does nothing". Squaring maps e.g. 20% -> 4% of full, 50% -> 25%, 100% -> full.
        float p = (float)externalLedBrightnessPct * externalLedBrightnessPct / 10000.0f;
        r *= p; g *= p; b *= p;
    }

    int budget = externalPowerInjection ? externalMaxCurrentMa : EXTERNAL_LED_5V_BUDGET_MA;
    // A zero/blank injected budget (LEDPowerInjection on but LEDMaxCurrent 0) must not silently disable
    // the clamp - fall back to the board default so the chain is always current-limited.
    if (budget <= 0) budget = EXTERNAL_LED_5V_BUDGET_MA;
    float sumChannels = r + g + b;
    // Log only on the rising edge so a static bright config doesn't spam the log every round.
    static bool wasClamped = false;
    if (LEDNumbers > 0 && sumChannels > 0.0f && budget > 0) {
        float estimate_mA = (float)LEDNumbers * sumChannels * LED_MA_PER_CHANNEL_FULL / 255.0f;
        if (estimate_mA > (float)budget) {
            float factor = (float)budget / estimate_mA;
            r *= factor; g *= factor; b *= factor;
            if (!wasClamped) {
                LogFile.WriteToFile(ESP_LOG_INFO, TAG,
                    "External LED draw ~" + std::to_string((int)estimate_mA) + " mA exceeds the " +
                    std::to_string(budget) + " mA budget" +
                    (externalPowerInjection ? " (injected supply)" : " (board default)") +
                    "; dimming to fit. Reduce LED count/brightness or enable power injection.");
                wasClamped = true;
            }
        } else {
            wasClamped = false;
        }
    }

    return Rgb{ (uint8_t)(r + 0.5f), (uint8_t)(g + 0.5f), (uint8_t)(b + 0.5f) };
}

// Low-level: write a single colour to all pixels of the configured WS281x LED.
void GpioHandler::driveWs281x(Rgb color)
{
    if (gpioMap == NULL || !externalLedEnabled) {
        return;
    }
    lastStripRaw = color;   // remember the unscaled colour so a live brightness change can re-draw it
    // The external LED brightness % is a master dimmer for the whole strip, so apply it to the
    // status-stage colours too (not just the capture flash) - and clamp to the 5V budget.
    color = scaleExtLedColor(color, /*applyBrightness=*/true);
    for (std::map<gpio_num_t, GpioPin*>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it) {
        if (it->second->getMode() != GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X) {
            continue;
        }
#ifdef __LEDGLOBAL
        if (leds_global == NULL) {
            leds_global = new SmartLed(LEDType, LEDNumbers, it->second->getGPIO(), 0, DoubleBuffer);
        } else {
            leds_global->wait();   // see SmartLeds issue #10
        }
        for (int i = 0; i < LEDNumbers; ++i) {
            (*leds_global)[i] = color;
        }
        leds_global->show();
#else
        SmartLed leds(LEDType, LEDNumbers, it->second->getGPIO(), 0, DoubleBuffer);
        for (int i = 0; i < LEDNumbers; ++i) {
            leds[i] = color;
        }
        leds.show();
#endif
        return;   // only one WS281x LED chain is supported
    }
}

// Show the colour for the given ProcessingStage (no-op if status LED disabled / not configured).
void GpioHandler::setStatusStageLED(int stage)
{
    if (!statusLedEnabled) {
        return;
    }
    if (stage < 0 || stage >= PROC_STAGE_COUNT) {
        return;
    }
    driveWs281x(statusLedColors[stage]);
}

// The onboard WS2812 (S3) is driven only through driveSystemStatusWs281x() - it is NOT a configurable
// GPIO pin, so flashLightEnable()/driveWs281x() never touch it. Its brightness is intentionally NOT
// tied to the "External LED brightness" % (that setting is the external strip's; coupling them would
// blank the Wi-Fi/AP status feedback when a user dims an unused strip to 0). The OnboardLED on/off
// toggle is the only control here; when on it shows at full brightness.
static uint8_t s_sysLedRaw[3] = { 0, 0, 0 };   // last colour requested (for re-drive)
static bool s_onboardLedEnabled = true;        // user toggle for the onboard status RGB (S3 GPIO48)

// Standalone WS2812 (RGB) write, independent of the GpioHandler instance/config - used to signal
// Wi-Fi status during boot and AP mode, before the GPIO handler is initialised (it never is in AP
// mode). Transient SmartLed so the RMT channel is freed immediately, leaving it for the handler's
// processing-stage LED once it comes up. Gated to the S3 (its onboard RGB is a WS2812 on FLASH_GPIO);
// a no-op elsewhere so it never pulses the ESP32-CAM's plain flash LED.
void driveSystemStatusWs281x(uint8_t r, uint8_t g, uint8_t b)
{
    s_sysLedRaw[0] = r; s_sysLedRaw[1] = g; s_sysLedRaw[2] = b;
#if defined(BOARD_ESP32S3_CAM) && defined(FLASH_GPIO)
    if (!s_onboardLedEnabled) { r = g = b = 0; }   // user disabled the onboard LED -> keep it dark
    SmartLed leds(LED_WS2812, 1, (int)FLASH_GPIO, 0, DoubleBuffer);
    leds[0] = Rgb{ r, g, b };
    leds.show();
    leds.wait();
#else
    (void)r; (void)g; (void)b;
#endif
}

// Enable/disable the onboard status RGB (S3 GPIO48) and re-draw immediately (off, or the last colour).
void setOnboardLedEnabled(bool enabled)
{
    s_onboardLedEnabled = enabled;
    driveSystemStatusWs281x(s_sysLedRaw[0], s_sysLedRaw[1], s_sysLedRaw[2]);
}

// Free-function trampoline registered with jomjol_helper so the flow can signal stages
// without jomjol_controlGPIO <-> jomjol_flowcontroll forming a circular dependency.
static void statusLedStageTrampoline(int stage)
{
    GpioHandler* h = gpio_handler_get();
    if (h != NULL) {
        h->setStatusStageLED(stage);
    }
}

void GpioHandler::flashLightEnable(bool value)
{
    ESP_LOGD(TAG, "GpioHandler::flashLightEnable %s", value ? "true" : "false");

    if (gpioMap != NULL) {
        for(std::map<gpio_num_t, GpioPin*>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it) 
        {
            if (it->second->getMode() == GPIO_PIN_MODE_BUILT_IN_FLASH_LED) //|| (it->second->getMode() == GPIO_PIN_MODE_EXTERNAL_FLASH_PWM) || (it->second->getMode() == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X))
            {
                std::string resp_str = "";
                it->second->setValue(value, GPIO_SET_SOURCE_INTERNAL, &resp_str);

                if (resp_str == "") {
                    ESP_LOGD(TAG, "Flash light pin GPIO %d switched to %s", (int)it->first, (value ? "on" : "off"));
                } else {
                    ESP_LOGE(TAG, "Can't set flash light pin GPIO %d.  Error: %s", (int)it->first, resp_str.c_str());
                }
            } else 
                {
                    if (it->second->getMode() == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X && externalLedEnabled)
                    {
#ifdef __LEDGLOBAL
                        if (leds_global == NULL) {
                            ESP_LOGI(TAG, "init SmartLed: LEDNumber=%d, GPIO=%d", LEDNumbers, (int)it->second->getGPIO());
                            leds_global = new SmartLed( LEDType, LEDNumbers, it->second->getGPIO(), 0, DoubleBuffer );
                        } else {
                            // wait until we can update: https://github.com/RoboticsBrno/SmartLeds/issues/10#issuecomment-386921623
                            leds_global->wait();
                        }
#else
                        SmartLed leds( LEDType, LEDNumbers, it->second->getGPIO(), 0, DoubleBuffer );
#endif
  
                        lastStripRaw = value ? LEDColor : Rgb{0, 0, 0};   // for live brightness re-draw
                        if (value)
                        {
                            // Apply the external output % and clamp to the 5V current budget.
                            Rgb flashColor = scaleExtLedColor(LEDColor, /*applyBrightness=*/true);
                            for (int i = 0; i < LEDNumbers; ++i)
#ifdef __LEDGLOBAL
                                (*leds_global)[i] = flashColor;
#else
                                leds[i] = flashColor;
#endif
                        }
                        else
                        {
                            for (int i = 0; i < LEDNumbers; ++i)
#ifdef __LEDGLOBAL
                                (*leds_global)[i] = Rgb{0, 0, 0};
#else
                                leds[i] = Rgb{0, 0, 0};
#endif
                        }
#ifdef __LEDGLOBAL
                        leds_global->show();
#else
                        leds.show();
#endif
                    }
                }
        }
    }
}

// Apply the external-LED brightness % immediately, without a reboot/config reload, and re-draw the
// configurable strip so the change is visible right away.
void GpioHandler::setExternalLedBrightnessLive(int pct)
{
    externalLedBrightnessPct = (pct < 0) ? 0 : (pct > 100 ? 100 : pct);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "External LED brightness set live to " +
                        std::to_string(externalLedBrightnessPct) + "%");
    // Re-draw the strip ONLY if it is currently lit, so changing the brightness can't switch a dark
    // strip on (it would then stay on between rounds). lastStripRaw is the strip's current colour.
    if (lastStripRaw.r || lastStripRaw.g || lastStripRaw.b) {
        driveWs281x(lastStripRaw);
    }
}

esp_err_t GpioHandler::handleLedBrightnessRequest(httpd_req_t *req)
{
    char query[64];
    char val[8];
    int pct = -1;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "value", val, sizeof(val)) == ESP_OK) {
        pct = atoi(val);
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (pct < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/invalid 'value' (0..100)");
        return ESP_OK;
    }
    setExternalLedBrightnessLive(pct);
    std::string resp = "external LED brightness = " + std::to_string(externalLedBrightnessPct) + "%";
    httpd_resp_send(req, resp.c_str(), resp.length());
    return ESP_OK;
}

gpio_num_t GpioHandler::resolvePinNr(uint8_t pinNr)
{
    switch(pinNr)  {
        case 0:
            return GPIO_NUM_0;
        case 1:
            return GPIO_NUM_1;
        case 3:
            return GPIO_NUM_3;
        case 4:
            return GPIO_NUM_4;
        case 12:
            return GPIO_NUM_12;
        case 13:
            return GPIO_NUM_13;
        default: 
            return GPIO_NUM_NC;   
    }
}

gpio_pin_mode_t GpioHandler::resolvePinMode(std::string input) 
{
    if( input == "disabled" ) return GPIO_PIN_MODE_DISABLED;
    if( input == "input" ) return GPIO_PIN_MODE_INPUT;
    if( input == "input-pullup" ) return GPIO_PIN_MODE_INPUT_PULLUP;
    if( input == "input-pulldown" ) return GPIO_PIN_MODE_INPUT_PULLDOWN;
    if( input == "output" ) return GPIO_PIN_MODE_OUTPUT;
    if( input == "built-in-led" ) return GPIO_PIN_MODE_BUILT_IN_FLASH_LED;
    if( input == "output-pwm" ) return GPIO_PIN_MODE_OUTPUT_PWM;
    if( input == "external-flash-pwm" ) return GPIO_PIN_MODE_EXTERNAL_FLASH_PWM;
    if( input == "external-flash-ws281x" ) return GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X;

    return GPIO_PIN_MODE_DISABLED;
}

gpio_int_type_t GpioHandler::resolveIntType(std::string input) 
{
    if( input == "disabled" ) return GPIO_INTR_DISABLE;
    if( input == "rising-edge" ) return GPIO_INTR_POSEDGE;
    if( input == "falling-edge" ) return GPIO_INTR_NEGEDGE;
    if( input == "rising-and-falling" ) return GPIO_INTR_ANYEDGE ;
    if( input == "low-level-trigger" ) return GPIO_INTR_LOW_LEVEL;
    if( input == "high-level-trigger" ) return GPIO_INTR_HIGH_LEVEL;


    return GPIO_INTR_DISABLE;
}

static GpioHandler *gpioHandler = NULL;

void gpio_handler_create(httpd_handle_t server) 
{
    if (gpioHandler == NULL)
        gpioHandler = new GpioHandler(CONFIG_FILE, server);
}

void gpio_handler_init() 
{
    if (gpioHandler != NULL) {
        gpioHandler->init();
    }
}

void gpio_handler_deinit() {
    if (gpioHandler != NULL) {
        gpioHandler->deinit();
   }
}

void gpio_handler_destroy()
{
    if (gpioHandler != NULL) {
        gpio_handler_deinit();
        delete gpioHandler;
        gpioHandler = NULL;
    }
}

GpioHandler* gpio_handler_get()
{
    return gpioHandler;
}

