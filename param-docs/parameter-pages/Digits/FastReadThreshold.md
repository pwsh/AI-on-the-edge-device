# Parameter `FastReadThreshold`
Default Value: `8`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Only relevant when [FastRead](FastRead.md) is enabled.

Mean absolute per-pixel difference (range `0`–`255`) between the current cropped digit
image and the cached one, below which the digit is treated as **unchanged** and its
previous result is reused without running the CNN.

- Lower values are stricter (more digits get re-read; safer, slower).
- Higher values are more permissive (fewer inferences; risk of missing a slow digit
  transition under noisy lighting).

A "changed" decision only ever costs one extra inference, so a moderate value is safe.
