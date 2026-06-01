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
🟡 Wire `TriggerFullEval()` into `ClassFlowPostProcessing`:
   - ✅ **consistency-check failure** (negative rate / rate-too-high) now calls
     `flowDigit->TriggerFullEval()` before `continue`, so a rejected reading forces a full
     re-read of every digit on the next cycle (recovers a stale per-digit cache).
   - 💡 digit carry / rollover is already covered by the per-digit pixel gate (the carried digit's
     pixels change → it is re-inferred), so no explicit trigger is needed there.
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
- ✅ **`std::string` pass-by-value → `const&` (alpha.14).** Converted the **read-only** helpers
  that only `.c_str()`/read the param — the hot file/path utilities in `Helper.{h,cpp}`
  (`DeleteFile`, `RenameFile`, `RenameFolder`, `MakeDir`, `FileExists`, `FolderExists`,
  `getFileType`, `getFileFullFileName`, `getDirectory`, `findDelimiterPos`, `numericStrToBool`,
  `stringToBoolean`) plus `GetFileSize` (CTfLiteClass + CCamera), `delete_all_in_directory`, and
  `isNewParagraph` (ConfigFile + ClassFlow). The compiler accepting `const&` confirms each is
  read-only. **Deliberately left by-value:** the transform helpers that *mutate* their param and
  return it (`toUpper`/`toLower`/`trim`/`ZerlegeZeile`/`FormatFileName`/`CopyFile`/`send_file`) —
  by-value is idiomatic there and `const&` would just force an internal copy (no win). `doFlow(string
  time)` stays skipped (SSO, per the earlier note).
- ✅ **`char zw[1024]` buffers — assessed, kept (alpha.14).** On inspection these are **not**
  wasteful format buffers: the configFile / ClassFlow / ClassFlowControll ones back `fgets(zw, 1024,
  …)` over **config-line** reads (lines can legitimately be long — passwords, URLs, multi-token ROI
  rows), `softAP.cpp` `buf[1024]` is a TCP **recv chunk** (bigger = fewer syscalls), and
  `MainFlowControl` `_query[512]` is an HTTP **query** parser. Their size is a *correctness* bound,
  not slack — shrinking risks truncation. The 2–3 genuinely-short readers (align.txt, prevalue.ini,
  update.txt) run only at boot/save on the large main-flow stack, so the few-hundred-byte saving is
  negligible. Net: a safe shrink isn't worth the truncation risk; left as-is.
