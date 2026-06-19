# Parameter `CPUFrequency`
Default Value: `160`

Set the CPU Frequency (MHz). The options are the speeds the ESP-IDF allows for these chips (ESP32 /
ESP32-S3): **80**, **160**, **240**.

!!! Warning
    Setting it to 240 will lead to a faster device, but it will also require a stronger power supply!
    Additionally, depending on the quality of your ESP32-CAM, it might run unstable! Setting it to 80
    saves power but is slower (camera capture and CNN inference take longer).

Possible values:

- 80
- 160
- 240
