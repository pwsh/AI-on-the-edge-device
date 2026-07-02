# [17.2.0] - 2026-07-02

### General

- **ROI Auto-tune moved into the firmware — faster, smarter, fragmentation-free**: the digit-ROI
  Auto-tune is now a single device-side search (`/editflow?task=autotune`) instead of ~17 sequential
  web requests. The CNN model is loaded **once** for the whole search and candidate boxes are cut
  **in memory** from the aligned frame, so a full run evaluates **~100 positions/sizes in ~3 s**
  (previously ~17 boxes in ~25 s) with no PSRAM churn. The search also scores candidates by
  **logit margin** (winner vs runner-up), which keeps ranking boxes after the softmax confidence
  saturates at 100 %, resolves near-ties to the **center of the tied plateau** (the box lands
  mid-digit instead of at the edge of the acceptable region), and **locks onto the digit identity**
  found in the coarse pass so the box can't drift onto a neighbouring digit or a high-confidence
  blank ("N"). Search pattern: 7×7 coarse position grid (±6 px) → 5×5 center-preserving size sweep
  (±4 px) → 5×5 fine position pass. Processing still pauses for the search and stays paused
  (use **Resume processing**). Digit ROIs only.
- **XCLK (camera clock) and colorbar as runtime settings**: new `[TakeImage]` `CamXclk` (6–20 MHz)
  and `CamColorbar`; XCLK changes re-initialise the camera so the sensor timing actually follows.
- **OV3660/OV5640 native night mode** (`CamNightMode`, default on): lets the auto-exposure drop the
  frame rate in low light for longer integration (no effect on OV2640).

# [17.1.2] - 2026-06-26

### Reliability

- **Meter Type alone no longer flags normal rates as "exceeds physical max"**: the physical rate-rejection
  ceiling is now strictly **opt-in**. Selecting a Meter Type (`Utility = water`/`gas`/`electricity`)
  *without* setting a pipe diameter (or electrical service rating) previously fell back to built-in
  **residential defaults** (a 1″ water pipe with m³ scaling) and derived a ~0.9&nbsp;units/min ceiling —
  rejecting ordinary readings as `Rate exceeds physical max` until the interval stretched enough to drop
  the per-minute rate under it (so the same read got flagged repeatedly at, e.g., 2.94 → 1.47 → 0.98).
  Those defaults are now used for **prediction only**; **rejection requires an explicitly configured**
  pipe diameter / service-amps (or a user MaxRate). My earlier "unset pipe = no ceiling" guard didn't
  catch this because the *default* diameter is non-zero (25.4&nbsp;mm), so it never read as unset.

# [17.1.1] - 2026-06-26

### Reliability

- **ROI Auto-tune no longer exhausts PSRAM**: the digit-ROI Auto-tune search now runs a short **sequential
  1-D pass** (optimise Y, then X, then size — ~17 candidate boxes) instead of a full 2-D grid (~74). The
  long grid search allocated and freed image/JPEG-decode buffers fast enough to **fragment PSRAM** until
  the largest free block collapsed and allocations failed (`Failed to allocate … STBI`, `reference.jpg …
  corrupted`) — and could take a concurrent round down with it. Auto-tune now also **refuses to start when
  free PSRAM is already fragmented** and continues to leave processing paused (use **Resume processing**)
  so a resumed round can't collide with the search. Verified: the largest free PSRAM block now holds steady
  (~3&nbsp;MB) across a full search instead of collapsing to ~160&nbsp;KB.

# [17.1.0] - 2026-06-26

### General

- **External LED brightness + 5V current budgeting**: the external WS281x (NeoPixel) strip now has an
  explicit **output % control** (`LEDBrightness`, 0–100). The firmware estimates the strip's peak 5V draw
  (≈ 60&nbsp;mA per LED at full white, scaled by colour and brightness) and **automatically dims it to stay
  under the board's safe budget (500&nbsp;mA)** so a long/bright strip can't brown out the board. A new
  **5V power-injection** toggle (`LEDPowerInjection`, off by default) lets you raise that cap to your own
  injected supply's rating (`LEDMaxCurrent`, mA) when the strip is powered separately. The config page
  shows a live "estimated peak current vs budget" readout that warns (red) when the strip will be dimmed.
- The internal flash LED's existing 0–100&nbsp;% intensity is now labelled **"internal flash LED"** to
  distinguish it from the new external-strip control. Defaults preserve previous behaviour
  (`LEDBrightness=100`, injection off).
- **Perceptual (gamma) brightness**: the external-LED brightness % now follows a gamma curve so the
  number tracks *perceived* brightness (20&nbsp;% looks dim, not ~45&nbsp;% as raw PWM did). Applied to
  the flash, stage colours and the onboard RGB. Brightness can be changed **live** (`/ledbrightness`).
- **External LED on any GPIO** via a new **`LEDPin`** data-pin field, with a board-specific default
  (ESP32-S3 → 21, ESP32-CAM → 12) and an **`ExternalLED`** master enable. The legacy
  `IOxx = external-flash-ws281x` mechanism is retired from the UI (auto-migrated to `LEDPin` on load).
- **Camera/SD-pin guard**: the firmware refuses to drive an LED/flash on a camera or SD pin (driving one
  broke image capture and crash-looped the device), logging a clear error instead.
- **ESP32-S3 onboard RGB (GPIO48)**: a board-aware **`OnboardLED`** enable/disable toggle; the GPIO4
  flash-LED block is hidden on the S3 (GPIO4 is a camera pin there). The GPIO config section is now
  labelled **"LED Configuration"** with each LED enabled on its own.
- **Per-LED on/off control of the external strip**: a new toggle grid (rendered as a **line, grid or
  circle**, `LEDMask`/`LEDLayout`/`LEDLayoutCols`) lets you turn individual WS281x pixels on or off, on
  both the config page and the camera-setup page. Each toggle drives the hardware **in realtime** via a
  new `/ledstate` endpoint (no reboot), so capture-flash, status colours and the always-on mode all
  honour the mask. The **5V power budget now counts only ENABLED LEDs** (not the strip length), and the
  config page's "estimated peak current" readout updates live as you toggle.
- **Always-on external LED** (`LEDAlwaysOn`, off by default): drives the strip continuously at
  `LEDColor`, overriding the per-stage status colours, keeping it lit through capture, and — because the
  scene is already lit — **skipping the pre-capture flash/settle delay**.
- **Alignment-grid overlay on the camera-setup / live-stream page**: an optional SVG grid (off / 3×3 /
  4×4 / 8×6, plus a centre cross) over the live image to help line up the camera and ROIs.
- **External LED strip no longer goes dark after a `doInit`** (e.g. when saving ROI edits): the
  persistent WS281x driver is now torn down and rebuilt on the reconfigured GPIO, instead of holding a
  dead RMT binding that silently stopped lighting the strip.
- **Configurable CPU frequency** (`CPUFrequency`, **80 / 160 / 240 MHz**): trade speed for power on any
  board. A boot bug is fixed so the configured value now actually applies — the ESP32-S3 boots at
  240 MHz, so a configured 160 was previously ignored there. On the **ESP32-S3** a new runtime
  **`DynamicFrequencyScaling`** option (off by default) down-clocks to 80 MHz when idle and back up under
  load; it is force-disabled on the ESP32-CAM, where the camera clock is APB-tied and scaling would
  corrupt captures.

### Recognition & accuracy

- **Confidence-gated FastRead cache**: a digit read is only cached for reuse when its confidence clears
  the history floor, so a single low-confidence misread can no longer be cached and replayed for the
  rest of the FastRead interval — it is simply re-inferred next round.
- **Temporal voting for low-confidence in-range digits**: when a digit reads 0–9 but below the
  confidence floor and the recent history has a stable majority, the reported value is corrected to that
  majority; a genuinely rolling digit has mixed history, so a real change is never masked.
- **`DigitConfidenceThreshold`** (new, `[Digits]`, default 0 = off, wired into the config page): a
  class-based-digit confidence floor — a read below it is marked unknown ("N") and resolved from
  confident history + carry physics rather than committing a shaky value (the class-model analogue of
  `CNNGoodThreshold`).
- **High-sensitivity FastRead by default**: the change-detection default (`FastReadThreshold`) is now
  **5 (High)** instead of 8 — a wrong "changed" decision only costs one extra inference, so erring
  sensitive avoids skipping a real digit change.
- **Faster recognition**: a purpose-built bilinear ROI→model-input downscale replaces the generic stb
  resizer on the hot path, and the tflite model is now **loaded lazily** — a fully-cached FastRead round
  does zero model load and skips the per-round model SD read.
