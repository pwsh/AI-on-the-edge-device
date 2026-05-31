# Camera sensor capabilities (OV2640 / OV3660 / OV5640 / OV7670 / OV7725)

Derived from the actual `esp32-camera` driver source in
`code/components/esp32-camera/` (`sensors/ov*.c` for the per-sensor `set_*`
implementations and `set_pixformat`; `driver/sensor.c` for the resolution table and
per-sensor max framesize) cross-referenced with what this firmware actually applies
(`setCFstatusToCam` / `setSensorDatenFromCCstatus` in `MainFlowControl.cpp` /
`ClassControllCamera.cpp`, plus the `SetCam*` helpers and the per-sensor clamps in
`ClassFlowTakeImage::ReadParameter`). Verified on hardware where noted.
Last reviewed 2026-05-31. (`OV7670`/`OV7725` are present in the driver but **not
usable with this firmware** — see note.)

> **A `set_*` that is a pure `return -1;` (or wired to the driver's `set_dummy`) does
> nothing on that sensor.** The firmware applies most settings directly (`s->set_X`)
> and a few via OV2640-aware wrappers (`SetCamContrastBrightness`, `SetCamGainceiling`,
> `SetCamSpecialEffect`, `SetCamSharpness`).

---

## Per-camera summary

### OV2640 — 2 MP
- **Max resolution: 1600×1200 (UXGA, 1.92 MP)**
- **Auto focus: no** (fixed-focus module)
- Supported frame sizes (up to max): `96×96`, `QQVGA 160×120`, `QCIF 176×144`,
  `HQVGA 240×176`, `240×240`, `QVGA 320×240`, `320×320`, `CIF 400×296`,
  `HVGA 480×320`, `VGA 640×480`, `SVGA 800×600`, `XGA 1024×768`, `HD 1280×720`,
  `SXGA 1280×1024`, `UXGA 1600×1200`
- Pixel formats: **JPEG**, RGB565, RGB888, YUV422, GRAYSCALE
- Settable ranges (sensor / firmware-clamped): brightness −2..2, contrast −2..2,
  saturation −2..2, AE level −2..2, special effect 0..6, white-balance mode 0..4,
  AGC gain 0..30, AEC value 0..1200, gain ceiling 2X..128X, JPEG quality 0..63
  (lower = better). Digital zoom: offset X ±480, offset Y ±360, size 0..29.
- ⚠️ Sharpness, Denoise: **no native support** (`return -1`). The project adds custom
  sharpness via `ov2640_set_sharpness` (manual **and** auto sharpness work). **Denoise
  is a true no-op** (also forced to 0 by the firmware for OV2640).

### OV3660 — 3 MP
- **Max resolution: 2048×1536 (QXGA, 3.15 MP)**
- **Auto focus: no**
- Supported frame sizes: all of the OV2640 list **plus** `FHD 1920×1080`,
  `QXGA 2048×1536` (and the portrait/square modes up to that index).
- Pixel formats: **JPEG**, RGB565, RGB888, YUV422, GRAYSCALE, **RAW** (Bayer)
- Settable ranges: brightness −3..3, contrast −3..3, saturation −4..4,
  **sharpness −3..3**, **denoise 0..8**, AE level −5..5, special effect 0..6,
  WB mode 0..4, AGC gain 0..30, AEC value 0..1200, gain ceiling 2X..128X,
  quality 0..63. Digital zoom: offset X ±704, offset Y ±528, size 0..43.
- All `set_*` are real. ⚠️ **Auto-sharpness is a firmware no-op** (forces
  `set_sharpness(0)`); manual sharpness works.

### OV5640 — 5 MP
- **Max resolution: 2560×1920 (QSXGA, 4.92 MP)** (the 2592×1944 5 MP mode is capped to
  QSXGA by the driver)
- **Auto focus: YES at the sensor/driver level** — `ov5640_af.c` (VCM autofocus, needs
  the AF firmware blob and an autofocus lens module). **Not exposed by this firmware.**
- Supported frame sizes: all of the OV3660 list **plus** `QHD 2560×1440`,
  `WQXGA 2560×1600`, `QSXGA 2560×1920`.
- Pixel formats: **JPEG**, RGB565, RGB888, YUV422, GRAYSCALE, **RAW**
- Settable ranges: brightness −3..3, contrast −3..3, saturation −4..4,
  sharpness −3..3, denoise 0..8, AE level −5..5, special effect 0..6, WB mode 0..4,
  AGC gain 0..30, AEC value 0..1200, gain ceiling 2X..128X, quality 0..63.
  Digital zoom: offset X ±960, offset Y ±720, size 0..59.
- ⚠️ Auto-sharpness is a firmware no-op (same as OV3660); manual sharpness works.

### OV7670 — 0.3 MP  ⚠️ not usable with this firmware
- **Max resolution: 640×480 (VGA, 0.3 MP)**
- **Auto focus: no**
- Supported frame sizes: up to `VGA 640×480`
- Pixel formats: RGB565, RGB888, YUV422, GRAYSCALE — **no JPEG, no RAW**
- ⚠️ **No JPEG support** → cannot be used by this firmware (capture is JPEG).
- Very limited control: only exposure_ctrl(AEC), gain_ctrl(AGC), hmirror, vflip,
  whitebal(AWB), colorbar, framesize, pixformat are real. **Unsupported** (driver
  `set_dummy`): brightness, contrast, saturation, sharpness, denoise, AE level,
  AGC gain, AEC value, AWB gain, AEC2, special effect, WB mode, quality, gain ceiling,
  bpc, wpc, raw_gma, lenc, dcw.

### OV7725 — 0.3 MP  ⚠️ not usable with this firmware
- **Max resolution: 640×480 (VGA, 0.3 MP)**
- **Auto focus: no**
- Supported frame sizes: up to `VGA 640×480`
- Pixel formats: RGB565, YUV422, GRAYSCALE — **no JPEG, no RAW, no RGB888**
- ⚠️ **No JPEG support** → cannot be used by this firmware.
- Real: brightness, contrast, AEC/AEC2/AEC value, AGC/AGC gain, AWB/AWB gain, bpc,
  wpc, raw_gma, lenc, dcw, hmirror, vflip, whitebal, colorbar, framesize, pixformat.
  **Unsupported** (`set_dummy`): saturation, sharpness, denoise, AE level,
  gain ceiling, special effect, WB mode, quality.

---

## Support matrix (native driver)
✅ real · ❌ `return -1` / `set_dummy` stub · ⭐ no native support but the project adds it

| setting | OV2640 | OV3660 | OV5640 | OV7670 | OV7725 |
|---|:--:|:--:|:--:|:--:|:--:|
| brightness | ✅ −2..2 | ✅ −3..3 | ✅ −3..3 | ❌ | ✅ |
| contrast | ✅ −2..2 | ✅ −3..3 | ✅ −3..3 | ❌ | ✅ |
| saturation | ✅ −2..2 | ✅ −4..4 | ✅ −4..4 | ❌ | ❌ |
| sharpness | ❌ ⭐ custom | ✅ −3..3 | ✅ −3..3 | ❌ | ❌ |
| denoise | ❌ (forced 0) | ✅ 0..8 | ✅ 0..8 | ❌ | ❌ |
| AE level | ✅ −2..2 | ✅ −5..5 | ✅ −5..5 | ❌ | ❌ |
| special effect | ✅ 0..6 | ✅ 0..6 | ✅ 0..6 | ❌ | ❌ |
| WB mode | ✅ 0..4 | ✅ 0..4 | ✅ 0..4 | ❌ | ❌ |
| gain ceiling | ✅ 2X..128X | ✅ | ✅ | ❌ | ❌ |
| AGC gain | ✅ 0..30 | ✅ 0..30 | ✅ 0..30 | ❌ | ✅ |
| AEC value | ✅ 0..1200 | ✅ | ✅ | ❌ | ✅ |
| exposure(AEC)/gain(AGC)/AWB | ✅ | ✅ | ✅ | ✅ | ✅ |
| aec2 / awb_gain | ✅ | ✅ | ✅ | ❌ | ✅ |
| hmirror / vflip | ✅ | ✅ | ✅ | ✅ | ✅ |
| bpc / wpc / raw_gma / lenc / dcw | ✅ | ✅ | ✅ | ❌ | ✅ |
| JPEG quality | ✅ 0..63 | ✅ | ✅ | ❌(no JPEG) | ❌(no JPEG) |
| colorbar (test pattern) | ✅ | ✅ | ✅ | ✅ | ✅ |
| **auto focus** | ❌ | ❌ | ✅ driver (unused) | ❌ | ❌ |

### Pixel formats & colour depth
| format | colours | OV2640 | OV3660 | OV5640 | OV7670 | OV7725 |
|---|---|:--:|:--:|:--:|:--:|:--:|
| JPEG (compressed) | full colour | ✅ | ✅ | ✅ | ❌ | ❌ |
| RGB565 | 65,536 (16-bit) | ✅ | ✅ | ✅ | ✅ | ✅ |
| RGB888 | 16,777,216 (24-bit) | ✅ | ✅ | ✅ | ✅ | ❌ |
| YUV422 | full colour, chroma-subsampled | ✅ | ✅ | ✅ | ✅ | ✅ |
| GRAYSCALE | 256 (8-bit) | ✅ | ✅ | ✅ | ✅ | ✅ |
| RAW (Bayer) | 8/10-bit raw | ❌ | ✅ | ✅ | ❌ | ❌ |

---

## API `set_*` functions NOT used by this firmware
- **`set_colorbar`** — sensor test pattern / colour bars (all sensors support it);
  the only *user-relevant* capability never surfaced.
- `set_pixformat`, `set_pll`, `set_xclk` — set once at camera init (JPEG, clocks).
- `set_reg`, `set_res_raw` — low-level register / raw-resolution escapes.
- OV5640 autofocus (`ov5640_af_init`, `ov5640_af_get_status`, …) — present in the
  driver but not wired up (no autofocus in this firmware).

## Practical note
This firmware captures as **JPEG**, so a sensor must support `PIXFORMAT_JPEG`:
**OV2640 / OV3660 / OV5640 are usable; OV7670 / OV7725 are not** (no on-chip JPEG).
