# FastRead Optimization & Tooling Plan

Tracking file for the incremental-read ("FastRead") optimization and related work.
Goal: shorten the meter update interval to **5–10 s** by only running CNN inference on
digit ROIs whose image actually changed, with a full validation pass whenever the base
value the delta is derived from could be wrong.

Status legend: ✅ done · ⬜ todo · 💡 idea/future

---

## 1. FastRead — digital path (current work)

✅ Cached state on `roi` struct (`fastCacheImg` / `fastCacheClass` / `fastCacheFloat`
   / `fastCacheValid`) — `code/components/jomjol_flowcontroll/ClassFlowDefineTypes.h`
✅ Config flags parsed under `[Digits]`: `FastRead`, `FastReadThreshold`,
   `FastReadFullInterval` — `ClassFlowCNNGeneral::ReadParameter`
✅ Resident tflite model across cycles when FastRead is on (skip per-cycle load/allocate)
✅ Per-digit change gate in `case Digit` and `case Digit100` (Analogue100 excluded)
✅ Full-validation triggers: cycle 0, every `FastReadFullInterval`, or `TriggerFullEval()`
✅ Parameter docs — `param-docs/parameter-pages/Digits/FastRead*.md`
✅ Compiles clean and links into firmware (verified `pio run -e esp32cam` → SUCCESS;
   FastRead symbols present in `firmware.elf`)
✅ Default **off** — runtime behavior unchanged until enabled in config.ini

### Immediate TODO
⬜ Wire `TriggerFullEval()` into `ClassFlowPostProcessing` so a full re-read is forced on:
   - digit carry / rollover (lowest digit wraps 9→0)
   - consistency-check failure (`CheckDigitConsistency`, negative/over-max rate)
   - see `ClassFlowPostProcessing.cpp` ~line 739 (rollover) for the hook point
✅ Added `FastRead` / `FastReadThreshold` / `FastReadFullInterval` to the web config UI
   (expert rows in `edit_config_template.html` Digits section, `ParamAddValue` registration
   in `readconfigparam.js`, Write/ReadParameter wiring). Tooltips auto-generate from the
   existing `param-docs/.../Digits/FastRead*.md`; validated with the tooltip generator.
⬜ On-device validation: confirm a 6–7 digit meter reads correctly at 5–10 s and that
   inference count per fast cycle drops to ~1–2 (instrument with the existing debug log)
⬜ Threshold tuning guidance: capture real ROI noise (lighting flicker/shadows) and
   recommend a safe default `FastReadThreshold` per model type
⬜ Verify heap headroom with the resident model held across cycles on a real ESP32-CAM
   (`WriteHeapInfo` already logs this); document the RAM trade-off

### Risks / correctness notes
- A higher digit can only change when its own pixels roll → the per-digit gate catches
  carry automatically; the interval backstop bounds any accumulated drift.
- A false "changed" decision only ever costs one extra inference, never a wrong value.
- Analog ROIs are intentionally never gated (distinct discrete-vs-continuous behavior).

---

## 2. FastRead — further optimization (future)

💡 **Skip alignment on fast cycles.** Alignment compensates for camera drift, which does
   not change in 5 s. Reuse the cached transform; re-align only on the full-validation
   pass, with a cheap reference-mark correlation check to force a re-align on drift.
   Touch point: `ClassFlowAlignment.cpp`. (Note: the change gate still needs the cut
   image, so the crop/resize stays; only the alignment search is skipped.)
💡 **LSD-first early-stop scan.** Digits are ordered MSD→LSD; the first "unchanged" digit
   walking up from the LSD guarantees everything above is unchanged. Lets the gate stop
   early instead of diffing every ROI.
💡 **Two-tier scheduler.** Explicit fast tick (5–10 s) vs full-validation tick (N min /
   on carry), instead of folding the interval into the inference loop.
💡 **Per-ROI adaptive threshold** based on observed per-digit noise floor.
💡 **Analog "slow pointer" optimization** — separate design; only the fastest-moving
   pointer changes between short intervals. Out of scope for the digital work.

---

## 3. Toolchain / ESP-IDF 6.0.1 upgrade — EVALUATION (in progress)

Current verified stack: **ESP-IDF 5.3.1** via `platformio/espressif32 @ 6.9.0`
(`code/platformio.ini`). `pio run -e esp32cam` → SUCCESS.

### 3.1 Toolchain availability — THE MAIN BLOCKER ⚠️
ESP-IDF v6.0 shipped (Mar 2026) and 6.0.1 is the current stable point release. **But as of
May 2026 no PlatformIO platform ships IDF 6.0.x:**
- Official `platformio/platform-espressif32` → still IDF 5.3 line. (Its old "6.0.1"
  *platform* tag is **not** IDF 6 — platform versioning ≠ IDF versioning.)
- Community `pioarduino/platform-espressif32` (the usual route to newer IDF) → latest
  release `55.03.38-1` is **ESP-IDF 5.5.4**, no 6.0.x release or prerelease yet.

➡️ **Conclusion:** the project cannot reach IDF 6.0.1 through PlatformIO today. Options:
- **(A) Wait** for pioarduino/official to ship an IDF 6.0 platform, keep the PlatformIO flow.
- **(B) Native `idf.py` build** against an official ESP-IDF 6.0.1 install. The project is
  already a standard IDF project (`code/CMakeLists.txt`, `main/CMakeLists.txt`,
  `partitions.csv`, `sdkconfig.defaults`), so `idf.py build` is structurally viable. Cost:
  CI rewrite, contributor-workflow change, firmware-packaging change. This also doubles as
  the only way to trial-build 6.0.1 right now.
