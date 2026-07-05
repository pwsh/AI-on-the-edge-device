# Provisioning & Migration

How to get a device running from scratch, and how to migrate an existing one to a new version —
**without pulling the SD card** wherever possible. Applies to ESP-IDF 6.0.1 / v17.

## Release artifacts

`tools/build-release.sh` (native `idf.py`) produces, in `dist_release/`:

| Artifact | Contains | Use for |
|---|---|---|
| `…__update__*.zip` | firmware + Web UI + CNN models | **Migrating / updating** an already-set-up device over the air (keeps config, reference images, prevalues). |
| `…__remote-setup__*.zip` | firmware + Web UI + **whole** `config/` | First-time **remote** provisioning of an SD card that's already in the device. |
| `…__manual-setup__*.zip` | `firmware.bin` + `bootloader.bin` + `partitions.bin` + `sd-card.zip` | **USB** flashing + preparing an SD card on a PC. |
| `firmware/*.bin` | raw bootloader / partition-table / app | esptool / web-installer flashing. |

The Web-UI and firmware both carry the same git short-hash; they are expected to match (see
*Migration* below).

## New install

### A. SD-card board (ESP32-CAM / WROVER)
1. **Flash the firmware** over USB (esptool or the web installer) using the `manual-setup` bins, **or**
   write a prepared SD card from `sd-card.zip`.
2. **First boot with no `wlan.ini` / `config.ini`** → the device starts **SoftAP setup mode**
   (`CheckStartAPMode`) and brings up the `AI-on-the-Edge` access point. The AP page shows the MAC
   and lets you enter Wi-Fi credentials; it can also receive uploads (`upload_post_handlerAP`).
3. Join the AP, enter your Wi-Fi, reboot → the device joins your network.
4. If the SD only has firmware (no Web UI / models yet), apply `…__remote-setup__*.zip` from the
   **OTA Update** page to populate `/html` + `/config`.

### B. SD-free board (ESP32-S3, or WROVER single-slot) — `USE_FLASH_FS`
The flash image already contains a LittleFS `storage` partition (gzipped Web UI + best-models +
default config) that is mounted at `/sdcard` when **no SD card is present**. So:
1. Flash the firmware (it includes `storage.bin`). No SD card needed.
2. First boot → SoftAP setup mode (no `wlan.ini`) → enter Wi-Fi → done.
3. Insert an SD card later and it is used instead (SD wins, flash is the fallback).

> Any SD-card layout works (MBR or SFD) — the historical "SD not detected" bug was a redundant
> `esp_psram_init()`, since fixed, not the card formatting.

## Migration (existing device → new version)

The Web UI and firmware are versioned by git hash. After a **firmware-only** flash (or a v16→v17
move where the on-SD Web UI is stale) they go out of sync. The device detects this:

- **Firmware log:** `Web UI version (…) does not match firmware version (…)`.
- **Web UI:** a warning banner — now a **one-click migration step**: it carries an
  **"Open the update page →"** button (`compareVersions()` → `gotoOtaUpdate()` in `common.js`) that
  loads the **OTA Update** page directly.

To migrate: open the update page (via that button or the *OTA Update* menu item), choose the
matching `…__update__*.zip`, press **Upload And Install**, and let the device reboot. This replaces
firmware + Web UI + models together, so the versions re-sync. Config, reference images and prevalues
are preserved (they are not in `update.zip`).

## Bootstrapping when the Web UI can't be reached

**Web password (optional):** the whole web interface + REST API can be protected with HTTP basic
auth. Configure it on the Wi-Fi settings page (`wlan_config.html`): a **Web password protection**
on/off switch plus username/password, stored in `wlan.ini` (`http_auth`, `http_username`,
`http_password`). It applies immediately, no reboot. Off by default; the credentials travel
unencrypted (plain HTTP), so treat it as protection against casual access on the LAN, not more.

If the device has firmware but no/old Web UI and you can't open the normal UI:
- **SoftAP** mode serves a minimal page (Wi-Fi config + reboot + file upload) when `wlan.ini` /
  `config.ini` are missing — use it to get on the network, then OTA the rest.
- **USB**: re-flash with the `manual-setup` bins, or write a fresh SD from `sd-card.zip`.
- **OTA endpoint**: `/ota?task=update&file=…` (the page at `ota_page.html`) applies an uploaded zip;
  the file server (`/upload`, `/delete`, `/fileserver`) can place individual files.
