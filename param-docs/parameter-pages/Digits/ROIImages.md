# Parameter `ROIImages`
Default Value: `false` (disabled)

Master on/off switch for saving the per-digit **ROI images** (the small cut-out image of each digit ROI)
to the SD card every round. **Off by default** to avoid the extra SD writes (these can be heavy on a slow
card and wear it over time).

- **disabled / `false`** (default): no ROI images are written, regardless of *ROI Images Location*.
- **enabled / `true`**: ROI images are written to *ROI Images Location* (default `/log/digit`), kept for
  *ROI Images Retention* days.

This is the digit-ROI equivalent of [Save Raw Images](../TakeImage/RawImages.md). For back-compatibility,
if `ROIImages` is not present at all, a configured *ROI Images Location* still enables saving.