- **(C) Interim hop to IDF 5.5** (available on pioarduino) to de-risk most breaking changes
  before 6.0 lands on a platform. Recommended stepping stone.

### 3.2 Code API impact (IDF 5.3 → 6.0) — scanned, LOW/MODERATE
Good news: most IDF 6.0 removals don't hit first-party code.
- ✅ **Legacy RMT removed** — smartleds already gates on `ESP_IDF_VERSION >= 5.0.0` and uses
  the new `driver/rmt_tx.h` (`RmtDriver5.h`). No `io_od_mode` usage. OK.
- ✅ **`ledc_timer_set()` removed** — we use `ledc_timer_config_t` / `ledc_timer_config()`
  (`ClassControllCamera.cpp`), not the removed call. OK.
- ✅ **Legacy ADC removed** — no `adc1_get_raw` / `esp_adc_cal_*` in first-party code. OK.
- ✅ **Legacy I2C (`driver/i2c.h`)** — only used inside the vendored `esp32-camera`
  submodule (`sccb.c`); marked EOL-for-v7.0, still present (deprecated) in 6.0. Resolved by
  the submodule bump below.
- ⚠️ **`gpio_pad_select_gpio`** (4 first-party sites: `main.cpp`, `connect_wlan.cpp`,
  `statusled.cpp`, `ClassControllCamera.cpp`) — deprecated; swap to
  `esp_rom_gpio_pad_select_gpio` (or `gpio_reset_pin`). Low effort.
- ⚠️ **HIMEM (`esp_himem_*`)** in `himem_memory_check.cpp` / `esp_sys.cpp` — only the
  `esp32cam-dev-himem` env; disabled in default config (`CONFIG_SPIRAM_BANKSWITCH_ENABLE=n`).
  Verify the himem API still exists in 6.0; otherwise gate it out. Not blocking default build.
- ⬜ Re-baseline `sdkconfig.defaults` / `sdkconfig.esp32cam` against IDF 6 (renamed/removed
  Kconfig keys are the most likely source of build-time noise).

### 3.3 Update ALL components / submodules to their latest release
Policy: as part of this upgrade, bump **every** git submodule and managed dependency to its
latest stable release, then pin it. Rationale: the newest releases carry the IDF 6.0 / GCC 15
compatibility fixes, so updating wholesale removes most per-API patching. Verify each builds
and re-pin in `.gitmodules` / `dependencies.lock`.

| Submodule / dep   | Pinned (was) | Latest / target | Status | Note |
|-------------------|--------------|-----------------|--------|------|
| esp32-camera      | v2.0.6       | **v2.1.6**      | ✅ bumped (trial) | IDF 6 reqs + i2c master |
| esp-tflite-micro  | v1.3.1       | **v1.3.5**      | ✅ bumped (trial) | fixes `std::is_pod` (C++26) |
| esp-nn            | v1.1.0       | **v1.2.0**      | ✅ bumped (trial) | bump with tflite-micro |
| esp-protocols/mdns| mdns 1.4.3   | **mdns-v1.11.1**| ✅ bumped (trial) | builds clean; only mdns_init/hostname_set used |
| stb               | 5736b15      | (no releases)   | ✅ current | header-only, untagged upstream; at master |
| espressif/mqtt    | (new)        | 1.0.0 (latest)  | ✅ added (managed) | replaces removed core mqtt |
| espressif/cjson   | (new)        | 1.7.19 (latest) | ✅ added (managed) | replaces removed core json |
| espressif/esp_jpeg| transitive   | 1.3.1           | ✅ current | pulled by esp32-camera; resolved by mgr |
| espressif/ethernet_init| transitive | 1.2.0       | ✅ current | resolved by mgr |

**All components are now current.** Every git submodule is at its latest upstream release
and every managed/registry dependency was freshly resolved by the component manager during
the IDF 6.0.1 build.

⬜ After the upgrade settles, run a full sweep: for each submodule
   `git fetch --tags && checkout <latest stable>`; for managed deps pin exact versions in
   `dependencies.lock`; rebuild and smoke-test. Keep this list current as the source of truth
   for component versions.

### 3.4 Effort estimate
- Code changes (gpio swap, himem gate, sdkconfig): **~0.5–1 day**.
- Submodule bumps + resolving their build errors: **~1–2 days** (esp32-camera is the wildcard).
- Toolchain/CI migration to native idf.py (if option B): **~1–2 days** including CI + docs.
- Total realistic: **~3–5 days** once an IDF 6.0 toolchain is usable. The dominant risk is
  toolchain availability (3.1), not the code.

### 3.5 Recommendation
1. Land **FastRead first** on the current 5.3 stack (small, bisectable).
2. Do the **gpio/himem/sdkconfig cleanup + submodule bumps on IDF 5.5 (pioarduino)** as an
   interim — clears ~80% of the 6.0 breakage with a supported PlatformIO platform.
3. Move to **IDF 6.0.1** when a platform ships it, or commit to the native `idf.py` path if
   6.0 is required sooner. A native 6.0.1 trial build is the next concrete validation step
   (see §3.6) — it needs a ~4 GB IDF install.

