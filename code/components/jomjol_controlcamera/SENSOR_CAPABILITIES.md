# Camera sensor capabilities (OV2640 / OV3660 / OV5640)

Determined from the actual `esp32-camera` driver source in
`code/components/esp32-camera/sensors/{ov2640,ov3660,ov5640}.c` (the per-sensor
`set_*` implementations) cross-referenced with what this firmware actually calls
(`setCFstatusToCam` / `setSensorDatenFromCCstatus` in `MainFlowControl.cpp` /
`ClassControllCamera.cpp`, plus the `SetCam*` helpers). Verified on hardware where
noted. Last reviewed 2026-05-31.

## Method
A `set_*` that is a pure `return -1;` body in the sensor driver does nothing on that
sensor ("not supported"). The firmware applies most settings directly
(`s->set_X(...)`) and a few via OV2640-aware wrappers (`SetCamContrastBrightness`,
`SetCamGainceiling`, `SetCamSpecialEffect`, `SetCamSharpness`).

## Native driver support (which `set_*` are real vs `return -1` stubs)
| `set_*` | OV2640 | OV3660 | OV5640 |
|---|:--:|:--:|:--:|
| brightness, contrast, saturation | ✅ | ✅ | ✅ |
| special_effect | ✅ | ✅ | ✅ |
| **sharpness** | ❌ stub `return -1` | ✅ | ✅ |
| **denoise** | ❌ stub `return -1` | ✅ | ✅ |
| gainceiling, gain_ctrl(agc), agc_gain | ✅ | ✅ | ✅ |
| exposure_ctrl(aec), aec2, aec_value, ae_level | ✅ | ✅ | ✅ |
| whitebal(awb), awb_gain, wb_mode | ✅ | ✅ | ✅ |
| bpc, wpc, raw_gma, lenc, dcw | ✅ | ✅ | ✅ |
| hmirror, vflip, quality, framesize | ✅ | ✅ | ✅ |
| colorbar (test pattern) | ✅ | ✅ | ✅ |

The **only** native gaps are **OV2640 sharpness and denoise**.

## Firmware overrides / quirks (what the user actually gets)
- **OV2640 sharpness** — the native stub is bypassed by custom code
  (`ov2640_set_sharpness` / `ov2640_enable_auto_sharpness`, `jomjol_controlGPIO`).
  So **manual *and* auto sharpness DO work on the OV2640.**
- **OV2640 denoise** — *not* worked around, and the firmware additionally forces
  `ImageDenoiseLevel = 0` for the OV2640 in `ClassFlowTakeImage::ReadParameter`.
  → **Denoise is a true no-op on the OV2640.**
- **Auto-sharpness on OV3660 / OV5640** — `SetCamSharpness()` deliberately calls
  `set_sharpness(0)` when "auto" is selected (comment: *"autoSharpness is not
  supported, default to zero"*). → **Auto-sharpness is a no-op on OV3660 / OV5640**
  (a firmware choice; manual sharpness works).
- brightness/contrast/special-effect verified live on the OV2640 (brightness −2 →
  mean L≈4, +2 → mean L≈70; negative effect inverts the image).

### ⇒ UI controls that have no effect, per sensor
| Sensor | No-effect control(s) |
|---|---|
| **OV2640** | **Denoise** |
| **OV3660 / OV5640** | **Auto-sharpness** |

Everything else on the camera-settings pages applies.

## API `set_*` functions NOT used by this firmware
- **`set_colorbar`** — sensor test-pattern / colour bars. Supported by all three
  sensors but never exposed or called (diagnostic only). The only *user-relevant*
  capability the firmware doesn't surface.
- `set_pixformat`, `set_pll`, `set_xclk` — set once during camera init (JPEG format,
  clocks), not per-round user settings.
- `set_reg`, `set_res_raw` — low-level register / raw-resolution escapes; intentionally
  not exposed.
- OV5640-only autofocus entry points (`set_mode`, `set_manual_position`, …) exist in
  the extended sensor API but are not wired up (no autofocus support in this firmware).