- **Faster alignment**: the template-match search drops the per-pixel `pow()` for an integer `d*d` and
  adds branch-and-bound (abandon a candidate once it can't beat the current best). The match result is
  identical; on the ESP32-CAM this cut the alignment step (≈85 % of every round) and brought a ~22 s
  round down to roughly 5 s.
- **Rate-limit: an unset pipe diameter no longer caps the rate at 0 flow**: with no pipe diameter /
  service amps configured, the physical-maximum ceiling is treated as *unknown* instead of being
  computed from a zero, so valid readings are no longer rejected as "exceeds physical max".
- **Pipe-diameter override is now opt-in**: selecting a Meter Type still suggests a Maximum Rate but no
  longer force-enables (and saves blank) the raw pipe-diameter / service-amps override — an untouched
  override stays commented out so the firmware uses its own default.
- **Confident reads can override a rate-limit rejection**: when the last 3 reads of a sequence all meet
  `DigitConfidenceThreshold`, a large jump is accepted (and logged) instead of being clamped back, for
  both the physical-max ceiling and the user `MaxRateValue` check. Inert when the threshold is off, and
  it never fires for analog-only sequences or in the first rounds after boot.

### Connectivity & security

- **Captive portal in setup/AP mode**: a small DNS responder + DHCP DNS option make phones/laptops pop
  the "Sign in to network" page straight onto the Wi-Fi setup form when connected to the `AI-on-the-Edge`
  access point. STA operation is unaffected.

### Web interface

- **Consistent config controls**: all enable/disable dropdowns read `enabled`/`disabled` (no raw
  `true`/`false`), and tooltips/labels match the controls and the current `LEDPin`.
- **Fixed Meter Type**: the Thermometer **°F** and **K** options were both mapped to Celsius
  (`temperature_c`) — they now correctly select `temperature_f` / `temperature_k`.
- **Safer Save**: a setting whose stored value is no longer a valid option is no longer nagged on every
  load; instead Save warns once and **comments it out** (keeping the old value) so it can't break the
  config. A stale-cache "unknown parameter" is a quiet console note, not a red alert.
- **Download a folder as a ZIP**: the web **file server** can now download a whole directory (including
  its sub-folders) as a single ZIP — a **"Download folder as ZIP"** button in the listing header, and a
  per-folder download icon on each sub-folder row (`GET /fileserver/<dir>/?zip=1`). The archive is built
  on-device with the existing miniz writer and streamed as `<foldername>.zip`; `wlan.ini` is always
  excluded.
- **Reworked ROI editor**: the digit and analog editors swap the name dropdown for a row of clickable
  **numbered chips** and **auto-name** ROIs (`<sequence><position>`), so you no longer manage ROI names.
  You can now **pick the digit/analog model** right on the editor screen (a configured model missing
  from the SD card is preserved as a `(missing)` option), edit the **Decimal Shift** inline with a live
  multiplier preview, and the **layout-lock preferences** (lock aspect ratio / synchronize / keep
  equidistance / spacing) are remembered across visits.
- **Apply ROI / sequence edits without a reboot**: saving the digit or analog editor now re-inits the
  processing flow via `/doinit` (~1–2 s, round-safe under the flow lock) instead of requiring a full
  reboot, falling back to a reboot prompt if the live re-init fails.
- **Auto-tune a digit ROI**: a new **Auto-tune ROI** button pauses processing, captures and aligns a
  single fresh image, drops the layout-lock constraints, and searches nearby positions then sizes
  (reusing the per-ROI examine test against that one static capture) for the box the CNN reads with the
  **highest confidence** — ignoring blank `N`/no-digit reads (which the model can report at ~100&nbsp;%)
  so the box can't drift off the digit. It applies the winning box for you to review and Save, and
  deliberately **leaves processing paused** (a new **Resume processing** button restarts it) so repeated
  tuning passes can't collide with a running round.
- **Persisted system settings**: **CPU Frequency**, **Dynamic Frequency Scaling**, **Backup Interval**,
  **Time Server** and **Hostname** now reliably save (some previously needed an easily-missed enable
  checkbox or had no save wiring at all). **Hostname** is now a non-expert field, and several
  per-sequence Meter Type / leak-detection settings that were silently dropped now load and save.
- **More robust Save**: one unknown/stale parameter no longer aborts the whole Save (so every other
  setting still saves); the **Meter Type (Utility)** select is no longer greyed-out/unselectable on
  load, and selecting a Meter Type auto-enables its matching sub-field (pipe diameter / service amps).
- **Capture controls**: **`WaitBeforeTakingPicture = 0`** now disables the pre-capture flash/settle
  delay (it used to be forced back to 2 s); explicit **Save Raw Images** and **Save ROI Images** on/off
  toggles (both default **off**) put per-round image logging under direct control regardless of any
  configured location; and the **overview** gains a **"Show ROI overlay boxes"** toggle (remembered per
  browser) to view the clean aligned image.
- **Tooltip readability**: config-page help popups now use dark text (no longer light-grey on white),
  hide the missing-font glyph icons that rendered as empty boxes, and sit **above the sticky Save bar**.

### Reliability

- **A stuck SD card can no longer wedge a reboot**: if a round blocks on a slow/failing SD write while
  holding the SD lock, the reboot path used to block too — it writes a `reboot.txt` marker and flushes
  the log to the card *before* `esp_restart()`, so those calls hung on the held lock and the device
  stayed frozen until it was physically power-cycled. A reboot **failsafe** now forces the restart after
  a hard timeout regardless of what the reboot path is blocked on, so a watchdog/software reboot always
  completes and a marginal SD degrades into an auto-recovering reboot instead of a brick.

# [17.0.0] - 2026-06-06

> **Production release of 17.0.0.** The ESP-IDF 6.0 migration, ESP32-S3 support and the full v17
> feature set are now considered stable. This consolidates everything from the alpha and rc.1
> entries below, plus the integrations work and the items in this entry.

### General

- **Faster first reading after boot**: the post-boot stabilisation delay before the first round was
  reduced from 10&nbsp;s to **3&nbsp;s**.

### Integrations

- **Data Publishing page** (`publishing.html`): a per-platform matrix to choose exactly which parameters
  are published to **MQTT, InfluxDB and Home Assistant**. Every currently-sent field can be turned off,
  and fields the device already gathers but never sent are now opt-in exposed (per-sequence **previous
  value** and **recognition confidence**; **raw/rate** for InfluxDB). Defaults match the previous
  behaviour exactly, so existing devices are unchanged until a parameter is toggled.
- **"Only send changed readings"** mode (toggle on the main config screen + the Data Publishing page):
  when a sequence's value is unchanged since the last publish, its reading topics are skipped; leak
  state and device/diagnostic topics still send every round. Reduces MQTT/InfluxDB traffic.
- Selection is stored in its own `/sdcard/config/publishing.cfg` (not `config.ini`, which the web config
  editor would otherwise strip). Turning a Home Assistant parameter off removes its entity via an empty
  retained discovery message.
- **Home Assistant** discovery now also covers per-sequence **recognition confidence** and **previous
  value**, plus device **analysis type / digits analysed / digits total**. An HA entity reads its state
  from the matching MQTT topic, so enabling a field for Home Assistant keeps its MQTT topic on (the page
  mirrors this when you tick an HA box).
- **Home Assistant units/device-classes**: *free memory* gained `data_size` (so HA offers B / kB / MB
  conversion) and *interval* gained `duration`; the other entities already carried a unit where one applies.
- **`raw` and `json` are now off by default** on all platforms (bulky, rarely needed) — re-enable per
  platform on the Data Publishing page.

### Reliability

- Full **zip-OTA** now reliably deploys the bundled web UI (the on-device unzip + folder swap were
  fixed); release-build hygiene cleaned up so binaries report a clean version.

# [17.0.0-rc.1] - 2026-06-04

> First **release candidate** for 17.0.0. The ESP-IDF 6.0 migration and the alpha feature set are
> considered feature-complete; this RC focuses on camera-recovery robustness and the ROI-editor
> tooling. Validate on your own meter before relying on it in production.

### Camera

- **The PWDN hardware reset now actually runs.** `PowerResetCamera` guarded its power-down toggle
  with `#if CAM_PIN_PWDN == GPIO_NUM_NC`, but `CAM_PIN_PWDN` expands to a `gpio_num_t` *enum* — which
  the C preprocessor evaluates as `0` — so the test was always true and the entire reset was compiled
  out on **every** board, including AI-Thinker (PWDN = GPIO 32). A wedged OV2640 therefore always
  needed a physical power cycle. The guard is now a runtime check so the reset fires, and boot
  pre-drains the sensor with escalating PWDN cycles plus a settle delay before the first init probe.

### ROI editor

- **On-demand "Examine selected ROI"** (digit + analog screens): cuts the selected ROI and runs the
  CNN on the spot, showing the analysed image, reading and per-digit confidence inline. Placed
  directly under the *Move ROI Higher/Lower* buttons.
- **"Pull fresh camera image"**: captures + aligns a fresh frame and shows it in the canvas in place
  of the stored reference, so you can judge whether the reference image / alignment is still accurate.
  Examine then analyses whichever image is on screen.
- Hardened: a failed image cut no longer panics the web-server task; the fresh examine reads the live
  aligned frame from PSRAM (it is not normally written to SD).

### Overview

- **Confidence display**: per-sequence confidence above each digit matrix and below each Value reading.
- **Fixed: the main values could appear frozen.** `/value`, `/statusflow`, `/digit_matrix` and `/info`
  do not send `Cache-Control`, so the browser could serve cached responses while the device kept
  returning fresh readings. The overview now cache-busts every live-data request.

# [17.0.0-alpha.14] - 2026-05-30

> :warning: **Alpha release.** Contains a major toolchain migration (ESP-IDF 6.0) and new,
> still-experimental features. Not recommended for production meters yet.

### Since alpha.13 — image-pipeline efficiency (alignment)

Three opt-in optimisations to cut the per-round image-processing footprint. Recognition (the CNN)
already only ever analyses small per-ROI crops; the whole-frame cost lives entirely in the
alignment step (a full-frame copy + reference-marker search + rotate, run every round). These target
exactly that.

- **Periodic alignment (`[Alignment] AlignmentInterval = N`).** Runs the expensive reference-marker
  search only every Nth round and re-applies the cached transform (offset + angle) in between — the
  camera framing is stable between captures. Default `1` = legacy behaviour (search every round).
  Cuts the marker-search cost on `(N-1)/N` of rounds. GUI: Expert → Alignment.
- **No-rotation fast path.** When the computed alignment angle is below a small dead-band
  (`ALIGNMENT_ROTATION_DEADBAND_DEG`, 0.05°) — the norm for a rigidly mounted camera — the full-frame
  rotate pass is skipped (translation alone aligns the frame), saving a whole-image transform.
- **Single-channel marker search** is the default search mode (`AlignmentAlgo = Default`, R-channel
  only); the CNN models remain 3-channel RGB (changing them would require retraining).
- **Crop (`[Alignment] Crop = x y w h`).** Restricts analysis to a sub-rectangle of the capture; the
  frame is repacked to the crop and all marker/ROI coordinates are shifted automatically. Shrinks the
  alignment working buffers + per-round CPU + logged-overview size. (The JPEG-decode peak that sets
  the PSRAM region floor is unchanged — that needs sensor windowing — but the floor tracks the
  analysed size, so a crop reduces it.) Not compatible with *Flip image*. GUI: Expert → Alignment.
- **Mask (`[Alignment] Mask = x y w h`, repeatable).** Blanks rectangles (to white) before analysis
  to cut image complexity and spurious marker matches in non-meter areas. Config-file + round-trips
  through the Web config editor (a visual box editor is a planned follow-up).

### Since alpha.12

- **PSRAM: boot-time region sizing keyed to the analyzed-image footprint (~330 KB freed, safely).**
  Verified the camera output is fixed VGA (`FRAMESIZE_VGA`, not config-driven), so the image-decode
  step is deterministic — the on-device measurement showed the TakeImage peak is **1.54 MB** (≈1.67×
  `IMAGE_SIZE`). The shared region is bounded by that image step (not the model), so its floor is now
  `2 × IMAGE_SIZE` = 1.84 MB (20% headroom over the measured peak) instead of the old 2.10 MB worst-
  case-model size. Frees **~330 KB** of PSRAM (≈150 KB → ≈480 KB free). The floor tracks `IMAGE_SIZE`,
  so a future crop/mask of the analyzed area (smaller `IMAGE_SIZE`) shrinks the region automatically.

### Since alpha.10

- **PSRAM: right-size the model buffer to the shipped models.** `MAX_MODEL_SIZE` reserved 1.3 MB for
  a worst-case model, but the largest model actually shipped is `dig-class11_1701_s2.tflite` = 356 KB
  → reduced to **512 KB** (largest shipped + margin; a larger custom model is still rejected
  gracefully at load). Correct and safe, but see the finding below.
- **Added `MEM-PROFILE` instrumentation** (`arena_used_bytes` per model, TakeImage STBI high-water
  mark) and measured on-device. Two findings: the in-use model needs only **28 KB of the 800 KB**
  tensor arena, and the **TakeImage STBI peak = 1.46 MB at VGA** (it scales with camera resolution).
- **Honest result: the model right-sizing does *not* free PSRAM on its own.** The shared region is
  bounded by the image-decode step (~1.46 MB), which already exceeds arena(800 KB)+model(512 KB)=
  1.31 MB, so the region stays at the known-good ~2.1 MB size (kept for resolution-safety). A static
  region cut isn't safe across camera resolutions — actually reclaiming PSRAM needs **per-config
  boot-time sizing** (measure model + image-step need at boot), tracked in PLAN §9.3 item 4.
- **⚠️ Process note (alpha.11):** an earlier attempt sized the region from a single 921 KB sample and
  undersized it → `psram_reserve_shared_stbi_memory` returned NULL → boot loop; and firmware-only OTA
  zips (no `html/`) corrupted `/sdcard/html` via the update's folder-rename. Both fixed; lesson:
  always deploy a **full** update zip, and size memory from the *peak*, not a single sample.

# [17.0.0-alpha.10] - 2026-05-30

> :warning: **Alpha release.** Contains a major toolchain migration (ESP-IDF 6.0) and new,
> still-experimental features. Not recommended for production meters yet. On-device testing
> (camera capture, CNN inference, MQTT/InfluxDB, mDNS, SD-card) is still pending.

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v16.1.0...v17.0.0-alpha.10)

