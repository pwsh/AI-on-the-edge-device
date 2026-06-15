#pragma once

#ifndef SERVER_GPIO_H
#define SERVER_GPIO_H

#include <esp_log.h>

#include <esp_http_server.h>
#include <map>
#include "driver/gpio.h"

#include "SmartLeds.h"

#include "../../include/defines.h"   // EXTERNAL_LED_5V_BUDGET_MA (external LED current budget)

typedef enum {
    GPIO_PIN_MODE_DISABLED              = 0x0,
    GPIO_PIN_MODE_INPUT                 = 0x1,
    GPIO_PIN_MODE_INPUT_PULLUP          = 0x2,
    GPIO_PIN_MODE_INPUT_PULLDOWN        = 0x3,
    GPIO_PIN_MODE_OUTPUT                = 0x4,
    GPIO_PIN_MODE_BUILT_IN_FLASH_LED    = 0x5,
    GPIO_PIN_MODE_OUTPUT_PWM            = 0x6,
    GPIO_PIN_MODE_EXTERNAL_FLASH_PWM    = 0x7,
    GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X = 0x8,
} gpio_pin_mode_t;

struct GpioResult {
    gpio_num_t gpio;
    int value;
};

typedef enum {
    GPIO_SET_SOURCE_INTERNAL  = 0,
    GPIO_SET_SOURCE_MQTT  = 1,
    GPIO_SET_SOURCE_HTTP  = 2,
} gpio_set_source;

class GpioPin {
public:
    GpioPin(gpio_num_t gpio, const char* name, gpio_pin_mode_t mode, gpio_int_type_t interruptType, uint8_t dutyResolution, std::string mqttTopic, bool httpEnable);
    ~GpioPin();

    void init();
    bool getValue(std::string* errorText);
    void setValue(bool value, gpio_set_source setSource, std::string* errorText);
#ifdef ENABLE_MQTT
    bool handleMQTT(std::string, char* data, int data_len);
#endif //ENABLE_MQTT
    void publishState();
    void gpioInterrupt(int value);
    gpio_int_type_t getInterruptType() { return _interruptType; }
    gpio_pin_mode_t getMode() { return _mode; }
    gpio_num_t getGPIO(){return _gpio;};

private:
    gpio_num_t _gpio;
    const char* _name;
    gpio_pin_mode_t _mode;
    gpio_int_type_t _interruptType;
    std::string _mqttTopic;
    int currentState = -1;
};

esp_err_t callHandleHttpRequest(httpd_req_t *req);
esp_err_t callHandleLedBrightness(httpd_req_t *req);
void taskGpioHandler(void *pvParameter);

// Drive the onboard WS2812 (RGB) LED to a single colour for system/Wi-Fi status (S3 only; no-op
// elsewhere). Safe to call before the GpioHandler is initialised (e.g. during Wi-Fi connect / AP).
void driveSystemStatusWs281x(uint8_t r, uint8_t g, uint8_t b);
void setSystemStatusLedBrightness(int pct);   // dim the onboard status WS2812 (S3) by the external-LED %
void setOnboardLedEnabled(bool enabled);      // enable/disable the onboard status WS2812 (S3 GPIO48)

class GpioHandler {
public:
    GpioHandler(std::string configFile, httpd_handle_t httpServer);
    ~GpioHandler();
    
    void init();
    void deinit();
    void registerGpioUri();
    esp_err_t handleHttpRequest(httpd_req_t *req);
    void taskHandler();
    void gpioInterrupt(GpioResult* gpioResult);  
    void flashLightEnable(bool value);
    void setStatusStageLED(int stage);   // drive the WS281x status LED for a ProcessingStage
    void setFlashLEDColor(uint8_t r, uint8_t g, uint8_t b) { LEDColor = Rgb{r, g, b}; }   // runtime flash colour (e.g. live camera-setup stream)
    void setExternalLedBrightnessLive(int pct);   // apply external-LED brightness % now (no reboot) + re-draw
    esp_err_t handleLedBrightnessRequest(httpd_req_t *req);   // GET /ledbrightness?value=N
    bool isEnabled() { return _isEnabled; }
#ifdef ENABLE_MQTT
    void handleMQTTconnect();
#endif //ENABLE_MQTT

private:
    std::string _configFile;
    httpd_handle_t _httpServer;
    std::map<gpio_num_t, GpioPin*> *gpioMap = NULL;
    TaskHandle_t xHandleTaskGpio = NULL;
    bool _isEnabled = false;

    int LEDNumbers = 2;
    int ledPin = 0;   // external WS281x data GPIO (LEDPin); 0 = use IOxx=external-flash-ws281x instead
    Rgb LEDColor = Rgb{ 255, 255, 255 };
    LedType LEDType = LED_WS2812;
#ifdef __LEDGLOBAL
    SmartLed *leds_global = NULL;
#endif

    // External LED 5V current budgeting (see defines.h EXTERNAL_LED_5V_BUDGET_MA).
    bool externalLedEnabled = true;                              // master on/off for the external WS281x strip
    int externalLedBrightnessPct = 100;                          // user output % for the flash colour
    bool externalPowerInjection = false;                         // external 5V injected? (off by default)
    int externalMaxCurrentMa = EXTERNAL_LED_5V_BUDGET_MA;        // injected-supply budget (used only when injection on)
    bool onboardLedEnabled = true;                               // onboard status RGB (S3 GPIO48) on/off
    // Apply brightness % (when applyBrightness) then clamp the colour so the whole chain's estimated
    // 5V draw stays under the active budget (injected supply if enabled, else the board default).
    Rgb scaleExtLedColor(Rgb base, bool applyBrightness);
    Rgb lastStripRaw = Rgb{ 0, 0, 0 };   // last (unscaled) colour written to the WS281x strip, for live re-draw

    // Status LED: show the current processing stage as a colour on the WS281x LED.
    bool statusLedEnabled = false;
    Rgb statusLedColors[8];   // indexed by ProcessingStage (>= PROC_STAGE_COUNT)
    void initStatusLedDefaults();
    void driveWs281x(Rgb color);   // shared low-level WS281x writer

    bool readConfig();
    void clear();
    
    gpio_num_t resolvePinNr(uint8_t pinNr);
    gpio_pin_mode_t resolvePinMode(std::string input);
    gpio_int_type_t resolveIntType(std::string input);
};

void gpio_handler_create(httpd_handle_t server);
void gpio_handler_init();
void gpio_handler_deinit();
void gpio_handler_destroy();
GpioHandler* gpio_handler_get();



#endif //SERVER_GPIO_H

