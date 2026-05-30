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
- ⬜ **Eliminate filtered-out log string construction (bigger win).** Callers still build
  `"..." + std::to_string(x) + ...` before `WriteToFile` even when the level is filtered
  (~23 such calls in `ClassFlowCNNGeneral.cpp` alone, in the inference loop). Add a
  level-checking macro, e.g. `LOGD(tag,msg)` →
  `if (LogFile.getLogLevel() >= ESP_LOG_DEBUG) LogFile.WriteToFile(...)`, and convert the
  hot-path debug calls. Removes per-cycle heap churn in production.
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

⬜ **Dark mode option for the web UI.** Add a user-selectable dark theme (config page +
   main pages). Considerations:
   - Implement via a CSS theme (CSS variables / `prefers-color-scheme` with a manual
     override toggle), persisted in `localStorage` so it survives reloads.
   - Cover all served pages (`index.html`, `edit_config(.template).html`, `edit_reference`,
     log/overview pages) and shared stylesheets in `sd-card/html/`.
   - Keep it lightweight — these pages are served from the ESP32/SD card, so avoid heavy
     frameworks; plain CSS + a small toggle script.
   - Optional: expose a "Theme" setting so the choice can also be set server-side.
- ✅ FastRead config options added to the UI (see §1).