### Since alpha.8 (IDF-6 adoption roadmap)

- **Toolchain perf:** the CNN-inference + image components build at `-O2` (vs global `-Os`).
  Measured **CNN digitize ~5520 ms → ~5330 ms (~3.4% faster/round)**, +11 KB flash, numerics
  unchanged.
- **Wi-Fi reconnect: exponential backoff.** Replaced "immediate reconnect ×10 then 5 s" with
  first-few-immediate then 1/2/4/8/16 s capped at 15 s (resets on connect) — gentler on a down AP,
  far less log spam and power during an outage.
- **Power management: Dynamic Frequency Scaling as an opt-in** (`ENABLE_DYNAMIC_FREQ_SCALING` in
  `defines.h`, **off by default**) — CPU down-clocks to 80 MHz when idle (no light sleep). Needs
  hardware validation (DFS scales APB → camera XCLK can drift).
- **ESP32-S3 target scaffolding** (config / partitions / board pin map; one codebase, per-target
  binary). ESP32-CAM build unchanged. P4/C6/C3 ruled out (no PSRAM/camera/USB host).

### :bug: Fixes since alpha.2

- **Crash fixed: null-pointer dereference serving `alg_roi.jpg`.** `GetJPGStream` checked
  `flowalignment` but then dereferenced `flowalignment->ImageBasis->ImageOkay()` without
  null-checking `ImageBasis` (4 sites). Loading the info/overview page before alignment had run
  (or when the image buffer couldn't be allocated) panicked the device. It now falls back to an
  empty response instead.
- **More robust camera init.** The boot-time camera initialization now retries up to 3 times
  (was 2) with a power-down reset between attempts, to recover a sensor left stuck after a
  software reset. (A software reset does not power-cycle the camera, so a physical power cycle may
  still be needed in the worst case — the error message now says so.)

- **SoftAP restored.** `ENABLE_SOFTAP` (the Wi-Fi setup access point shown when no `wlan.ini` is
  present) was also lost in the build migration. Re-enabled; the blocker was a stale unused
  `protocol_examples_common.h` include in `softAP.h`, now removed.
- **Hot-path logging perf.** New guarded `LOGD()` macro only builds debug message strings when the
  log level is DEBUG; the 32 such calls in the CNN inference loop no longer churn the heap each
  cycle at the default INFO level. Removed the unused `protocol_examples_common` component dir.

- **MQTT / InfluxDB / Webhook were entirely disabled (migration regression).** The feature flags
  `ENABLE_MQTT`, `ENABLE_INFLUXDB`, `ENABLE_WEBHOOK` (and the MQTT SSL flags) were only provided by
  `platformio.ini build_flags`, which the native ESP-IDF (`idf.py`) build does not read — so those
  flow steps were compiled out and the device never published anything (no Home Assistant / MQTT
  updates, no InfluxDB, no webhooks). They are now set as **global IDF compile definitions** in
  `code/CMakeLists.txt`. Also fixed a stale `NumberPost::ErrorMessage` reference in the webhook code
  that this exposed. (Diagnostics like uptime/RSSI are published by the MQTT step unconditionally;
  only the meter reading is withheld on a consistency error, per "Skip Messages on Error".)
  `ENABLE_SOFTAP` is still pending (needs `protocol_examples_common` wired into the component).
- **Config pages no longer get stuck on a stale cached version.** HTML pages were sent with
  `Cache-Control: max-age=43200` (12 h); a plain browser refresh (which doesn't carry the
  `?v=<hash>` cache-buster) could keep serving an old page after an update. HTML/HTM now use
  `no-cache` (revalidate); versioned assets (js/css/images) keep the long cache.

- **Status LED colour pickers showed black on load.** The pickers are synced from their hidden
  R/G/B inputs by `syncStatusColorsFromValues()`, which was only called on the save path, not when
  the config page loads — so the pickers defaulted to black even though the stored values were
  correct (the LED worked, and values reappeared after a reboot/save). The sync now also runs at
  the end of `UpdateInput()` (load path), so the pickers show the right colours immediately.

- **Status LED config was greyed out / mis-written.** The `StatusLED` enable was registered as a
  per-number parameter (`_isNUMBER=true`), so it was written to `config.ini` as
  `main.StatusLED` / `rate.StatusLED` and its UI control stayed disabled when absent from an
  existing config. It is now a global `[GPIO]` parameter and is force-enabled on load (like the
  LED/camera params), so the toggle and the seven stage colour pickers are editable.

### Known issues
No software is perfect. We know that our software has some quirks. If you have an issue, please first check the [issues](https://github.com/jomjol/AI-on-the-edge-device/issues) and
[discussions](https://github.com/jomjol/AI-on-the-edge-device/discussions) before reporting a new issue.

### :rocket: New Features

- **FastRead — incremental digit reading** (experimental, default **off**). To support much
  shorter update intervals (target 5–10 s), the CNN now only re-runs inference on digit ROIs
  whose cropped image actually changed since the last reading; unchanged digits reuse their
  previous result. The tflite model is kept resident across cycles (skips the per-cycle
  load/allocate). A full re-read of all digits is forced periodically and on demand. Only
  affects digital ROIs (`Digit` / `dig-class100`); analog ROIs are always read in full.
  - New config options (under `[Digits]`): `FastRead`, `FastReadThreshold`,
    `FastReadFullInterval`. See the new parameter docs.
  - :information_source: No web-UI controls yet — configure via `config.ini`.
- **"Skip Messages on Error" now works** (`PostProcessing` → `ErrorMessage`). This option was
  exposed in the UI and documented but had **no effect** in firmware. It is now implemented:
  on a consistency-check rejection (negative rate / rate-too-high), `true` (default) skips the
  transmission for that reading (empty value to MQTT/InfluxDB/REST), and `false` transmits the
  **last valid value** instead. The default init was corrected (`false` → `true`) to match the
  documented/shipped default, and the contradictory parameter doc was rewritten.

### :building_construction: Build System — migrated to ESP-IDF 6.0.1 (from 5.3)

> :warning: There is currently no PlatformIO platform that ships ESP-IDF 6.0, so the build was
> validated with the native `idf.py` toolchain. The project is a standard IDF project; CI and
> board-selection (currently in `platformio.ini`) will need to move accordingly.

- esp-mqtt and cJSON were removed from ESP-IDF core in 6.0 and are now pulled as managed
  components (`espressif/mqtt`, `espressif/cjson`); added `code/main/idf_component.yml`.
  `REQUIRES json` → `cjson` in the MQTT and webhook components.
- ESP-IDF 6.0 enforces include↔requires consistency: added explicit `esp_driver_*`
  (`gpio`, `rmt`, `spi`, `ledc`, `sdmmc`, `sdspi`), `mdns`, `esp_wifi`, `nvs_flash`, etc.
  to the affected component `CMakeLists.txt` files and to `main`.
- API/source updates for 6.0 + GCC 15:
  - `esp_vfs_fat_register()` now takes a config struct → `esp_vfs_fat_register_cfg()`.
  - private `sdmmc_common.h` → public `sdmmc_cmd.h`.
  - `HSPI_HOST` → `SPI3_HOST`; dropped `SOC_RMT_*` caps → ESP32 fallbacks;
    `rmt_tx_channel_config_t::intr_priority` added.
  - `WIFI_REASON_NOT_AUTHED` → `WIFI_REASON_ASSOC_NOT_AUTHED` (version-gated).
  - GCC-15 fixes: cross-enum comparison cast, transposed `calloc` args.
- Native-build board define + `-Wno-error` injected via CMake for non-PlatformIO builds.
- **Critical SD fix:** the redundant `esp_psram_init()` in `app_main` (PSRAM is already
  initialized at boot) re-maps PSRAM on IDF 6.0 and corrupted the already-mounted SD card's
  FATFS state (config became unreadable, device couldn't start). Now guarded with
  `esp_psram_is_initialized()`. Verified on hardware: boots, reads config, camera + CNN run.

### :package: Dependency / Component Updates (all bumped to latest)

- esp32-camera `v2.0.6` → **`v2.1.6`**
- esp-tflite-micro `v1.3.1` → **`v1.3.5`** (fixes `std::is_pod` removal under `-std=gnu++26`)
- esp-nn `v1.1.0` → **`v1.2.0`**
- esp-protocols / mdns `v1.4.3` → **`v1.11.1`**
- New managed components: `espressif/mqtt`, `espressif/cjson` (+ transitive `esp_jpeg`,
  `ethernet_init`)

### :zap: Performance & Memory

- Logging hot path: `ClassLogFile::WriteToFile` now takes `tag`/`message` by
  `const std::string&` (was by value → 2 copies/call) and defers all
  time/format/filename work until *after* the log-level guard, so filtered messages skip it.
  Added `getLogLevel()`.
- **Buffered log writes (SD-card wear).** Log lines were written with an `fopen`/append/`fclose`
  per line (a FAT metadata write each time) — fine at minute intervals, but heavy at FastRead's
  5–10 s target. Lines now accumulate in a thread-safe RAM buffer and flush in batches (4 KB / 10 s
  / date rollover / end of each round / before reboot / when the log is read), turning ~one write
  per line into ~one per round. Concurrent logging is now mutex-guarded (the previous per-call
  open/close raced on a shared static handle). Per-round flush keeps logs durable to within one
  round; a hard power loss may drop the current buffer window.
- Added per-step flow **diagnostics** at DEBUG level (duration, internal/PSRAM heap free + delta,
  total round time) so performance can be monitored from the normal log without a special build.
- **Size-based log rotation.** The message log is date-named with only age-based retention, so a
  verbose day (e.g. DEBUG at a short interval) could grow a single file without bound. The active
  file is now rotated when it reaches **10 MB** (archived as `log_<date>_<HHMMSS>.txt`) and the log
  directory is pruned to the **5 most-recent** files — a ~50 MB cap. The pre-NTP `log_1970-01-01.txt`
  boot log is preserved if the clock was never set.

### :broom: Code Quality / Cleanup

- Removed genuinely-dead `NumberPost::ErrorMessage` struct field.
- Corrected a misleading comment: `NumberPost::timeStampTimeUTC` is **used** by the InfluxDB
  v1/v2 exporters (was wrongly marked "not used; can be removed").
- Removed a commented-out dead MQTT branch in `ClassFlowMQTT`.

### :art: UI

- **Dark mode** for the web UI: a lightweight CSS-variable theme (`theme.css` / `theme.js`)
  with a 🌙/☀️ toggle in the header, persisted in `localStorage` and following the OS
  `prefers-color-scheme` by default. Applies across the parent page and all iframe pages;
  light mode is unchanged.
- **FastRead** options added to the config page (Digit ROI Processing, expert section).
- The existing **"Skip Messages on Error"** control is now **functional** (previously had
  no effect in firmware).
- **Status LED** (addressable WS281x on GPIO12): show the current processing stage as a colour
  (idle / take image / align / digitize / post-process / transmit / error). New enable toggle and
  seven colour pickers in the config page, with sensible defaults; driven via a decoupled
  stage callback so it doesn't conflict with the capture flash.
- **Pause / Resume processing** as an always-visible top-menu item (new `/pause` endpoint). Stops
  new rounds without interrupting an in-progress one and keeps the last status so reference/ROI
  setup still works while paused.
- **Live log viewer**: a "Live (auto-scroll)" checkbox that tails new log entries automatically
  instead of requiring a manual reload.
- **Time Zone is now a searchable dropdown** (region/city) generated from the IANA tz database;
  selecting a zone applies its POSIX string automatically. Removes the separate `timezones.html`.
- **Flexible round interval (sub-minute + unit dropdown).** The Round Interval is now an integer
  plus a unit selector (**seconds / minutes / hours / days**) instead of whole minutes only.
  Seconds-level intervals are supported (pairs with FastRead). Config format is
  `Interval = <number> <unit>`; a bare number (legacy `Interval = 5`) is still read as minutes.
  The MQTT keep-alive/LWT timeout has a 60 s floor so short intervals don't make it too aggressive.
- **FastRead self-corrects on rejected readings.** When a reading is rejected by a consistency
  check (negative rate / rate-too-high), the digit CNN is told to do a full re-read of every digit
  on the next cycle (`TriggerFullEval`), so a stale per-digit FastRead cache can't get stuck.


# [16.1.0] - 2026-01-11

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v16.0.0...v16.1.0)

### Known issues
No software is perfect. We know that our software has some quirks. If you have an issue, please first check the [issues](https://github.com/jomjol/AI-on-the-edge-device/issues) and
[discussions](https://github.com/jomjol/AI-on-the-edge-device/discussions) before reporting a new issue.


### Core Changes and Bug fixes
- Various minor improvements and changes
- Various documentation corrections/improvements
- Parameter `ChangeRateThreshold` can now also be zero [#3668](https://github.com/jomjol/AI-on-the-edge-device/pull/3668)
- MQTT: Added a proper `device_class` (duration) to "uptime" [#3659](https://github.com/jomjol/AI-on-the-edge-device/pull/3659)
- MQTT: Added `default_entity_id` to MQTT HA autodiscovery payload [#3970](https://github.com/jomjol/AI-on-the-edge-device/pull/3970)
- Added new models
- Updated SDCard Manufacturer List [#3730](https://github.com/jomjol/AI-on-the-edge-device/pull/3730)
- Added option for gal/min rate for Home Assistant MQTT Autodiscovery[#3868](https://github.com/jomjol/AI-on-the-edge-device/pull/3868)
- Fixed Setup Wizard [#3906](https://github.com/jomjol/AI-on-the-edge-device/pull/3906)
- `checkDigitConsistency`: added ESP_LOG_DEBUG output [#3962](https://github.com/jomjol/AI-on-the-edge-device/pull/3962)
- Added "Draw from center" option to Analog ROI editor [#3975](https://github.com/jomjol/AI-on-the-edge-device/pull/3975)
- Enabled OV3660 support in sdkconfig.defaults [#3996](https://github.com/jomjol/AI-on-the-edge-device/pull/3996)


# [16.0.0] - 2025-03-15

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.7.0...v16.0.0)

### Known issues
No software is perfect. We know that our software has some quirks. If you have an issue, please first check the [issues](https://github.com/jomjol/AI-on-the-edge-device/issues) and
[discussions](https://github.com/jomjol/AI-on-the-edge-device/discussions) before reporting a new issue.

### Homeassistant compatibility
:warning: Please check your Homeassistant instance to make sure it is handled correctly! Since the last release (`15.7.0`) a lot of changes happened!

### Core Changes (ordered by commit date)

- [#3436](https://github.com/jomjol/AI-on-the-edge-device/pull/3436) Added basic authentification of the Web Interface and the REST API, see https://jomjol.github.io/AI-on-the-edge-device-docs/Password-Protection

- [#3454](https://github.com/jomjol/AI-on-the-edge-device/pull/3454) Add thermometer

- [#3521](https://github.com/jomjol/AI-on-the-edge-device/pull/3521) openmetrics endpoint extension

- [#3537](https://github.com/jomjol/AI-on-the-edge-device/pull/3537) Added data export (CSV files) functionality

- [#3499](https://github.com/jomjol/AI-on-the-edge-device/pull/3499) Added and activated espressif mDNS service

- [#3547](https://github.com/jomjol/AI-on-the-edge-device/pull/3547) Enhanced `IgnoreLeadingNaN`. Marked it in the UI as a per-sequence parameter

- [#3580](https://github.com/jomjol/AI-on-the-edge-device/pull/3580) Update Homeassistant discovery to comply with Homeassistant `2025.2.4`

- [#3538](https://github.com/jomjol/AI-on-the-edge-device/pull/3538), [#3568](https://github.com/jomjol/AI-on-the-edge-device/pull/3568) Compress all Web UI files with gzip

- [#3590](https://github.com/jomjol/AI-on-the-edge-device/pull/3590) Validate uploaded OTA update file before installing it

- [#3583](https://github.com/jomjol/AI-on-the-edge-device/pull/3583) Remove HTML directory on update
  
- [#3590](https://github.com/jomjol/AI-on-the-edge-device/pull/3590) Consolidated the `reboot` and `save` buttons -> The `reboot` button ois not part of the notification
 corrected enable/disable texts on the configuration page to make it more intuitive

- [#3520](https://github.com/jomjol/AI-on-the-edge-device/pull/3520) Rewrite InfluxDB Interface

- [#3500](https://github.com/jomjol/AI-on-the-edge-device/pull/3500) Update smart-LED driver

- [#3423](https://github.com/jomjol/AI-on-the-edge-device/pull/3423) Removed `Autostart` parameter and make the flow to be always enabled 
- [#3423](https://github.com/jomjol/AI-on-the-edge-device/pull/3423) Enable `Flow start` menu entry in UI 
- [#3332](https://github.com/jomjol/AI-on-the-edge-device/pull/3332) Updated the Homeassistant Discovery topics :
    - `raw` has now set the `State Class` to `measurement`. Before it was always set to `""`. 
    - `value` has now only set the `State Class` to `total_increasing` if the parameter `Allow Negative Rates` is **not** set. Else it uses `measurement` since the rate could also be negative. Before it was always set to `total_increasing`.
    - The `rate_per_time_unit` topic of an **Energy** meter needs a `Device Class`=`power`. For `gas` and `water` it should be `volume_flow_rate`. Before it was always set to `""`.
    - [#3415](https://github.com/jomjol/AI-on-the-edge-device/pull/3415) Added button for `flow start` 
    - [#3359](https://github.com/jomjol/AI-on-the-edge-device/pull/3359) Added support for Domoticz MQTT integration 
    - Added Date and time to overview page
    - Updated submodules and models

- [#3316](https://github.com/jomjol/AI-on-the-edge-device/pull/3316) Update esp32-camera submodule to `v2.0.13` 
- [#3317](https://github.com/jomjol/AI-on-the-edge-device/pull/3317) Added contributor list 
- [#3315](https://github.com/jomjol/AI-on-the-edge-device/pull/3315) Added files for demo mode 

- Renamed MQTT topic from `rate_per_digitalization_round` to `rate_per_digitization_round`

- Updated parameter documentation pages
- [#3291](https://github.com/jomjol/AI-on-the-edge-device/pull/3291) Rename/remove unused parameters 
- [#3288](https://github.com/jomjol/AI-on-the-edge-device/pull/3288) Migrate-cam-parameters 

- [#3063](https://github.com/jomjol/AI-on-the-edge-device/pull/3063) Add support for OV5640 camera 
- New tflite-Models
- [#3088](https://github.com/jomjol/AI-on-the-edge-device/pull/3088) Homeassistant service discovery: derive node_id when using nested topics 
- [#3081](https://github.com/jomjol/AI-on-the-edge-device/pull/3081) Added Prometheus/OpenMetrics exporter 
- [#3148](https://github.com/jomjol/AI-on-the-edge-device/pull/3148), [#3163](https://github.com/jomjol/AI-on-the-edge-device/pull/3163), [#3174](https://github.com/jomjol/AI-on-the-edge-device/pull/3148), [#3163](https://github.com/jomjol/AI-on-the-edge-device/pull/3163), [#3174](https://github.com/jomjol/AI-on-the-edge-device/pull/3174) Added Webhook 
- [#3195](https://github.com/jomjol/AI-on-the-edge-device/pull/3195) Add rate threshold parameter 
- [#3068](https://github.com/jomjol/AI-on-the-edge-device/pull/3068) Added a Delay between the WiFi reconnections 
- Web UI improvements
- Various minor changes
- Update platformIO to 6.9.0 (Contains ESP IDF 5.3.1)

### Bug Fixes (ordered by commit date)

 - [#3450](https://github.com/jomjol/AI-on-the-edge-device/pull/3450) fix crash due to empty CAM parameters in migration
 - [#3446](https://github.com/jomjol/AI-on-the-edge-device/pull/3446) fix for incorrect decimal shift
 - [#3418](https://github.com/jomjol/AI-on-the-edge-device/pull/3418) Added fix for ledintensity 
 - [#3417](https://github.com/jomjol/AI-on-the-edge-device/pull/3417) Added fix for OV2640 brightness contrast saturation 
 - [#3393](https://github.com/jomjol/AI-on-the-edge-device/pull/3393) Added fix for 'AnalogToDigitTransitionStart' always using 9.2 regardless of the configured value 
 - [#3342](https://github.com/jomjol/AI-on-the-edge-device/pull/3342) Added fix for HA menu entry 
 - [#3313](https://github.com/jomjol/AI-on-the-edge-device/pull/3313) Added delay in InitCam  to fix `Camera not detected` issues
- [#3269](https://github.com/jomjol/AI-on-the-edge-device/pull/3269) Re-did revertion of TFlite submodule update as certain modules crash with it  (change was lost)
- [#3269](https://github.com/jomjol/AI-on-the-edge-device/pull/3269) Reverted TFlite submodule update as certain modules crash with it 
- [#3279](https://github.com/jomjol/AI-on-the-edge-device/pull/3279) Changed the webhook UploadImg to false 
- Changed default value from boolean to numeric value in parameter camDenoise documentation
- Updated config page
- [#3220](https://github.com/jomjol/AI-on-the-edge-device/pull/3220) Handle crash on corrupted model 
- [#3175](https://github.com/jomjol/AI-on-the-edge-device/pull/3175) Bugfix for boot loop 
- [#3180](https://github.com/jomjol/AI-on-the-edge-device/pull/3180) Bugfix for time stamp 
- [#3162](https://github.com/jomjol/AI-on-the-edge-device/pull/3162) Handle empty prevalue.ini gracefully 
- [#3213](https://github.com/jomjol/AI-on-the-edge-device/pull/3213) Added note about only TLS 1.2 is supported 

# [15.7.0] - 2024-02-17

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.6.0...v15.7.0)

### Core Changes
- Added new camera settings (See `Settings > Alignment > Reference Image and Camera Settings`). You might need to re-create the reference image and alignment marks. Note worthy:
  - You can now crop the image
  - Support to configure sharpness, grayscale, negatoive and exposure
- Enhanced various WebUI pages with better explanations and usability
- Add Firmware Version to MQTT

### Bug Fixes
- Reverted "Implemented late analog / digit transition [#2778](https://github.com/jomjol/AI-on-the-edge-device/pull/2778) (introduced in `v15.5`) as is seems to cause issues for many users.


# [15.6.0] - 2024-02-09

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.5.0...v15.6.0)

### Fixed

* Fixed issues with the SD-Card initialization

# [15.5.0] - 2024-02-02

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.4.0...v15.5.0)

### Changed

 - Update PlattformIO to v6.5.0, which means esp-idf to v5.1
 - Enhance busy notification
 - Implemented late analog / digit transition

### Fixed

* ATA-TRIM: workaround for old SD-cards with no trim function to work with esp-idf v5.x
* InfluxDB: Modified the time conversions to be more stable (UTC vs. local time shifts)
* Fix negatives on extended resolution false
* Show chip infos on info page
* Fix memory leaks in tflite integration

# [15.4.0] - 2023-12-22

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.3.0...v15.4.0)

### Changed

 - Updates submodules (esp-nn, tflite-micro-example, esp-camera)

 - Explicitly included needed tflite network layers (instead of all) , resulting in much smaller firmware size

 - Added shortcut icon

 - Rename in InfluxDB 'Database' to 'Bucket'

 - Updated analog tflite files
   - dig-class100-0167_s2_q.tflite
   - dig-class11_1700_s2.tflite
   - ana-cont_1208_s2_q.tflite
  
 - Added config entries for MQTT TLS


### Fixed

* InfluxDB: consider DST setting for UTC time conversion

* Minor html response bugfix 

 - Memory leakage (MQTT)
   

# [15.3.0] - 2023-07-22

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.3.0...v15.2.4)

### Changed

 - Updated PlatformIO to `6.3.2`
 - Updated analog tflite files
   - ana-cont_1207_s2_q.tflite
   - dig-cont_0620_s3_q.tflite

# [15.2.4] - 2023-05-02

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.2.1...v15.2.4)

### Changed
 - Updated PlatformIO to `6.2.0`
 - [#2376](https://github.com/jomjol/AI-on-the-edge-device/pull/2376) Improve logging if Autostart is not enabled

### Fixed
 - [#2373](https://github.com/jomjol/AI-on-the-edge-device/pull/2373) Allow the Alignment Mark step while status is "Initializing" or "Initialization (delayed)" or while in setup mode
 - [#2381](https://github.com/jomjol/AI-on-the-edge-device/pull/2381) Fix broken sysinfo REST API


# [15.2.1] - 2023-04-27

## Changes

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.2.0...v15.2.1)

### Fixed
 - [#2357](https://github.com/jomjol/AI-on-the-edge-device/pull/2357) Fix Alignment Mark issue


# [15.2.0] - 2023-04-23

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.1.1...v15.2.0)

### Added

-  [#2286](https://github.com/jomjol/AI-on-the-edge-device/pull/2286) Implement a camera livestream handler
-  [#2252](https://github.com/jomjol/AI-on-the-edge-device/pull/2252) Set prevalue using MQTT + set prevalue to RAW value (REST+MQTT)
-  [#2319](https://github.com/jomjol/AI-on-the-edge-device/pull/2319) Extend InfluxDBv1 with individual topic names

### Changed

-  [#2285](https://github.com/jomjol/AI-on-the-edge-device/pull/2285) Re-implemented PSRAM usage
-  [#2325](https://github.com/jomjol/AI-on-the-edge-device/pull/2325) Keep MainFlowTask alive to handle reboot
-  [#2233](https://github.com/jomjol/AI-on-the-edge-device/pull/2233) Remove trailing slash in influxDBv1
-  [#2305](https://github.com/jomjol/AI-on-the-edge-device/pull/2305) Migration of PlatformIO `5.2.0` to `6.1.0` (resp. ESP IDF from `4.4.2` to `5.0.1`)
-  Various cleanup and refactoring

### Fixed

-  [#2326](https://github.com/jomjol/AI-on-the-edge-device/pull/2326) Activate save button after Analogue ROI creationSet prevalue using MQTT + set prevalue to RAW value (REST+MQTT)
-  [#2283](https://github.com/jomjol/AI-on-the-edge-device/pull/2283) Fix Timezone issues on InfluxDB
-  Various minor fixes

### Removed

-   n.a.


# [15.1.1] - 2023-03-23

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.1.0...v15.1.1)

### Added

- [#2206](https://github.com/jomjol/AI-on-the-edge-device/pull/2206) Log PSRAM usage
- [#2216](https://github.com/jomjol/AI-on-the-edge-device/pull/2216) Log MQTT connection refused reasons

### Changed

- n.a.

### Fixed

-  [#2224](https://github.com/jomjol/AI-on-the-edge-device/pull/2224), [#2213](https://github.com/jomjol/AI-on-the-edge-device/pull/2213) Reverted some of the PSRAM usage changes due to negative sideffects 
-  [#2203](https://github.com/jomjol/AI-on-the-edge-device/issues/2203) Correct API for pure InfluxDB v1
-  [#2180](https://github.com/jomjol/AI-on-the-edge-device/pull/2180) Fixed links in Parameter Documentation
-  Various minor fixes

### Removed

-   n.a.

# [15.1.0] - 2023-03-12

## Update Procedure

Update Procedure see [online documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/#update-ota-over-the-air)

:bangbang: Afterwards you should force-reload the Web Interface (usually Ctrl-F5 will do it)!

:bangbang: Afterwards you should check your configuration for errors!

## Changes

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v15.0.3...v15.1.0)

### Added
- The Configuration page has now tooltips with enhanced documentation
- MQTT:
    - Added `GJ` (`gigajoule`) as an energy meter unit
    - Removed State Class and unit from `raw` topic
    - Various Improvements (Only send Homeassistant Discovery the first time we connect, ...) (https://github.com/jomjol/AI-on-the-edge-device/pull/2091
- Added Expert Parameter to change CPU Clock from `160` to `240 Mhz`
- SD card basic read/write check and a folder/file presence check at boot to indicate SD card issues or missing folders / files ([#2085](https://github.com/jomjol/AI-on-the-edge-device/pull/2085))
- Simplified "WIFI roaming" by client triggered channel scan (AP switching at low RSSI) -> using expert parameter "RSSIThreshold" ([#2120](https://github.com/jomjol/AI-on-the-edge-device/pull/2120))
- Log WLAN disconnect reason codes (see [WLAN disconnect reasons](https://jomjol.github.io/AI-on-the-edge-device-docs/WLAN-disconnect-reason))
- Support of InfluxDB v2 ([#2004](https://github.com/jomjol/AI-on-the-edge-device/pull/2004))


### Changed
- Updated models (tflite files), removed old versions (https://github.com/jomjol/AI-on-the-edge-device/pull/2089, https://github.com/jomjol/AI-on-the-edge-device/pull/2133)
  :bangbang: **Attention:** Update your configuration!
    -   Hybrid CNN network to `dig-cont_0611_s3` 
    -   Analog CNN network to `ana-cont-11.0.5` and `ana-clas100-1.5.7`
    -   Digit CNN network to `dig-class100-1.6.0`
-   Various Web interface Improvements/Enhancements:
    - Restructured Menu (Needs cache clearing to be applied)
    - Enhanced `Previous Value` page
    - Improved/faster Graph page
    - Various minor improvements
    - ROI config pages improvements
    - Improved Backup Functionality
- Added log file logs for Firmware Update
- Improved memory management (moved various stuff to external PSRAM, https://github.com/jomjol/AI-on-the-edge-device/pull/2117)
- Camera driver update: Support of contrast and saturation ([#2048](https://github.com/jomjol/AI-on-the-edge-device/pull/2048))   
  :bangbang:  **Attention**: This could have impact to old configurations. Please check your configuration and potentially adapt parametrization, if detection is negativly affected.
- Improved error handling and provide more verbose output in error cases during boot phase ([#2020](https://github.com/jomjol/AI-on-the-edge-device/pull/2020))
- Red board LED is indicating more different errors and states (see [Status LED Blink Codes](https://jomjol.github.io/AI-on-the-edge-device-docs/StatusLED-BlinkCodes))
- Logfile: Print start indication block after time is synced to indicate start in logfile after a cold boot
- `Image Quality Index`: Limit lower input range to 8 to avoid system instabilities

### Fixed
- Various minor fixes
- Added State Class "measurement" to rate_per_time_unit
- GPIO: Avoid MQTT publishing to empty topic when "MQTT enable" flag is not set
- Fix timezone config parser
- Remote Setup truncated long passwords (https://github.com/jomjol/AI-on-the-edge-device/issues/2167)
-  Problem with timestamp in InfluxDB interface

### Removed
-   n.a.


# [15.0.3] - 2023-02-28

**Name: Parameter Migration**

## Update Procedure

Update Procedure see [online documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/#update-ota-over-the-air)

:bangbang: Afterwards you should force-reload the Web Interface (usually Ctrl-F5 will do it).

## Changes

This release only migrates some parameters, see #2023 for details and a list of all parameter changes.
The parameter migration happens automatically on the next startup. No user interaction is required.
A backup of the config is stored on the SD-card as `config.bak`.

Beside of the parameter change and the bugfix listed below, no changes are contained in this release!

If you want to revert back to `v14` or earlier, you will have to revert the migration changes in `config.ini` manually!

### Added

-   n.a.

### Changed

-   [#2023](https://github.com/jomjol/AI-on-the-edge-device/pull/2023) Migrated Parameters
-   Removed old `Topic` parameter, it is not used anymore

### Fixed

-   [#2036](https://github.com/jomjol/AI-on-the-edge-device/issues/2036) Fix wrong url-encoding
-   **NEW v15.0.2:**  [#1933](https://github.com/jomjol/AI-on-the-edge-device/issues/1933) Bugfix InfluxDB Timestamp
-   **NEW v15.0.3:**  Re-added lost dropdownbox filling for Postprocessing Individual Parameters

### Removed

-   n.a.


# [14.0.3] -2023-02-05

**Name: Stabilization and Improved User Experience**

Thanks to over 80 Pull Requests from 6 contributors, we can anounce another great release with many many improvements and new features:

## Update Procedure

Update Procedure see [online documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/#update-ota-over-the-air)

## Changes

For a full list of changes see [Full list of changes](https://github.com/jomjol/AI-on-the-edge-device/compare/v13.0.8...v14.0.0)

### Added

-   [1877](https://github.com/jomjol/AI-on-the-edge-device/pull/1877) Show WIFI signal text labels / Log RSSI value to logfile
-   [1671](https://github.com/jomjol/AI-on-the-edge-device/pull/1671) Added experimental support for WLAN 802.11k und 802.11v (Mesh-Support)
-   Web UI caching of static files
-   Added various debug tools
-   [1798](https://github.com/jomjol/AI-on-the-edge-device/pull/1798) Add error handling for memory intensive tasks
-   [1784](https://github.com/jomjol/AI-on-the-edge-device/pull/1784) Add option to disable brownout detector
-   Added full web browser based installation mode (including initial setup of SD-card) - see [WebInstaller](https://jomjol.github.io/AI-on-the-edge-device/index.html)
-   Added [Demo Mode](https://jomjol.github.io/AI-on-the-edge-device-docs/Demo-Mode)
-   [1648](https://github.com/jomjol/AI-on-the-edge-device/pull/1648) Added trigger to start a flow by [REST](https://jomjol.github.io/AI-on-the-edge-device-docs/REST-API) API or [MQTT](https://jomjol.github.io/AI-on-the-edge-device-docs/MQTT-API/)
-   Show special images during steps `Initializing` and `Take Image` as the current camera image might be incomplete or outdated

### Changed

-   Migrated documentation (Wiki) to <https://jomjol.github.io/AI-on-the-edge-device-docs>. Please help us to make it even better.
-   New OTA Update page with progress indication
-   Various memory optimizations
-   Cleanup code/Web UI
-   Updated models
-   [1809](https://github.com/jomjol/AI-on-the-edge-device/pull/1809) Store preprocessed image with ROI to RAM
-   Better log messages on some errors/issues
-   [1742](https://github.com/jomjol/AI-on-the-edge-device/pull/1742) Replace alert boxes with overlay info boxes
-   Improve log message when web UI is installed incomplete
-   [1676](https://github.com/jomjol/AI-on-the-edge-device/pull/1676) Improve NTP handling
-   HTML: improved user informations (info boxes, error hints, ...)
-   [1904](https://github.com/jomjol/AI-on-the-edge-device/pull/1904) Removed newlines in JSON and replaced all whitespaces where there was more than one

### Fixed

-   Fixed many many things
-   [1509](https://github.com/jomjol/AI-on-the-edge-device/pull/1509) Protect `wifi.ini` from beeing deleted.
-   [1530](https://github.com/jomjol/AI-on-the-edge-device/pull/1530) Homeassistant `Problem Sensor`
-   [1518](https://github.com/jomjol/AI-on-the-edge-device/pull/1518) JSON Strings
-   [1817](https://github.com/jomjol/AI-on-the-edge-device/pull/1817) DataGraph: datafiles sorted -> newest on top

### Removed

-   n.a.

# [13.0.8] - 2022-12-19

**Name: Home Assistant MQTT Discovery Support**

## Update Procedure see [online documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/#update-ota-over-the-air)

## Added

-   Implementation of [Home Assistant MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
-   Improved ROIs configuration: locked ROI geometry, equidistant delta x
-   Improved OTA Update mechanism (only working after installation for next update)
-   Added data logging in `/log/data` - One day per file and each measurement is on one line
    -   Format: csv - comma separated
    -   Content: `time`, `name-of-number`, `raw-value`, `return-value`, `pre-value`, `change-rate`, `change-absolute`, `error-text`, `cnn-digit`, `cnn-analog`
-   Show graph of values direct in the user interface (thanks to [@rdmueller](https://github.com/rdmueller))

    -   Using new data logging (see above)
    -   Possibility to choose different values and switch between different numbers (if present)

    Note: You need to activate data logging for this feature to work, see above!
-   PreValue is now contained in `/json` ([#1154](https://github.com/jomjol/AI-on-the-edge-device/issues/1154))
-   SD card info into the `System>Info` menu (thanks to [@Slider007](https://github.com/Slider0007))
-   Version check (Firmware vs. Web UI)
-   Various minor new features

## Changed

-   Updated tflite (`dig-cont_0600_s3.tflite`)
-   Updated OTA functionality (more robust, but not fully bullet prove yet)
-   Updated Espressif library to `espressif32@v5.2.0`
-   [#1176](https://github.com/jomjol/AI-on-the-edge-device/discussions/1176) accept minor negative values (-0.2) if extended resolution is enabled
-   [#1143](https://github.com/jomjol/AI-on-the-edge-device/issues/1143) added config parameter `AnalogDigTransitionStart`. It can setup very early and very late digit transition starts.
-   New version of `dig-class100` (v1.4.0): added images of heliowatt powermeter 
-   NEW v13.0.2: Update Tool "Logfile downloader and combiner" to handle the new csv file format.
-   NEW v13.0.2: MQTT: Added MQTT topic `status` (Digitization Status), Timezone to MQTT topic `timestamp`.#
-   NEW v13.0.2: Logging: Disable heap logs by default, cleanup
-   NEW v13.0.7:
    -   log NTP server name
    -   Improved log messages
    -   Various preparations for next release
-   **NEW v13.0.8**: 
    -   Continue booting on PSRAM issues, Web UI will show an error
    -   Updated models
    -   Various UI enhancements
    -   Various internal improvements
    -   Show uptime in log
    -   Show uptime and round on overview page

## Fixed

-   [#1116](https://github.com/jomjol/AI-on-the-edge-device/issues/1116) precision problem at setting prevalue
-   [#1119](https://github.com/jomjol/AI-on-the-edge-device/issues/1119) renamed `firmware.bin` not working in OTA
-   [#1143](https://github.com/jomjol/AI-on-the-edge-device/issues/1143) changed postprocess for `analog->digit` (lowest digit processing)
-   [#1280](https://github.com/jomjol/AI-on-the-edge-device/issues/1280) check ROIs name for unsupported characters
-   [#983](https://github.com/jomjol/AI-on-the-edge-device/issues/983) old log files did not get deleted 
-   Failed NTP time sync during startup gets now retried every round if needed
-   Whitespaces and `=` in MQTT and InfluxDB passwords
-   Various minor fixes and improvements
-   NEW v13.0.2: Corrected Version comparison between firmware and Web UI.
-   NEW v13.0.3: Re-updated build environment to v5.2.0 (from accidental downgrad to v4.4.0)
-   NEW v13.0.4: Fix for reboot in case of MQTT not used
-   NEW v13.0.5: No reboot in case of missing NTP-connection
-   NEW v13.0.7:
    -   Prevent autoreboot on cam framebuffer init error
    -   Properly protect `wlan.ini` against deletion
    -   Fixed various MQTT topic content issues
    -   Fix Digit detected as 10 (<https://github.com/jomjol/AI-on-the-edge-device/pull/1525>)
    -   Fix frozen time in datafile on error
    -   Various minor fixes
-   **NEW v13.0.8**: 
    -   Fix Rate Problem ([#1578](https://github.com/jomjol/AI-on-the-edge-device/issues/1578), [#1572](https://github.com/jomjol/AI-on-the-edge-device/issues/1572))
    -   Stabilized MQTT
    -   Fixed redundant calls in OTA
    -   Block REST API calls till resource is ready
    -   Fixed number renaming ([#1635](https://github.com/jomjol/AI-on-the-edge-device/issues/1635))

## Removed

-   n.a.

# [12.0.1] 2022-09-29

Name: Improve **u**ser e**x**perience 

:bangbang: The release breaks a few things in ota update :bangbang:

**Make sure to read the instructions below carfully!**.

1.  Backup your configuration (use the `System > Backup/Restore` page)!
2.  You should update to `11.3.1` before you update to this release. All other migrations are not tested. 
    Rolling newer than `11.3.1` can also be used, but no guaranty.
3.  Upload and update the `firmware.bin` file from this release. **but do not reboot**
4.  Upload the `html-from-11.3.1.zip` in html upload and update the web interface.
5.  Now you can reboot.

If anything breaks you can try to
1\. Call `http://<IP>/ota?task=update&file=firmware.bin` resp. `http://<IP>/ota?task=update&file=html.zip` if the upload successed but the extraction failed.
1\. Use the initial_esp32_setup.zip ( <https://github.com/jomjol/AI-on-the-edge-device/wiki/Installation> ) as alternative.

## Added

-   Automatic release creation
-   Newest firmware of rolling branch now automatically build and provided in [Github Actions Output](https://github.com/jomjol/AI-on-the-edge-device/actions) (developers only)
-   [#1068](https://github.com/jomjol/AI-on-the-edge-device/issues/1068) New update mechanism: 
    -   Handling of all files (`zip`, `tfl`, `tflite`, `bin`) within in one common update interface
    -   Using the `update.zip` from the [Release page](https://github.com/jomjol/AI-on-the-edge-device/releases)
    -   Status (`upload`, `processing`, ...) displayed on Web Interface
    -   Automatical detection and suggestion for reboot where needed (Web Interface uupdates only need a page refresh)
    -   :bangbang: Best for OTA use Firefox. Chrome works with warnings. Safari stuck in upload.

## Changed

-   Integrated version info better shown on the Info page and in the log
-   Updated menu
-   Update used libraries (`tflite`, `esp32-cam`, `esp-nn`, as of 20220924) 

## Fixed

-   [#1092](https://github.com/jomjol/AI-on-the-edge-device/issues/1092) censor passwords in log outputs 
-   [#1029](https://github.com/jomjol/AI-on-the-edge-device/issues/1029) wrong change of `checkDigitConsistency` now working like releases before `11.3.1` 
-   Spelling corrections (**[cristianmitran](https://github.com/cristianmitran)**) 

## Removed

-   Remove the folder `/firmware` from GitHub repository. 
    If you want to get the latest `firmware.bin` and `html.zip` files, please download from the automated [build action](https://github.com/jomjol/AI-on-the-edge-device/actions) or [release page](https://github.com/jomjol/AI-on-the-edge-device/releases)

# [11.3.1](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v11.3.1), 2022-09-17

Intermediate Digits

-   **ATTENTION**: 

    -   first update the `firmware.bin` and ensure that the new version is running

    -   Only afterwards update the `html.zip`

    -   Otherwise the downwards compatibility of the new counter clockwise feature is not given and you end in a reboot loop, that needs manual flashing!


-   **NEW v11.3.1**: corrected corrupted asset `firmware.bin`
-   Increased precision (more than 6-7 digits)
-   Implements Counter Clockwise Analog Pointers
-   Improved post processing algorithm
-   Debugging: intensive use of testcases
-   MQTT: improved handling, extended logging, automated reconnect
-   HTML: Backup Option for Configuration
-   HTML: Improved Reboot
-   HTML: Update WebUI (Reboot, Infos, CPU Temp, RSSI)
-   This version is largely also based on the work of **[caco3](https://github.com/caco3)**,  **[adellafave](https://github.com/adellafave)**,  **[haverland](https://github.com/haverland)**,  **[stefanbode](https://github.com/stefanbode)**, **[PLCHome](https://github.com/PLCHome)**

# [11.2.0](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v11.2.0), 2022-08-28

Intermediate Digits

-   Updated Tensorflow / TFlite to newest tflite (version as of 2022-07-27)

-   Updated analog neural network file (`ana-cont_11.3.0_s2.tflite` - default, `ana-class100_0120_s1_q.tflite`)

-   Updated digit neural network file (`dig-cont_0570_s3.tflite` - default, `dig-class100_0120_s2_q.tflite`)

-   Added automated filtering of tflite-file in the graphical configuration (thanks to @**[caco3](https://github.com/caco3)**)

-   Updated consistency algorithm & test cases

-   HTML: added favicon and system name, Improved reboot dialog  (thanks to @**[caco3](https://github.com/caco3)**)

# [11.1.1](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v11.1.1), 2022-08-22

Intermediate Digits

-   New and improved consistency check (especially with analog and digit counters mixed)
-   Bug Fix: digit counter algorithm

# [11.0.1](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v11.0.1), 2022-08-18

Intermediate Digits

-   **NEW v11.0.1**: Bug Fix InfluxDB configuration (only update of html.zip necessary)

-   Implementation of new CNN types to detect intermediate values of digits with rolling numbers

    -   By default the old algo (0, 1, ..., 9, "N") is active (due to the limited types of digits trained so far)
    -   Activation can be done by selecting a tflite file with the new trained model in the 'config.ini'
    -   **Details can be found in the [wiki](https://github.com/jomjol/AI-on-the-edge-device/wiki/Neural-Network-Types)** (different types, trained image types, naming convention)

-   Updated  neural network files (and adaptation to new naming convention)

-   Published a tool to download and combine log files - Thanks to [Contributor]

    -   Files see ['/tools/logfile-tool'](tbd), How-to see [wiki](https://github.com/jomjol/AI-on-the-edge-device/wiki/Gasmeter-Log-Downloader)

-   Bug Fix: InfluxDB enabling in graphic configuration

# [10.6.2](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v10.6.2), 2022-07-24

Stability Increase

## Added

-   **NEW 10.6.2**: ignore hidden files in model selection (configuration page)

-   **NEW 10.6.1**: Revoke esp32cam & tflite update

-   **NEW 10.6.1**: Bug Fix: tflite-filename with ".", HTML spelling error

-   InfluxDB: direct injection into InfluxDB - thanks to **[wetneb](https://github.com/wetneb)**

-   MQTT: implemented "Retain Flag" and extend with absolute Change (in addition to rate)

-   `config.ini`: removal of modelsize (readout from tflite)

-   Updated analog neural network file (`ana1000s2.tflite`) & digit neural network file (`dig1400s2q.tflite`)

-   TFMicro/Lite: Update (espressif Version 20220716)

-   Updated esp32cam (v20220716)

-   ESP-IDF: Update to 4.4

-   Internal update (CNN algorithm optimizations, reparation for new neural network type)

-   Bug Fix: no time with fixed IP, Postprocessing, MQTT

# [10.5.2](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v10.5.2), 2022-02-22

Stability Increase

## Changed

-   NEW 10.5.2: Bug Fix: wrong `firmware.bin` (no rate update)
-   NEW 10.5.1: Bug Fix: wrong return value, rate value & PreValue status, HTML: SSID & IP were not displayed
-   MQTT: changed wifi naming to "wifiRSSI"
-   HTML: check selectable values for consistency
-   Refactoring of check postprocessing consistency (e.g. max rate, negative rate, ...)
-   Bug Fix: corrected error in "Check Consistency Increase"

# [10.4.0](https://github.com/jomjol/AI-on-the-edge-device/releases/tag/v10.4.0), 2022-02-12

Stability Increase

## Changed

-   Graphical configuration: select available neural network files (_.tfl,_.tflite) from drop down menu
-   OTA-update: add option to upload tfl / tflite files to the correct location (`/config/`)
    -   In the future the new files will also be copied to the `firmware` directory of the repository
-   Added Wifi RSSI to MQTT information
-   Updated analog neural network file (`ana-s3-q-20220105.tflite`)
-   Updated digit neural network file (`dig-s1-q-20220102.tflite`)
-   Updated build environment to `Espressif 3.5.0`

# [10.3.0] - (2022-01-29)

Stability Increase

## Changed

-   Implemented LED flash dimming (`LEDIntensity`).
    Remark: as auto illumination in the camera is used, this is rather for energy saving. It will not help reducing reflections
-   Additional camera parameters: saturation, contrast (although not too much impact yet)
-   Some readings will have removable "N"s that can not be removed automatically and are handled with an "error" --> no return value in the field "value" anymore (still reported back via field "raw value")
-   Updated esp32 camera hardware driver
-   Bug fix: MQTT, HTML improvements

**ATTENTION:  The new ESP32 camera hardware driver is much more stable on newer OV2640 versions (no or much less reboots) but seems to be not fully compatible with older versions.**

If you have problem with stalled systems you can try the following

-   Update the parameter `ImageQuality` to `12` instead of current value `5` (manually in the `config.ini`)

-   If this is not helping, you might need to update your hardware or stay with version 9.2

# [10.2.0] - (2022-01-14)

Stability Increase

## Changed

-   Due to the updated camera driver, the image looks different and a new setup might be needed

    -   Update reference image
    -   Update Alignment marks

-   Reduce reboot due to camera problems

-   Update esp32-camera to new version (master as of 2022-01-09)

# [10.1.1] - (2022-01-12)

 Stability Increase

## Changed

-   Bug Fix MQTT problem
-   Issue:
    -   Changing from v9.x to 10.x the MQTT-parameter "Topic" was renamed into "MainTopic" to address multiple number meters. This renaming should have been done automatically in the background within the graphical configuration, but was not working. Instead the parameter "Topic" was deleted and "MainTopic" was set to disabled and "undefined".
-   ToDo
    -   Update the `html.zip`
    -   If old `config.ini` available: copy it to `/config`, open the graphical configuration and save it again.
    -   If old `config.ini` not available: reset the parameter "MainTopic" within the `config.ini` manually
    -   Reboot

# [10.1.0] -  (2022-01-09)

Stability Increase

## Changed

-   Reduce ESP32 frequency to 160MHz

-   Update tflite (new source: <https://github.com/espressif/tflite-micro-esp-examples>)

-   Update analog neural network (ana-s3-q-20220105.tflite)

-   Update digit neural network (dig-s1-q-20220102.tflite)

-   Increased web-server buffers

-   bug fix: compiler compatibility

# [10.0.2] - (2022-01-01)

Stability Increase

## Changed

-   NEW v10.0.2: Corrected JSON error

-   Updated compiler toolchain to ESP-IDF 4.3

-   Removal of memory leak

-   Improved error handling during startup (check PSRAM and camera with remark in logfile)

-   MQTT: implemented raw value additionally, removal of regex contrain

-   Normalized Parameter `MaxRateValue`  to "change per minute"

-   HTML: improved input handling

-   Corrected error handling: in case of error the old value, rate, timestamp are not transmitted any more

# [9.2.0] -  (2021-12-02)

External Illumination

## Changed

-   Direct JSON access: `http://IP-ADRESS/json`
-   Error message in log file in case camera error during startup
-   Upgrade analog CNN to v9.1.0
-   Upgrade digit CNN to v13.3.0 (added new images)
-   html: support of different ports

# [9.1.1] - External Illumination (2021-11-16)

## Changed

-   NEW 9.1.1 bug fix: LED implemenetation
-   External LEDs: change control mode (resolve bug with more than 2 LEDs)
-   Additional info into log file
-   Bug fix: decimal shift, html, log file

# [9.0.0] - External Illumination (2021-10-23)

## Changed

-   Implementation of external illumination to adjust positioning, brightness and color of the illumination now set individually
    -   Technical details can be found in the wiki: <https://github.com/jomjol/AI-on-the-edge-device/wiki/External-LED>
        <img src="https://raw.githubusercontent.com/jomjol/ai-on-the-edge-device/master/images/intern_vs_external.jpg" width="500">
-   New housing published for external LEDs and small clearing: <https://www.thingiverse.com/thing:5028229>

# [8.5.0] - Multi Meter Support (2021-10-07)

## Changed

-   Upgrade digit CNN to v13.1.0 (added new images)
-   bug fix: wlan password with space, double digit output

# [8.4.0] - Multi Meter Support (2021-09-25)

## Changed

-   License change (remove MIT license, remark see below)

-   html: show hostname in title and main page

-   configuration:

    -   moved setting `ExtendedResolution` to individual number settings
    -   New parameter `IgnoreLeadingNaN` (delete leading NaN's specifically)
    -   **ATTENTION**: update of the `config.ini` needed (open, adjust `ExtendedResolution`, save)

-   Bug fixing (html, images of recognized numbers)

    **ATTENTION: LICENSE CHANGE - removal of MIT License.**

-   Currently no licence published - copyright belongs to author

-   If you are interested in a commercial usage or dedicated versions please contact the developer
    -   no limits to private usage

# [8.3.0] - Multi Meter Support (2021-09-12)

## Changed

-   Upgrade digit CNN to v12.1.0 (added new images)
-   Dedicated NaN handling, internal refactoring (CNN-Handling)
-   HTML: confirmation after config.ini update
-   Bug fixing

# [8.2.0] - Multi Meter Support (2021-08-24)

## Changed

-   Improve server responsiveness


-   Flow status and prevalue status in overview
-   Improved prevalue handling

# [8.1.0] - Multi Meter Support (2021-08-12)

## Changed

-   GPIO: using the general mqtt main topic for GPIO


-   Upgrade digit CNN to v12.0.0  (added new images)
-   Update tfmicro to new master (2021-08-07)
-   Bug fix: remove text in mqtt value, remove connect limit in wlan reconnet

# [8.0.5] - Multi Meter Support (2021-08-01)

## Changed

-   NEW 8.0.5: bug fix: saving prevalue


-   NEW 8.0.4: bug fix: load config.ini after upgrade
-   NEW 8.0.3: bug fix: reboot during `config.ini` handling, html error
-   NEW 8.0.2: saving roundes prevalue, bug fix html server
-   NEW 8.0.1: bug fix: html handling of parameter `FixedExposure` and `ImageSize`
-   Dual / multi meter support (more than 1 number to be recognized)
    This is implemented with the feature "number" on the ROI definition as well as selected options
-   MQTT: standardization of the naming - including new topics (`json`,  `freeMem`, `uptime`)c
-   Preparation for extended GPIO support (thanks to Zwerk2k) - not tested and fully functional yet
-   Bug fixing: html server, memory leak, MQTT connect, hostname, turn of flash LED

<span style="color: red;">**ATTENTION: the configuration and prevalue files are modified automatically and will not be backward compatible!**</span>

# [7.1.2] MQTT-Update - (2021-06-17)

## Changed

-   NEW: 7.1.2: bug fix setting hostname, Flash-LED not off during reboot


-   NEW: 7.1.1: bug fix wlan password with "="  (again)

-   MQTT error message: changes "no error", send retain flag

-   Update wlan handling to esp-idf 4.1

-   Upgrade digit CNN to v8.7.0  (added new images)

-   Bug fix: MQTT, WLAN, LED-Controll, GPIO usage, fixed IP, calculation flow rate

# [7.0.1] MQTT-Update - (2021-05-13)

## Changed

-   NEW: 7.0.1: bug fix wlan password with "="


-   Upgrade digit CNN to v8.5.0  (added new images)

-   New MQTT topics: flow rate (units/minute), time stamp (last correct read readout)

-   Update MQTT/Error topic to " " in case no error (instead of empty string)

-   Portrait or landscape image orientation in rotated image (avoid cropping)

# [6.7.2] Image Processing in Memory - (2021-05-01)

## Changed

-   NEW 6.7.2: Updated html for setup modus - remove reboot on edit configuration)


-   NEW 6.7.1: Improved stability of camera (back to v6.6.1) - remove black strips and areas

-   Upgrade digit CNN to v8.3.0  (added new type of digits)

-   Internal update: TFlite (v2.5), esp32cam, startup sequence

-   Rollback to espressif v2.1.0, as v3.2.0 shows unstable reboot

-   Bugfix: WLan-passwords, reset of hostname

# [6.6.1] Image Processing in Memory - (2021-04-05)

## Changed

-   NEW 6.6.1: failed SD card initialization indicated by fast blinking LED at startup


-   Improved SD-card handling (increase compatibility with more type of cards)

# [6.5.0] Image Processing in Memory - (2021-03-25)

## Changed

-   Upgrade digit CNN to v8.2.0  (added new type of digits)


-   Supporting alignment structures in ROI definition
-   Bug fixing: definition of  hostname in `config.ini`

# [6.4.0] Image Processing in Memory - (2021-03-20)

## Changed

-   Additional alignment marks for settings the ROIs (analog and digit)


-   Upgrade analog CNN to v7.0.0 (added new type of pointer)

# [6.3.1] Image Processing in Memory - (2021-03-16)

## Changed

-   NEW: 6.3.1: bug fixing in initial edit reference image and `config.ini` (Spelling error in `InitialRotate`)


-   Initial setup mode: bug fixing, error correction
-   Bug-fixing

# [6.2.2] Image Processing in Memory - (2021-03-10)

## Changed

-   NEW 6.2.2: bug fixing


-   NEW 6.2.1: Changed brightness and contrast to default if not enabled (resolves to bright images)

-   Determination of fixed illumination settings during startup - speed up of 5s in each run

-   Update digit CNN to v8.1.1 (additional digit images trained)

-   Extended error message in MQTT error message

-   Image brightness is now adjustable

-   Bug fixing: minor topics

# [6.1.0] Image Processing in Memory - (2021-01-20)

## Changed

-   Disabling of analog / digit counters in configuration


-   Improved Alignment Algorithm (`AlignmentAlgo`  = `Default`,  `Accurate` , `Fast`)
-   Analog counters: `ExtendedResolution` (last digit is extended by sub comma value of CNN)
-   `config.ini`: additional parameter `hostname`  (additional to wlan.ini)
-   Switching of GPIO12/13 via http-interface: `/GPIO?GPIO=12&Status=high/low`
-   Bug fixing: html configuration page, wlan password ("=" now possible)

# [6.0.0] Image Processing in Memory - (2021-01-02)

## Changed

-   **Major change**: image processing fully in memory - no need of SD card buffer anymore

    -   Need to limit camera resolution to VGA (due to memory limits)


-   MQTT: Last Will Testament (LWT) implemented: "connection lost" in case of connection lost to `TopicError`
-   Disabled `CheckDigitIncreaseConsistency` in default configuration - must now be explicit enabled if needed
-   Update digit CNN to v7.2.1 (additional digit images trained)
-   Setting of arbitrary time server in `config.ini`
-   Option for fixed IP-, DNS-Settings in `wlan.ini`
-   Increased stability (internal image and camera handling)
-   Bug fixing: edit digits, handling PreValue, html-bugs

# [5.0.0] Setup Modus - (2020-12-06)

## Changed

-   Implementation of initial setup modus for fresh installation


-   Code restructuring (full compatibility between pure ESP-IDF and Platformio w/ espressif)

# [4.1.1] Configuration editor - (2020-12-02)

## Changed

-   Bug fixing: internal improvement of file handling (reduce not responding)

# [4.1.0] Configuration editor - (2020-11-30)

## Changed

-   Implementation of configuration editor (including basic and expert mode)


-   Adjustable time zone to adjust to local time setting (incl. daylight saving time)

-   MQTT: additional topic for error reporting

-   standardized access to current logfile via `http://IP-ADRESS/logfileact`

-   Update digit CNN to v7.2.0, analog CNN to 6.3.0

-   Bug fixing: truncation error,  CheckDigitConsistency & PreValue implementation

# [4.0.0] Tflite Core - (2020-11-15)

## Changed

-   Implementation of rolling log-files


-   Update Tflite-Core to master@20201108 (v2.4)

-   Bug-fixing for reducing reboots

# [3.1.0] MQTT-Client - (2020-10-26)

## Changed

-   Update digit CNN to v6.5.0 and HTML (Info to hostname, IP, ssid)

-   New implementation of "checkDigitConsistency" also for digits

-   MQTT-Adapter: user and password for sign in MQTT-Broker

# [3.0.0] MQTT-Client  (2020-10-14)

## Changed

-   Implementation of MQTT Client


-   Improved Version Control
-   bug-fixing

# [2.2.1] Version Control  (2020-09-27)

## Changed

-   Bug-Fixing (hostname in wlan.ini and error handling inside flow)

## \[2.2.0| Version Control  (2020-09-27)

## Changed

-   Integrated automated versioning system (menu: SYSTEM --> INFO)


-   Update Build-System to PlatformIO - Espressif 32 v2.0.0 (ESP-IDF 4.1)

# [2.1.0] Decimal Shift, Chrome & Edge  (2020-09-25)

## Changed

-   Implementation of Decimal Shift


-   Update default CNN for digits to v6.4.0

-   Improvement HTML

-   Support for Chrome and Edge

-   Reduce logging to minimum - extended logging on demand

-   Implementation of hostname in wlan.ini (`hostname = "HOSTNAME")`

-   Bug fixing, code corrections

# [2.0.0] Layout update  (2020-09-12)

## Changed

-   Update to **new and modern layout**
-   Support for Chrome improved
-   Improved robustness: improved error handling in auto flow reduces spontaneous reboots
-   File server: Option for "DELETE ALL"
-   WLan: support of spaces in SSID and password
-   Reference Image: Option for mirror image, option for image update on the fly
-   additional parameter in `wasserzaehler.html?noerror=true`  to suppress an potential error message
-   bug fixing

# [1.1.3](2020-09-09)

## Changed

-   **Bug in configuration of analog ROIs corrected** - correction in v.1.0.2 did not work properly


-   Improved update page for the web server (`/html` can be updated via a zip-file, which is provided in `/firmware/html.zip`)
-   Improved Chrome support

# [1.1.0](2020-09-06)

## Changed

-   Implementation of "delete complete directory"
    **Attention: beside the `firmware.bin`, also the content of `/html` needs to be updated!**

# [1.0.2](2020-09-06)

## Changed

-   Bug in configuration of analog ROIs corrected


-   minor Bug correction

# [1.0.1](2020-09-05)

## Changed

-   preValue.ini Bug corrected


-   minor Bug correction

# [1.0.0](2020-09-04)

## Changed

-   **First usable version** - compatible to previous project (<https://github.com/jomjol/water-meter-system-complete>)


-   NEW:
    -   no docker container for CNN calculation necessary
    -   web based configuration editor on board

# [0.1.0](2020-08-07)

## Changed

-   Initial Version


[15.2.4]: https://github.com/jomjol/AI-on-the-edge-device/compare/v15.2.1...v15.2.4
[15.2.1]: https://github.com/jomjol/AI-on-the-edge-device/compare/v15.2.0...v15.2.1
[15.2.0]: https://github.com/jomjol/AI-on-the-edge-device/compare/v15.1.1...v15.2.0
[15.1.1]: https://github.com/jomjol/AI-on-the-edge-device/compare/v15.1.0...v15.1.1
[15.1.0]: https://github.com/jomjol/AI-on-the-edge-device/compare/v15.0.3...v15.1.0
[15.0.3]: https://github.com/jomjol/AI-on-the-edge-device/compare/v14.0.3...v15.0.3
[14.0.3]: https://github.com/jomjol/AI-on-the-edge-device/compare/v13.0.8...v14.0.3
[13.0.8]: https://github.com/jomjol/AI-on-the-edge-device/compare/v12.0.1...v13.0.8
[13.0.7]: https://github.com/jomjol/AI-on-the-edge-device/compare/v12.0.1...v13.0.7
[13.0.5]: https://github.com/jomjol/AI-on-the-edge-device/compare/v12.0.1...v13.0.5
[13.0.4]: https://github.com/jomjol/AI-on-the-edge-device/compare/v12.0.1...v13.0.4
[13.0.1]: https://github.com/jomjol/AI-on-the-edge-device/compare/v12.0.1...v13.0.1
[12.0.1]: https://github.com/jomjol/AI-on-the-edge-device/compare/v11.3.1...v12.0.1
[11.4.3]: https://github.com/haverland/AI-on-the-edge-device/compare/v10.6.2...v11.4.3
[11.4.2]: https://github.com/haverland/AI-on-the-edge-device/compare/v10.6.2...v11.4.2
[11.3.9]: https://github.com/haverland/AI-on-the-edge-device/compare/v10.6.2...v11.3.9
