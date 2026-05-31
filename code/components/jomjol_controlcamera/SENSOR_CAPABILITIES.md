# Camera sensor capabilities (OV2640 / OV3660 / OV5640 / OV7670 / OV7725)

Derived from the actual `esp32-camera` driver source (`sensors/ov*.c` for per-sensor `set_*` real-vs-stub, `set_pixformat`, and the per-frame-size `set_pll` configs; `driver/sensor.c` for the resolution table and max frame size) cross-checked with what this firmware applies and clamps. Reviewed 2026-05-31.

> ✅ supported · ❌ `return -1` / `set_dummy` stub · ⭐ no native support but the project adds it. Ranges are the sensor-native settable min..max; this firmware additionally clamps brightness/contrast/saturation to ±2 in the UI.

> **Night mode:** there is *no* night-mode function in any OV driver — OV3660/OV7670 have night-mode / auto-frame-rate registers but they are left off by the init tables. Low light is handled by Auto exposure (AEC/AEC2) + gain.


## Per-camera summary

### OV2640 — 2 MP
- **Max resolution: 1600×1200 (UXGA, 1.92 MP)**
- **Auto focus: no**
- Frame sizes: 96×96, QQVGA 160×120, QCIF 176×144, HQVGA 240×176, 240×240, QVGA 320×240, 320×320, CIF 400×296, HVGA 480×320, VGA 640×480, SVGA 800×600, XGA 1024×768, HD 1280×720, SXGA 1280×1024, UXGA 1600×1200
- Pixel formats: JPEG, RGB565, RGB888, YUV422, GRAYSCALE
- Frame rate (set_pll): set_pll is a no-op stub — frame rate is fixed by the init register tables (not per-resolution selectable). Typical: ~15 fps at low res, dropping toward ~6 fps at UXGA (format/light dependent).

### OV3660 — 3 MP
- **Max resolution: 2048×1536 (QXGA, 3.15 MP)**
- **Auto focus: no**
- Frame sizes: OV2640 list + FHD 1920×1080, QXGA 2048×1536
- Pixel formats: JPEG, RGB565, RGB888, YUV422, GRAYSCALE, RAW
- Frame rate (set_pll): set_pll chosen per frame size. RGB modes (driver comments, 16 MHz XCLK): >HVGA → 4.44 fps, QVGA–HVGA → 10.25 fps, <QVGA → 17.77 fps. JPEG: 40–50 MHz SYSCLK / 10 MHz PCLK (faster).

### OV5640 — 5 MP
- **Max resolution: 2560×1920 (QSXGA, 4.92 MP)**
- **Auto focus: yes — driver/VCM (ov5640_af.c); needs AF firmware + AF lens. Not wired into this firmware.**
- Frame sizes: OV3660 list + QHD 2560×1440, WQXGA 2560×1600, QSXGA 2560×1920
- Pixel formats: JPEG, RGB565, RGB888, YUV422, GRAYSCALE, RAW
- Frame rate (set_pll): set_pll chosen per frame size (sysclk computed by calc_sysclk). JPEG: PLL multiplier 160/180/200 by size (smaller = higher fps); 10 MHz PCLK target. RGB: multiplier 10/8/20 by size.

### OV7670 — 0.3 MP  ⚠️ **not usable with this firmware (no JPEG)**
- **Max resolution: 640×480 (VGA, 0.3 MP)**
- **Auto focus: no**
- Frame sizes: up to VGA 640×480
- Pixel formats: RGB565, RGB888, YUV422, GRAYSCALE — **no JPEG, no RAW**
- Frame rate (set_pll): No set_pll (fixed). Init comment: ~30 fps at VGA on a 12 MHz clock.

### OV7725 — 0.3 MP  ⚠️ **not usable with this firmware (no JPEG)**
- **Max resolution: 640×480 (VGA, 0.3 MP)**
- **Auto focus: no**
- Frame sizes: up to VGA 640×480
- Pixel formats: RGB565, YUV422, GRAYSCALE — **no JPEG, no RAW, no RGB888**
- Frame rate (set_pll): No set_pll (fixed); frame rate set by init register tables (~30–60 fps at VGA typical).

## Function support & ranges

