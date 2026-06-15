# Parameter `LEDPin`
Default Value: board-specific - ESP32-S3 `21`, ESP32-CAM `12`

The GPIO the **external** WS281x (NeoPixel) strip's data line is connected to. Set it to the pin your
strip is wired to; `0` disables the strip.

**Pick a free GPIO — never a camera or SD pin** (the firmware refuses those and logs an error, because
driving a camera pin breaks image capture and reboots the device):

* **ESP32-S3 CAM**: good choices are **GPIO21** (default) or **GPIO47**. **Never use 4, 12 or 13**
  (camera I2C / data / pixel-clock).
* **ESP32-CAM (AI-Thinker)**: **GPIO12** is the usual choice (its onboard flash LED is GPIO4).

After changing this you must reboot (or re-apply the configuration) for the new pin to take effect.
Make sure the strip's data wire is physically moved to the chosen GPIO.
