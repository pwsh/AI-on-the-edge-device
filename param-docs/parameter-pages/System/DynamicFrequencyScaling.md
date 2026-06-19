# Parameter `DynamicFrequencyScaling`
Default Value: `false` (disabled)

!!! Warning
    This is an **Expert Parameter** and is **ESP32-S3 only**! On the classic ESP32 / ESP32-CAM it is
    ignored: the 20&nbsp;MHz camera clock (XCLK) can only be sourced from the APB clock, so scaling the
    APB corrupts captures. The ESP32-S3 sources the XCLK from a scaling-independent crystal, so it can
    coexist with the camera.

Dynamic Frequency Scaling (DFS) lets the CPU **down-clock to 80&nbsp;MHz when idle** (between rounds) and
scale back up to the configured [CPUFrequency](CPUFrequency.md) under load, to save power. When disabled,
the frequency is fixed at [CPUFrequency](CPUFrequency.md).

- **Disabled / `false`** (default): fixed frequency — original behaviour.
- **Enabled / `true`**: down-clocks when idle. **Validate on hardware** — DFS scales the APB clock and the
  camera XCLK can drift; check that image quality (and reads) stay good across lighting conditions.

The power saving is modest while Wi-Fi is always on (the radio keeps the system busy).
