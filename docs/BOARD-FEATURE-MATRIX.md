# Board Feature Matrix

Functionality / feature differences between the supported boards. **Keep this table updated whenever a
board-specific feature is added or changed** — it is the source of truth for catching feature drift
between the ESP32-CAM and the ESP32-S3 variants.

> Most firmware features are **target-agnostic** and identical on all boards (see "Common features"
> below). This matrix tracks only the things that actually differ by board.

| Area | ESP32-CAM (AI-Thinker) | ESP32-S3 8 MB | ESP32-S3 16 MB |
|---|---|---|---|
| **IDF target / silicon** | `esp32` (LX6, dual-core) | `esp32s3` (LX7, dual-core) | `esp32s3` |
| **Board define** | `BOARD_ESP32CAM_AITHINKER` | `BOARD_ESP32S3_CAM` | `BOARD_ESP32S3_CAM` |
| **Build selector** | default (`idf.py` esp32) | `IDF_TARGET=esp32s3` | `IDF_TARGET=esp32s3 AIOTEDGE_S3_FLASH=16mb` |
| **Flash size** | 4 MB | 8 MB | 16 MB |
| **PSRAM** | ~4 MB mapped, **quad**, 40 MHz | 8 MB, **octal (OPI)**, 80 MHz | 8 MB octal, 80 MHz |
| **CPU frequency** | 160 MHz (IDF default) | **240 MHz** | 240 MHz |
| **Main task stack** | 3584 B (default) | **8192 B** | 8192 B |
| **Partition table** | `partitions.csv` | `partitions_esp32s3_8mb.csv` | `partitions_esp32s3_16mb.csv` |
| **OTA app slots** | dual-OTA, 1.9 MB each | dual-OTA, **2.5 MB** each | dual-OTA, **3 MB** each |
| **In-flash `storage` partition** | ❌ none | ✅ **2.75 MB** | ✅ **9.5 MB** |
| **Storage model** | **SD card required** | **flash-FS** (LittleFS @ `/sdcard`); SD optional (SD wins) | flash-FS; SD optional |
| **`USE_FLASH_FS`** | not defined | defined | defined |
| **Camera peripheral** | I2S DVP | LCD_CAM DVP | LCD_CAM DVP |
| **Camera driver** | esp32-camera (via `ICameraBackend`) | esp32-camera (via `ICameraBackend`) | same |
| **Status / flash LED** | PWM LED (LEDC); `USE_PWM_LEDFLASH`; flash GPIO4 | **WS2812 RGB** on GPIO48 (RMT); flash via GPIO handler | same |
| **RGB Wi-Fi status feedback** | ❌ (simple LED) | ✅ `driveSystemStatusWs281x()` (orange/red/green) | ✅ |
| **Info-page "Hardware details" section** | ❌ hidden (S3-gated) | ✅ shown | ✅ shown |
| **Power management (DFS/light-sleep)** | ❌ **not feasible** (no XTAL LEDC clock for XCLK → APB-tied) | 🟡 **feasible candidate** (XTAL LEDC clock; needs HW validation) | 🟡 same |
| **SPI flash 120 MHz** | ❌ (80 MHz max) | ❌ blocked by octal-PSRAM@80 (compile-time); viable only on quad/no-PSRAM S3 | ❌ same |
| **OTA** | ✅ dual-OTA + rollback | ✅ dual-OTA + rollback | ✅ dual-OTA + rollback |

## Common features (target-agnostic — identical on all boards)
These are not board-specific and should stay in sync automatically; listed so they are *not* mistaken
for drift:
OTA app rollback (auto-recover a crash-looping update) · `esp_crt_bundle` (verified MQTTS/HTTPS without
a manual cert) · async MJPEG live stream (non-blocking UI) · `esp_log` v2 · heap failed-alloc hook ·
`ICameraBackend` seam · confidence-vote post-processing (§10, default off) · changed-digit overview
highlight · pause-processing menu · scheduling · FastRead · dark mode · the STBI decode safety net.

## Notes / known drift risks
- **ESP32-WROVER** (`BOARD_WROVER_KIT`) is also `esp32` and feature-equivalent to the ESP32-CAM, but
  uses a **single `factory` slot (no OTA)** + a `storage` partition (`USE_FLASH_FS`) on 4 MB — so OTA
  rollback is a no-op there. Treat it as an ESP32-CAM column with "OTA: ❌, flash-FS: ✅".
- The **S3 resident-model-in-dedicated-PSRAM** optimization (PLAN §9.3.4) would be an S3-only feature
  once implemented — add a row here when it lands.
- Any new `#if defined(BOARD_ESP32S3_CAM)` / `USE_FLASH_FS` / `CONFIG_IDF_TARGET_ESP32S3` gate is, by
  definition, board drift → **add/refresh a row here in the same change.**

_Last updated: 2026-06-01._
