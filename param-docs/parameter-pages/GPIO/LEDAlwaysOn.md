# Parameter `LEDAlwaysOn`
Default Value: `false` (disabled)

Keeps the **external WS281x LED constantly lit** at the configured [LEDColor](LEDColor.md) (and
[brightness %](LEDBrightness.md)) instead of only flashing during capture.

When **enabled** it:

- **Overrides the per-stage [Status LED](StatusLED.md) colours** — the strip stays at `LEDColor` rather
  than changing colour per processing step.
- **Disables the pre-capture delay** ([WaitBeforeTakingPicture](../TakeImage/WaitBeforeTakingPicture.md)) —
  because the scene is continuously lit, the camera's auto-exposure is already settled, so no flash/AEC
  settle wait is needed. This removes the biggest single cost in a round.

Use it when you want **constant illumination** of the meter (e.g. a dim location) and the fastest possible
capture. The trade-off is the LED is always on (more power / heat; mind the 5V current budget — see
[LEDBrightness](LEDBrightness.md) / [LEDMaxCurrent](LEDMaxCurrent.md)).

!!! Note
    Requires the external LED to be enabled and configured ([ExternalLED](ExternalLED.md), `LEDColor`,
    `LEDNumbers`, `LEDPin`/`IO`).