### 3.6 Native ESP-IDF 6.0.1 trial build — RESULTS (branch `idf6-upgrade-eval`)
Installed official ESP-IDF v6.0.1 at `/home/eric/esp/esp-idf` and built with `idf.py`.
Real error surface, in the order hit (each fixed to reach the next):

1. **Toolchain gaps (environment, not code):**
   - ESP-IDF `install.sh esp32` installed only compilers — **cmake & ninja were missing**.
     Fixed by `pip install cmake ninja` into the IDF python env. (PlatformIO bundles these;
     a native flow must provide them.)
   - Shallow `--recursive` IDF clone did **not** populate all submodules (e.g. `esp-mqtt`).
     A full `git submodule update --init --recursive` is required.
2. **`mqtt` removed from IDF core (real 6.0 change).** esp-mqtt is no longer bundled
   (`components/mqtt/` has only `test_apps`). Fix: add managed dep `espressif/mqtt` →
   created `code/main/idf_component.yml`.
3. **`json` (cJSON) removed from IDF core (real 6.0 change).** Fix: add `espressif/cjson`
   **and** rename `REQUIRES json` → `cjson` in `jomjol_mqtt` and `jomjol_webhook`
   CMakeLists (the managed component registers as `cjson`, not `json`). Headers stay
   `#include "cJSON.h"`.
4. **Board define (tooling, not 6.0).** `#error "Board not selected"` — PlatformIO injects
   `-D BOARD_ESP32CAM_AITHINKER` via `platformio.ini`, absent in native builds. Added a
   guarded `add_compile_definitions(BOARD_ESP32CAM_AITHINKER)` in `code/CMakeLists.txt`
   for native (non-PLATFORMIO) builds. A real native migration must move board selection
   into CMake/Kconfig.

**✅ RESULT: GREEN BUILD.** `idf.py build` produces a flashable image:
`build/AI-on-the-edge.bin`, 0x179020 bytes, 21% app-partition free, on ESP-IDF v6.0.1 /
GCC 15 (xtensa-esp-elf 15.2.0). Full ordered list of every change needed:

**A. Toolchain / environment**
- `pip install cmake ninja` into the IDF python env (IDF `install.sh esp32` ships only compilers).
- `git submodule update --init --recursive` in the IDF clone (shallow recursive missed esp-mqtt).

**B. Component-manager extractions (real 6.0)**
- esp-mqtt removed from core → add `espressif/mqtt` in new `code/main/idf_component.yml`.
- cJSON removed from core → add `espressif/cjson`; rename `REQUIRES json` → `cjson` in
  `jomjol_mqtt` and `jomjol_webhook`.

**C. Driver-component split — add explicit `REQUIRES` (real 6.0; 6.0 enforces include↔requires)**
- `jomjol_controlGPIO`: + `esp_driver_gpio esp_driver_rmt esp_driver_spi`
- `jomjol_controlcamera`: + `esp_driver_gpio esp_driver_ledc`
- `jomjol_fileserver_ota`: + `esp_driver_gpio`
- `jomjol_helper`: + `esp_driver_gpio esp_driver_sdmmc esp_driver_sdspi`
- `jomjol_wlan`: + `esp_driver_gpio mdns` (mdns was a link-time undefined ref)
- `main`: + the full IDF set it includes (`esp_wifi nvs_flash esp_event esp_netif esp_eth
  esp_http_server esp_pm esp-tls fatfs esp_hw_support esp_system esp_driver_sdmmc`) **and**
  the 10 local jomjol components it includes.

**D. Source / API changes (real 6.0 + GCC 15)**
- `Helper.cpp`: `#include "../sdmmc_common.h"` (now private) → public `sdmmc_cmd.h`.
- `sdcard_init.c`: `esp_vfs_fat_register()` now takes an `esp_vfs_fat_conf_t` struct →
  use `esp_vfs_fat_register_cfg()`; fix transposed `calloc(size,1)` → `calloc(1,size)`.
- `server_GPIO.cpp`: cross-enum compare (`gpio_int_type_t` vs `gpio_pin_mode_t`) → cast to int.
- `RmtDriver5.cpp`: add `.intr_priority = 0` to `rmt_tx_channel_config_t`.
- `SmartLeds.h`: `HSPI_HOST` removed → `SPI3_HOST`.
- `RmtDriver5.h`: `SOC_RMT_GROUPS` / `SOC_RMT_CHANNELS_PER_GROUP` dropped from public
  soc_caps → ESP32 fallback defines (1×8).
- `connect_wlan.cpp`: enum `WIFI_REASON_NOT_AUTHED` → `WIFI_REASON_ASSOC_NOT_AUTHED`
  (version-gated shim on `ESP_IDF_VERSION`).