| Function (what it does) | OV2640 | OV3660 | OV5640 | OV7670 | OV7725 |
|---|---|---|---|---|---|
| **Brightness** — Overall image lightness. Higher = brighter, lower = darker (shifts every pixel). | ✅ −2..2 | ✅ −3..3 | ✅ −3..3 | ❌ | ✅ |
| **Contrast** — Difference between light and dark tones. Higher = more punch/harsher, lower = flatter/softer. | ✅ −2..2 | ✅ −3..3 | ✅ −3..3 | ❌ | ✅ |
| **Saturation** — Colour intensity. Higher = more vivid colours, lower = toward grayscale. | ✅ −2..2 | ✅ −4..4 | ✅ −4..4 | ❌ | ❌ |
| **Sharpness** — Edge enhancement. Higher = crisper edges (more apparent detail, more noise); lower = softer. | ⭐ custom | ✅ −3..3 | ✅ −3..3 | ❌ | ❌ |
| **Denoise** — Noise reduction / smoothing. Higher = less grain but can blur fine detail. | ❌ forced 0 | ✅ 0..8 | ✅ 0..8 | ❌ | ❌ |
| **Special effect** — Colour effect applied to the whole image. Modes: 0 None, 1 Negative, 2 Grayscale, 3 Red tint, 4 Green tint, 5 Blue tint, 6 Sepia. | ✅ 0..6 | ✅ 0..6 | ✅ 0..6 | ❌ | ❌ |
| **Auto exposure (AEC)** — Automatically adjusts exposure time to the scene brightness. Off = use the manual exposure value below. | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off |
| **AEC DSP (aec2)** — Secondary DSP-side auto-exposure path; helps in low light / fine exposure. (No dedicated ‘night mode’ function exists — low light is handled by AEC/AEC2 + gain.) | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **Manual exposure (AEC value)** — Exposure time used when AEC is off. Higher = brighter but more motion blur and, at the extreme, lower frame rate. | ✅ 0..1200 | ✅ 0..1200 | ✅ 0..1200 | ❌ | ✅ 0..1200 |
| **AE level (exposure compensation)** — Biases the auto-exposure target brighter or darker without leaving auto mode. | ✅ −2..2 | ✅ −5..5 | ✅ −5..5 | ❌ | ❌ |
| **Auto gain (AGC)** — Automatically raises sensor gain (ISO) in low light. Off = use the manual gain below. | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off |
| **Manual gain (AGC gain)** — Sensor gain when AGC is off. Higher = brighter but noisier image. | ✅ 0..30 | ✅ 0..30 | ✅ 0..30 | ❌ | ✅ |
| **Gain ceiling** — Maximum gain the auto-gain may use. Modes: 2X, 4X, 8X, 16X, 32X, 64X, 128X. Higher = brighter low-light but more noise. | ✅ 2X–128X | ✅ 2X–128X | ✅ 2X–128X | ❌ | ❌ |
| **Auto white balance (AWB)** — Automatically removes colour casts so whites look white. | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off |
| **AWB gain** — Lets the auto-white-balance apply per-channel gain (finer colour correction). | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **White-balance mode** — Manual white-balance preset for the light source. Modes: 0 Auto, 1 Sunny, 2 Cloudy, 3 Office (fluorescent), 4 Home (incandescent). Shifts colour temperature. | ✅ 0..4 | ✅ 0..4 | ✅ 0..4 | ❌ | ❌ |
| **Gamma (raw_gma)** — Gamma correction curve on the raw data. Brightens/darkens midtones non-linearly for better tonal balance. | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **Black-pixel correction (BPC)** — Hides stuck dark/dead pixels. | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **White-pixel correction (WPC)** — Hides stuck bright/hot pixels. | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **Lens correction (LENC)** — Compensates lens vignetting (darker corners) for even brightness. | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **DCW (downsize/scale)** — Enables the DSP downsize/scaling pipeline used to produce non-native sizes. | ✅ on/off | ✅ on/off | ✅ on/off | ❌ | ✅ on/off |
| **Horizontal mirror** — Flips the image left-right. | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off |
| **Vertical flip** — Flips the image top-bottom. | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off |
| **JPEG quality** — On-chip JPEG compression quality, 0..63 (lower number = better quality, bigger file). | ✅ 0..63 | ✅ 0..63 | ✅ 0..63 | ❌ no JPEG | ❌ no JPEG |
| **Colour bar** — Sensor test pattern (diagnostic). Not exposed by this firmware. | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off | ✅ on/off |
| **Auto focus** — Drives a VCM focus motor. OV5640 only, at the driver level; not used by this firmware. | ❌ | ❌ | ✅ driver | ❌ | ❌ |
| **Digital zoom (firmware)** — Crops a window out of the sensor frame (a firmware feature, not a sensor register). Offset X/Y recentres the crop; size sets how far in. | ✅ X±480 Y±360 sz0..29 | ✅ X±704 Y±528 sz0..43 | ✅ X±960 Y±720 sz0..59 | ✅ — | ✅ — |

### Pixel formats & colour depth

| format | colours | OV2640 | OV3660 | OV5640 | OV7670 | OV7725 |
|---|---|---|---|---|---|---|
| JPEG (compressed) | full colour | ✅ | ✅ | ✅ | ❌ | ❌ |
| RGB565 | 65,536 (16-bit) | ✅ | ✅ | ✅ | ✅ | ✅ |
| RGB888 | 16,777,216 (24-bit) | ✅ | ✅ | ✅ | ✅ | ❌ |
| YUV422 | full colour (chroma-subsampled) | ✅ | ✅ | ✅ | ✅ | ✅ |
| GRAYSCALE | 256 (8-bit) | ✅ | ✅ | ✅ | ✅ | ✅ |
| RAW (Bayer) | 8/10-bit raw | ❌ | ✅ | ✅ | ❌ | ❌ |

## `set_*` functions not used by this firmware
- `set_colorbar` (test pattern) — the only user-relevant capability never surfaced.
- `set_pixformat`, `set_pll`, `set_xclk` — set at camera init.
- `set_reg`, `set_res_raw` — low-level escapes.
- OV5640 autofocus (`ov5640_af_*`) — present in the driver, not wired up.


## Practical note
Capture is JPEG, so a sensor must support `PIXFORMAT_JPEG`: **OV2640 / OV3660 / OV5640 are usable; OV7670 / OV7725 are not.**
