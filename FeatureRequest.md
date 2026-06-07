## Feature Requests

**There are a lot of ideas for further improvements, but only limited capacity on the side of the
developer.** This page collects those ideas so they are not forgotten, and so anyone with the time
and passion to help can pick one up.

1. Whoever has a new idea can add it here.
2. Whoever has the time, capacity and passion can take any idea and implement it — support and help
   will be provided wherever possible.

### How each entry is structured

Every request below uses the same layout so the request, its value, and where it can realistically
run are all clear at a glance:

* **Request** — what is being asked for, in one or two sentences.
* **Benefit** — why it is worth doing.
* **Feasibility** — how practical it is on each board family: **ESP32-CAM** (AI-Thinker, 4 MB flash,
  4 MB PSRAM, SD-based), **ESP32-S3** (8/16 MB flash, in-flash filesystem), and **ESP32 WROVER**.
  Most features are board-agnostic software; hardware-bound ones (sleep, serial, heavy compute) call
  out the differences.
* **Status** — current state, using the legend below.

**Status legend:** ✅ Implemented · 🟡 Partially implemented · 🔬 Analysed (feasibility known, not
built) · ⬜ Open

____

#### #40 Trigger with cron-like exact time slot

* **Request:** Schedule readings at exact wall-clock times (cron-style), not just at a fixed interval. See [#2470](https://github.com/jomjol/AI-on-the-edge-device/issues/2470).
* **Benefit:** Align readings with tariff windows or other devices; predictable timestamps.
* **Feasibility:** All boards — feasible (software only). SNTP time is already available; needs a small scheduler and config UI. The interval is now re-read live without a reboot, which is a useful building block.
* **Status:** ⬜ Open (interval scheduling exists; exact cron slots not yet).

#### #39 UPnP/SSDP auto-discovery of the device

* **Request:** Announce the device via UPnP/SSDP so it is auto-discovered on the network. See [#2481](https://github.com/jomjol/AI-on-the-edge-device/issues/2481).
* **Benefit:** Easier first-time discovery without scanning for the IP. mDNS hostname already helps; SSDP would add it to "Network" device lists.
* **Feasibility:** All boards — feasible (software only); small always-on UDP listener adds a little RAM/CPU overhead.
* **Status:** ⬜ Open.

#### #38 Energy saving (deep sleep between recognitions)

* **Request:** Put the device to sleep between readings to cut power. See [#2486](https://github.com/jomjol/AI-on-the-edge-device/issues/2486).
* **Benefit:** Lower average power; enables battery or solar operation.
* **Feasibility (analysed):** **ESP32-CAM — ❌ not feasible** for meaningful savings: the camera XCLK is driven from the APB clock (no XTAL LEDC source on the ESP32), so dynamic-frequency scaling/light-sleep starves the camera, and the OV2640 + Wi-Fi must be fully re-initialised each wake (multi-second, power-hungry). **ESP32-S3 — 🟡 limited:** it has an XTAL LEDC clock source so light-sleep DFS is possible in principle, but the always-on camera and Wi-Fi association dominate the power budget; deep sleep only pays off with long intervals and a full re-init each cycle. See [docs/BOARD-FEATURE-MATRIX.md](docs/BOARD-FEATURE-MATRIX.md). Closely related to **#20**.
* **Status:** 🔬 Analysed — not worthwhile on ESP32-CAM; limited upside on ESP32-S3.

#### #37 Auto-init / format SD card in firmware

* **Request:** Fully handle the SD card (including formatting an empty/new card) from firmware. See [#2488](https://github.com/jomjol/AI-on-the-edge-device/issues/2488).
* **Benefit:** Simpler first-time setup; recover from a blank or corrupt card without a PC.
* **Feasibility:** **ESP32-CAM/WROVER** — feasible and most valuable (SD is mandatory). **ESP32-S3** — less critical: it already runs from an in-flash LittleFS filesystem and needs no SD card at all.
* **Status:** ⬜ Open (S3 in-flash filesystem sidesteps the need on that board).

#### #36 Run demo without a camera

* **Request:** Allow demo/playground mode on boards with no (or a broken) camera, instead of failing with "Cam bad".
* **Benefit:** Try the software on any ESP32, or keep using a board whose camera ribbon has failed.
* **Feasibility:** All boards — feasible (software only); feed the pipeline from stored demo images and bypass the capture step. The v17 capture safety-net (a failed capture now skips the round instead of rebooting) is a useful precursor.
* **Status:** ⬜ Open.

#### #35 Use the model with an image from a smartphone camera

* **Request:** Accept an occasional photo taken with a phone (e.g. weekly) and run it through the same model, rather than reading every few minutes.
* **Benefit:** "Apparent accuracy" (DE: *Scheingenauigkeit*) — usage barely varies week to week, so sparse readings interpolate fine while avoiding 24/7 hardware.
* **Feasibility:** All boards — feasible as an upload endpoint; mostly a UI/flow change (accept an uploaded image, align, infer, publish). Alignment without the fixed mounting is the main challenge.
* **Status:** ⬜ Open.

#### #34 State + ROI for water-leak detection

* **Request:** Add a ROI/state that flags movement between readings; sustained movement within a time window raises a leak alarm. ![example](https://user-images.githubusercontent.com/38385805/207858812-2a6ba41d-1a8c-4fa1-9b6a-53cdd113c106.png)
* **Benefit:** Publish a leak state over MQTT to trigger actions (e.g. close a valve).
* **Feasibility:** All boards — feasible (software only).
* **Status:** ✅ Implemented (v17) — via the meter value rather than a separate movement ROI: **continuous usage** (the value never holds steady between two readings) past a configurable threshold (default 2 h, so lawn watering etc. doesn't false-alarm) flags a potential leak. `Leak Detection` defaults on for water/gas (off for electricity). Exposes `leak` (binary) and `continuous_usage` (seconds) to MQTT, a Home Assistant *moisture* binary sensor + *duration* sensor, InfluxDB v1/v2, and the REST `/json`.

#### #33 Implement the Matter protocol

* **Request:** Expose the device over Matter. See [#1404](https://github.com/jomjol/AI-on-the-edge-device/issues/1404).
* **Benefit:** Native integration with major smart-home ecosystems.
* **Feasibility:** **ESP32-CAM — ❌ impractical:** the Matter SDK (esp-matter) plus this app does not fit in 4 MB flash alongside the camera stack. **ESP32-S3 16 MB — 🟡 possible** in principle, but Matter is a large, RAM-heavy dependency competing with the CNN. High effort.
* **Status:** 🔬 Analysed — only conceivable on ESP32-S3 16 MB; high cost.

#### #32 Correct a misinterpreted value (and collect training data)

* **Request:** Let the user fix a wrongly-read value; save the offending ROIs to a "training data" folder, ideally uploadable with one button.
* **Benefit:** Easy corrections and a feedback loop to improve the models.
* **Feasibility:** All boards — feasible (software only); some SD/flash space for saved ROIs. The v17 confidence-vote logic (a single spurious high read can be overridden) reduces some bad reads but is not the manual-correction UI this asks for. v17 also adds an **Examine selected ROI** tool in the ROI editors that runs the CNN on one region on demand and shows the analysed crop + reading + confidence — a natural building block for a "save this ROI as training data" button.
* **Status:** 🟡 Partially — on-demand single-ROI examination + confidence display exist (v17); the save-to-training-folder + one-click upload loop is still open.

#### #31 InfluxDB v2.x interface

* **Request:** Support InfluxDB v2.x (org/bucket/token), not just v1.x. See [#1160](https://github.com/jomjol/AI-on-the-edge-device/issues/1160).
* **Benefit:** Works with current InfluxDB deployments and InfluxDB Cloud.
* **Feasibility:** All boards — feasible (software only).
* **Status:** ✅ Implemented — `jomjol_influxdb` supports the v2 `/api/v2/write` API with bucket, organization and token; HTTPS via the built-in CA bundle.

#### #30 Support meter "clocking over" (rollover at max value)

* **Request:** When a meter reaches its maximum and wraps to 0, accept the new value and compute the difference correctly (see `ClassFlowPostProcessing.cpp`).
* **Benefit:** No spurious huge negative rate when a mechanical meter rolls over.
* **Feasibility:** All boards — feasible (software only).
* **Status:** 🟡 Partially implemented — rate guards exist (`MaxRateValue`/`MaxRateType`, `AllowNegativeRates`) to bound and accept decreases; a dedicated full-scale wrap-around calculation is still worth hardening.

#### ~~#29 Add favicon and use the hostname for the website~~

* **Status:** ✅ Implemented (v11.3.1). See [#927](https://github.com/jomjol/AI-on-the-edge-device/issues/927).

#### #28 Improved error handling for out-of-image ROIs

* **Request:** When a ROI lies outside the image, show an error instead of silently using a nonsense crop.
* **Benefit:** Catch mis-configuration immediately rather than getting bad reads.
* **Feasibility:** All boards — feasible (software only); validate ROI bounds at config-save and at runtime. The v17 ROI editors add keyboard nudging and clearer guidance, but explicit out-of-bounds errors are still open.
* **Status:** ⬜ Open.

#### #27 Use the Homie convention for MQTT

* **Request:** Publish using the standardized [Homie](https://homieiot.github.io/) MQTT convention.
* **Benefit:** Auto-discovery in Homie-aware controllers; standard topic structure.
* **Feasibility:** All boards — feasible (software only); add a Homie topic/àttribute layer over the existing MQTT client (which already supports Home Assistant discovery and inbound subscriptions).
* **Status:** ⬜ Open.

#### #26 Smarter "N" replacement

* **Request:** When the higher digits have already increased by at least 1, set an undetermined ("N") digit to "0" rather than to its last value. See [#792](https://github.com/jomjol/AI-on-the-edge-device/issues/792).
* **Benefit:** More accurate values right after a carry/rollover of a higher digit.
* **Feasibility:** All boards — feasible (software only); a post-processing rule change.
* **Status:** 🟡 Partially addressed — v17 adds confidence-vote logic so a single spurious high read no longer sticks; the specific carry-aware "N→0" rule is not yet implemented.

#### #25 Trigger a measurement via MQTT

* **Request:** Start a reading on demand by publishing to an MQTT topic. See [#727](https://github.com/jomjol/AI-on-the-edge-device/issues/727).
* **Benefit:** On-demand reads from automations without polling the web API.
* **Feasibility:** All boards — feasible (software only).
* **Status:** ✅ Implemented — the MQTT client subscribes to inbound topics and dispatches via a callback map (`subscribeFunktionMap`, `MQTT_EVENT_DATA`); a publish to the control topic triggers the flow.

#### #24 Show MQTT state in the web server

* **Request:** Display the MQTT connection state / log on the web page (connected, failed to connect, …).
* **Benefit:** Diagnose broker/credential problems from the UI.
* **Feasibility:** All boards — feasible (software only). The overview already surfaces process status, CPU temperature, RSSI and uptime; an MQTT-state line is a small addition.
* **Status:** ⬜ Open.

#### #23 CPU temperature (web + MQTT)

* **Request:** Show the CPU temperature in the web page and publish it over MQTT.
* **Benefit:** Spot thermal problems and log enclosure temperature.
* **Feasibility:** All boards — feasible (software only).
* **Status:** ✅ Implemented — CPU temperature is shown on the overview page and available via the info API.

#### ~~#22 Direct links to the neural-network files in the other repositories~~

* **Status:** ✅ Implemented (> v11.3.1). See [#644](https://github.com/jomjol/AI-on-the-edge-device/issues/644).

#### #21 Extended "CheckDigitalConsistency" logic

* **Request:** Strengthen digit-consistency checking. See [#590](https://github.com/jomjol/AI-on-the-edge-device/issues/590).
* **Benefit:** Fewer implausible jumps accepted as valid readings.
* **Feasibility:** All boards — feasible (software only).
* **Status:** 🟡 Partially improved — v17 adds confidence-vote override for stuck-high reads and changed-digit highlighting on the overview; further consistency rules remain open.

#### #20 Deep sleep + push mode (battery operation)

* **Request:** Keep the device in deep sleep, wake periodically to read and push via MQTT/HTTP, ideally using ESP-NOW to avoid Wi-Fi association overhead — enabling battery power and/or night-time sleep windows.
* **Benefit:** Battery/solar operation; much lower average power.
* **Feasibility (analysed):** Same hardware reality as **#38**. **ESP32-CAM — ❌** the camera and Wi-Fi re-init per wake dominate and the camera clock cannot be scaled; battery operation is not practical. **ESP32-S3 — 🟡** technically possible for long intervals, but each wake still re-inits camera + Wi-Fi. ESP-NOW could cut the network overhead but bypasses the MQTT/HTTP/InfluxDB paths users rely on. See [docs/BOARD-FEATURE-MATRIX.md](docs/BOARD-FEATURE-MATRIX.md).
* **Status:** 🔬 Analysed — not practical on ESP32-CAM; limited on ESP32-S3.

#### #19 Extended log information

* **Request:** Richer logging. See [#580](https://github.com/jomjol/AI-on-the-edge-device/issues/580).
* **Benefit:** Easier debugging and history.
* **Feasibility:** All boards — feasible (software only). v17 already moves to `esp_log` v2 and adds heap-failure diagnostics and OTA trial-boot logging; data/event logs and the log viewer exist.
* **Status:** 🟡 Partially — logging infrastructure improved in v17; specific additions from the issue remain open.

#### ~~#18 Show WLAN signal strength on the web page~~

* **Status:** ✅ Implemented. RSSI is shown on the overview page. See [#563](https://github.com/jomjol/AI-on-the-edge-device/issues/563).

#### ~~#17 Direct InfluxDB connection~~

* **Status:** ✅ Implemented (v10.6.0); extended to InfluxDB v2 — see **#31**.

#### #16 Serial (RX/TX) communication

* **Request:** Send the readout over a serial RX/TX interface with a dedicated tag, as its own flow module with configuration. See [#512](https://github.com/jomjol/AI-on-the-edge-device/issues/512).
* **Benefit:** Integrate with serial-only hosts/PLCs and isolated networks.
* **Feasibility:** **ESP32-CAM — 🟡 constrained:** few free GPIOs (most are taken by the camera and SD); UART0 is the console. **ESP32-S3 — ✅** more free pins and multiple UARTs. Software effort is moderate (a new flow module).
* **Status:** ⬜ Open.

#### #15 Calibration for fisheye/lens distortion

* **Request:** Correct fisheye lens distortion before reading. See [#507](https://github.com/jomjol/AI-on-the-edge-device/issues/507). Needs: a correction algorithm using ESP32-friendly libraries, a new flow module, config + HTML extensions, and per-lens tuning.
* **Benefit:** Use wide-angle/close-mount lenses without warped digits.
* **Feasibility:** **ESP32-CAM — 🟡 heavy:** per-pixel remap is compute- and RAM-intensive on the ESP32; doable on the full image but adds latency. **ESP32-S3 — ✅ better headroom** (more RAM, vector instructions). High implementation + per-lens tuning effort on all boards.
* **Status:** ⬜ Open.

#### ~~#14 Backup and restore option for configuration~~

* **Status:** ✅ Implemented (v11.3.1) and extended in v17 — zip backup/restore in the web UI, plus automatic config snapshots on save (keeps the latest 10) and a shipped-model manifest so backups exclude the bundled models. See [#459](https://github.com/jomjol/AI-on-the-edge-device/issues/459).

#### #13 Non-linear gauge handling without CNN retraining

* **Request:** Support non-linear analog meters via a lookup table instead of retraining the network. See [#443](https://github.com/jomjol/AI-on-the-edge-device/issues/443).
* **Benefit:** Handle non-linear dials without ML expertise.
* **Feasibility:** All boards — feasible (software only); a configurable LUT applied in post-processing.
* **Status:** ⬜ Open.

#### ~~#12 Fewer reboots due to memory leakage~~

* **Status:** ✅ Implemented — ongoing memory hardening; v17 adds a heap-failed-allocation callback and an image-decode safety net (skip the round instead of rebooting/boot-looping). See [#414](https://github.com/jomjol/AI-on-the-edge-device/issues/414), [#425](https://github.com/jomjol/AI-on-the-edge-device/issues/425), [#430](https://github.com/jomjol/AI-on-the-edge-device/issues/430).

#### #11 MQTT — configurable payload

* **Request:** Let the user define the MQTT payload format. See [#344](https://github.com/jomjol/AI-on-the-edge-device/issues/344).
* **Benefit:** Match downstream consumers (custom JSON, units, field names) without code changes.
* **Feasibility:** All boards — feasible (software only); a template/format string in config.
* **Status:** 🟡 Partially — the v17 **Data Publishing** page lets you pick exactly which parameters are sent per platform (MQTT / InfluxDB / Home Assistant), turn off bulky topics, and only publish on change; a free-form payload **template** (custom JSON shape / field renaming) is still open.

#### #10 Improve and fix image-logging

* **Request:** Fix and improve logging of images. See [#307](https://github.com/jomjol/AI-on-the-edge-device/issues/307).
* **Benefit:** Reliable image history for debugging bad reads.
* **Feasibility:** **ESP32-CAM/WROVER** — feasible (SD storage). **ESP32-S3** — feasible but mind in-flash filesystem space/wear; an SD card is optional for bulk image logs.
* **Status:** ⬜ Open.

#### #9 Basic authentication for the UI

* **Request:** Protect the web UI with authentication. See [#283](https://github.com/jomjol/AI-on-the-edge-device/issues/283).
* **Benefit:** Keep the device's controls off-limits on shared networks.
* **Feasibility:** All boards — feasible (software only).
* **Status:** ✅ Implemented — HTTP Basic Auth (`jomjol_wlan/basic_auth.h`) applied across the file, OTA, camera, GPIO and MQTT endpoints via an auth filter.

#### #8 MQTT-configurable readout interval

* **Request:** Change the readout interval at runtime via MQTT. See the inbound-MQTT work under **#2/#25**.
* **Benefit:** Adjust cadence from automations without a reboot.
* **Feasibility:** All boards — feasible (software only).
* **Status:** 🟡 Partially — the MQTT client now receives commands, and the interval is re-read live (no reboot); a dedicated "set interval" command/topic should be wired to that path.

#### #7 Extended error handling (surfaced on the web page)

* **Request:** Detect important error types (e.g. missing tflite) and show them on the web page. Needs a list of important errors, a checking routine, and firmware + HTML support.
* **Benefit:** Users see actionable errors instead of silent failures.
* **Feasibility:** All boards — feasible (software only).
* **Status:** 🟡 Partially — the overview surfaces process status/diagnostics and v17 adds capture/decode safety nets; a structured error catalog shown in the UI is still open.

#### ~~#6 Check for duplicate ROI names~~

* **Status:** ✅ Implemented (v8.0.0) — ROI names are checked for uniqueness in the editor before saving.

#### #5 Configurable decimal separator (point or comma)

* **Request:** Make the decimal separator configurable for different locales.
* **Benefit:** Output values in the format downstream systems expect.
* **Feasibility:** All boards — feasible (software only).
* **Status:** 🟡 Partially — post-processing is decimal-separator-aware when parsing and supports decimal shifting (`DecimalShift`); an explicit user-facing point/comma output setting is still worth adding.

#### ~~#4 Initial shifting and rotation~~

* **Status:** ✅ Implemented (v7.0.0) — initial rotation and shifting of the raw camera image, with configuration and HTML support. See [#123](https://github.com/jomjol/AI-on-the-edge-device/issues/123).

#### ~~#3 Group digits into multiple reading values~~

* **Status:** ✅ Implemented (v8.0.0) — multiple independent readouts in one setup. See [#123](https://github.com/jomjol/AI-on-the-edge-device/issues/123).

#### #2 MQTT control with callback (online config updates)

* **Request:** Extend the MQTT client to accept callbacks that override `config.ini` settings, handling updates online (currently most changes need a restart). See [#105](https://github.com/jomjol/AI-on-the-edge-device/issues/105).
* **Benefit:** Reconfigure the device remotely without rebooting.
* **Feasibility:** All boards — feasible (software only).
* **Status:** 🟡 Partially — inbound MQTT callbacks exist (`subscribeFunktionMap`), and v17 adds live config apply / interval reload without a reboot; a full "set any config key over MQTT" mapping is still open.

____

#### ~~#1 Optional GPIO for external flash/lighting~~

* **Status:** ✅ Implemented (v8.0.0) — configurable external light source over GPIO (in addition to the on-board flash LED). See [#133](https://github.com/jomjol/AI-on-the-edge-device/issues/133).
