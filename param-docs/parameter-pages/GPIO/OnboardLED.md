# Parameter `OnboardLED`
Default Value: `enabled`

Enables or disables the **onboard RGB LED** (the WS2812 on **GPIO48**) of the ESP32-S3 camera board.
When **enabled** it shows the Wi-Fi / setup status (e.g. blue in AP mode, green when connected). Select
**disabled** to keep it dark - useful if the light is distracting or reflects into the camera.

(ESP32-S3 only. On the ESP32-CAM the onboard light is the GPIO4 flash LED, configured separately.)