**E. Build-system / tooling (native vs PlatformIO)**
- `code/CMakeLists.txt`: for native (non-PLATFORMIO) builds, inject board define via
  `idf_build_set_property(COMPILE_DEFINITIONS "BOARD_ESP32CAM_AITHINKER")` and a bare
  `-Wno-error` (IDF 6.0 hardcodes `-Werror`; vendored stb/miniz/SmartLeds aren't clean
  under GCC 15 — PlatformIO didn't treat these as errors).

**F. Submodule bumps (see §3.3)** — esp32-camera v2.1.6, esp-tflite-micro v1.3.5
(fixes `std::is_pod` under `-std=gnu++26`), esp-nn v1.2.0.

**Key takeaways**
- IDF 6.0 **code** impact is modest and mechanical: component-manager extractions
  (mqtt/cJSON), the driver-component split (explicit `REQUIRES`), a handful of renamed
  APIs/enums, and GCC-15 strictness. No deep rewrites.
- The real cost is the **build-system migration**: there is no PlatformIO platform for IDF
  6.0, so this used native `idf.py`. Productionizing means choosing native idf.py (rewrite
  CI in `.github/workflows/build.yaml`, move board selection out of `platformio.ini`) or
  waiting for a 6.0 PlatformIO platform.
- **"Update all components to latest" (your instruction) paid off** — the tflite-micro bump
  alone removed the only hard C++-standard breakage.

⬜ Remaining before merge: on-device smoke test (flash + camera + CNN + MQTT), decide the
   PlatformIO-vs-native CI path, re-baseline sdkconfig for 6.0, address himem/gpio_pad
   deprecations (§3.2), and finish the submodule sweep (§3.3).

> Trial build artifacts live on branch `idf6-upgrade-eval`. The IDF 6.0.1 SDK is at
> `/home/eric/esp/esp-idf`; build via `bash /home/eric/esp/build-aiotedge.sh`.

### Local build setup (already provisioned)
- PlatformIO in `.pio-venv/`, isolated core in `.pio-core/`.
- Build:
  ```
  cd code && PLATFORMIO_CORE_DIR=$PWD/../.pio-core ../.pio-venv/bin/pio run -e esp32cam
  ```
- Submodules must be initialized: `git submodule update --init --recursive`.

### 3.7 On-device boot test — PASSED ✅ (ESP32-D0WDQ6, 4MB flash, 8MB PSRAM)
Flashed the IDF 6.0.1 build to real hardware (esptool, /dev/ttyUSB0) and captured serial:
- Boots clean on **ESP-IDF v6.0.1**, app version **v17.0.0-alpha**, no panics.
- **PSRAM** init OK (8MB found, 4MB mapped — normal ESP32 limit without himem).
- **SD card** basic R/W check **successful** — validates the new `esp_vfs_fat_register_cfg`
  + public `sdmmc_cmd.h` migration on hardware.
- **Camera** OV2640 detected + configured, frame buffer allocated in PSRAM — validates the
  esp32-camera v2.1.6 bump + driver-component split.
- Remaining errors are all "empty SD card" (no config.ini / /html / /config models) since
  only firmware was flashed — **expected**, not firmware faults. Device finishes init and
  idles ("SSID empty, init aborted").
⬜ Full functional test still needs the **SD-card content provisioned** (config + Web UI +
  CNN models) to exercise WiFi/MQTT, the CNN flow, FastRead, and the dark-mode UI.

### 3.8 ✅ RESOLVED: redundant `esp_psram_init()` corrupted the mounted SD on IDF 6.0
**Root cause (one line):** `main.cpp` re-calls `esp_psram_init()` after the SD card is already
mounted. PSRAM is already initialized at boot (`CONFIG_SPIRAM_BOOT_INIT=y`); on IDF 5.x the
re-init was a harmless no-op, but on **IDF 6.0 it re-maps PSRAM/cache and corrupts the
already-mounted SD's FATFS in-memory state** → `stat`/directory traversal fails → "Config file
seems to be missing" → device idles.

**Fix:**
```c
esp_err_t PSRAMStatus = esp_psram_is_initialized() ? ESP_OK : esp_psram_init();
```
(only init if not already initialized). One line in `code/main/main.cpp`. **Verified on
hardware:** folder/config checks pass, camera up, "Initialization completed successfully",
WiFi connects from `wlan.ini`, CNN Round #1 runs — the meter reads on v17.0.0-alpha / IDF 6.0.1.

**Why it took ~27 iterations / what it was NOT:** the symptom *appeared* to be camera-triggered
because `Camera.InitCam()`'s ~2 s of `vTaskDelay`s sit right after `esp_psram_init()`, so the
break surfaced "after the camera." Disabling camera init entirely proved config read **still**
failed → camera was a red herring. Bracketing `esp_psram_init()` with `stat` probes pinned it
exactly (`before=OK`, `after=FAIL`). Also conclusively ruled out (each on-device): SD card/data
(Linux reads it fine), read hardware (raw `sdmmc_read_sectors` returns correct data at every
sector, identical checksums before/after), FATFS partition detection, sector size, PM, GPIO,
SD clock, DMA-RAM, and **card layout — MBR *and* SFD both failed** before the real fix.

Lesson: don't fixate on the first plausible trigger (camera) — bisect with probes. The 2 s
delay between the real cause and the visible symptom sent the investigation down a long detour.

### 3.9 ⬜ Interim setup / provisioning process (v16→v17 migration + new installs)
With v17 working, define a clean way for users to get the SD-card content (config + `/html` +
`/config/*.tflite`) onto a card without pulling it, since the on-disk layout/Web-UI changes
between v16 and v17:
- **Flash firmware first** (USB/esptool or OTA), then **provision the SD over the air or via
  USB** rather than requiring a card reader.
- Reuse the existing release artifacts: `update.zip` (OTA: firmware + Web UI + models) and
  `remote_setup.zip` (firmware + Web UI + full config). The device already exposes an
  OTA/file-server path (`register_server_ota_sdcard_uri`, `/fileserver`) — wire a guided
  flow around it.
- **Migration guard:** the firmware already warns on a Web-UI/firmware version mismatch
  (`getHTMLcommit()` vs `GIT_REV`) and recommends re-running `update__*.zip` — make this the
  one-click migration step.
- **New install / empty card:** SoftAP setup mode (`CheckStartAPMode`) already starts when
  `wlan.ini`/`config.ini` are missing — ensure it can serve the minimal UI to upload the
  rest, or document the USB-serial / OTA bootstrap.
- Note: any SD card layout works now (MBR or SFD) — the SD bug was `esp_psram_init`, not the
  card, so no special card formatting is required for users.

---

## 4. Open questions
- Default cadence: expose the fast-tick interval as config, or derive from existing
  read-interval settings?
- Should FastRead auto-disable when a consistency failure rate threshold is exceeded
  (safety fallback to full reads)?

---

## 5. Performance / memory / cleanup (from code review)

### Done ✅
- **Logging hot-path cost reduced.** `ClassLogFile::WriteToFile` now takes `tag`/`message`
  by `const std::string&` (was by value → 2 copies/call) and defers all
  `time()`/`localtime()`/`strftime()`/filename work until *after* the file-level guard, so
  filtered messages skip it. Added `getLogLevel()`. (`ClassLogFile.h/.cpp`)
- **Removed genuinely-dead field** `NumberPost::ErrorMessage` (no `->ErrorMessage`
  references existed; the same-named uses are an unrelated class member).
- **Corrected a wrong comment:** `NumberPost::timeStampTimeUTC` was marked "not used; can
  be removed" but is set in PostProcessing and **read by both InfluxDB v1 and v2** exporters
  — kept it, fixed the comment. (Do NOT remove.)
- **himem:** already fully gated behind `USE_HIMEM_IF_AVAILABLE` /
  `CONFIG_SPIRAM_BANKSWITCH_ENABLE` (off by default) — not compiled into the default
  firmware, so no change needed (earlier "dead weight" note was incorrect).

### To do ⬜
- ✅ **Eliminate filtered-out log string construction (bigger win).** Added the guarded
  `LOGD(tag,msg)` macro in `ClassLogFile.h` (only evaluates the message when file log level is
  DEBUG+) and converted the 32 string-building `WriteToFile(ESP_LOG_DEBUG, TAG, …)` calls in the
  CNN inference loop (`ClassFlowCNNGeneral.cpp`). Removes per-cycle heap churn at the default INFO
  level; identical behavior under DEBUG. The macro is reusable for other hot paths
  (`ClassFlowPostProcessing` / `ClassFlowAlignment`) if needed later.
- ⬜ **`std::string` pass-by-value across signatures** (configFile, ClassControllCamera,
  server_*). Convert hot ones to `const std::string&` to cut heap alloc/free churn.
  Note: the per-cycle virtual `doFlow(string time)` (14 sites) was assessed and **skipped**
  — `time` is a short timestamp copied ~10×/cycle and small strings are SSO (no heap), so
  it's low value vs. the risk of changing a virtual signature across all overrides. Target
  instead functions that copy long/variable strings in loops.
- ⬜ **Shrink `char zw[1024]` stack buffers** (7 sites: configFile, ClassFlow*, server_ota,
  softAP). Most format short strings; ~128–256 B is plenty. Frees task stack.
- ✅ **Removed disabled dead block** at `ClassFlowMQTT.cpp:338` (commented-out "no longer a
  use case" branch).
- ✅ **Implemented the "Skip Messages on Error" feature** (`ErrorMessage`), which was a
  documented/UI-exposed option that the C++ ignored (member was write-only). Behavior
  (confirmed with user): on a consistency error (neg-rate / rate-too-high), `true` (default)
  **skips** the transmission for that reading (empty value) and `false` transmits the **last
  valid value** instead. Also fixed the default init (`false`→`true`, matching the documented
  default) and rewrote the contradictory param-doc. `ClassFlowPostProcessing.cpp` +
  `param-docs/.../ErrorMessage.md`. UI label "Skip Messages on Error" now matches behavior.
- ⬜ **Audit vendored `miniz`** (`jomjol_fileserver_ota/miniz`, ~11k LOC) — confirm which
  APIs the zip backup/restore actually uses; large surface to carry.
- 💡 No fully-unused *components*: all 15 are wired into the firmware.

### SD / flash write-wear review (2026-05-30)
- ✅ **Internal flash (NVS) is not worn per round** — no `nvs_set`/`nvs_commit` in the flow path;
  NVS is only initialized at boot. Config/wlan/prevalue all live on the **SD card**, not flash.
- ✅ **New features are write-safe**: Status LED (LED hardware only; config written on Save),
  Pause (in-memory; `/pause` poll is read-only, only logs on an actual state change), per-step
  Diagnostics (DEBUG-gated → zero SD writes at the default INFO level), Live log viewer (GET `/log`
  every 3 s = reads only, opt-in, auto-stops when hidden), Timezone dropdown (client-side).
- ✅ **Heaviest writer (per-round images) is OFF by default** — `SaveAllFiles=false`, so
  `/sdcard/img_tmp/*.jpg` are not rewritten each round unless the debug option is enabled.
- ✅ **Main SD write-amplifier fixed: log lines are now buffered.** Previously every `WriteToFile`
  did `fopen("a+")` + write + `fclose` → a FAT/dir metadata update **per log line** (multiplying
  ~30–60× at FastRead's 5–10 s target). Now lines accumulate in a mutex-guarded RAM buffer in
  `ClassLogFile` and flush in batches: when the buffer reaches 4 KB, after 10 s, on date rollover,
  at the end of each flow round, when the log is read back (so the viewer is current), and before
  any reboot/OTA. Net: ~one SD write per round instead of one per line. Trade-off: up to one flush
  window of logs may be lost on a hard power loss (documented in code). Also made concurrent
  logging thread-safe (the old per-call `fopen`/`fclose` raced on a shared static handle).
- ⬜ Other per-round SD writes are small/bounded: data CSV (1 append, gated by `DataLogActive`) and
  `prevalue.ini` (rewritten only when the value changes, tens of bytes). Consider buffering both
  if sub-minute intervals become common (tie-in with FastRead §1).

---

## 6. Image processing efficiency (investigate)

⬜ **Audit the image pipeline for performance / memory wins.** The image path is the largest
   RAM consumer and a big chunk of per-cycle CPU. Areas to check:
   - **Per-cycle allocations:** `CImageBasis` temp images are `new`/`delete`d every round
     (rawImage, alignment `ImageTMP`/`AlignAndCutImage`, `SendRawImage`, per-ROI cut/resize
     buffers). Look for buffers that can be pooled/reused across cycles instead of
     reallocated (PSRAM fragmentation + alloc cost). See `ClassFlowTakeImage.cpp`,
     `ClassFlowAlignment.cpp`, `ClassFlowCNNGeneral.cpp`, `CImageBasis.cpp`.
   - **Alignment cost:** the rotate/shift search over the full frame each cycle is the
     heaviest non-inference step — tie in with FastRead §2 (reuse cached transform, only
     re-align on the full-validation pass).
   - **Resize/cut:** `Resize()`/`CutAndSave()` run per ROI every cycle; check the
     interpolation cost and whether output buffers can be reused.
   - **JPEG decode/encode:** confirm decode happens once per capture (not repeatedly) and
     that the new `espressif/esp_jpeg` managed component is used efficiently.
   - **PSRAM vs internal:** verify large buffers land in PSRAM (`MALLOC_CAP_SPIRAM`) and only
     latency-sensitive ones use internal RAM; review the `SPIRAM_MALLOC_ALWAYSINTERNAL`
     threshold and the name-based PSRAM allocation note in `ClassFlowAlignment.cpp`.
   - Measure first (`WriteHeapInfo` / timing logs) before optimizing; quantify per-stage cost.

---

## 7. UI / front-end

- ✅ **Dark mode for the web UI.** Implemented as a lightweight CSS-variable theme:
   - New `sd-card/html/theme.css` — dark palette + overrides gated on `html[data-theme="dark"]`
     (attribute-prefixed selectors out-specify the existing light rules; a few `!important`
     guards cover inline `style="color:black"` labels). `color-scheme: dark` themes native
     controls/scrollbars. Light mode is untouched (rules only apply when the attribute is set).
   - New `sd-card/html/theme.js` — applies the saved theme on parse (before paint, no flash),
     persists to `localStorage` (`aiotedge-theme`), falls back to `prefers-color-scheme`,
     and live-propagates a toggle from the parent into the open iframe.
   - `index.html` — includes theme.css/theme.js early in `<head>` + a fixed-position 🌙/☀️
     toggle button; theme.css/js auto-injected into all 28 content/iframe pages (and the
     config template) so each picks up the shared theme.
   - Packaged automatically by the release flow (copied + gzipped like the other assets).
   - Validated: no JS name collisions, `node --check` clean, light mode unchanged.
   - ⬜ Follow-up (optional): expose a server-side "Theme" config so the default can be set
     in `config.ini`; on-device visual pass across every page.
- ✅ FastRead config options added to the UI (see §1).
- ⬜ **"Pause processing" menu item.** Add a control in the web UI to pause/resume the flow
   (CNN reading loop). Very useful while setting up reference image, alignment, and ROIs so
   the device doesn't keep capturing/processing mid-setup. Implementation: a flag checked by
   the flow task (`MainFlowControl`/`server_tflite`) + a REST endpoint
   (e.g. `/pause?status=1`) + a menu/toolbar toggle; persist across the current session and
   show the paused state clearly. Tie in with the existing "trigger single round" handler.
- 🟡 **LED status + countdown to next processing.** Use the onboard/external LED(s) to show
   processing state and count down to the next round:
   - ✅ Per-step status via RGB color (idle / take image / align / digitize CNN / post-process /
     transmit / error) — implemented: `StatusLED` enable + 7 per-stage colour params, driven via
     a decoupled stage callback (`jomjol_helper` → `jomjol_controlGPIO`), UI colour pickers +
     tooltips. Honors the existing flash-LED (same WS281x on GPIO12).
   - ⬜ Countdown: e.g. blink rate or a fading ramp as the next round approaches (not yet done).
   - ⬜ Follow-up: a configurable mode (off / status-only / countdown+status); on-device colour
     visibility pass.
- ⬜ **UI cleanup / modernization + responsiveness.** The web UI is legacy (table layouts,
   hardcoded colors, fixed widths, per-page `<style>`). Modernize incrementally:
   - Consolidate styles into shared CSS variables (the dark-mode `theme.css` is a starting
     point) and remove inline `style="color:black"` etc.
   - Responsive layout (fl/grid, mobile-friendly) — the menu, config tables, and overview
     should work on phones; replace fixed `min-width:688px` constraints.
   - Modern component styling (cards, spacing, typography), consistent across pages.
   - Keep it dependency-light (served from ESP32/SD, gzipped) — plain CSS/JS, no heavy
     frameworks. Coordinate with the dark-mode variables so both themes stay consistent.
- ⬜ **Apply configuration changes live (no reboot) where possible.** Today saving config
   from the UI requires a restart to take effect (see FeatureRequest #2). Add a path to
   re-apply settings at runtime:
   - On save, re-run the relevant `ReadParameter`/init for the affected flow modules instead
     of forcing a reboot — e.g. ROI/digit/analog config, post-processing, MQTT/InfluxDB
     endpoints, intervals, FastRead/dark-mode-irrelevant params.
   - Cleanest hook: pause the flow (see the "Pause processing" item), reload config into the
     `ClassFlow*` objects (re-`ReadParameter`), then resume — avoids races with an in-flight
     round. Reuse the config-reload work already needed for MQTT callbacks (FeatureRequest #2).
   - Classify each parameter as **live-reloadable** vs **reboot-required** (e.g. camera/SD/
     WiFi/partition changes may still need a restart); the UI shows which applies and only
     prompts for a reboot when actually necessary.
   - Keep a safe fallback: if live-reload of a given change isn't supported, fall back to the
     current "reboot to apply" behavior rather than applying a partial/invalid state.
- ⬜ **Flexible processing interval (sub-minute + unit dropdown).** The auto-timer interval is
   currently whole minutes (`[AutoTimer] AutoStart`/`Intervall` in minutes). Allow finer and
   coarser granularity:
   - UI: an integer entry + a **unit dropdown (seconds / minutes / hours / days)**; store the
     resulting interval (convert to a common unit, e.g. seconds, internally).
   - Allow **sub-minute** intervals (seconds) — pairs naturally with FastRead (§1) for the
     5–10 s target; guard against intervals shorter than one flow round can complete.
   - **Default: 5 minutes** in the base configuration (unchanged default behavior).
   - Firmware: widen the interval type/parsing (currently minutes) to seconds and update the
     auto-timer loop; keep config back-compat (treat a bare number as minutes if no unit).
- 🟡 **Live log viewer (auto-scroll / tail).** The log viewer page currently requires a manual
   reload (or button press) to see new entries. Add a "Live / auto-scroll" checkbox that polls
   (or streams) new log lines and appends them, auto-scrolling to the bottom; unchecking pauses
   the tail. Keep it lightweight (periodic fetch of the log tail; avoid re-rendering the whole
   buffer). *(In progress — see request batch 2026-05-30.)*
- ⬜ **Improve ROI selection GUI.** The reference/ROI editors (digit & analog) are functional but
   fiddly: improve the drag/resize handles, snapping/alignment aids, zoom & pan, keyboard nudge,
   per-ROI add/duplicate/delete ergonomics, and clearer overlay of current vs. proposed ROIs.
   Make it responsive and touch-friendly. Coordinate with the "Draw from center" analog option
   already added. Goal: setting up a new meter should be fast and forgiving.
- ⬜ **Better tooltips / inline documentation.** Expand the per-parameter tooltip docs
   (`param-docs/parameter-pages/*`) to explain what each setting does *and what it affects*
   (interactions, when to change it, typical values, side effects). Add inline help to the
   non-config UIs too (ROI editor, overview, log viewer). Audit for missing/blank tooltips and
   stale text; keep the markdown→tooltip generator as the single source of truth.
- ⬜ **Improve graphing functionality & performance.** The data/graph view should be faster and
   more capable: efficient handling of long history (downsample/window instead of loading all
   points), zoom/pan, selectable time ranges, multiple series (per meter value / rate), and
   clearer styling that respects dark mode. Reduce client-side work and payload size for big
   data logs.

## 8. Serve the web UI from flash instead of the SD card — FEASIBILITY

**Goal (user):** ship the web UI inside the firmware `.bin` so a single compiled image
updates the UI; keep models, logs, and config on the SD card for portability.

**Numbers (4 MB board — the common ESP32-CAM, incl. this device ESP32-D0WDQ6):**
- Flash budget 4 MB. Current partitions: `ota_0` 1900 KB + `ota_1` 1900 KB = **3.7 MB** for the
  dual app slots, plus bootloader (~21 KB) / nvs (16 KB) / otadata (8 KB) / phy (4 KB) / table.
  → only **~200 KB unallocated** on 4 MB.
- App binary ≈ 1.48 MB, ~20 % (~395 KB) free inside each 1.9 MB slot.
- Web assets ≈ **2.4 MB raw / ~530 KB gzipped**, 67 files.

**Verdict by board:**
- **4 MB + dual-OTA (current default): NOT feasible.** A ~0.5–1 MB LittleFS/SPIFFS web partition
  doesn't fit in the ~200 KB that's left, and embedding assets in the app (EMBED_FILES) would push
  each slot past 1.9 MB (counted in *both* OTA slots). The math simply doesn't close on 4 MB while
  keeping A/B OTA rollback safety.
- **4 MB, willing to drop to a single app slot:** frees ~1.86 MB → room for a ~1 MB `web`
  LittleFS partition. Works, but loses safe A/B OTA (a bad flash can brick until re-flashed over
  serial). Trade-off decision.
- **8 MB / 16 MB flash boards (incl. most ESP32-S3):** **feasible and clean.** Add a dedicated
  `web` LittleFS partition (~1–1.5 MB), build its image alongside the app so `idf.py flash` writes
  one combined image, and serve from it.

**Recommended design (when targeting 8 MB+ or single-slot 4 MB):**
1. Move `sd-card/html/*` into a build-time **LittleFS image** (`littlefs_create_partition_image`)
   on a new `web` data partition; keep files gzipped as today.
2. File server: look up requests in the flash `web` mount **first**, fall back to `/sdcard/html`
   if absent — so a user can still override/patch a page from SD (portability + emergency edits).
3. Keep models, `config.ini`, `wlan.ini`, prevalue, logs, and data on SD (unchanged).
4. Package the web partition image in the release flow; OTA-update it via a second OTA image
   (data-OTA) so the UI can still be updated without a full SD swap.

**Decision (2026-05-30): keep the UI on the SD card (status quo).** The web UI stays on SD and is
updated via the existing OTA Update page (no card swap needed); flash space is reserved for the
firmware + dual-OTA rollback safety. Revisit only if the project targets 8 MB+ boards, where a
dedicated `web` LittleFS partition (option c above) becomes the clean path.

## 9. ESP-IDF 6.0 build-flag parity + opportunities

### 9.1 ✅/⬜ Build-flag parity audit (platformio.ini → native idf.py)
The native `idf.py` build does **not** read `platformio.ini build_flags`. Audited every `-D` flag:
- ✅ **Restored as global IDF compile definitions** (`code/CMakeLists.txt`, non-PLATFORMIO block):
  `BOARD_ESP32CAM_AITHINKER`, `ENABLE_MQTT`, `MQTT_ENABLE_SSL`,
  `MQTT_SUPPORTED_FEATURE_SKIP_CRT_CMN_NAME_CHECK`, `ENABLE_INFLUXDB`, `ENABLE_WEBHOOK`. Without
  these, MQTT/InfluxDB/Webhook were compiled out (device published nothing). Must be **global**
  (not in defines.h) because several files test `#ifdef ENABLE_MQTT` before including defines.h.
- ✅ **`ENABLE_SOFTAP` restored.** The blocker was a stale `#include "protocol_examples_common.h"`
  in `softAP.h` that nothing actually used (softAP.cpp only calls `esp_netif_create_default_wifi_ap()`).
  Dropped the include, set `ENABLE_SOFTAP` as a global IDF compile definition, and removed the now-
  unused `protocol_examples_common` from `EXTRA_COMPONENT_DIRS` (root + 2 component CMakeLists).
  The Wi-Fi setup AP (shown when no `wlan.ini`) is back.
- 💤 **Inert** (not referenced in source): `USE_ESP32`, `USE_ESP_IDF`, `USE_ESP32_FRAMEWORK_ESP_IDF`,
  `BOARD_HAS_PSRAM`.
- 🔧 **Non-default envs only** (not in `[env:esp32cam]`): the `CONFIG_*` power-management / task-WDT
  flags live in `esp32cam-power-management` / `esp32cam-dev`. In a native idf build these belong in
  `sdkconfig.defaults` (Kconfig), not as `-D`. Port them if/when those build variants are recreated.
- 🚫 **Commented out** (never active): `MQTT_PROTOCOL_311`, `MQTT_ENABLE_WS`, `MQTT_ENABLE_WSS`,
  `MQTT_SUPPORTED_FEATURE_CRT_CMN_NAME`, `MQTT_SUPPORTED_FEATURE_CLIENT_KEY_PASSWORD`, the `DEBUG_*`
  and `HEAP_TRACING_*` / `TASK_ANALYSIS_ON` switches.
- ⬜ **Recreate the alternate build envs** for the native toolchain (board-rev3, cpu-freq-240,
  power-management, no-softap, himem, task-analysis) as idf.py build profiles / sdkconfig variants.

### 9.2 ⬜ IDF 6.0 features worth adopting (evaluate)
Opportunities the 5.3→6.0 jump opens up for this project (each TBD / measure before adopting):
- **Power management / light sleep** (`esp_pm` DFS + tickless idle): meaningful idle-power savings
  for battery/solar meter installs between rounds. Pairs with the flexible-interval work (§7).
- **Wi-Fi 802.11k/v/r roaming + connection-stability** improvements: the project already has roaming
  scaffolding (`WLAN_USE_ROAMING_BY_SCANNING`); 6.0's stack is more robust and lower-memory.
- **Newer toolchain (GCC 15, C++23/26)**: better optimization and `constexpr`/`std::string_view`
  opportunities in the hot CNN/post-processing paths (ties into the perf items in §5).
- **mbedTLS 4.x / PSA crypto**: stronger, smaller TLS for MQTTS / HTTPS / webhook endpoints.
- **Heap allocator (TLSF) + PSRAM refinements**: relevant to the memory-tight CNN workload; could
  reduce fragmentation/peaks (measure with the new per-step heap diagnostics).
- **New `esp_driver_*` split drivers** (RMT already used for the WS281x status LED): cleaner APIs,
  smaller link footprint if legacy drivers are dropped.
- **Secure Boot v2 / flash encryption** maturity: optional hardening for production deployments.
- **Larger-flash / ESP32-S3 targets**: 6.0's better S3 support pairs with the "web UI in flash"
  idea (§8) and more PSRAM/where a bigger CNN or higher-res capture becomes feasible.