- ✅ **Removed disabled dead block** at `ClassFlowMQTT.cpp:338` (commented-out "no longer a
  use case" branch).
- ✅ **Implemented the "Skip Messages on Error" feature** (`ErrorMessage`), which was a
  documented/UI-exposed option that the C++ ignored (member was write-only). Behavior
  (confirmed with user): on a consistency error (neg-rate / rate-too-high), `true` (default)
  **skips** the transmission for that reading (empty value) and `false` transmits the **last
  valid value** instead. Also fixed the default init (`false`→`true`, matching the documented
  default) and rewrote the contradictory param-doc. `ClassFlowPostProcessing.cpp` +
  `param-docs/.../ErrorMessage.md`. UI label "Skip Messages on Error" now matches behavior.
- ✅ **Audited vendored `miniz` (alpha.14).** The OTA path uses only **6 reader/inflate APIs**
  (`mz_zip_reader_init_file` / `_get_num_files` / `_file_stat` / `_extract_file_to_heap` / `_end`,
  `mz_free`) — **no** compression/writer (`tdefl_*` / `mz_zip_writer_*`) usage anywhere first-party.
  Finding: the unused deflate+writer half is **not** flash bloat — the linker's `--gc-sections`
  already strips unreferenced functions (measured: defining `MINIZ_NO_DEFLATE_APIS` changed the
  binary by 32 B). The one real waste was the recursive source glob compiling miniz's **example
  programs** (`miniz/examples/*.c`, each with its own `main()`); now excluded in the component
  `CMakeLists.txt`. Kept the full library source (read APIs intact; a future zip *backup* feature
  could need the writer).
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

✅ **Audited the image pipeline (alpha.14) — measured first, then fixed the real waste.**
   Per-stage Diag timing on device (stable across rounds): **TakeImage ~9.5 s** (of which ~5 s is the
   intentional `WaitBeforePicture` flash/exposure warm-up + camera capture + one JPEG decode),
   **CNN ~5.3 s** (dominated by tflite inference across the ROIs), **Alignment ~1.4 s**,
   PostProcessing/MQTT ~0.1 s each → **round ~16.5 s**. So the round time is dominated by *intentional*
   camera warm-up and *inherent* neural inference, not by image-processing waste.
   - **Per-cycle allocations — already pooled (measured: per-stage heap/PSRAM delta ≈ 0; heap steady
     ~93 KB, PSRAM ~508 KB free).** `rawImage` is allocated once in `ClassFlowTakeImage::ReadParameter`;
     each ROI's `image`/`image_org` once at config load (`ClassFlowCNNGeneral` getNetworkParameter) and
     reused via `CutAndSave`/`Resize`; the STBI decode and alignment `ImageTMP` use the shared PSRAM
     region. The only per-round `new`/`delete` are the small `ImageTMP`/`AlignAndCutImage` *objects*
     (pixels are the shared region) — negligible. The hypothesised pooling win was already done.
   - ✅ **Fixed the genuine waste: two per-byte copy loops → `memcpy`.** `CCamera::CaptureToBasisImage`
     copied the decoded ~921 KB frame `_zwImage`→`_Image` one byte at a time (index multiply per
     pixel); and `CAlignAndCutImage::CutAndSave(...,_target)` (run once per ROI, ~16×/round, ~820 KB
     total) did the same per byte. Both are identical-layout copies → replaced with a contiguous
     `memcpy` (full-frame) / per-row `memcpy` (ROI). Zero behaviour change.
   - ✅ **Alignment cost — DONE (alpha.14).** The full-frame rotate/shift + marker search is no
     longer paid every round: `AlignmentInterval=N` reuses the cached transform between full
     searches; a no-rotation dead-band skips the full-frame rotate when the angle is ~0; and
     `Crop`/`Mask` shrink the analysed frame. Validated on device (see §6.1). Remaining tie-in
     with FastRead §2 (re-align only on the full-validation pass) is now trivial via the interval.
   - **Resize/cut:** output buffers are reused (ROI `image`/`image_org` are persistent). `CutAndSave`
     now row-`memcpy`s; `Resize()` is bilinear into the persistent ROI buffer. Cost is small vs.
     inference. The remaining two `CutAndSave` overloads (file-save / `SendImage`) still loop per byte
     but are **cold paths** (SaveAllFiles / live view), not per-round — left as-is.
   - ✅ **JPEG decode happens once per capture.** `CaptureToBasisImage` does one
     `_zwImage->LoadFromMemory(fb->buf, fb->len)` (STBI software decode); no re-decode downstream
     (alignment/CNN read the decoded RGB `rawImage`). The per-round raw-image JPEG **log** is gated off
     unless `RawImagesLocation` is set, so no per-round re-encode. Note: on the **ESP32** (no hardware
     JPEG) any decoder is software; a faster software decoder (esp_jpeg/tjpgd vs STBI) is a possible
     future lever but unproven and risky — not pursued.
   - ✅ **Large buffers are in PSRAM.** rawImage, the shared region, STBI buffers and ROI buffers all
     allocate with `MALLOC_CAP_SPIRAM`; PSRAM free held steady at ~508 KB across rounds.
   - ✅ Measured first (per-stage Diag + `MEM-PROFILE`), then changed only the byte-loop copies.

### 6.1 Pipeline scope findings (2026-05-30) — grounds tasks 2/3 + alignment questions

**Where the full frame is actually touched (VGA 640×480×3 = `IMAGE_SIZE` 921,600 B):**

| Stage | Scope | Per-cycle cost | File |
|---|---|---|---|
| TakeImage (STBI decode) | full frame | ~1.54 MB peak shared-region (decode + crop planes) | `ClassFlowTakeImage.cpp` |
| **Alignment** | **full frame** | full `ImageTMP` copy (~921 KB) + 2-marker search + **full-frame rotate/shift** + overview draw | `ClassFlowAlignment.cpp` `doFlow` |
| CNN / Digitization | **ROI crops only** | per-ROI `CutAndSave` → `Resize` to model input (~32×20×3) → inference (arena_used ~28 KB) | `ClassFlowCNNGeneral.cpp` |

**Answer — "does analysis ever check the full image, or only individual regions?"**
The *recognition* (CNN) only ever looks at small per-ROI crops resized to the model input
([ClassFlowCNNGeneral.cpp:602-612](code/components/jomjol_flowcontroll/ClassFlowCNNGeneral.cpp#L602-L612)).
The only stage that touches the whole frame is **Alignment**: it copies the entire frame to
`ImageTMP`, finds 2 reference markers in their search windows, then rotates/shifts the *whole*
image so the ROIs land at fixed coordinates. Functionally it only needs (a) the two marker
search windows and (b) the ROI boxes — the full-frame copy+rotate is overhead.

**Q "alignment only needs a buffer around the reference image" — CONFIRMED feasible.**
Two independent reductions:
  - *Periodic alignment:* the marker offset drifts slowly. Cache the transform (dx,dy,angle),
    re-run the marker search only every N cycles (or on drift/on demand), reuse the cached
    transform on intermediate cycles. Skips the ~921 KB copy + full rotate + search on most
    rounds. This is the same idea as §2 "reuse cached transform; only re-align on the full
    validation pass," and pairs with FastRead. Alignment can already be turned fully OFF via
    `alignment_algo == 3`, so a "re-align every N rounds" interval is a natural extension.
  - *Localized alignment:* never materialize a full rotated frame — cut each ROI directly from
    the raw decoded image at the transform-adjusted position (cut-with-rotation per ROI). Drops
    the full-frame `ImageTMP`. The full rotated frame is only needed for the optional AlgROI
    overview JPEG (debug/SaveAllFiles), which can be gated behind that flag.

**Task 2 — crop + mask before analysis — feasible, biggest structural win:**
  - The meter occupies a fraction of the frame (confirmed on the downloaded `alg_roi.jpg`).
  - A configured crop (bounding box of markers + all ROIs) shrinks the *analyzed* footprint.
    Because the PSRAM region floor is `IMAGE_SIZE * 2` and `IMAGE_SIZE` would track the cropped
    dimensions, **cropping shrinks the shared region automatically** (see psram.cpp note) — on
    top of the CPU saving from copying/rotating fewer pixels.
  - Camera-level digital zoom already exists (sensor window). The new piece is a *software*
    post-capture crop driven by a GUI-set box in the reference/alignment phase.
  - Masking (zeroing non-ROI areas) does **not** shrink buffers (same dimensions) — its only
    value is alignment robustness against moving backgrounds. Lower priority than cropping.

**Task 3 — grayscale / single channel — NOT a drop-in for the CNN:**
  - The shipped models are **hardcoded 3-channel RGB**: input dim 3 = `im_channel`
    ([CTfLiteClass.cpp:107-109](code/components/jomjol_tfliteclass/CTfLiteClass.cpp#L107-L109))
    and the loader writes 3 floats/pixel R,G,B
    ([CTfLiteClass.cpp:176-188](code/components/jomjol_tfliteclass/CTfLiteClass.cpp#L176-L188)).
    Grayscale/single-channel CNN input requires **retraining** every dig/analog model *and*
    changing the loader. Out of scope for a firmware-only change.
  - However the *alignment* marker search does not need color — it could run on a 1-channel
    (luma) buffer, cutting the big alignment buffer to ~1/3. The CNN ROIs stay RGB (cut from
    the raw image), so model compatibility is preserved. This is the realistic grayscale win.

**Recommended priority (lowest risk → highest structural payoff):**
  1. **Periodic alignment** (cache transform, re-align every N rounds) — biggest CPU + transient
     memory win, reuses existing FastRead/`alignment_algo` infrastructure, no model/GUI change.
  2. **GUI crop box** (task 2) — shrinks both the PSRAM region (auto, via the IMAGE_SIZE floor)
     and per-cycle CPU; needs reference-phase UI + capture-time crop.
  3. **Localized / grayscale alignment** — drop the full-frame `ImageTMP`, run the search on luma.
  4. Masking and CNN-grayscale — deprioritized (masking: no buffer win; CNN-grayscale: retraining).

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
- ✅ **Flexible processing interval (sub-minute + unit dropdown).** Implemented:
   - UI: integer entry + **unit dropdown (seconds / minutes / hours / days)** on the Round Interval
     row (`edit_config_template.html`); `Interval` registered with `anzParam=2` and a back-compat
     unit default of `minutes` in `readconfigparam.js`.
   - Firmware (`ClassFlowControll::ReadParameter`): parses `Interval = <number> [unit]`, converts to
     minutes in the existing `float AutoInterval` (which already supports sub-minute), guards against
     a 0/negative value; a bare number stays minutes (back-compat). The auto-timer loop is unchanged
     (it already skips the delay when a round runs longer than the interval).
   - MQTT keep-alive/LWT timeout gets a 60 s floor so short intervals don't make it too aggressive.
   - Tooltip doc updated (`param-docs/.../AutoTimer/Interval.md`). Default stays **5 minutes**.
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

### 9.3 🟡 Adoption roadmap — agreed priority order (2026-05-30)
Work the IDF-6 opportunities in this order, keeping the device stable at each step:
1. **Newer toolchain** (GCC 15 / C++23-26). 🟢 Largely done. Toolchain active via IDF 6.0
   (xtensa-esp-elf **15.2.0**, `-std=gnu++26`).
   - ✅ Perf-critical components (`esp-tflite-micro`, `esp-nn`, `jomjol_image_proc`,
     `jomjol_tfliteclass`) now build at **`-O2`** (vs global `-Os`), applied post-`project()` so the
     vendored submodules need no edits. **Measured: CNN digitize ~5520 ms → ~5330 ms (~3.4% faster/
     round), +11 KB flash, numerics unchanged** (esp-nn conv kernels are already asm).
   - ⬜ Follow-up: modern-C++ hot-path idioms (`std::string_view`/`const&`, `constexpr`) per §5.
   - 💡 The real round-time lever is TakeImage (~9.8 s, dominated by `WaitBeforePicture` config, not
     CPU) — separate from the toolchain.
2. **Power management** (`esp_pm` DFS + tickless idle / light sleep). 🟡 Analyzed — needs hardware
   validation before defaulting (do **not** blind-flip on a camera you rely on).
   - **Current state:** `CONFIG_PM_ENABLE=y` already, but `CONFIG_PM_DFS_INIT_AUTO` and
     `CONFIG_FREERTOS_USE_TICKLESS_IDLE` are **off** → the PM framework is linked but does nothing.
     WiFi modem-sleep (IDF default `WIFI_PS_MIN_MODEM`) is the only active saver today.
   - **Camera-clock risk (the blocker):** XCLK is generated by LEDC with `LEDC_AUTO_CLK`
     (`ClassControllCamera.cpp`). DFS scales the APB clock; if LEDC derives XCLK from APB, the XCLK
     drifts when the CPU down-clocks → image artifacts → bad reads. The esp32-camera driver must
     hold an `ESP_PM_APB_FREQ_MAX` lock during capture for DFS to be safe; verify it does on v2.x.
     This is precisely why upstream kept PM in a separate `esp32cam-power-management` env.
   - **Architecture limit:** this firmware is *always-on* (web server + MQTT + ~per-interval round),
     so deep-sleep-between-rounds (the big saver) isn't compatible without a redesign (wake → round
     → publish → sleep, dropping the always-reachable web UI). DFS + automatic light-sleep keep
     connectivity but yield only modest savings on a Wi-Fi-connected device.
   - **Safe enabling path (opt-in, validate on hardware):** `esp_pm_configure({max=cpu_freq,
     min=80MHz, light_sleep_enable=false})` for DFS-only first; confirm CNN reads stay valid and
     images are clean across lighting, then consider `light_sleep_enable=true` +
     `CONFIG_FREERTOS_USE_TICKLESS_IDLE`. Expose as a config/build option, not a forced default.
     Needs a **power meter** to quantify benefit (not measurable remotely).
   - Pairs with the flexible interval (§7) and the per-target sdkconfig work (item 5).
3. **Wi-Fi** (roaming + connection stability). 🟢 Done for now.
   - ✅ **Exponential-backoff reconnect** (`connect_wlan.cpp`): replaced the "immediate reconnect ×10
     then 5 s" loop with first-few-immediate then 1/2/4/8/16 s capped at 15 s, resetting on a
     successful connect. Gentler on a down/unreachable AP, far less log spam, lower power during an
     outage. (Status-LED + per-reason logging preserved.)
   - ℹ️ **Roaming already scaffolded:** `WLAN_USE_ROAMING_BY_SCANNING` (scan-based, RSSI-triggered) is
     **on**; full **802.11k/v** (RRM/BTM via `esp_rrm`/`esp_wnm`) is present but **opt-in**
     (`WLAN_USE_MESH_ROAMING`, off) because it needs ~6–8 KB of the scarce **internal** RAM and only
     helps multi-AP/mesh deployments. Leave opt-in; revisit on the S3 (more internal RAM headroom).
   - 💡 802.11r (FT fast-transition) and lower WiFi static-buffer counts are possible but env-specific
     / RAM-throughput trade-offs — not pursued by default.
4. **Heap allocation** (TLSF allocator + PSRAM tuning). 🟡 Profiled on-device (alpha.9):
   - **Measured:** internal heap ~63 KB free (min 40 KB); **PSRAM ~150 KB free, largest block
     ~144 KB, min-free dipped to ~12 KB**. Per-round churn is small (hundreds of B – ~10 KB). The
     ~790 KB `alg_roi` overview path is *already* avoided (`ALGROI_LOAD_FROM_MEM_AS_JPG` on); the
     earlier crash was the null-deref fallback (fixed), not that allocation.
   - **Finding: memory is already well-architected, not wasteful.** The dominant consumer is a single
     **shared 2.1 MB PSRAM region** (`TENSOR_ARENA_SIZE` 800 KB + `MAX_MODEL_SIZE` 1.3 MB) allocated
     once and time-multiplexed between the tflite arena/model and the image (STBI) buffers; camera is
     `fb_count=1`. Those sizes are **worst-case** (largest supported model ~1.1 MB), so they can't be
     globally shrunk without breaking large-model users. Tightness is inherent to the ESP32's
     **~4 MB-mapped PSRAM** (the other 4 MB of the 8 MB needs himem/bank-switching).
   - ⭐ **Right-size the shared region to the *chosen* model — MEASURED (alpha.11/12).** Attempted the
     static cut; the on-device `MEM-PROFILE` data corrected the design:
     - `MAX_MODEL_SIZE` reduced to **512 KB** (largest shipped model 356 KB + margin) — correct & safe.
     - Tensor arena: in-use model needs only **28 KB of the reserved 800 KB**.
     - **TakeImage STBI peak = 1,536,046 B (~1.46 MB) at VGA**, and it **scales with camera
       resolution** (the JPEG decode), so it is not a fixed value.
     - **Key correction:** the region is bounded by the *image-decode step* (~1.46 MB), which already
       exceeds arena(800 KB)+model(512 KB)=1.31 MB → the model right-sizing **does not free PSRAM by
       itself**, and a *static* region cut isn't safe across resolutions (a higher-res config would
       crash on the NULL STBI alloc — which is exactly the alpha.11 boot loop). Region kept at the
       known-good ~2.1 MB for safety.
   - ✅ **Per-config boot-time sizing — DONE (alpha.13).** `reserve_psram_shared_region()` now sizes
     the region at boot to `max(TENSOR_ARENA_SIZE + MAX_MODEL_SIZE, 2 × IMAGE_SIZE)`. The camera
     output is fixed VGA (`FRAMESIZE_VGA`), so the image-step peak is deterministic — measured
     **1,536,046 B**, and the `2 × IMAGE_SIZE` (1.84 MB) floor sits ~20% above it. This replaced the
     static ~2.1 MB worst-case-model size, **freeing ~330 KB** (≈150 KB → ≈480 KB free), and the
     floor tracks `IMAGE_SIZE` so cropping the analysed area (alpha.14 §6) shrinks the region
     automatically. `MAX_MODEL_SIZE` also right-sized to 512 KB (largest shipped 356 KB + margin),
     with oversized models rejected gracefully at load. `MEM-PROFILE` logs retained.
   - ⬜ **Remaining safety net:** make the STBI NULL path degrade gracefully (failed round + clear
     log) instead of returning NULL → crash, so a future higher-res/larger-image config can't boot-
     loop the way alpha.11 did.
   - Other headroom levers: **himem** (upper 4 MB; complex) or the **ESP32-S3** (8 MB+ mapped). TLSF
     is already the IDF default allocator; no change needed there.
5. **Second target: ESP32-S3 — the one alternative to the ESP32-CAM (once items 1–4 are stable).**
   Scope is deliberately limited to **ESP32 (ESP32-CAM-class, i.e. any ESP32 *with PSRAM*) + ESP32-S3**.
   P4/C6/C3 are **out of scope** (C3/C6 verified non-viable — no PSRAM / no camera / no USB host; P4
   dropped to keep focus).

   **Board suitability — the only hard requirement is PSRAM** (the shared ~1.84 MB region holds the
   JPEG-decode buffer + the tflite arena/model; without PSRAM the firmware cannot run):
   | Board | Verdict | Notes |
   |---|---|---|
   | **ESP32-CAM (AI-Thinker)** | ✅ reference | ESP32-D0WDQ6 + PSRAM; the validated default (`BOARD_ESP32CAM_AITHINKER`). |
   | **ESP32-WROVER / WROVER-DEV/KIT** | ✅ **suitable** | Same `esp32` target — ESP32 **+ PSRAM**. It's effectively "an ESP32-CAM with a different GPIO map", so it needs **no new toolchain/binary type**, just a board pin map (`BOARD_WROVER_KIT` already exists in `defines.h`). Good **dev/test** board (USB-serial + breakout). ✅ **Build verified green this session** (1.75 MB) via the new `AIOTEDGE_BOARD` override. The OV2640 + SD must be wired to the selected map (the `BOARD_WROVER_KIT` default = the Espressif WROVER-KIT camera-header pinout; a hand-wired DevKit needs its own `CAM_PIN_*`/`GPIO_SDCARD_*`). |
   | **ESP32-WROOM (WROOM-32/32D/32E)** | ❌ **not suitable** | **No PSRAM.** The CNN + full-frame image buffers can't fit in the ~520 KB internal SRAM, so the digitization pipeline cannot run. (Only a non-standard PSRAM-equipped WROOM variant would qualify — the common WROOM modules do not have PSRAM.) |
   - **Board selection (native idf.py):** `AIOTEDGE_BOARD=<BOARD_*> idf.py -B build_<x> build` overrides
     the per-target default (`code/CMakeLists.txt`). Any board defined in `include/defines.h`
     (`BOARD_WROVER_KIT`, `BOARD_M5STACK_PSRAM`, `BOARD_ESP32CAM_AITHINKER`, `BOARD_ESP32S3_CAM`) works;
     all the PSRAM-ESP32 boards share the same `esp32` binary type.

   #### ESP32-CAM vs ESP32-WROVER — which to use when (dev board on hand, verified on hardware)
   Both are the **same silicon for this project**: classic ESP32 (dual-core LX6) **+ PSRAM + 4 MB flash**
   — so there is **no compute or memory difference**. The choice is purely form-factor / I-O / workflow:

   | | **ESP32-CAM (AI-Thinker)** | **ESP32-WROVER-DEV** |
   |---|---|---|
   | Camera | ✅ **integrated** OV2640 + FPC connector (no wiring) | ⚠️ none — hand-wire an OV2640 |
   | SD card | ✅ **on-board microSD slot** (4-bit SDMMC) | ⚠️ add a microSD module |
   | Flashing | ⚠️ needs an external USB-serial + IO0/reset jumper dance | ✅ **on-board USB-serial + auto-reset** (one cable) |
   | Free GPIOs | ⚠️ almost none (camera+SD use most pins) | ✅ **many broken out** (sensors, logic analyzer, JTAG) |
   | Flash LED | ✅ on-board high-power LED (GPIO4) | ⚠️ none (WROVER-KIT maps a small LED) |
   | Size / cost | ✅ tiny, ~$5, enclosure-friendly | ⚠️ larger DevKit, pricier |
   | Validation | ✅ **the reference platform** — defaults/models/tests tuned on it | ⚠️ needs a pin map + re-tune |

   **Verdict:** the **ESP32-CAM is the deployment/production board** (integrated, cheap, validated,
   fits a meter enclosure); the **ESP32-WROVER-DEV is the better *development/bench* board** (flash over
   one USB cable with auto-reset, exposed GPIOs for probing, easy to swap cameras). They produce the
   **same `esp32` binary** — only the GPIO map (and here the partition table) differs. *(8 MB/16 MB
   WROVER modules also exist and would relax the §8 flash-storage math — this dev unit is 4 MB.)*

   #### ✅ ESP32-WROVER single-app-slot (4 MB, no dual-OTA) — built + flashed + boot-tested this session
   - `code/partitions_wrover.csv` + `code/sdkconfig.defaults.wrover` (merged when
     `AIOTEDGE_BOARD=BOARD_WROVER_KIT`): **factory 2 MB** app (no `ota_0/ota_1/otadata`) + **`storage`
     spiffs 1.86 MB** reserved for the web UI + the **best-models** CNN set (the §8 flash-serving path —
     the best-models-only cut, **688 KB**, is what makes ~1.5 MB of content fit a 4 MB board). Trade-off:
     **no OTA** (re-flash over USB); the ESP32-CAM keeps dual-OTA `partitions.csv`.
   - **On-hardware boot test (the connected CH340 WROVER-DEV, `/dev/ttyUSB0`):** flashed the single-slot
     image; serial boot log confirms ✅ **PSRAM** (`Found 4MB PSRAM device` + `SPI SRAM memory test OK`,
     4082 KB pool), ✅ the single-slot **partition table active** (`coredump @ 0x210000`), ✅ clean boot
     on **IDF v6.0.1 / app v17.0.0-alpha.14**. The **OV2640 was not reached**: with **no SD card** the
     firmware fails the early SD R/W check (`0x107`) and **aborts init before camera bring-up** — so an
     SD card (or the §8 flash-storage feature, which must also stop the init from *requiring* SD) is the
     next prerequisite to validate the camera + a meter read on the WROVER.
   - **Each chip needs its own compiled binary** (Xtensa ESP32 vs Xtensa ESP32-S3 differ in ISA
     extensions + memory map; IDF builds per `set-target`). **One codebase**, per-target build configs
     + compile-time feature gating (`#if CONFIG_IDF_TARGET_ESP32S3 ...`) so the 4 MB ESP32-CAM is
     never bloated by S3-only features. OTA is cross-flash-safe (bootloader validates the image
     header `chip_id`), and the release/web-installer ships the right binary per board.
   - **Why S3:** verified IDF SoC caps — PSRAM (**8 MB+ mapped → ~2× heap headroom**, addresses the §4
     memory tightness), DVP **and** USB-OTG host (**USB-UVC camera support**), dual-core, Wi-Fi.
   - **🟡 Scaffolding done (this session):**
     - ✅ `code/sdkconfig.defaults.esp32s3` — 8 MB flash, **octal PSRAM @ 80 MHz**, 240 MHz CPU,
       custom partition table (auto-merged by IDF on top of `sdkconfig.defaults` for the s3 target).
     - ✅ `code/partitions_esp32s3.csv` — dual-OTA **3 MB** app slots (vs 1.9 MB on 4 MB), ~2 MB free
       for a future `web` LittleFS partition (§8).
     - ✅ `defines.h` `BOARD_ESP32S3_CAM` pin map (Freenove ESP32-S3-WROOM CAM DVP pinout; **SD pins
       marked verify-per-board**).
     - ✅ `code/CMakeLists.txt` selects the board by `IDF_TARGET` (esp32 → AiThinker; esp32s3 → S3),
       and the 4 MB ESP32-CAM build stays green (verified, binary unchanged).
   - **✅ S3 build is GREEN (this session).** `IDF_TARGET=esp32s3 idf.py -B build_s3 -D
     SDKCONFIG=build_s3/sdkconfig build` produces a flashable `build_s3/AI-on-the-edge.bin`
     (**1.71 MB, 46% free** in the 3 MB OTA slot), bootloader + partition table, on octal PSRAM /
     8 MB flash / 240 MHz. The shared Xtensa toolchain (`xtensa-esp-elf` esp-15.2.0) covers both
     esp32 and esp32s3 — no separate `install.sh esp32s3` was needed. Separate `build_s3/` dir so the
     esp32 build is untouched (**re-verified esp32 green, binary behaviour unchanged**). Only **two**
     target fixes were required (both target-independent, esp32 still builds):
       1. `connect_wlan.cpp` DNS — set the esp_netif `esp_ip_addr_t` union directly
          (`.ip.type = ESP_IPADDR_TYPE_V4; .ip.u_addr.ip4.addr = …`) instead of lwip's
          `ip_addr_set_ip4_u32` macro, which only compiles in IPv4-only mode (S3's default config
          enables LWIP_IPV6).
       2. `temperatureRead()` (`Helper.cpp`) — the ESP32-only ROM `temprature_sens_read()` doesn't
          exist on S3; gated `#if CONFIG_IDF_TARGET_ESP32` (ROM) vs the `temperature_sensor` driver
          (`esp_driver_tsens`, added to `jomjol_helper` REQUIRES off-esp32).
   - **⬜ Remaining (needs a real S3 board to validate on hardware):**
     - On-device bring-up: flash, then verify PSRAM octal init, **camera LCD_CAM** DVP init, SD pins,
       Wi-Fi/MQTT, and a meter read. Expect per-board pin fixes (`defines.h BOARD_ESP32S3_CAM`).
     - **Camera interface abstraction**: see §9.4 — wrap capture behind an interface with a **DVP**
       backend (esp32-camera, today) and a **USB-UVC** backend (S3 USB host). The big code item.
     - Per-board pin verification, S3 build in CI next to esp32, web-installer entry.

### 9.4 🟡 Camera support: more DVP sensors (OV3660 / OV5640) + USB-UVC cameras (ESP32-S3)
Goal: broaden beyond the OV2640 to the other DVP sensors **and** add USB-connected (UVC) cameras,
the latter only meaningful on the **ESP32-S3** (it has the USB-OTG host the classic ESP32 lacks).
Scope split: the two extra DVP sensors work on **both** ESP32 and S3 (shared `esp32-camera` DVP
driver); **USB-UVC is S3-only**.

#### 9.4.1 OV3660 / OV5640 (DVP) — partly here already, finish it
**Current state (verified in `code/components/jomjol_controlcamera/`):**
- ✅ Sensor auto-detect handles all three PIDs (`OV2640_PID` / `OV3660_PID` / `OV5640_PID`);
  anything else fails init with "Camera module is unknown" (`ClassControllCamera.cpp` ~L156).
- ✅ Per-sensor branches already exist for several controls (e.g. sharpness, the OV2640-only
  contrast/brightness/special-effect emulation in `ov2640_*.cpp`, OV5640/OV3660 cases in the
  effect/whitebalance paths) and a thorough `SENSOR_CAPABILITIES.md` documents the real
  driver-level ranges per sensor.
- ⚠️ **Gap — settings are clamped to the OV2640 envelope.** `ClassFlowTakeImage::ReadParameter`
  clips brightness/contrast/saturation to **±2** for *all* sensors, but the OV3660/OV5640 natively
  accept **brightness/contrast ±3** and **saturation ±4** (and native denoise 0..8 + native
  sharpness ±3, vs the OV2640's firmware emulation). So the extra sensors *run* but can't be tuned
  to their full range.

**🟡 To finish DVP multi-sensor support:**
- 🟡 Make the brightness/contrast/saturation (and AE-level, denoise, gain-ceiling) clamps
  **per-sensor**, keyed off `CCstatus.CamSensor_id` (the sharpness path is the existing template).
  **OV2640 ranges unchanged** (the bundled CNN models + defaults are tuned for it), so the live
  ESP32-CAM is byte-for-byte unaffected; only OV3660/OV5640 gain their wider envelope.
  - ✅ **Firmware done:** added `sensorClampLimit()` in `ClassFlowTakeImage.cpp` and applied it to
    **brightness/contrast (±2 OV2640 → ±3 OV3660/OV5640)** and **saturation (±2 → ±4)**. Verified the
    esp32 build stays green and the OV2640 limits are identical (the helper returns 2 for OV2640).
  - ⬜ Remaining: widen the matching UI min/max in `edit_config_template.html` (+ any client-side range
    checks) **conditioned on the detected sensor**, and extend the same per-sensor treatment to
    AE-level / denoise / gain-ceiling.
- **Frame-size / resolution:** capture is hardcoded `FRAMESIZE_VGA`. OV3660 (QXGA) / OV5640 (QSXGA)
  can deliver much higher res, but only the **S3** has the mapped PSRAM to decode it (the §4 STBI
  peak is `~1.67×IMAGE_SIZE` and the shared region floor is `2×IMAGE_SIZE`). Plan: keep VGA the
  default everywhere; expose a higher capture size **only when `CONFIG_IDF_TARGET_ESP32S3`** and let
  the existing crop (§6) keep the analysed area small. The CNN ROIs are downscaled to the model input
  regardless, so higher res only buys cropping headroom for small/distant meters.
- **Autofocus (OV5640):** the driver has a VCM AF path (`ov5640_af.c`) that is **not wired in**;
  leave fixed-focus for now, note it as a future S3 extra (needs the AF firmware blob + AF lens).
- Validate on hardware: an OV3660 and an OV5640 module each read a meter; capture per-sensor
  reference notes (exposure/ROI re-tune guidance) for the docs.

#### 9.4.2 USB-UVC cameras (ESP32-S3 only) — new backend
**Why S3-only:** UVC needs a **USB-OTG host**, which the classic ESP32 does not have; the S3 (and
S2) do. So this is gated `#if CONFIG_IDF_TARGET_ESP32S3` and never compiled into the 4 MB ESP32-CAM.
**Why it's wanted:** decouples the camera from the board (use a standard USB webcam / a longer cable /
a better lens/AF module), and many UVC cams output **MJPEG** which drops straight into the existing
JPEG-decode path.

**Building blocks:**
- Espressif **`usb_host_uvc`** managed component (`espressif/usb_host_uvc`) on top of the IDF USB
  host stack — add to `code/main/idf_component.yml` gated to the s3 target. (Pin a known-good
  version, like the mqtt/cjson deps in §3.6.)
- USB host needs a **5 V VBUS supply** to the camera and the OTG D+/D- pins; document per-board
  (the S3 dev boards differ). This is a hardware/power note for the board matrix, not code.
- MJPEG UVC frames → feed the existing `CImageBasis::LoadFromMemory` (STBI JPEG decode) unchanged.
  Uncompressed/YUY2 UVC streams would need a YUYV→RGB step (avoid by selecting an MJPEG format).

**The enabling refactor — a camera backend interface (shared by §9.3 item 5):**
- Define a thin `ICameraBackend` (capture-to-`CImageBasis` / capture-to-file / sensor-settings)
  and move today's `esp_camera_*` calls in `ClassControllCamera.cpp` behind a **DVP backend**
  (no behaviour change on ESP32/ESP32-CAM — this is a pure extract-interface step, buildable and
  testable on the current esp32 toolchain *before* any S3 hardware exists).
- Add a **UVC backend** implementing the same interface, compiled only for the s3 target.
- Selection: auto (probe DVP first, fall back to USB) or an explicit `[Camera] Interface = dvp|usb`
  config key; surface the active backend + sensor on the system-info / capabilities page.
- The camera-settings UI already keys off the sensor; for UVC, expose only the controls the UVC
  device actually reports (UVC exposes a different, device-dependent control set than the OV sensors).

**Staged tasks (lowest risk first):**
1. ⬜ **Extract `ICameraBackend` + DVP backend** from `ClassControllCamera.cpp`; keep the esp32
   build green and the OV2640 device byte-identical (interface extraction only). *(Doable now, no S3.)*
2. 🟡 **Per-sensor DVP ranges** (§9.4.1) — firmware clamps done (brightness/contrast/saturation);
   UI min/max + remaining controls (AE-level/denoise/gain-ceiling) still to do. *(Doable now, no S3.)*
3. ✅ **S3 build green** (§9.3 item 5) — flashable `build_s3/AI-on-the-edge.bin`, two target fixes
   (DNS/IPv6 + temperature sensor); esp32 build re-verified unchanged. Needs hardware bring-up next.
4. ⬜ **UVC backend** behind the interface, s3-gated; bring up an MJPEG USB cam, validate a meter read.
5. ⬜ Optional S3 capture-resolution option + OV5640 autofocus; CI + web-installer entries per board.

> Reminder: every camera change must keep the **OV2640 / ESP32-CAM** path unchanged (it's the
> validated reference and the overwhelming majority of installs). All new sensors/backends are
> additive and target/sensor-gated.
